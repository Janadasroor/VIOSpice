#ifndef VIRTUAL_TERMINAL_WINDOW_H
#define VIRTUAL_TERMINAL_WINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QUuid>
#include <QVector>
#include <QPair>
#include "../../simulator/core/sim_results.h"
#include "../items/virtual_terminal_item.h"

class VirtualTerminalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit VirtualTerminalWindow(const QUuid& id, const QString& title, QWidget* parent = nullptr);

    void updateData(const SimResults& results, const QString& rxNet, const QString& txNet, const VirtualTerminalItem::Config& config);
    void setConfig(const VirtualTerminalItem::Config& config);
    VirtualTerminalItem::Config config() const { return m_config; }
    void clear();

    static bool parityCheck(unsigned char byte, int dataBits, const QString& parity);
    QVector<QPair<double, double>> generateTxWaveform(const QByteArray& data) const;

Q_SIGNALS:
    void windowClosing(const QUuid& id);
    void configChanged(const QUuid& id, const VirtualTerminalItem::Config& cfg);
    void propertiesRequested(const QUuid& id);
    void txDataReady(const QUuid& id, const QVector<QPair<double, double>>& waveform);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUI();
    void processSerialData(const std::vector<double>& times, const std::vector<double>& values, const VirtualTerminalItem::Config& config, bool isRx);
    void sendData();

    QUuid m_id;
    QPlainTextEdit* m_terminal;
    QLineEdit* m_txInput;

    VirtualTerminalItem::Config m_config;

    // Serial RX state
    double m_lastProcessedTime = 0.0;
    bool m_inStartBit = false;
    int m_bitsCaptured = 0;
    unsigned char m_currentChar = 0;
    double m_nextBitTime = 0.0;

    // Serial TX state (for monitoring TX net from circuit)
    double m_txLastProcessedTime = 0.0;
    bool m_txInStartBit = false;
    int m_txBitsCaptured = 0;
    unsigned char m_txCurrentChar = 0;
    double m_txNextBitTime = 0.0;
};

#endif // VIRTUAL_TERMINAL_WINDOW_H
