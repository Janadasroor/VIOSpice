/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BUSENTRYITEM_H
#define BUSENTRYITEM_H

#include "schematic_item.h"
#include <QPen>

class BusEntryItem : public SchematicItem {
public:
    enum OrientationMode {
        DiagonalSlash = 0,     // "/" from (-10, 10) to (10, -10)
        DiagonalBackslash = 1  // "\" from (-10, -10) to (10, 10)
    };

    BusEntryItem(QPointF pos = QPointF(), bool flipped = false, QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "BusEntry"; }
    ItemType itemType() const override { return static_cast<ItemType>(SchematicItem::CustomType + 1); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QList<QPointF> connectionPoints() const override;
    bool supportsTransformAction(TransformAction action) const override { Q_UNUSED(action) return true; }

    bool flipped() const { return m_flipped; }
    void setFlipped(bool flipped);
    void toggleFlip();
    void rotateClockwise();

    QPointF localP1() const;
    QPointF localP2() const;
    QPointF sceneP1() const;
    QPointF sceneP2() const;

private:
    void updatePen();
    bool m_flipped;
    QPen m_pen;
};

#endif // BUSENTRYITEM_H
