/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_TEARDROP_ITEM_H
#define PCB_TEARDROP_ITEM_H

#include "pcb_item.h"
#include <QPolygonF>
#include <QPainterPath>

/**
 * @brief Teardrop Copper Item for PCB Editor
 * 
 * Represents a smooth curved or filleted copper teardrop transition
 * between pads/vias and traces.
 */
class TeardropItem : public PCBItem {
public:
    explicit TeardropItem(QGraphicsItem* parent = nullptr);
    TeardropItem(const QPolygonF& polygon, int layer, const QString& netName = "", QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Teardrop"; }
    ItemType itemType() const override { return TeardropType; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    QPolygonF polygon() const { return m_polygon; }
    void setPolygon(const QPolygonF& poly);

private:
    QPolygonF m_polygon;
};

#endif // PCB_TEARDROP_ITEM_H
