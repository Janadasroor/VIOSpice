/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lcouple_dialog.h"
#include "../items/lcouple_item.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>

LcoupleDialog::LcoupleDialog(LcoupleItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("Inductive Coupling Properties");
    setModal(true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_turnsSpin = new QDoubleSpinBox();
    m_turnsSpin->setRange(1.0, 1e9);
    m_turnsSpin->setDecimals(0);
    m_turnsSpin->setValue(m_item->numTurns());
    m_turnsSpin->setSingleStep(1.0);
    form->addRow("Number of turns:", m_turnsSpin);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &LcoupleDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void LcoupleDialog::onAccept() {
    m_item->setNumTurns(m_turnsSpin->value());
    accept();
}
