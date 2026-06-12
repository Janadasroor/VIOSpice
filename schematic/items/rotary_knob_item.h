/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ROTARY_KNOB_ITEM_H
#define ROTARY_KNOB_ITEM_H

#include "schematic_item.h"

/**
 * @brief An interactive rotary dial for tuning SPICE parameters or Flux variables.
 * Provides a professional lab-instrument feel.
 */
class RotaryKnobItem : public SchematicItem {
    Q_OBJECT
public:
    explicit RotaryKnobItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    ItemType itemType() const override { return ItemType::CustomType; }
    QString itemTypeName() const override { return "RotaryKnob"; }
    QString referencePrefix() const override { return "KN"; }

    virtual void rebuildPrimitives() override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override { return {}; } // No electrical pins

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    void setValueRange(double min, double max) { m_min = min; m_max = max; update(); }
    double currentValue() const { return m_current; }
    void setCurrentValue(double v);

    QString targetParameter() const { return m_targetParam; }
    void setTargetParameter(const QString& p) { m_targetParam = p; }

    bool isInteractive() const override { return true; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    double angleFromPos(const QPointF& pos) const;
    void triggerSimulationUpdate();

    double m_min = 0.0;
    double m_max = 100.0;
    double m_current = 50.0;
    double m_angle = 0.0; // Current indicator angle in degrees
    
    QString m_targetParam;
    bool m_dragging = false;
    
    const double m_radius = 25.0;
};

#endif // ROTARY_KNOB_ITEM_H
