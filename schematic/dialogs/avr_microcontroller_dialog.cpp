/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "avr_microcontroller_dialog.h"
#include "../items/avr_microcontroller_item.h"
#include "../items/arduino_board_def.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QTabWidget>
#include <QSettings>
#include <QSplitter>

AvrMicrocontrollerDialog::AvrMicrocontrollerDialog(AvrMicrocontrollerItem* item, QWidget* parent)
    : QDialog(parent)
    , m_item(item) {
    setWindowTitle("AVR Microcontroller Properties");
    setMinimumSize(640, 580);

    setStyleSheet(
        "QDialog { background-color: #1e1e24; color: #f0f0f0; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #383842; background: #23232b; border-radius: 6px; }"
        "QTabBar::tab { background: #1a1a20; padding: 8px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 4px; color: #a0a0b0; font-weight: 600; font-size: 12px; }"
        "QTabBar::tab:selected { background: #23232b; border-bottom: 2px solid #3b82f6; color: #60a5fa; }"
        "QTabBar::tab:hover { background: #282834; color: #e2e8f0; }"
        "QLabel { color: #cbd5e1; font-weight: 500; font-size: 12px; }"
        "QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox { background: #121218; border: 1px solid #475569; border-radius: 4px; color: #ffffff; padding: 6px 10px; font-size: 12px; min-height: 22px; }"
        "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #3b82f6; background: #1a1a24; }"
        "QListWidget { background: #121218; alternate-background-color: #1a1a24; border: 1px solid #475569; border-radius: 4px; color: #ffffff; }"
        "QListWidget::item { padding: 4px 8px; color: #ffffff; }"
        "QListWidget::item:selected { background: #2563eb; color: #ffffff; }"
        "QCheckBox, QRadioButton { color: #f8fafc; font-size: 12px; font-weight: 500; spacing: 6px; }"
        "QPushButton { background: #334155; color: #f8fafc; border: 1px solid #475569; padding: 6px 14px; border-radius: 4px; font-weight: 600; font-size: 12px; min-width: 65px; }"
        "QPushButton:hover { background: #475569; }"
        "QPushButton:pressed { background: #1e293b; }"
        "QDialogButtonBox QPushButton[text='OK'], QDialogButtonBox QPushButton[text='&OK'] { background: #2563eb; border-color: #3b82f6; color: white; }"
        "QDialogButtonBox QPushButton[text='OK']:hover, QDialogButtonBox QPushButton[text='&OK']:hover { background: #1d4ed8; }"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // ── Tab widget: Chip vs Board ───────────────────────────────────────
    m_tabWidget = new QTabWidget();

    // ── Tab 1: Chip Mode ────────────────────────────────────────────────
    auto* chipTab = new QWidget();
    auto* chipLayout = new QVBoxLayout(chipTab);

    auto* chipLabel = new QLabel("MCU Model:");
    chipLabel->setStyleSheet("font-weight: bold;");
    chipLayout->addWidget(chipLabel);

    m_chipSearchEdit = new QLineEdit();
    m_chipSearchEdit->setPlaceholderText("Search devices... (e.g. ATmega328, ATtiny, AVR128)");
    m_chipSearchEdit->setClearButtonEnabled(true);
    connect(m_chipSearchEdit, &QLineEdit::textChanged, this, &AvrMicrocontrollerDialog::onChipSearchChanged);
    chipLayout->addWidget(m_chipSearchEdit);

    // Family filter buttons
    auto* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(4);
    m_chipFilterGroup = new QButtonGroup(this);
    m_chipFilterGroup->setExclusive(true);

    auto addFilter = [&](const QString& label, const QString& prefix) {
        auto* btn = new QRadioButton(label);
        btn->setObjectName("filter_" + prefix);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_chipFilterGroup->addButton(btn);
        connect(btn, &QRadioButton::toggled, this, &AvrMicrocontrollerDialog::onChipFilterChanged);
        filterLayout->addWidget(btn);
    };

    auto* allBtn = new QRadioButton("All");
    allBtn->setChecked(true);
    m_chipFilterGroup->addButton(allBtn);
    connect(allBtn, &QRadioButton::toggled, this, &AvrMicrocontrollerDialog::onChipFilterChanged);
    filterLayout->addWidget(allBtn);

    addFilter("ATmega", "ATmega");
    addFilter("ATtiny", "ATtiny");
    addFilter("AVR-Dx", "AVR-DA");
    addFilter("AVR-Ex", "AVR-EA");
    addFilter("XMEGA", "ATxmega");
    addFilter("Other", "OTHER");
    filterLayout->addStretch();
    chipLayout->addLayout(filterLayout);

    m_chipDeviceList = new QListWidget();
    m_chipDeviceList->setAlternatingRowColors(true);
    m_chipDeviceList->setMinimumHeight(140);
    connect(m_chipDeviceList, &QListWidget::itemClicked, this, &AvrMicrocontrollerDialog::onChipItemClicked);
    connect(m_chipDeviceList, &QListWidget::itemDoubleClicked, this, &AvrMicrocontrollerDialog::onChipItemClicked);
    chipLayout->addWidget(m_chipDeviceList);

    m_tabWidget->addTab(chipTab, "Chip");

    // ── Tab 2: Board Mode ───────────────────────────────────────────────
    auto* boardTab = new QWidget();
    auto* boardLayout = new QVBoxLayout(boardTab);

    auto* boardLabel = new QLabel("Arduino Board:");
    boardLabel->setStyleSheet("font-weight: bold;");
    boardLayout->addWidget(boardLabel);

    m_boardSearchEdit = new QLineEdit();
    m_boardSearchEdit->setPlaceholderText("Search boards... (e.g. Uno, Mega, Nano, Leonardo)");
    m_boardSearchEdit->setClearButtonEnabled(true);
    connect(m_boardSearchEdit, &QLineEdit::textChanged, this, &AvrMicrocontrollerDialog::onBoardSearchChanged);
    boardLayout->addWidget(m_boardSearchEdit);

    m_boardList = new QListWidget();
    m_boardList->setAlternatingRowColors(true);
    m_boardList->setMinimumHeight(140);
    connect(m_boardList, &QListWidget::itemClicked, this, &AvrMicrocontrollerDialog::onBoardItemClicked);
    connect(m_boardList, &QListWidget::itemDoubleClicked, this, &AvrMicrocontrollerDialog::onBoardItemClicked);
    boardLayout->addWidget(m_boardList);

    m_boardInfoLabel = new QLabel();
    m_boardInfoLabel->setWordWrap(true);
    m_boardInfoLabel->setStyleSheet("color: gray; font-size: 11px; padding: 4px; background: palette(base); border: 1px solid palette(mid); border-radius: 4px;");
    boardLayout->addWidget(m_boardInfoLabel);

    m_tabWidget->addTab(boardTab, "Board");

    mainLayout->addWidget(m_tabWidget);

    // ── Shared Settings ─────────────────────────────────────────────────
    auto* settingsGroup = new QFormLayout();

    auto* fwLayout = new QHBoxLayout();
    m_firmwareEdit = new QLineEdit(item->firmwarePath());
    m_firmwareEdit->setPlaceholderText("Path to .hex firmware file...");
    auto* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &AvrMicrocontrollerDialog::onBrowseFirmware);
    fwLayout->addWidget(m_firmwareEdit);
    fwLayout->addWidget(browseBtn);
    settingsGroup->addRow("Firmware:", fwLayout);

    m_clockSpin = new QDoubleSpinBox();
    m_clockSpin->setRange(100000, 100000000);
    m_clockSpin->setDecimals(0);
    m_clockSpin->setSingleStep(1000000);
    m_clockSpin->setSuffix(" Hz");
    m_clockSpin->setValue(item->clockFrequency());
    auto* clockLayout = new QHBoxLayout();
    clockLayout->addWidget(m_clockSpin);
    for (double freq : {1e6, 8e6, 16e6, 20e6}) {
        auto* presetBtn = new QPushButton(QString("%1 MHz").arg(freq / 1e6, 0, 'f', 0));
        presetBtn->setMinimumWidth(55);
        connect(presetBtn, &QPushButton::clicked, this, [this, freq]() { m_clockSpin->setValue(freq); });
        clockLayout->addWidget(presetBtn);
    }
    settingsGroup->addRow("Clock:", clockLayout);

    m_jitCheck = new QCheckBox("Enable x86-64 JIT compilation");
    m_jitCheck->setChecked(item->jitEnabled());
    settingsGroup->addRow("", m_jitCheck);

    m_adcVoltageSpin = new QDoubleSpinBox();
    m_adcVoltageSpin->setRange(1.0, 5.5);
    m_adcVoltageSpin->setDecimals(1);
    m_adcVoltageSpin->setSingleStep(0.1);
    m_adcVoltageSpin->setSuffix(" V");
    m_adcVoltageSpin->setValue(item->adcVoltage());
    settingsGroup->addRow("ADC Reference:", m_adcVoltageSpin);

    mainLayout->addLayout(settingsGroup);

    // Info
    auto* infoLabel = new QLabel(
        "The AVR microcontroller runs firmware in a cycle-accurate simulator "
        "co-simulated with the analog circuit via ngspice d_cosim.");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
    mainLayout->addWidget(infoLabel);

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AvrMicrocontrollerDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Populate lists and select current
    populateDeviceList();
    populateBoardList();

    // Set initial tab based on current item
    if (item->isArduinoMode() && !item->boardType().isEmpty()) {
        m_tabWidget->setCurrentIndex(1); // Board tab
    } else {
        m_tabWidget->setCurrentIndex(0); // Chip tab
    }
}

void AvrMicrocontrollerDialog::populateDeviceList() {
    const auto& db = AvrMicrocontrollerItem::mcuDatabase();
    m_allDevices.clear();
    for (auto it = db.constBegin(); it != db.constEnd(); ++it) {
        m_allDevices.append(it.key());
    }
    m_allDevices.sort(Qt::CaseInsensitive);

    applyChipFilter();

    // Select current MCU
    QString current = m_item->mcuModel();
    if (!current.isEmpty()) {
        auto items = m_chipDeviceList->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_chipDeviceList->setCurrentItem(items.first());
            m_chipDeviceList->scrollToItem(items.first());
        }
    }
}

void AvrMicrocontrollerDialog::populateBoardList() {
    const auto& db = arduinoBoardDatabase();
    m_boardList->clear();
    QStringList boards = db.keys();
    boards.sort(Qt::CaseInsensitive);
    for (const QString& name : boards) {
        auto* item = new QListWidgetItem(name);
        m_boardList->addItem(item);
    }

    // Select current board
    QString current = m_item->boardType();
    if (!current.isEmpty()) {
        auto items = m_boardList->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_boardList->setCurrentItem(items.first());
            m_boardList->scrollToItem(items.first());
            // Show board info
            const auto& def = db[current];
            m_boardInfoLabel->setText(
                QString("<b>%1</b><br>MCU: %2 | Clock: %3 MHz | Flash: %4 KB | SRAM: %5 bytes | %6 pins")
                    .arg(def.boardName, def.mcuModel)
                    .arg(def.defaultClock / 1e6, 0, 'f', 0)
                    .arg(def.flashBytes / 1024)
                    .arg(def.sramBytes)
                    .arg(def.pins.size()));
        }
    }
}

void AvrMicrocontrollerDialog::applyChipFilter() {
    QString search = m_chipSearchEdit->text().trimmed().toLower();
    QString familyFilter;

    QRadioButton* checked = qobject_cast<QRadioButton*>(m_chipFilterGroup->checkedButton());
    if (checked && checked->objectName() != "filter_") {
        familyFilter = checked->objectName().remove("filter_");
    }

    m_chipDeviceList->clear();
    for (const QString& name : m_allDevices) {
        if (!familyFilter.isEmpty()) {
            if (familyFilter == "OTHER") {
                if (name.startsWith("ATmega", Qt::CaseInsensitive) ||
                    name.startsWith("ATtiny", Qt::CaseInsensitive) ||
                    name.startsWith("ATxmega", Qt::CaseInsensitive))
                    continue;
            } else if (!name.startsWith(familyFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }
        if (!search.isEmpty() && !name.toLower().contains(search))
            continue;

        auto* item = new QListWidgetItem(name);
        if (name == "ATmega328P" || name == "ATmega2560" || name == "ATtiny85" || name == "ATmega4809") {
            item->setForeground(QColor(34, 197, 94));
        }
        m_chipDeviceList->addItem(item);
    }
}

void AvrMicrocontrollerDialog::applyBoardFilter() {
    QString search = m_boardSearchEdit->text().trimmed().toLower();
    const auto& db = arduinoBoardDatabase();

    for (int i = 0; i < m_boardList->count(); ++i) {
        auto* item = m_boardList->item(i);
        if (search.isEmpty() || item->text().toLower().contains(search)) {
            item->setHidden(false);
        } else {
            item->setHidden(true);
        }
    }
}

void AvrMicrocontrollerDialog::onChipSearchChanged(const QString&) { applyChipFilter(); }
void AvrMicrocontrollerDialog::onChipFilterChanged() { applyChipFilter(); }
void AvrMicrocontrollerDialog::onChipItemClicked(QListWidgetItem*) {}
void AvrMicrocontrollerDialog::onBoardSearchChanged(const QString&) { applyBoardFilter(); }

void AvrMicrocontrollerDialog::onBoardItemClicked(QListWidgetItem* item) {
    if (!item) return;
    const auto& db = arduinoBoardDatabase();
    QString name = item->text();
    if (db.contains(name)) {
        const auto& def = db[name];
        m_boardInfoLabel->setText(
            QString("<b>%1</b><br>MCU: %2 | Clock: %3 MHz | Flash: %4 KB | SRAM: %5 bytes | %6 pins")
                .arg(def.boardName, def.mcuModel)
                .arg(def.defaultClock / 1e6, 0, 'f', 0)
                .arg(def.flashBytes / 1024)
                .arg(def.sramBytes)
                .arg(def.pins.size()));
        // Auto-fill clock from board definition
        m_clockSpin->setValue(def.defaultClock);
    }
}

void AvrMicrocontrollerDialog::onTabChanged(int index) {
    Q_UNUSED(index);
}

QString AvrMicrocontrollerDialog::selectedMcu() const {
    auto* item = m_chipDeviceList->currentItem();
    return item ? item->text() : QString();
}

QString AvrMicrocontrollerDialog::selectedBoard() const {
    auto* item = m_boardList->currentItem();
    return item ? item->text() : QString();
}

void AvrMicrocontrollerDialog::onBrowseFirmware() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select AVR Firmware", QString(),
        "Intel HEX Files (*.hex);;ELF Files (*.elf);;Arduino Sketches (*.ino);;All Files (*)");
    if (!path.isEmpty()) {
        m_firmwareEdit->setText(path);
    }
}

void AvrMicrocontrollerDialog::onAccept() {
    QString board = selectedBoard();
    if (!board.isEmpty()) {
        m_item->setBoardType(board);
    } else {
        QString mcu = selectedMcu();
        if (!mcu.isEmpty()) {
            m_item->setMcuModel(mcu);
        }
        m_item->setBoardType("");
    }

    m_item->setFirmwarePath(m_firmwareEdit->text());
    m_item->setClockFrequency(m_clockSpin->value());
    m_item->setJitEnabled(m_jitCheck->isChecked());
    m_item->setAdcVoltage(m_adcVoltageSpin->value());

    QSettings settings("VioraEDA", "AVRDialog");
    settings.setValue("lastMcu", m_item->mcuModel());
    if (!board.isEmpty()) settings.setValue("lastBoard", board);
    settings.setValue("lastTab", m_tabWidget->currentIndex());

    accept();
}
