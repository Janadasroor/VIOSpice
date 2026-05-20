#include "analog_function_dialog.h"
#include "../items/analog_function_item.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QScrollArea>

AnalogFunctionDialog::AnalogFunctionDialog(AnalogFunctionItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("Analog Function Properties");
    setModal(true);
    setMinimumWidth(350);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel("Function type:"));
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems(AnalogFunctionItem::availableFunctions());
    m_typeCombo->setCurrentText(m_item->functionType());
    layout->addWidget(m_typeCombo);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto* paramWidget = new QWidget();
    m_paramLayout = new QFormLayout(paramWidget);
    scroll->setWidget(paramWidget);
    layout->addWidget(scroll);

    rebuildFields(m_item->functionType());

    connect(m_typeCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged), this, &AnalogFunctionDialog::onTypeChanged);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AnalogFunctionDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AnalogFunctionDialog::onTypeChanged(const QString& type) {
    rebuildFields(type);
}

void AnalogFunctionDialog::rebuildFields(const QString& type) {
    while (m_paramLayout->rowCount() > 0)
        m_paramLayout->removeRow(0);
    m_spins.clear();

    auto addField = [&](const QString& name, double value, double step = 0.1) {
        auto* spin = new QDoubleSpinBox();
        spin->setRange(-1e12, 1e12);
        spin->setDecimals(9);
        spin->setValue(value);
        spin->setSingleStep(step);
        m_paramLayout->addRow(name + ":", spin);
        m_spins[name] = spin;
    };

    if (type == "gain") {
        addField("gain", m_item->param("gain", 1.0));
        addField("in_offset", m_item->param("in_offset", 0.0));
        addField("out_offset", m_item->param("out_offset", 0.0));
    } else if (type == "hyst") {
        addField("in_low", m_item->param("in_low", 0.0));
        addField("in_high", m_item->param("in_high", 1.0));
        addField("hyst", m_item->param("hyst", 0.1));
        addField("out_low", m_item->param("out_low", 0.0));
        addField("out_high", m_item->param("out_high", 5.0));
    } else if (type == "int") {
        addField("gain", m_item->param("gain", 1.0));
        addField("in_offset", m_item->param("in_offset", 0.0));
        addField("out_lower_limit", m_item->param("out_lower_limit", -1e6));
        addField("out_upper_limit", m_item->param("out_upper_limit", 1e6));
    } else if (type == "d_dt") {
        addField("gain", m_item->param("gain", 1.0));
        addField("out_offset", m_item->param("out_offset", 0.0));
    } else if (type == "limit") {
        addField("gain", m_item->param("gain", 1.0));
        addField("in_offset", m_item->param("in_offset", 0.0));
        addField("out_lower_limit", m_item->param("out_lower_limit", -1.0));
        addField("out_upper_limit", m_item->param("out_upper_limit", 1.0));
        addField("limit_range", m_item->param("limit_range", 0.01));
    } else if (type == "slew") {
        addField("rise_slope", m_item->param("rise_slope", 1e-9), 1e-9);
        addField("fall_slope", m_item->param("fall_slope", 1e-9), 1e-9);
    }
}

void AnalogFunctionDialog::onAccept() {
    m_item->setFunctionType(m_typeCombo->currentText());
    for (auto it = m_spins.begin(); it != m_spins.end(); ++it)
        m_item->setParam(it.key(), it.value()->value());
    accept();
}
