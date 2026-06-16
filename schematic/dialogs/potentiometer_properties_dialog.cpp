/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "potentiometer_properties_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>

PotentiometerPropertiesDialog::PotentiometerPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("Potentiometer Properties - %1").arg(item ? item->reference() : "RPOT?"));
    setModal(true);
    setMinimumWidth(450);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void PotentiometerPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    auto* basicGroup = new QGroupBox("Basic Settings");
    auto* basicForm = new QFormLayout(basicGroup);

    m_refEdit = new QLineEdit();
    basicForm->addRow("Reference:", m_refEdit);

    m_resEdit = new QLineEdit();
    m_resEdit->setPlaceholderText("e.g. 10k, 1Meg");
    basicForm->addRow("Total Resistance (R):", m_resEdit);

    mainLayout->addWidget(basicGroup);

    auto* wiperGroup = new QGroupBox("Wiper Position");
    auto* wiperLayout = new QVBoxLayout(wiperGroup);

    auto* sliderRow = new QHBoxLayout();
    m_wiperSlider = new QSlider(Qt::Horizontal);
    m_wiperSlider->setRange(0, 1000);
    m_wiperSlider->setValue(500);
    
    m_wiperSpin = new QDoubleSpinBox();
    m_wiperSpin->setRange(0.0, 1.0);
    m_wiperSpin->setSingleStep(0.01);
    m_wiperSpin->setDecimals(3);
    m_wiperSpin->setValue(0.5);

    sliderRow->addWidget(new QLabel("0%"));
    sliderRow->addWidget(m_wiperSlider, 1);
    sliderRow->addWidget(new QLabel("100%"));
    sliderRow->addSpacing(10);
    sliderRow->addWidget(m_wiperSpin);
    
    wiperLayout->addLayout(sliderRow);
    mainLayout->addWidget(wiperGroup);

    auto* advancedGroup = new QGroupBox("Taper & Advanced");
    auto* advForm = new QFormLayout(advancedGroup);

    m_logCheck = new QCheckBox("Logarithmic Taper");
    advForm->addRow("", m_logCheck);

    m_logMultSpin = new QDoubleSpinBox();
    m_logMultSpin->setRange(0.001, 1000.0);
    m_logMultSpin->setValue(1.0);
    m_logMultSpin->setEnabled(false);
    advForm->addRow("Log Multiplier:", m_logMultSpin);

    mainLayout->addWidget(advancedGroup);

    mainLayout->addWidget(new QLabel("SPICE Model Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New'; font-size: 10pt;");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PotentiometerPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Connections
    connect(m_wiperSlider, &QSlider::valueChanged, this, &PotentiometerPropertiesDialog::onSliderChanged);
    connect(m_wiperSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PotentiometerPropertiesDialog::onSpinBoxChanged);
    connect(m_logCheck, &QCheckBox::toggled, m_logMultSpin, &QDoubleSpinBox::setEnabled);
    
    connect(m_refEdit, &QLineEdit::textChanged, this, &PotentiometerPropertiesDialog::updateCommandPreview);
    connect(m_resEdit, &QLineEdit::textChanged, this, &PotentiometerPropertiesDialog::updateCommandPreview);
    connect(m_wiperSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PotentiometerPropertiesDialog::updateCommandPreview);
    connect(m_logCheck, &QCheckBox::toggled, this, &PotentiometerPropertiesDialog::updateCommandPreview);
    connect(m_logMultSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PotentiometerPropertiesDialog::updateCommandPreview);
}

void PotentiometerPropertiesDialog::loadValues() {
    if (!m_item) return;

    m_refEdit->setText(m_item->reference());
    m_resEdit->setText(m_item->value());

    const auto pe = m_item->paramExpressions();
    
    double pos = pe.value("pot.position", "0.5").toDouble();
    m_wiperSpin->setValue(pos);
    m_wiperSlider->setValue(static_cast<int>(pos * 1000));

    m_logCheck->setChecked(pe.value("pot.log", "false") == "true");
    m_logMultSpin->setValue(pe.value("pot.log_multiplier", "1.0").toDouble());
}

void PotentiometerPropertiesDialog::onSliderChanged(int value) {
    m_wiperSpin->blockSignals(true);
    m_wiperSpin->setValue(value / 1000.0);
    m_wiperSpin->blockSignals(false);
}

void PotentiometerPropertiesDialog::onSpinBoxChanged(double value) {
    m_wiperSlider->blockSignals(true);
    m_wiperSlider->setValue(static_cast<int>(value * 1000));
    m_wiperSlider->blockSignals(false);
}

void PotentiometerPropertiesDialog::updateCommandPreview() {
    QString ref = reference();
    if (ref.isEmpty()) ref = "RPOT1";
    
    QString res = totalResistance();
    if (res.isEmpty()) res = "10k";

    QString logStr = isLogarithmic() ? "TRUE" : "FALSE";
    
    // .model MODEL_REF potentiometer(r=RES position=POS log=LOG log_multiplier=MULT)
    
    m_commandPreview->setText(QString("A_%1 [1 2 3] pot_mod_%1").arg(ref));
}

void PotentiometerPropertiesDialog::applyChanges() {
    if (m_item) {
        m_item->setReference(reference());
        m_item->setValue(totalResistance());
        
        QMap<QString, QString> pe = m_item->paramExpressions();
        pe["pot.position"] = QString::number(wiperPosition());
        pe["pot.log"] = isLogarithmic() ? "true" : "false";
        pe["pot.log_multiplier"] = QString::number(logMultiplier());
        for (auto it = pe.constBegin(); it != pe.constEnd(); ++it)
            m_item->setParamExpression(it.key(), it.value());
    }
    accept();
}

QString PotentiometerPropertiesDialog::reference() const {
    return m_refEdit->text().trimmed();
}

QString PotentiometerPropertiesDialog::totalResistance() const {
    return m_resEdit->text().trimmed();
}

double PotentiometerPropertiesDialog::wiperPosition() const {
    return m_wiperSpin->value();
}

bool PotentiometerPropertiesDialog::isLogarithmic() const {
    return m_logCheck->isChecked();
}

double PotentiometerPropertiesDialog::logMultiplier() const {
    return m_logMultSpin->value();
}

QMap<QString, QString> PotentiometerPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    if (m_item) pe = m_item->paramExpressions();
    pe["pot.position"] = QString::number(wiperPosition());
    pe["pot.log"] = isLogarithmic() ? "true" : "false";
    pe["pot.log_multiplier"] = QString::number(logMultiplier());
    return pe;
}
