/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ic_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>

ICItem::ICItem(QPointF pos, QString value, int pinCount, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value), m_pinCount(pinCount) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    // Default label positions for IC
    qreal halfH = (m_pinCount / 2 * 30.0 + 15) / 2;
    qreal halfW = 90.0 / 2;
    createLabels(QPointF(-halfW, -halfH - 22.5), QPointF(-halfW, halfH + 15));
}

void ICItem::buildPrimitives() {
    m_primitives.clear();
    
    // Calculate IC body size based on pin count
    int pinsPerSide = m_pinCount / 2;
    qreal pinSpacing = 30.0;
    qreal bodyHeight = pinsPerSide * pinSpacing;
    qreal bodyWidth = 90.0;
    qreal halfH = bodyHeight / 2;
    qreal halfW = bodyWidth / 2;
    
    // IC body rectangle
    m_primitives.push_back(std::make_unique<RectPrimitive>(
        QRectF(-halfW, -halfH, bodyWidth, bodyHeight), false));
    
    // Notch at top (facing up for vertical IC)
    m_primitives.push_back(std::make_unique<ArcPrimitive>(
        QRectF(-11.25, -halfH - 5.625, 22.5, 11.25), 0, 180 * 16));
    
    // Pin 1 dot indicator
    m_primitives.push_back(std::make_unique<CirclePrimitive>(
        QPointF(-halfW + 11.25, -halfH + 11.25), 3.75, true));
    
    // Left side pins
    for (int i = 0; i < pinsPerSide; ++i) {
        qreal y = -halfH + 15.0 + i * pinSpacing;
        // Pin lead (15 units)
        m_primitives.push_back(std::make_unique<LinePrimitive>(
            QPointF(-halfW, y), QPointF(-halfW - 15.0, y)));
        // Connection dot
        m_primitives.push_back(std::make_unique<CirclePrimitive>(
            QPointF(-halfW - 15.0, y), 3.75, true));
    }
    
    // Right side pins (numbered from bottom up)
    for (int i = 0; i < pinsPerSide; ++i) {
        qreal y = halfH - 15.0 - i * pinSpacing;
        // Pin lead (15 units)
        m_primitives.push_back(std::make_unique<LinePrimitive>(
            QPointF(halfW, y), QPointF(halfW + 15.0, y)));
        // Connection dot
        m_primitives.push_back(std::make_unique<CirclePrimitive>(
            QPointF(halfW + 15.0, y), 3.75, true));
    }
}

void ICItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        buildPrimitives();
        update();
    }
}

void ICItem::setPinCount(int count) {
    if (m_pinCount != count && count >= 4 && count % 2 == 0) {
        m_pinCount = count;
        buildPrimitives();
        update();
    }
}

QRectF ICItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void ICItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    for (const auto& prim : m_primitives) {
        prim->paint(painter, m_pen, m_brush);
    }

    // Draw highlighted connection points
    drawConnectionPointHighlights(painter);

    if (isSelected()) {
        PCBTheme* theme = ThemeManager::theme();
        painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }
}

QJsonObject ICItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["manufacturer"] = manufacturer();
    json["mpn"] = mpn();
    json["description"] = description();
    json["pinCount"] = m_pinCount;
    json["x"] = pos().x();
    json["y"] = pos().y();

    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }
    json["pinPadMapping"] = pinPadMappingToJson();
    return json;
}

bool ICItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;

    setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    loadPinPadMappingFromJson(json);
    setManufacturer(json["manufacturer"].toString());
    setMpn(json["mpn"].toString());
    setDescription(json["description"].toString());
    m_pinCount = json["pinCount"].toInt(8);
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    buildPrimitives();
    // Default labels for IC
    int pinsPerSide = m_pinCount / 2;
    qreal halfH = (pinsPerSide * 30.0 + 15) / 2;
    qreal halfW = 90.0 / 2;
    createLabels(QPointF(-halfW, -halfH - 22.5), QPointF(-halfW, halfH + 15));
    
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    update();
    return true;
}

SchematicItem* ICItem::clone() const {
    ICItem* newItem = new ICItem(pos(), m_value, m_pinCount, parentItem());
    newItem->setName(name());
    return newItem;
}

QList<QPointF> ICItem::connectionPoints() const {
    QList<QPointF> points;
    int pinsPerSide = m_pinCount / 2;
    qreal pinSpacing = 30.0;
    qreal bodyHeight = pinsPerSide * pinSpacing;
    qreal bodyWidth = 90.0;
    qreal halfH = bodyHeight / 2;
    qreal halfW = bodyWidth / 2;

    // Left side pins
    for (int i = 0; i < pinsPerSide; ++i) {
        qreal y = -halfH + 15.0 + i * pinSpacing;
        points.append(QPointF(-halfW - 15.0, y));
    }

    // Right side pins (numbered from bottom up)
    for (int i = 0; i < pinsPerSide; ++i) {
        qreal y = halfH - 15.0 - i * pinSpacing;
        points.append(QPointF(halfW + 15.0, y));
    }
    return points;
}

