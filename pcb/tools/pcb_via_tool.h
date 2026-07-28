/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCBVIATOOL_H
#define PCBVIATOOL_H

#include "pcb_tool.h"

/**
 * PCB Via Tool - Places vias on the PCB
 * 
 * Vias connect traces between layers.
 */
class ViaItem;

class PCBViaTool : public PCBTool {
    Q_OBJECT
public:
    explicit PCBViaTool(QObject* parent = nullptr);

    QCursor cursor() const override;

    void activate(class PCBView* view) override;
    void deactivate() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

    // Via properties
    double viaDiameter() const { return m_viaDiameter; }
    void setViaDiameter(double diameter);
    
    double holeDiameter() const { return m_holeDiameter; }
    void setHoleDiameter(double diameter);

    int startLayer() const { return m_startLayer; }
    void setStartLayer(int layer);

    int endLayer() const { return m_endLayer; }
    void setEndLayer(int layer);

    bool microviaMode() const { return m_microviaMode; }
    void setMicroviaMode(bool enable);

private:
    void updatePreview();

    double m_viaDiameter;  // Outer copper diameter
    double m_holeDiameter; // Drill hole diameter
    int m_startLayer;
    int m_endLayer;
    bool m_microviaMode;

    ViaItem* m_previewVia;
};

#endif // PCBVIATOOL_H
