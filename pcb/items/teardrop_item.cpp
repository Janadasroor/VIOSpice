/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "teardrop_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>

TeardropItem::TeardropItem(QGraphicsItem* parent)
    : PCBItem(parent) {
    setZValue(10);
}

TeardropItem::TeardropItem(const QPolygonF& polygon, int layer, const QString& netName, QGraphicsItem* parent)
    : PCBItem(parent), m_polygon(polygon) {
    setLayer(layer);
    setNetName(netName);
    setZValue(10);
}

QRectF TeardropItem::boundingRect() const {
    return m_polygon.boundingRect();
}

QPainterPath TeardropItem::shape() const {
    QPainterPath p;
    p.addPolygon(m_polygon);
    return p;
}

#include "../layers/pcb_layer.h"

void TeardropItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (!painter || m_polygon.isEmpty()) return;

    PCBTheme* theme = ThemeManager::theme();
    PCBLayer* l = PCBLayerManager::instance().layer(layer());
    QColor col = l ? l->color() : theme->trace();
    if (isSelected()) col = col.lighter(140);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(col));
    painter->drawPolygon(m_polygon);
}

QJsonObject TeardropItem::toJson() const {
    QJsonObject json = PCBItem::toJson();
    json["type"] = "Teardrop";
    QJsonArray pts;
    for (const QPointF& p : m_polygon) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        pts.append(pt);
    }
    json["polygon"] = pts;
    return json;
}

bool TeardropItem::fromJson(const QJsonObject& json) {
    if (!PCBItem::fromJson(json)) return false;
    QPolygonF poly;
    QJsonArray pts = json["polygon"].toArray();
    for (const QJsonValue& val : pts) {
        QJsonObject pt = val.toObject();
        poly.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
    }
    setPolygon(poly);
    return true;
}

PCBItem* TeardropItem::clone() const {
    TeardropItem* c = new TeardropItem(m_polygon, layer(), netName());
    c->setPos(pos());
    return c;
}

void TeardropItem::setPolygon(const QPolygonF& poly) {
    prepareGeometryChange();
    m_polygon = poly;
    update();
}
