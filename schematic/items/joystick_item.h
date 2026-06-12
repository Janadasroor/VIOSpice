/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JOYSTICK_ITEM_H
#define JOYSTICK_ITEM_H

#include "schematic_item.h"

/**
 * @brief An interactive 2D X-Y pad for tuning two parameters simultaneously.
 */
class JoystickItem : public SchematicItem {
    Q_OBJECT
public:
    explicit JoystickItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    ItemType itemType() const override { return ItemType::CustomType; }
    QString itemTypeName() const override { return "Joystick"; }
    QString referencePrefix() const override { return "JS"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override { return {}; }

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    void setXRange(double min, double max) { m_xMin = min; m_xMax = max; update(); }
    void setYRange(double min, double max) { m_yMin = min; m_yMax = max; update(); }

    void setTargetX(const QString& p) { m_targetX = p; }
    void setTargetY(const QString& p) { m_targetY = p; }

    bool isInteractive() const override { return true; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void triggerSimulationUpdate();

    double m_xMin = 0.0, m_xMax = 1.0;
    double m_yMin = 0.0, m_yMax = 1.0;
    double m_xCurr = 0.5, m_yCurr = 0.5;

    QString m_targetX, m_targetY;
    bool m_dragging = false;

    const double m_padSize = 100.0;
};

#endif // JOYSTICK_ITEM_H
