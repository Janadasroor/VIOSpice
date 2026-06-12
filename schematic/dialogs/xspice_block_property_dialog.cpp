/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "xspice_block_property_dialog.h"
#include "xspice_block_item.h"
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QGroupBox>

XspiceBlockPropertyDialog::XspiceBlockPropertyDialog(XspiceBlockItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("XSPICE Block Properties");
    setMinimumWidth(450);

    auto* mainLayout = new QVBoxLayout(this);

    // Model type selector
    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel("Model Type:"));
    m_typeCombo = new QComboBox;
    m_typeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Group by category
    QString currentCategory;
    for (const auto& def : XspiceBlockItem::modelDatabase()) {
        if (def.category != currentCategory) {
            currentCategory = def.category;
            m_typeCombo->addItem(QString("── %1 ──").arg(def.category));
            int idx = m_typeCombo->count() - 1;
            m_typeCombo->setItemData(idx, QColor(120, 120, 140), Qt::ForegroundRole);
        }
        m_typeCombo->addItem(def.name);
    }
    typeRow->addWidget(m_typeCombo);
    mainLayout->addLayout(typeRow);

    // Select current type
    for (int i = 0; i < m_typeCombo->count(); ++i) {
        if (m_typeCombo->itemText(i) == item->modelType()) {
            m_typeCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &XspiceBlockPropertyDialog::onModelTypeChanged);

    // Param form
    auto* paramGroup = new QGroupBox("Parameters");
    m_paramLayout = new QFormLayout(paramGroup);
    mainLayout->addWidget(paramGroup);

    // Preview
    auto* prevGroup = new QGroupBox("SPICE Preview");
    auto* prevLayout = new QVBoxLayout(prevGroup);
    m_preview = new QTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(100);
    m_preview->setStyleSheet("QTextEdit { font-family: monospace; font-size: 9pt; background: #1a1a2e; color: #a0d0ff; }");
    prevLayout->addWidget(m_preview);
    mainLayout->addWidget(prevGroup);

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    rebuildParamForm();
}

void XspiceBlockPropertyDialog::onModelTypeChanged(int index) {
    if (index < 0) return;
    QString text = m_typeCombo->itemText(index);
    if (text.startsWith("──")) return; // separator
    rebuildParamForm();
    m_preview->setText(QString("A[REF] [pins] %1_model\n.model %1_model %1 (...)").arg(text));
}

void XspiceBlockPropertyDialog::rebuildParamForm() {
    // Clear old widgets
    for (auto it = m_paramWidgets.begin(); it != m_paramWidgets.end(); ++it) {
        m_paramLayout->removeRow(it.value());
    }
    m_paramWidgets.clear();

    QString modelType;
    for (int i = 0; i < m_typeCombo->count(); ++i) {
        QString text = m_typeCombo->itemText(i);
        if (!text.startsWith("──")) {
            if (m_typeCombo->currentIndex() == i) {
                modelType = text;
                break;
            }
        }
    }

    const XspiceModelDef* def = nullptr;
    for (const auto& d : XspiceBlockItem::modelDatabase()) {
        if (d.name == modelType) { def = &d; break; }
    }
    if (!def) return;

    QJsonObject currentParams;
    if (m_item && m_item->modelType() == modelType) {
        currentParams = m_item->xspiceParams();
    }

    for (const auto& p : def->params) {
        QWidget* w = nullptr;
        QVariant defaultVal = p.defaultValue;
        QVariant curVal = currentParams.value(p.name).toVariant();
        if (!curVal.isValid()) curVal = defaultVal;

        switch (p.widget) {
        case XspiceParamDef::SpinboxDouble: {
            auto* sb = new QDoubleSpinBox;
            sb->setRange(p.min, p.max);
            sb->setDecimals(6);
            sb->setValue(curVal.toDouble());
            sb->setToolTip(p.description);
            w = sb;
            break;
        }
        case XspiceParamDef::SpinboxInt: {
            auto* sb = new QDoubleSpinBox;
            sb->setRange(p.min, p.max);
            sb->setDecimals(0);
            sb->setValue(curVal.toDouble());
            sb->setToolTip(p.description);
            w = sb;
            break;
        }
        case XspiceParamDef::Checkbox: {
            auto* cb = new QCheckBox;
            cb->setChecked(curVal.toDouble() != 0.0);
            cb->setToolTip(p.description);
            w = cb;
            break;
        }
        case XspiceParamDef::LineEdit:
        default: {
            auto* le = new QLineEdit(curVal.toString());
            le->setToolTip(p.description);
            w = le;
            break;
        }
        }

        if (w) {
            m_paramLayout->addRow(p.name + ":", w);
            m_paramWidgets[p.name] = w;
        }
    }

    // Update preview
    QString preview;
    preview += QString("A[REF] [pins] %1_model\n").arg(def->spiceType);
    preview += QString(".model %1_model %1 (").arg(def->spiceType);
    QStringList paramStrs;
    for (const auto& p : def->params) {
        auto it = m_paramWidgets.find(p.name);
        if (it != m_paramWidgets.end()) {
            QString val;
            if (auto* sb = qobject_cast<QDoubleSpinBox*>(it.value()))
                val = QString::number(sb->value(), 'g', 6);
            else if (auto* cb = qobject_cast<QCheckBox*>(it.value()))
                val = cb->isChecked() ? "1" : "0";
            else if (auto* le = qobject_cast<QLineEdit*>(it.value()))
                val = le->text();
            if (!val.isEmpty() && val != p.defaultValue.toString())
                paramStrs << QString("%1=%2").arg(p.name, val);
        }
    }
    preview += paramStrs.join(" ");
    preview += ")";
    m_preview->setText(preview);
}

QString XspiceBlockPropertyDialog::modelType() const {
    int idx = m_typeCombo->currentIndex();
    QString text = m_typeCombo->itemText(idx);
    // Skip separators — walk back
    while (text.startsWith("──") && idx > 0) {
        text = m_typeCombo->itemText(--idx);
    }
    return text;
}

QJsonObject XspiceBlockPropertyDialog::xspiceParams() const {
    QJsonObject params;
    QString mt = modelType();
    const XspiceModelDef* def = nullptr;
    for (const auto& d : XspiceBlockItem::modelDatabase()) {
        if (d.name == mt) { def = &d; break; }
    }
    if (!def) return params;

    for (const auto& p : def->params) {
        auto it = m_paramWidgets.find(p.name);
        if (it != m_paramWidgets.end()) {
            if (auto* sb = qobject_cast<QDoubleSpinBox*>(it.value()))
                params[p.name] = sb->value();
            else if (auto* cb = qobject_cast<QCheckBox*>(it.value()))
                params[p.name] = cb->isChecked() ? 1.0 : 0.0;
            else if (auto* le = qobject_cast<QLineEdit*>(it.value()))
                params[p.name] = le->text();
        }
    }
    return params;
}

void XspiceBlockPropertyDialog::accept() {
    QDialog::accept();
}
