/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oscilloscope_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/core/sim_value_parser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QScrollArea>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <algorithm>

static const QColor s_defaultColors[8] = {
    Qt::yellow, Qt::cyan, Qt::magenta, QColor(0, 255, 100),
    QColor(255, 165, 0), QColor(147, 112, 219), QColor(255, 105, 180), QColor(0, 191, 255)
};

OscilloscopePropertiesDialog::OscilloscopePropertiesDialog(OscilloscopeItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene) {
    setWindowTitle(QString("Oscilloscope Properties — %1").arg(item->reference()));
    setMinimumWidth(680);
    setMinimumHeight(600);

    m_config = item->config();

    setStyleSheet(
        "QDialog { background-color: #1a1a24; color: #f8fafc; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QGroupBox { border: 1.5px solid #334155; border-radius: 6px; margin-top: 18px; padding-top: 14px; background: #222634; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 2px 8px; color: #60a5fa; font-weight: bold; font-size: 12px; background: #1a1a24; border: 1px solid #334155; border-radius: 4px; }"
        "QLabel { color: #cbd5e1; font-weight: 600; font-size: 12px; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #0f111a; border: 1.5px solid #475569; border-radius: 4px; color: #ffffff; padding: 6px 10px; font-size: 13px; min-height: 24px; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #3b82f6; background: #151824; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background: #1e2230; color: #f8fafc; selection-background-color: #3b82f6; selection-color: white; border: 1px solid #475569; }"
        "QCheckBox { color: #f8fafc; font-size: 12px; font-weight: 600; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1.5px solid #64748b; border-radius: 4px; background: #0f111a; }"
        "QCheckBox::indicator:checked { background: #3b82f6; border-color: #3b82f6; }"
        "QPushButton { background: #334155; color: #ffffff; border: 1px solid #475569; padding: 7px 16px; border-radius: 5px; font-weight: 600; font-size: 12px; min-width: 80px; }"
        "QPushButton:hover { background: #475569; border-color: #64748b; }"
        "QPushButton:pressed { background: #1e293b; }"
        "QPushButton#okBtn, QPushButton#applyBtn { background: #2563eb; border-color: #3b82f6; color: white; }"
        "QPushButton#okBtn:hover, QPushButton#applyBtn:hover { background: #1d4ed8; }"
    );

    setupUI();
    loadFromConfig();
}

void OscilloscopePropertiesDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(14);

    // 1. Header Banner / General Config Box
    QGroupBox* genBox = new QGroupBox("General Instrument Configuration", this);
    QGridLayout* genLayout = new QGridLayout(genBox);
    genLayout->setContentsMargins(12, 16, 12, 12);
    genLayout->setHorizontalSpacing(14);
    genLayout->setVerticalSpacing(10);

    genLayout->addWidget(new QLabel("Designator / Ref:", this), 0, 0);
    m_refEdit = new QLineEdit(this);
    m_refEdit->setText(m_item->reference());
    genLayout->addWidget(m_refEdit, 0, 1);

    genLayout->addWidget(new QLabel("Channel Count (1-8):", this), 0, 2);
    m_channelCountSpin = new QSpinBox(this);
    m_channelCountSpin->setRange(1, 8);
    m_channelCountSpin->setValue(m_config.channelCount);
    genLayout->addWidget(m_channelCountSpin, 0, 3);

    genLayout->addWidget(new QLabel("Timebase (s/Div):", this), 1, 0);
    m_timebaseSpin = new QDoubleSpinBox(this);
    m_timebaseSpin->setRange(1e-9, 10.0);
    m_timebaseSpin->setDecimals(6);
    m_timebaseSpin->setValue(m_config.timebase);
    genLayout->addWidget(m_timebaseSpin, 1, 1);

    genLayout->addWidget(new QLabel("Trigger Source:", this), 1, 2);
    m_trigSourceCombo = new QComboBox(this);
    for (int i = 1; i <= 8; ++i) m_trigSourceCombo->addItem(QString("CH%1").arg(i));
    m_trigSourceCombo->addItem("External");
    m_trigSourceCombo->setCurrentText(m_config.triggerSource);
    genLayout->addWidget(m_trigSourceCombo, 1, 3);

    genLayout->addWidget(new QLabel("Trigger Level (V):", this), 2, 0);
    m_trigLevelSpin = new QDoubleSpinBox(this);
    m_trigLevelSpin->setRange(-1000.0, 1000.0);
    m_trigLevelSpin->setDecimals(3);
    m_trigLevelSpin->setValue(m_config.triggerLevel);
    genLayout->addWidget(m_trigLevelSpin, 2, 1);

    mainLayout->addWidget(genBox);

    // 2. Channel Configuration Strip
    QGroupBox* chMasterBox = new QGroupBox("Channel Setup & Floating Reference Probes", this);
    QVBoxLayout* chMasterLayout = new QVBoxLayout(chMasterBox);
    chMasterLayout->setContentsMargins(8, 14, 8, 8);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none;");

    QWidget* chContainer = new QWidget();
    chContainer->setStyleSheet("background: transparent;");
    m_channelsContainerLayout = new QVBoxLayout(chContainer);
    m_channelsContainerLayout->setContentsMargins(4, 4, 4, 4);
    m_channelsContainerLayout->setSpacing(8);

    scroll->setWidget(chContainer);
    chMasterLayout->addWidget(scroll);
    mainLayout->addWidget(chMasterBox, 1);

    // 3. Dialog Action Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton("Apply", this);
    applyBtn->setObjectName("applyBtn");
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* okBtn = new QPushButton("OK", this);
    okBtn->setObjectName("okBtn");

    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_channelCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &OscilloscopePropertiesDialog::onChannelCountChanged);
    connect(applyBtn, &QPushButton::clicked, this, &OscilloscopePropertiesDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, [this]() {
        onApply();
        accept();
    });

    rebuildChannelRows();
}

void OscilloscopePropertiesDialog::rebuildChannelRows() {
    if (!m_channelsContainerLayout) return;

    QLayoutItem* item;
    while ((item = m_channelsContainerLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    m_channelRows.clear();

    int count = m_channelCountSpin->value();
    m_channelRows.resize(count);

    for (int i = 0; i < count; ++i) {
        const auto& ch = (i < m_config.channels.size()) ? m_config.channels[i] : OscilloscopeItem::ChannelConfig();
        QColor chColor = ch.color.isValid() ? ch.color : s_defaultColors[i % 8];

        QGroupBox* rowGroup = new QGroupBox(QString("Channel %1 (+ / - Probes)").arg(i + 1), this);
        rowGroup->setStyleSheet(QString("QGroupBox::title { color: %1; border-color: %1; }").arg(chColor.name()));
        
        QGridLayout* rowLayout = new QGridLayout(rowGroup);
        rowLayout->setContentsMargins(10, 12, 10, 8);
        rowLayout->setHorizontalSpacing(12);

        m_channelRows[i].group = rowGroup;
        m_channelRows[i].color = chColor;

        m_channelRows[i].enabled = new QCheckBox("Active", this);
        m_channelRows[i].enabled->setChecked(ch.enabled);
        rowLayout->addWidget(m_channelRows[i].enabled, 0, 0);

        m_channelRows[i].floating = new QCheckBox("Differential / Floating (CH- pin)", this);
        m_channelRows[i].floating->setToolTip("Measure (CH+ - CH-) across floating nodes");
        m_channelRows[i].floating->setChecked(ch.floatingGround);
        rowLayout->addWidget(m_channelRows[i].floating, 0, 1);

        m_channelRows[i].colorBtn = new QPushButton(this);
        m_channelRows[i].colorBtn->setText("Color");
        m_channelRows[i].colorBtn->setStyleSheet(QString("background-color: %1; color: black; font-weight: bold; border-radius: 4px;").arg(chColor.name()));
        rowLayout->addWidget(m_channelRows[i].colorBtn, 0, 2);

        rowLayout->addWidget(new QLabel("Scale (V/div):", this), 1, 0);
        m_channelRows[i].scaleSpin = new QDoubleSpinBox(this);
        m_channelRows[i].scaleSpin->setRange(0.001, 1000.0);
        m_channelRows[i].scaleSpin->setValue(ch.scale > 0 ? (1.0 / ch.scale) : 1.0);
        rowLayout->addWidget(m_channelRows[i].scaleSpin, 1, 1);

        rowLayout->addWidget(new QLabel("Offset (V):", this), 1, 2);
        m_channelRows[i].offsetSpin = new QDoubleSpinBox(this);
        m_channelRows[i].offsetSpin->setRange(-1000.0, 1000.0);
        m_channelRows[i].offsetSpin->setValue(ch.offset);
        rowLayout->addWidget(m_channelRows[i].offsetSpin, 1, 3);

        m_channelsContainerLayout->addWidget(rowGroup);

        connect(m_channelRows[i].colorBtn, &QPushButton::clicked, [this, i]() { onChooseColor(i); });
    }
}

void OscilloscopePropertiesDialog::onChannelCountChanged(int count) {
    count = std::clamp(count, 1, 8);
    m_config.channelCount = count;
    m_config.channels.resize(count);
    rebuildChannelRows();
}

void OscilloscopePropertiesDialog::onChooseColor(int ch) {
    if (ch < 0 || ch >= m_channelRows.size()) return;
    QColor c = QColorDialog::getColor(m_channelRows[ch].color, this, QString("Choose Color for Channel %1").arg(ch + 1));
    if (c.isValid()) {
        m_channelRows[ch].color = c;
        m_channelRows[ch].colorBtn->setStyleSheet(QString("background-color: %1; color: black; font-weight: bold; border-radius: 4px;").arg(c.name()));
        m_channelRows[ch].group->setStyleSheet(QString("QGroupBox::title { color: %1; border-color: %1; }").arg(c.name()));
    }
}

void OscilloscopePropertiesDialog::loadFromConfig() {
    m_refEdit->setText(m_item->reference());
    m_channelCountSpin->setValue(m_config.channelCount);
    m_timebaseSpin->setValue(m_config.timebase);
    m_trigSourceCombo->setCurrentText(m_config.triggerSource);
    m_trigLevelSpin->setValue(m_config.triggerLevel);
}

void OscilloscopePropertiesDialog::onApply() {
    if (!m_item) return;

    OscilloscopeItem::Config newCfg;
    newCfg.channelCount = m_channelCountSpin->value();
    newCfg.timebase = m_timebaseSpin->value();
    newCfg.triggerSource = m_trigSourceCombo->currentText();
    newCfg.triggerLevel = m_trigLevelSpin->value();

    newCfg.channels.resize(newCfg.channelCount);
    for (int i = 0; i < newCfg.channelCount && i < m_channelRows.size(); ++i) {
        newCfg.channels[i].enabled = m_channelRows[i].enabled->isChecked();
        newCfg.channels[i].floatingGround = m_channelRows[i].floating->isChecked();
        double vdiv = m_channelRows[i].scaleSpin->value();
        newCfg.channels[i].scale = (vdiv > 0) ? (1.0 / vdiv) : 1.0;
        newCfg.channels[i].offset = m_channelRows[i].offsetSpin->value();
        newCfg.channels[i].color = m_channelRows[i].color;
    }

    if (m_undoStack) {
        m_undoStack->beginMacro("Update Oscilloscope Configuration");
        if (m_refEdit->text() != m_item->reference()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Reference", m_item->reference(), m_refEdit->text(), ""));
        }
        m_undoStack->push(new ChangeOscilloscopeConfigCommand(m_item, m_item->config(), newCfg));
        m_undoStack->endMacro();
    } else {
        m_item->setReference(m_refEdit->text());
        m_item->setConfig(newCfg);
    }
}

