/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "transistor_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>

namespace {
constexpr qreal kGridAlignedRightPinX = 15.0;
}

TransistorItem::TransistorItem(QPointF pos, QString value, TransistorType type, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value), m_transistorType(type) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(45, -30), QPointF(45, 30));
}

void TransistorItem::buildPrimitives() {
    m_primitives.clear();
    
    if (m_transistorType == NPN || m_transistorType == PNP) {
        // BJT Transistor Symbol (Vertical)
        // Base bar
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -30), QPointF(0, 30)));
        // Base terminal
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-30, 0), QPointF(0, 0)));
        
        // Collector: Diagonal + Vertical Post
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -15), QPointF(45, -45)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(45, -45), QPointF(45, -60)));
        
        // Emitter: Diagonal + Vertical Post
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 15), QPointF(45, 45)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(45, 45), QPointF(45, 60)));
        
        // Arrow on emitter
        QList<QPointF> arrow;
        if (m_transistorType == NPN) {
            // Arrow pointing away from base (at emitter post start)
            arrow << QPointF(45, 45) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
        } else {
            // Arrow pointing toward base (at emitter bar start)
            arrow << QPointF(0, 15) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
        }
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(arrow, false));
        
    } else {
        // MOSFET Symbol (Vertical)
        // 3-segment dashed channel (at x=0)
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -37.5), QPointF(0, -22.5)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -7.5), QPointF(0, 7.5)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 22.5), QPointF(0, 37.5)));
        
        // Gate bar (at x=-7.5)
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-7.5, -30), QPointF(-7.5, 30)));
        // Gate terminal
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-22.5, 0), QPointF(-7.5, 0)));
        
        // Drain terminal
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -30), QPointF(30, -30)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(30, -30), QPointF(30, -45)));
        
        // Source terminal
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 30), QPointF(30, 30)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(30, 30), QPointF(30, 45)));
        
        // Source-Bulk tie
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 0), QPointF(30, 0)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(30, 0), QPointF(30, 30)));
        
        QList<QPointF> arrow;
        if (m_transistorType == NMOS) {
            // Arrow pointing toward channel
            arrow << QPointF(0, 0) << QPointF(7.5, -3.75) << QPointF(7.5, 3.75);
        } else {
            // Arrow pointing away from channel
            arrow << QPointF(7.5, 0) << QPointF(0, -3.75) << QPointF(0, 3.75);
        }
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(arrow, false));
    }
    
    // Pin dots (terminal tips)
    if (m_transistorType == NPN || m_transistorType == PNP) {
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-30, 0), 3.75, true)); // Base
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(45, -60), 3.75, true)); // Collector
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(45, 60), 3.75, true)); // Emitter
    } else {
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-22.5, 0), 3.75, true)); // Gate
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(30, -45), 3.75, true)); // Drain
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(30, 45), 3.75, true)); // Source
    }
}

void TransistorItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        m_spiceModel = value.trimmed();
        updateLabelText();
        buildPrimitives();
        update();
    }
}

void TransistorItem::setTransistorType(TransistorType type) {
    if (m_transistorType != type) {
        m_transistorType = type;
        buildPrimitives();
        update();
    }
}

QRectF TransistorItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void TransistorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

QJsonObject TransistorItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["spiceModel"] = m_spiceModel;
    json["excludeFromSim"] = m_excludeFromSimulation;
    json["excludeFromPcb"] = m_excludeFromPcb;
    json["isLocked"] = m_isLocked;
    json["isMirroredX"] = m_isMirroredX;
    json["isMirroredY"] = m_isMirroredY;
    json["rotation"] = rotation();

    QJsonObject exprs;
    for (auto it = m_paramExpressions.begin(); it != m_paramExpressions.end(); ++it) exprs[it.key()] = it.value();
    json["paramExpressions"] = exprs;

    QJsonObject tols;
    for (auto it = m_tolerances.begin(); it != m_tolerances.end(); ++it) tols[it.key()] = it.value();
    json["tolerances"] = tols;

    json["transistorType"] = static_cast<int>(m_transistorType);
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

bool TransistorItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;

    if (json.contains("id")) setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    if (json.contains("spiceModel")) m_spiceModel = json["spiceModel"].toString();
    if (json.contains("excludeFromSim")) m_excludeFromSimulation = json["excludeFromSim"].toBool(false);
    if (json.contains("excludeFromPcb")) m_excludeFromPcb = json["excludeFromPcb"].toBool(false);
    if (json.contains("isLocked")) m_isLocked = json["isLocked"].toBool(false);
    if (json.contains("isMirroredX")) m_isMirroredX = json["isMirroredX"].toBool(false);
    if (json.contains("isMirroredY")) m_isMirroredY = json["isMirroredY"].toBool(false);
    if (json.contains("rotation")) setRotation(json["rotation"].toDouble());

    m_paramExpressions.clear();
    if (json.contains("paramExpressions")) {
        QJsonObject exprs = json["paramExpressions"].toObject();
        for (auto it = exprs.begin(); it != exprs.end(); ++it) m_paramExpressions[it.key()] = it.value().toString();
    }

    m_tolerances.clear();
    if (json.contains("tolerances")) {
        QJsonObject tols = json["tolerances"].toObject();
        for (auto it = tols.begin(); it != tols.end(); ++it) m_tolerances[it.key()] = it.value().toString();
    }

    loadPinPadMappingFromJson(json);
    m_transistorType = static_cast<TransistorType>(json["transistorType"].toInt());
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    buildPrimitives();
    createLabels(QPointF(45, -30), QPointF(45, 30));
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    updateLabelText();
    update();
    return true;
}

SchematicItem* TransistorItem::clone() const {
    auto* newItem = new TransistorItem(pos(), m_value, m_transistorType, parentItem());
    QJsonObject state = toJson();
    state["id"] = QUuid::createUuid().toString();
    newItem->fromJson(state);
    return newItem;
}

QString TransistorItem::pinName(int index) const {
    if (m_transistorType == NPN || m_transistorType == PNP) {
        switch (index) {
            case 0: return "B";
            case 1: return "C";
            case 2: return "E";
        }
    } else {
        switch (index) {
            case 0: return "G";
            case 1: return "D";
            case 2: return "S";
        }
    }
    return QString::number(index + 1);
}

QList<QPointF> TransistorItem::connectionPoints() const {
    QList<QPointF> points;
    if (m_transistorType == NPN || m_transistorType == PNP) {
        points.append(QPointF(-30, 0));  // Base
        points.append(QPointF(45, -60)); // Collector
        points.append(QPointF(45, 60));  // Emitter
    } else {
        points.append(QPointF(-22.5, 0)); // Gate
        points.append(QPointF(30, -45));  // Drain
        points.append(QPointF(30, 45));   // Source
    }
    return points;
}
