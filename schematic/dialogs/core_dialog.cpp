/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core_dialog.h"
#include "../items/core_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>

CoreDialog::CoreDialog(CoreItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("Magnetic Core Properties");
    setModal(true);
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);

    auto* geoGroup = new QGroupBox("Core Geometry");
    auto* geoForm = new QFormLayout(geoGroup);
    m_areaSpin = new QDoubleSpinBox();
    m_areaSpin->setRange(1e-12, 1.0);
    m_areaSpin->setDecimals(12);
    m_areaSpin->setValue(m_item->area());
    m_areaSpin->setSingleStep(1e-6);
    m_areaSpin->setSuffix(" m\u00B2");
    geoForm->addRow("Cross-sectional area:", m_areaSpin);

    m_lengthSpin = new QDoubleSpinBox();
    m_lengthSpin->setRange(1e-6, 10.0);
    m_lengthSpin->setDecimals(6);
    m_lengthSpin->setValue(m_item->length());
    m_lengthSpin->setSingleStep(1e-3);
    m_lengthSpin->setSuffix(" m");
    geoForm->addRow("Magnetic path length:", m_lengthSpin);
    layout->addWidget(geoGroup);

    auto* bhGroup = new QGroupBox("B-H Curve");
    auto* bhForm = new QFormLayout(bhGroup);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("PWL (nonlinear B-H)", 1);
    m_modeCombo->addItem("Hysteresis", 2);
    m_modeCombo->setCurrentIndex(m_item->mode() == 2 ? 1 : 0);
    bhForm->addRow("Mode:", m_modeCombo);

    m_hArrayEdit = new QLineEdit(m_item->hArray());
    bhForm->addRow("H array (A/m):", m_hArrayEdit);

    m_bArrayEdit = new QLineEdit(m_item->bArray());
    bhForm->addRow("B array (T):", m_bArrayEdit);

    m_inputDomainSpin = new QDoubleSpinBox();
    m_inputDomainSpin->setRange(0.0, 1.0);
    m_inputDomainSpin->setDecimals(6);
    m_inputDomainSpin->setValue(m_item->inputDomain());
    m_inputDomainSpin->setSingleStep(0.01);
    bhForm->addRow("Smoothing domain:", m_inputDomainSpin);

    m_fractionCheck = new QCheckBox();
    m_fractionCheck->setChecked(m_item->fraction());
    bhForm->addRow("Fractional smoothing:", m_fractionCheck);

    layout->addWidget(bhGroup);

    auto* hystGroup = new QGroupBox("Hysteresis Parameters");
    auto* hystForm = new QFormLayout(hystGroup);
    m_inLowSpin = new QDoubleSpinBox();
    m_inLowSpin->setRange(-1e6, 1e6);
    m_inLowSpin->setDecimals(6);
    m_inLowSpin->setValue(m_item->inLow());
    hystForm->addRow("Input low:", m_inLowSpin);

    m_inHighSpin = new QDoubleSpinBox();
    m_inHighSpin->setRange(-1e6, 1e6);
    m_inHighSpin->setDecimals(6);
    m_inHighSpin->setValue(m_item->inHigh());
    hystForm->addRow("Input high:", m_inHighSpin);

    m_hystSpin = new QDoubleSpinBox();
    m_hystSpin->setRange(0.0, 1e6);
    m_hystSpin->setDecimals(6);
    m_hystSpin->setValue(m_item->hyst());
    hystForm->addRow("Hysteresis width:", m_hystSpin);

    m_outLowerSpin = new QDoubleSpinBox();
    m_outLowerSpin->setRange(-1e6, 1e6);
    m_outLowerSpin->setDecimals(6);
    m_outLowerSpin->setValue(m_item->outLowerLimit());
    hystForm->addRow("Output lower limit:", m_outLowerSpin);

    m_outUpperSpin = new QDoubleSpinBox();
    m_outUpperSpin->setRange(-1e6, 1e6);
    m_outUpperSpin->setDecimals(6);
    m_outUpperSpin->setValue(m_item->outUpperLimit());
    hystForm->addRow("Output upper limit:", m_outUpperSpin);
    layout->addWidget(hystGroup);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CoreDialog::onModeChanged);
    rebuildFields();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &CoreDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void CoreDialog::onModeChanged(int) {
    rebuildFields();
}

void CoreDialog::rebuildFields() {
    bool isPwl = m_modeCombo->currentData().toInt() == 1;
    m_hArrayEdit->setEnabled(isPwl);
    m_bArrayEdit->setEnabled(isPwl);
    m_inputDomainSpin->setEnabled(isPwl);
    m_fractionCheck->setEnabled(isPwl);
    bool isHyst = !isPwl;
    m_inLowSpin->setEnabled(isHyst);
    m_inHighSpin->setEnabled(isHyst);
    m_hystSpin->setEnabled(isHyst);
    m_outLowerSpin->setEnabled(isHyst);
    m_outUpperSpin->setEnabled(isHyst);
}

void CoreDialog::onAccept() {
    m_item->setArea(m_areaSpin->value());
    m_item->setLength(m_lengthSpin->value());
    m_item->setMode(m_modeCombo->currentData().toInt());
    m_item->setHArray(m_hArrayEdit->text());
    m_item->setBArray(m_bArrayEdit->text());
    m_item->setInputDomain(m_inputDomainSpin->value());
    m_item->setFraction(m_fractionCheck->isChecked());
    m_item->setInLow(m_inLowSpin->value());
    m_item->setInHigh(m_inHighSpin->value());
    m_item->setHyst(m_hystSpin->value());
    m_item->setOutLowerLimit(m_outLowerSpin->value());
    m_item->setOutUpperLimit(m_outUpperSpin->value());
    accept();
}
