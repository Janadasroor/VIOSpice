#include "virtual_terminal_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QCloseEvent>

VirtualTerminalWindow::VirtualTerminalWindow(const QUuid& id, const QString& title, QWidget* parent)
    : QMainWindow(parent), m_id(id) {
    setWindowTitle(title);
    resize(600, 400);
    setupUI();
}

void VirtualTerminalWindow::setupUI() {
    QWidget* central = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_terminal = new QPlainTextEdit();
    m_terminal->setReadOnly(true);
    m_terminal->setBackgroundRole(QPalette::Base);
    m_terminal->setStyleSheet(
        "background-color: #0c0c0c; color: #00ff41; font-family: 'Consolas', 'Monaco', monospace; font-size: 13px; border: none;"
    );
    layout->addWidget(m_terminal, 1);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(4, 4, 4, 4);

    m_txInput = new QLineEdit();
    m_txInput->setPlaceholderText("Type here to transmit on TX pin...");
    m_txInput->setStyleSheet(
        "background-color: #1a1a1a; color: #00ff41; font-family: 'Consolas', 'Monaco', monospace; font-size: 13px; "
        "border: 1px solid #333; padding: 4px;"
    );
    connect(m_txInput, &QLineEdit::returnPressed, this, &VirtualTerminalWindow::sendData);
    inputLayout->addWidget(m_txInput, 1);

    QPushButton* sendBtn = new QPushButton("Send");
    sendBtn->setStyleSheet(
        "background-color: #2a2a2a; color: #00ff41; border: 1px solid #444; padding: 4px 12px;"
    );
    connect(sendBtn, &QPushButton::clicked, this, &VirtualTerminalWindow::sendData);
    inputLayout->addWidget(sendBtn);

    layout->addLayout(inputLayout);

    QToolBar* toolbar = addToolBar("Terminal Controls");
    QAction* clearAct = toolbar->addAction("Clear Display");
    connect(clearAct, &QAction::triggered, this, &VirtualTerminalWindow::clear);

    toolbar->addSeparator();
    QAction* settingsAct = toolbar->addAction("Settings...");
    connect(settingsAct, &QAction::triggered, this, [this]() {
        Q_EMIT propertiesRequested(m_id);
    });

    setCentralWidget(central);
}

void VirtualTerminalWindow::clear() {
    m_terminal->clear();
    m_lastProcessedTime = 0.0;
    m_inStartBit = false;
    m_bitsCaptured = 0;
    m_currentChar = 0;
    m_nextBitTime = 0.0;
    m_txLastProcessedTime = 0.0;
    m_txInStartBit = false;
    m_txBitsCaptured = 0;
    m_txCurrentChar = 0;
    m_txNextBitTime = 0.0;
}

void VirtualTerminalWindow::setConfig(const VirtualTerminalItem::Config& config) {
    m_config = config;
}

void VirtualTerminalWindow::updateData(const SimResults& results, const QString& rxNet, const QString& txNet, const VirtualTerminalItem::Config& config) {
    m_config = config;

    // Reset serial state machine if simulation restarted from time zero
    if (!results.waveforms.empty() && !results.waveforms[0].xData.empty()) {
        double firstTime = results.waveforms[0].xData[0];
        if (firstTime < std::min(m_lastProcessedTime, m_txLastProcessedTime)) {
            m_lastProcessedTime = 0.0;
            m_inStartBit = false;
            m_bitsCaptured = 0;
            m_currentChar = 0;
            m_nextBitTime = 0.0;
            m_txLastProcessedTime = 0.0;
            m_txInStartBit = false;
            m_txBitsCaptured = 0;
            m_txCurrentChar = 0;
            m_txNextBitTime = 0.0;
        }
    }

    auto matchNet = [](const QString& waveName, const QString& netName) -> bool {
        if (netName.isEmpty()) return false;
        if (waveName.compare(netName, Qt::CaseInsensitive) == 0) return true;
        if (waveName.compare("V(" + netName + ")", Qt::CaseInsensitive) == 0) return true;
        if (waveName.endsWith(")")) {
            int paren = waveName.indexOf('(');
            if (paren > 0) {
                QString inner = waveName.mid(paren + 1, waveName.length() - paren - 2);
                if (inner.compare(netName, Qt::CaseInsensitive) == 0) return true;
            }
        }
        return false;
    };

    for (const auto& wave : results.waveforms) {
        QString name = QString::fromStdString(wave.name);

        if (matchNet(name, rxNet)) {
            processSerialData(wave.xData, wave.yData, config, true);
        } else if (matchNet(name, txNet)) {
            processSerialData(wave.xData, wave.yData, config, false);
        }
    }
}

bool VirtualTerminalWindow::parityCheck(unsigned char byte, int dataBits, const QString& parity) {
    if (parity == "None") return true;

    int bitCount = 0;
    for (int i = 0; i < dataBits; ++i) {
        if (byte & (1 << i)) ++bitCount;
    }
    bool even = (bitCount % 2 == 0);

    if (parity == "Even") return !even;
    if (parity == "Odd") return even;
    if (parity == "Mark") return true;
    if (parity == "Space") return false;

    return true;
}

void VirtualTerminalWindow::processSerialData(const std::vector<double>& times, const std::vector<double>& values, const VirtualTerminalItem::Config& config, bool isRx) {
    if (times.empty() || times.size() != values.size()) return;

    double& lastTime = isRx ? m_lastProcessedTime : m_txLastProcessedTime;
    bool& inStart = isRx ? m_inStartBit : m_txInStartBit;
    int& bitsCaptured = isRx ? m_bitsCaptured : m_txBitsCaptured;
    unsigned char& currentChar = isRx ? m_currentChar : m_txCurrentChar;
    double& nextBitTime = isRx ? m_nextBitTime : m_txNextBitTime;

    double bitPeriod = 1.0 / (double)config.baudRate;
    double threshold = 2.5;

    bool prevVal = true;
    size_t n = std::min(times.size(), values.size());

    for (size_t i = 0; i < n; ++i) {
        double t = times[i];
        if (t <= lastTime) continue;
        lastTime = t;

        bool val = values[i] > threshold;

        if (!inStart) {
            // Detect falling edge (HIGH→LOW transition) = start bit
            if (prevVal && !val) {
                inStart = true;
                nextBitTime = t + bitPeriod * 1.5;
                bitsCaptured = 0;
                currentChar = 0;
            }
            prevVal = val;
        } else {
            if (t >= nextBitTime) {
                if (bitsCaptured < config.dataBits) {
                    if (val) currentChar |= (1 << bitsCaptured);
                    bitsCaptured++;
                    nextBitTime += bitPeriod;
                } else {
                    bool parityOk = true;
                    if (config.parity != "None") {
                        bool expectedParity = VirtualTerminalWindow::parityCheck(currentChar, config.dataBits, config.parity);
                        parityOk = (val == expectedParity);
                        nextBitTime += bitPeriod;
                    }

                    if (parityOk && val) {
                        QString text;
                        if (config.hexMode) {
                            text = QString("%1 ").arg((int)currentChar, 2, 16, QChar('0')).toUpper();
                        } else {
                            text = QString(QChar(currentChar));
                        }
                        if (!isRx) {
                            m_terminal->insertPlainText(QString("<TX: %1").arg(text));
                            m_terminal->insertPlainText(">");
                        } else {
                            m_terminal->insertPlainText(text);
                        }
                        if (config.autoScroll) {
                            m_terminal->moveCursor(QTextCursor::End);
                        }
                    }
                    inStart = false;
                }
            }
        }
    }
}

void VirtualTerminalWindow::sendData() {
    QString text = m_txInput->text();
    if (text.isEmpty()) return;

    QByteArray data = text.toUtf8();
    m_txInput->clear();

    m_terminal->insertPlainText(QString("<TX: %1").arg(text));
    Q_EMIT configChanged(m_id, m_config);

    QVector<QPair<double, double>> waveform = generateTxWaveform(data);
    if (!waveform.isEmpty()) {
        Q_EMIT txDataReady(m_id, waveform);
    }
}

QVector<QPair<double, double>> VirtualTerminalWindow::generateTxWaveform(const QByteArray& data) const {
    QVector<QPair<double, double>> waveform;
    if (data.isEmpty()) return waveform;

    double bitPeriod = 1.0 / m_config.baudRate;
    double t = 0.0;

    for (int c = 0; c < data.size(); ++c) {
        unsigned char byte = (unsigned char)data[c];

        // Start bit (low)
        waveform.append({t, 5.0});
        waveform.append({t + bitPeriod * 0.1, 0.0});
        t += bitPeriod;

        // Data bits (LSB first)
        for (int i = 0; i < m_config.dataBits; ++i) {
            double level = (byte & (1 << i)) ? 5.0 : 0.0;
            waveform.append({t, level});
            t += bitPeriod;
        }

        // Parity bit
        if (m_config.parity != "None") {
            bool expectedParity = VirtualTerminalWindow::parityCheck(byte, m_config.dataBits, m_config.parity);
            double level = expectedParity ? 5.0 : 0.0;
            waveform.append({t, level});
            t += bitPeriod;
        }

        // Stop bit(s) (high)
        double level = 5.0;
        waveform.append({t, level});
        t += bitPeriod * m_config.stopBits;
    }

    return waveform;
}

void VirtualTerminalWindow::closeEvent(QCloseEvent* event) {
    Q_EMIT windowClosing(m_id);
    QMainWindow::closeEvent(event);
}
