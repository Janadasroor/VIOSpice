/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adc_bridge_dialog.h"
#include "../items/adc_bridge_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>

AdcBridgeDialog::AdcBridgeDialog(AdcBridgeItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("ADC Bridge Properties");
    setModal(true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_inLowSpin = new QDoubleSpinBox();
    m_inLowSpin->setRange(0.0, 1e6);
    m_inLowSpin->setDecimals(6);
    m_inLowSpin->setValue(m_item->inLow());
    m_inLowSpin->setSingleStep(0.1);
    form->addRow("Input low threshold (V):", m_inLowSpin);

    m_inHighSpin = new QDoubleSpinBox();
    m_inHighSpin->setRange(0.0, 1e6);
    m_inHighSpin->setDecimals(6);
    m_inHighSpin->setValue(m_item->inHigh());
    m_inHighSpin->setSingleStep(0.1);
    form->addRow("Input high threshold (V):", m_inHighSpin);

    m_riseDelaySpin = new QDoubleSpinBox();
    m_riseDelaySpin->setRange(0.0, 1.0);
    m_riseDelaySpin->setDecimals(12);
    m_riseDelaySpin->setValue(m_item->riseDelay());
    m_riseDelaySpin->setSingleStep(1e-9);
    m_riseDelaySpin->setSuffix(" s");
    m_riseDelaySpin->setDecimals(9);
    form->addRow("Rise delay:", m_riseDelaySpin);

    m_fallDelaySpin = new QDoubleSpinBox();
    m_fallDelaySpin->setRange(0.0, 1.0);
    m_fallDelaySpin->setDecimals(12);
    m_fallDelaySpin->setValue(m_item->fallDelay());
    m_fallDelaySpin->setSingleStep(1e-9);
    m_fallDelaySpin->setSuffix(" s");
    m_fallDelaySpin->setDecimals(9);
    form->addRow("Fall delay:", m_fallDelaySpin);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AdcBridgeDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AdcBridgeDialog::onAccept() {
    m_item->setInLow(m_inLowSpin->value());
    m_item->setInHigh(m_inHighSpin->value());
    m_item->setRiseDelay(m_riseDelaySpin->value());
    m_item->setFallDelay(m_fallDelaySpin->value());
    accept();
}
