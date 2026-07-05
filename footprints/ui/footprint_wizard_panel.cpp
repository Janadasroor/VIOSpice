/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_wizard_panel.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>

FootprintWizardPanel::FootprintWizardPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void FootprintWizardPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QGroupBox* wizGroup = new QGroupBox("Footprint Wizard", this);
    QFormLayout* wizForm = new QFormLayout(wizGroup);
    wizForm->setContentsMargins(10, 25, 10, 10);
    
    m_wizType = new QComboBox(this);
    m_wizType->addItems({"DIP", "SOIC", "Passive (0603/0805)", "Passive (TH Axial)"});
    
    m_wizPins = new QSpinBox(this); m_wizPins->setRange(2, 100); m_wizPins->setValue(8);
    m_wizPitch = new QDoubleSpinBox(this); m_wizPitch->setRange(0.1, 10); m_wizPitch->setValue(2.54);
    m_wizSpan = new QDoubleSpinBox(this); m_wizSpan->setRange(1, 100); m_wizSpan->setValue(7.62);
    m_wizPadW = new QDoubleSpinBox(this); m_wizPadW->setRange(0.1, 10); m_wizPadW->setValue(1.5);
    m_wizPadH = new QDoubleSpinBox(this); m_wizPadH->setRange(0.1, 10); m_wizPadH->setValue(1.5);
    
    QString wizInputStyle = "QSpinBox, QDoubleSpinBox, QComboBox { "
                            "background-color: #2d2d2d; color: #ececec; "
                            "border: 1px solid #3c3c3c; border-radius: 4px; "
                            "padding: 4px; min-height: 24px; }"
                            "QSpinBox::up-button, QDoubleSpinBox::up-button, "
                            "QSpinBox::down-button, QDoubleSpinBox::down-button { "
                            "background-color: #3d3d3d; width: 16px; border-left: 1px solid #3c3c3c; }"
                            "QComboBox::drop-down { border-left: 1px solid #3c3c3c; width: 20px; }";
    
    m_wizType->setStyleSheet(wizInputStyle);
    m_wizPins->setStyleSheet(wizInputStyle);
    m_wizPitch->setStyleSheet(wizInputStyle);
    m_wizSpan->setStyleSheet(wizInputStyle);
    m_wizPadW->setStyleSheet(wizInputStyle);
    m_wizPadH->setStyleSheet(wizInputStyle);

    wizForm->addRow("Type:", m_wizType);
    wizForm->addRow("Pins:", m_wizPins);
    wizForm->addRow("Pitch:", m_wizPitch);
    wizForm->addRow("Row Span:", m_wizSpan);
    wizForm->addRow("Pad W:", m_wizPadW);
    wizForm->addRow("Pad H:", m_wizPadH);

    QPushButton* wizBtn = new QPushButton("Generate & Save", this);
    wizBtn->setCursor(Qt::PointingHandCursor);
    wizBtn->setStyleSheet("QPushButton { background-color: #059669; color: white; font-weight: bold; padding: 8px; border-radius: 4px; border: none; }"
                          "QPushButton:hover { background-color: #047857; }"
                          "QPushButton:pressed { background-color: #065f46; }");
    connect(wizBtn, &QPushButton::clicked, this, &FootprintWizardPanel::onGenerate);
    wizForm->addRow(wizBtn);

    QPushButton* importKiCadWizardBtn = new QPushButton("Import KiCad Footprint", this);
    connect(importKiCadWizardBtn, &QPushButton::clicked, this, &FootprintWizardPanel::importKicadFootprintRequested);
    wizForm->addRow(importKiCadWizardBtn);

    layout->addWidget(wizGroup);
    layout->addStretch();
}

void FootprintWizardPanel::onGenerate() {
    FootprintDefinition newDef;
    newDef.clearPrimitives();

    QString type = m_wizType->currentText();
    int pins = m_wizPins->value();
    double pitch = m_wizPitch->value();
    double span = m_wizSpan->value();
    QSizeF padSize(m_wizPadW->value(), m_wizPadH->value());

    if (type == "DIP" || type == "SOIC") {
        int half = pins / 2;
        for (int i = 0; i < half; ++i) {
            double y = (i - (half-1)/2.0) * pitch;
            QString shape1 = (i == 0) ? "Rect" : (type == "DIP" ? "Round" : "Rect");
            QString shape2 = (type == "DIP") ? "Round" : "Rect";
            
            auto p1 = FootprintPrimitive::createPad(QPointF(-span/2, y), QString::number(i+1), shape1, padSize);
            auto p2 = FootprintPrimitive::createPad(QPointF(span/2, -y), QString::number(half+i+1), shape2, padSize);
            
            p1.layer = FootprintPrimitive::Top_Copper;
            p2.layer = FootprintPrimitive::Top_Copper;

            if (type == "DIP") {
                p1.data["drill_size"] = 0.8;
                p2.data["drill_size"] = 0.8;
            }
            
            newDef.addPrimitive(p1);
            newDef.addPrimitive(p2);
        }

        double yMax = ((pins/2 - 1) / 2.0) * pitch;
        FootprintPrimitive fabRect = FootprintPrimitive::createRect(QRectF(-span/2 + 0.5, -yMax - 0.5, span - 1.0, yMax*2 + 1.0).normalized());
        fabRect.layer = FootprintPrimitive::Top_Fabrication;
        newDef.addPrimitive(fabRect);

        FootprintPrimitive courtRect = FootprintPrimitive::createRect(QRectF(-span/2 - 1.0, -yMax - 1.0, span + 2.0, yMax*2 + 2.0).normalized());
        courtRect.layer = FootprintPrimitive::Top_Courtyard;
        newDef.addPrimitive(courtRect);

        newDef.setName(QString("%1-%2").arg(type).arg(pins));
    } else if (type == "Passive (TH Axial)") {
        auto p1 = FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Rect", padSize);
        p1.data["drill_size"] = 0.8;
        auto p2 = FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", padSize);
        p2.data["drill_size"] = 0.8;
        
        newDef.addPrimitive(p1);
        newDef.addPrimitive(p2);
        newDef.setName("R_Axial_TH");
    } else if (type.startsWith("Passive")) {
        newDef.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Rect", padSize));
        newDef.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Rect", padSize));
        newDef.setName("R_0805");
    }

    emit footprintGenerated(newDef);
}
