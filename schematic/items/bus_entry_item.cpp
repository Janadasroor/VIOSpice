/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bus_entry_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>

BusEntryItem::BusEntryItem(QPointF pos, bool flipped, QGraphicsItem *parent)
    : SchematicItem(parent), m_flipped(flipped) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setZValue(2);
    updatePen();
}

void BusEntryItem::updatePen() {
    QColor color = QColor("#3b82f6");
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->schematicBus();
        if (color == Qt::transparent) color = QColor("#3b82f6");
    }
    m_pen = QPen(color, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void BusEntryItem::setFlipped(bool flipped) {
    if (m_flipped != flipped) {
        prepareGeometryChange();
        m_flipped = flipped;
        update();
    }
}

void BusEntryItem::toggleFlip() {
    prepareGeometryChange();
    m_flipped = !m_flipped;
    update();
}

void BusEntryItem::rotateClockwise() {
    prepareGeometryChange();
    setRotation(std::fmod(rotation() + 90.0, 360.0));
    update();
}

QPointF BusEntryItem::localP1() const {
    return m_flipped ? QPointF(-10, 10) : QPointF(-10, -10);
}

QPointF BusEntryItem::localP2() const {
    return m_flipped ? QPointF(10, -10) : QPointF(10, 10);
}

QPointF BusEntryItem::sceneP1() const {
    return mapToScene(localP1());
}

QPointF BusEntryItem::sceneP2() const {
    return mapToScene(localP2());
}

QRectF BusEntryItem::boundingRect() const {
    return QRectF(-14, -14, 28, 28);
}

void BusEntryItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen p = m_pen;
    if (isSelected()) {
        p.setColor(QColor("#facc15")); // Bright amber-yellow when selected
        p.setWidthF(3.2);
    }
    
    const QPointF p1 = localP1();
    const QPointF p2 = localP2();

    // Draw glowing selection underlay if selected
    if (isSelected()) {
        QPen glowPen(QColor(250, 204, 21, 60), 6.0, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(glowPen);
        painter->drawLine(p1, p2);
    }

    painter->setPen(p);
    painter->drawLine(p1, p2);
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> BusEntryItem::connectionPoints() const {
    return { localP1(), localP2() };
}

QJsonObject BusEntryItem::toJson() const {
    QJsonObject json;
    json["type"] = "BusEntry";
    json["id"] = m_id.toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["rotation"] = rotation();
    json["isMirroredX"] = isMirroredX();
    json["isMirroredY"] = isMirroredY();
    json["flipped"] = m_flipped;
    return json;
}

bool BusEntryItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setRotation(json["rotation"].toDouble(0.0));
    setMirroredX(json["isMirroredX"].toBool(false));
    setMirroredY(json["isMirroredY"].toBool(false));
    m_flipped = json["flipped"].toBool();
    updatePen();
    update();
    return true;
}

SchematicItem* BusEntryItem::clone() const {
    auto* item = new BusEntryItem(pos(), m_flipped);
    item->setRotation(rotation());
    item->setMirroredX(isMirroredX());
    item->setMirroredY(isMirroredY());
    return item;
}
