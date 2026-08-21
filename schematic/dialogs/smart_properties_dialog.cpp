/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "smart_properties_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QToolTip>
#include <QRegularExpression>
#include <QShortcut>

SmartPropertiesDialog::SmartPropertiesDialog(const QList<SchematicItem*>& items, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_items(items), m_undoStack(undoStack), m_scene(scene) {
    setWindowTitle("Properties");
    setMinimumWidth(560);
    setMinimumHeight(480);
    
    // Snapshot original states for live preview reversion
    for (auto* item : m_items) {
        if (item) m_originalStates[item->id()] = item->toJson();
    }

    setStyleSheet(
        "QDialog { background-color: #1a1a22; color: #f8fafc; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #334155; background: #1e222d; border-radius: 6px; top: -1px; }"
        "QTabBar::tab { background: #12141a; padding: 10px 20px; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 4px; color: #94a3b8; font-weight: 600; font-size: 12px; border: 1px solid #334155; border-bottom: none; }"
        "QTabBar::tab:selected { background: #1e222d; border-top: 2px solid #3b82f6; color: #60a5fa; }"
        "QTabBar::tab:hover { background: #242936; color: #f1f5f9; }"
        "QLabel { color: #e2e8f0; font-weight: 600; font-size: 12px; }"
        "QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox { background: #0f1117; border: 1.5px solid #475569; border-radius: 5px; color: #ffffff; padding: 6px 12px; font-size: 13px; min-height: 26px; selection-background-color: #3b82f6; }"
        "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { border: 1.5px solid #3b82f6; background: #161922; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView { background: #1e222d; color: #f8fafc; selection-background-color: #3b82f6; selection-color: white; border: 1px solid #475569; }"
        "QCheckBox { color: #f8fafc; font-size: 12px; font-weight: 600; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1.5px solid #64748b; border-radius: 4px; background: #0f1117; }"
        "QCheckBox::indicator:checked { background: #3b82f6; border-color: #3b82f6; }"
        "QPushButton { background: #334155; color: #ffffff; border: 1px solid #475569; padding: 8px 18px; border-radius: 5px; font-weight: 600; font-size: 12px; min-width: 75px; }"
        "QPushButton:hover { background: #475569; border-color: #64748b; }"
        "QPushButton:pressed { background: #1e293b; }"
        "QDialogButtonBox QPushButton[text='OK'], QDialogButtonBox QPushButton[text='&OK'], QDialogButtonBox QPushButton[text='Apply'], QDialogButtonBox QPushButton[text='&Apply'] { background: #2563eb; border-color: #3b82f6; color: white; }"
        "QDialogButtonBox QPushButton[text='OK']:hover, QDialogButtonBox QPushButton[text='&OK']:hover, QDialogButtonBox QPushButton[text='Apply']:hover, QDialogButtonBox QPushButton[text='&Apply']:hover { background: #1d4ed8; }"
    );

    m_tabWidget = new QTabWidget(this);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);
    mainLayout->addWidget(m_tabWidget);
    
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this);
    
    mainLayout->addWidget(m_buttonBox);
    
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SmartPropertiesDialog::onApply);

    // Ctrl+Enter → apply without closing
    auto* applyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(applyShortcut, &QShortcut::activated, this, &SmartPropertiesDialog::onApply);
}

void SmartPropertiesDialog::addTab(const PropertyTab& tab) {
    m_tabs.append(tab);
    
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none;");

    QWidget* page = new QWidget();
    page->setStyleSheet("background: transparent;");
    QFormLayout* layout = new QFormLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    layout->setLabelAlignment(Qt::AlignRight);
    
    for (const auto& field : tab.fields) {
        createFieldWidget(field, layout);
    }
    
    scroll->setWidget(page);
    m_tabWidget->addTab(scroll, tab.title);
}

void SmartPropertiesDialog::createFieldWidget(const PropertyField& field, QFormLayout* layout) {
    QWidget* widget = nullptr;
    QWidget* rowWidget = nullptr;
    
    switch (field.type) {
        case PropertyField::Text:
        case PropertyField::EngineeringValue:
            widget = new QLineEdit();
            break;
        case PropertyField::MultilineText: {
            auto* edit = new QPlainTextEdit();
            edit->setMinimumHeight(90);
            widget = edit;
            break;
        }
        case PropertyField::Integer: {
            auto* spin = new QSpinBox();
            spin->setRange(-1000000, 1000000);
            widget = spin;
            break;
        }
        case PropertyField::Double: {
            auto* dspin = new QDoubleSpinBox();
            dspin->setRange(-1e12, 1e12);
            dspin->setDecimals(4);
            widget = dspin;
            break;
        }
        case PropertyField::Boolean:
            widget = new QCheckBox();
            break;
        case PropertyField::Choice: {
            auto* combo = new QComboBox();
            combo->addItems(field.choices);
            widget = combo;
            break;
        }
    }
    
    if (widget) {
        widget->setObjectName(field.name);
        widget->setToolTip(field.tooltip);
        
        m_widgets[field.name] = widget;
        
        QLabel* label = new QLabel(field.label + ":");
        if (!field.unit.isEmpty()) {
            QHBoxLayout* h = new QHBoxLayout();
            h->addWidget(widget);
            h->addWidget(new QLabel(field.unit));
            layout->addRow(label, h);
        } else {
            layout->addRow(label, widget);
        }
        
        // Error label (initially hidden)
        QLabel* errLabel = new QLabel();
        errLabel->setStyleSheet("color: #ff4444; font-size: 10px;");
        errLabel->setVisible(false);
        m_errorLabels[field.name] = errLabel;
        layout->addRow("", errLabel);
        
        // Connect change signal
        if (auto* le = qobject_cast<QLineEdit*>(widget))
            connect(le, &QLineEdit::textChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* pe = qobject_cast<QPlainTextEdit*>(widget))
            connect(pe, &QPlainTextEdit::textChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* cb = qobject_cast<QCheckBox*>(widget))
            connect(cb, &QCheckBox::toggled, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* cmb = qobject_cast<QComboBox*>(widget))
            connect(cmb, &QComboBox::currentIndexChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            connect(dsb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SmartPropertiesDialog::onFieldChanged);
    }
}

QVariant SmartPropertiesDialog::getPropertyValue(const QString& name) const {
    QWidget* w = m_widgets.value(name);
    if (!w) return QVariant();
    
    if (auto* le = qobject_cast<QLineEdit*>(w)) return le->text();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) return pe->toPlainText();
    if (auto* cb = qobject_cast<QCheckBox*>(w)) return cb->isChecked();
    if (auto* cmb = qobject_cast<QComboBox*>(w)) return cmb->currentText();
    if (auto* sb = qobject_cast<QSpinBox*>(w)) return sb->value();
    if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) return dsb->value();
    
    return QVariant();
}

void SmartPropertiesDialog::setPropertyValue(const QString& name, const QVariant& value) {
    QWidget* w = m_widgets.value(name);
    if (!w) return;
    
    if (auto* le = qobject_cast<QLineEdit*>(w)) le->setText(value.toString());
    else if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) pe->setPlainText(value.toString());
    else if (auto* cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(value.toBool());
    else if (auto* cmb = qobject_cast<QComboBox*>(w)) cmb->setCurrentText(value.toString());
    else if (auto* sb = qobject_cast<QSpinBox*>(w)) sb->setValue(value.toInt());
    else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) dsb->setValue(value.toDouble());
}

void SmartPropertiesDialog::setTabVisible(int index, bool visible) {
    if (m_tabWidget) {
        m_tabWidget->setTabVisible(index, visible);
    }
}

bool SmartPropertiesDialog::validateAll() {
    bool allValid = true;
    for (const auto& tab : m_tabs) {
        for (const auto& field : tab.fields) {
            QString err;
            QVariant value = getPropertyValue(field.name);
            
            if (field.type == PropertyField::EngineeringValue) {
                QString s = value.toString().trimmed();
                if (!s.isEmpty()) {
                    QRegularExpression re("^([\\-+]?\\d*\\.?\\d+)([kMGTunpfμ]?[ΩFHV]?)$");
                    if (!re.match(s).hasMatch()) {
                        err = "Invalid engineering value (e.g., 10k, 4.7u)";
                    }
                }
            }
            
            if (err.isEmpty() && field.validator) {
                err = field.validator(value);
            }
            
            QLabel* l = m_errorLabels.value(field.name);
            if (l) {
                l->setText(err);
                l->setVisible(!err.isEmpty());
            }
            if (!err.isEmpty()) allValid = false;
        }
    }
    return allValid;
}

void SmartPropertiesDialog::onFieldChanged() {
    if (validateAll()) {
        applyPreview();
    }
}

void SmartPropertiesDialog::applyPreview() {
    // To be implemented by subclasses
}

void SmartPropertiesDialog::revertToOriginal() {
    for (auto* item : m_items) {
        if (item && m_originalStates.contains(item->id())) {
            item->fromJson(m_originalStates[item->id()]);
            item->update();
        }
    }
}

void SmartPropertiesDialog::reject() {
    revertToOriginal();
    QDialog::reject();
}

void SmartPropertiesDialog::onApply() {
    if (validateAll()) {
        // To be implemented by subclasses to push commands to undo stack
    }
}

void SmartPropertiesDialog::accept() {
    onApply();
    QDialog::accept();
}
