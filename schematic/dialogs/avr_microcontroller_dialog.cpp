/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "avr_microcontroller_dialog.h"
#include "../items/avr_microcontroller_item.h"

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
#include <QSplitter>
#include <QSettings>

AvrMicrocontrollerDialog::AvrMicrocontrollerDialog(AvrMicrocontrollerItem* item, QWidget* parent)
    : QDialog(parent)
    , m_item(item) {
    setWindowTitle("AVR Microcontroller Properties");
    setMinimumSize(560, 520);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // ── MCU Selection (searchable) ──────────────────────────────────────
    auto* mcuLabel = new QLabel("MCU Model:");
    mcuLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(mcuLabel);

    // Search bar
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search devices... (e.g. ATmega328, ATtiny, AVR128)");
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AvrMicrocontrollerDialog::onSearchChanged);
    mainLayout->addWidget(m_searchEdit);

    // Family filter buttons
    auto* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(4);
    m_filterGroup = new QButtonGroup(this);
    m_filterGroup->setExclusive(true);

    auto addFilter = [&](const QString& label, const QString& prefix) {
        auto* btn = new QRadioButton(label);
        btn->setObjectName("filter_" + prefix);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_filterGroup->addButton(btn);
        connect(btn, &QRadioButton::toggled, this, &AvrMicrocontrollerDialog::onFilterChanged);
        filterLayout->addWidget(btn);
    };

    auto* allBtn = new QRadioButton("All");
    allBtn->setChecked(true);
    m_filterGroup->addButton(allBtn);
    connect(allBtn, &QRadioButton::toggled, this, &AvrMicrocontrollerDialog::onFilterChanged);
    filterLayout->addWidget(allBtn);

    addFilter("ATmega", "ATmega");
    addFilter("ATtiny", "ATtiny");
    addFilter("AVR-Dx", "AVR-DA");
    addFilter("AVR-Ex", "AVR-EA");
    addFilter("XMEGA", "ATxmega");
    addFilter("Other", "OTHER");

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Device list
    m_deviceList = new QListWidget();
    m_deviceList->setAlternatingRowColors(true);
    m_deviceList->setMinimumHeight(160);
    connect(m_deviceList, &QListWidget::itemClicked, this, &AvrMicrocontrollerDialog::onItemClicked);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this, &AvrMicrocontrollerDialog::onItemClicked);
    mainLayout->addWidget(m_deviceList);

    // Device count label
    auto* countLabel = new QLabel();
    countLabel->setObjectName("countLabel");
    countLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(countLabel);

    // ── Settings ────────────────────────────────────────────────────────
    auto* settingsGroup = new QFormLayout();

    // Firmware
    auto* fwLayout = new QHBoxLayout();
    auto* firmwareEdit = new QLineEdit(item->firmwarePath());
    firmwareEdit->setPlaceholderText("Path to .hex firmware file...");
    firmwareEdit->setObjectName("firmwareEdit");
    auto* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &AvrMicrocontrollerDialog::onBrowseFirmware);
    fwLayout->addWidget(firmwareEdit);
    fwLayout->addWidget(browseBtn);
    settingsGroup->addRow("Firmware:", fwLayout);

    // Clock frequency
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

    // JIT
    m_jitCheck = new QCheckBox("Enable x86-64 JIT compilation");
    m_jitCheck->setChecked(item->jitEnabled());
    settingsGroup->addRow("", m_jitCheck);

    // ADC voltage
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

    // Populate and select current
    populateDeviceList();
}

void AvrMicrocontrollerDialog::populateDeviceList() {
    const auto& db = AvrMicrocontrollerItem::mcuDatabase();
    m_allDevices.clear();
    for (auto it = db.constBegin(); it != db.constEnd(); ++it) {
        m_allDevices.append(it.key());
    }
    m_allDevices.sort(Qt::CaseInsensitive);

    // Pre-select current MCU or last used
    QString current = m_item->mcuModel();
    if (current.isEmpty() || current == "ATmega328P") {
        QSettings settings("VioraEDA", "AVRDialog");
        QString lastMcu = settings.value("lastMcu").toString();
        if (!lastMcu.isEmpty()) current = lastMcu;
    }
    applyFilter();

    // Select current in list
    if (!current.isEmpty()) {
        auto items = m_deviceList->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_deviceList->setCurrentItem(items.first());
            m_deviceList->scrollToItem(items.first());
        }
    }

    // Update count
    auto* countLabel = findChild<QLabel*>("countLabel");
    if (countLabel) {
        countLabel->setText(QString("%1 devices available").arg(m_allDevices.size()));
    }
}

void AvrMicrocontrollerDialog::applyFilter() {
    QString search = m_searchEdit->text().trimmed().toLower();
    QString familyFilter;

    // Determine active family filter
    QRadioButton* checked = qobject_cast<QRadioButton*>(m_filterGroup->checkedButton());
    if (checked && checked->objectName() != "filter_") {
        familyFilter = checked->objectName().remove("filter_");
    }

    m_deviceList->clear();
    int shown = 0;
    for (const QString& name : m_allDevices) {
        // Family filter
        if (!familyFilter.isEmpty()) {
            if (familyFilter == "OTHER") {
                // "Other" = not ATmega, ATtiny, or ATxmega
                if (name.startsWith("ATmega", Qt::CaseInsensitive) ||
                    name.startsWith("ATtiny", Qt::CaseInsensitive) ||
                    name.startsWith("ATxmega", Qt::CaseInsensitive))
                    continue;
            } else if (!name.startsWith(familyFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Search filter
        if (!search.isEmpty() && !name.toLower().contains(search))
            continue;

        auto* item = new QListWidgetItem(name);
        // Highlight hardcoded MCUs (with detailed pin layouts)
        if (name == "ATmega328P" || name == "ATmega2560" || name == "ATtiny85" || name == "ATmega4809") {
            item->setForeground(QColor(34, 197, 94));  // Green accent
            item->setData(Qt::UserRole, true);  // Mark as detailed
        }
        m_deviceList->addItem(item);
        shown++;
    }

    auto* countLabel = findChild<QLabel*>("countLabel");
    if (countLabel) {
        if (shown == m_allDevices.size())
            countLabel->setText(QString("%1 devices available").arg(shown));
        else
            countLabel->setText(QString("%1 of %2 devices").arg(shown).arg(m_allDevices.size()));
    }
}

void AvrMicrocontrollerDialog::onSearchChanged(const QString&) {
    applyFilter();
}

void AvrMicrocontrollerDialog::onFilterChanged() {
    applyFilter();
}

void AvrMicrocontrollerDialog::onItemClicked(QListWidgetItem* item) {
    if (item) {
        // Could auto-fill clock from MCU database here
    }
}

QString AvrMicrocontrollerDialog::selectedMcu() const {
    auto* item = m_deviceList->currentItem();
    return item ? item->text() : QString();
}

void AvrMicrocontrollerDialog::onBrowseFirmware() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select AVR Firmware", QString(),
        "Intel HEX Files (*.hex);;ELF Files (*.elf);;All Files (*)");
    if (!path.isEmpty()) {
        auto* edit = findChild<QLineEdit*>("firmwareEdit");
        if (edit) edit->setText(path);
    }
}

void AvrMicrocontrollerDialog::onAccept() {
    QString mcu = selectedMcu();
    if (mcu.isEmpty()) {
        mcu = m_item->mcuModel();
    }
    m_item->setMcuModel(mcu);
    auto* edit = findChild<QLineEdit*>("firmwareEdit");
    if (edit) m_item->setFirmwarePath(edit->text());
    m_item->setClockFrequency(m_clockSpin->value());
    m_item->setJitEnabled(m_jitCheck->isChecked());
    m_item->setAdcVoltage(m_adcVoltageSpin->value());

    // Remember last selected MCU for next time
    QSettings settings("VioraEDA", "AVRDialog");
    settings.setValue("lastMcu", mcu);

    accept();
}
