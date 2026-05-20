#include "dac_bridge_dialog.h"
#include "../items/dac_bridge_item.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

DacBridgeDialog::DacBridgeDialog(DacBridgeItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("DAC Bridge Properties");
    setModal(true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_outLowSpin = new QDoubleSpinBox();
    m_outLowSpin->setRange(-1e6, 1e6);
    m_outLowSpin->setDecimals(6);
    m_outLowSpin->setValue(m_item->outLow());
    m_outLowSpin->setSuffix(" V");
    m_outLowSpin->setSingleStep(0.1);
    form->addRow("Output for logic 0:", m_outLowSpin);

    m_outHighSpin = new QDoubleSpinBox();
    m_outHighSpin->setRange(-1e6, 1e6);
    m_outHighSpin->setDecimals(6);
    m_outHighSpin->setValue(m_item->outHigh());
    m_outHighSpin->setSuffix(" V");
    m_outHighSpin->setSingleStep(0.1);
    form->addRow("Output for logic 1:", m_outHighSpin);

    m_outUndefSpin = new QDoubleSpinBox();
    m_outUndefSpin->setRange(-1e6, 1e6);
    m_outUndefSpin->setDecimals(6);
    m_outUndefSpin->setValue(m_item->outUndef());
    m_outUndefSpin->setSuffix(" V");
    m_outUndefSpin->setSingleStep(0.1);
    form->addRow("Output for undefined:", m_outUndefSpin);

    m_inputLoadSpin = new QDoubleSpinBox();
    m_inputLoadSpin->setRange(0.0, 1.0);
    m_inputLoadSpin->setDecimals(15);
    m_inputLoadSpin->setValue(m_item->inputLoad());
    m_inputLoadSpin->setSuffix(" F");
    m_inputLoadSpin->setSingleStep(1e-12);
    form->addRow("Input capacitance:", m_inputLoadSpin);

    m_tRiseSpin = new QDoubleSpinBox();
    m_tRiseSpin->setRange(0.0, 1.0);
    m_tRiseSpin->setDecimals(12);
    m_tRiseSpin->setValue(m_item->tRise());
    m_tRiseSpin->setSuffix(" s");
    m_tRiseSpin->setSingleStep(1e-9);
    m_tRiseSpin->setDecimals(9);
    form->addRow("Rise time:", m_tRiseSpin);

    m_tFallSpin = new QDoubleSpinBox();
    m_tFallSpin->setRange(0.0, 1.0);
    m_tFallSpin->setDecimals(12);
    m_tFallSpin->setValue(m_item->tFall());
    m_tFallSpin->setSuffix(" s");
    m_tFallSpin->setSingleStep(1e-9);
    m_tFallSpin->setDecimals(9);
    form->addRow("Fall time:", m_tFallSpin);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &DacBridgeDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void DacBridgeDialog::onAccept() {
    m_item->setOutLow(m_outLowSpin->value());
    m_item->setOutHigh(m_outHighSpin->value());
    m_item->setOutUndef(m_outUndefSpin->value());
    m_item->setInputLoad(m_inputLoadSpin->value());
    m_item->setTRise(m_tRiseSpin->value());
    m_item->setTFall(m_tFallSpin->value());
    accept();
}
