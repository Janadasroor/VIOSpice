/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "capacitor_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>

CapacitorItem::CapacitorItem(QPointF pos, QString value, CapacitorStyle style, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value), m_style(style) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(-18, -37.5), QPointF(-18, 37.5));
}

void CapacitorItem::buildPrimitives() {
    m_primitives.clear();
    
    // Left plate (vertical line at x=-5)
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-5, -15), QPointF(-5, 15)));
    // Left lead wire
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-45, 0), QPointF(-5, 0)));
    
    // Right plate
    if (m_style == Polarized) {
        // Curved plate for polarized (arc)
        m_primitives.push_back(std::make_unique<ArcPrimitive>(QRectF(5, -15, 10, 30), 90 * 16, 180 * 16));
        m_primitives.push_back(std::make_unique<TextPrimitive>("+", QPointF(-22.5, -12), 12));
        
        // Right lead wire (starts from arc edge)
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(15, 0), QPointF(45, 0)));
    } else {
        // Standard vertical line at x=5
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(5, -15), QPointF(5, 15)));
        // Right lead wire
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(5, 0), QPointF(45, 0)));
    }
    
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-45, 0), 3.75, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(45, 0), 3.75, true));
}

void CapacitorItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        updateLabelText();
        buildPrimitives();
        update();
    }
}

QRectF CapacitorItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void CapacitorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

QJsonObject CapacitorItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["x"] = pos().x();
    json["y"] = pos().y();
    
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["manufacturer"] = manufacturer();
    json["mpn"] = mpn();
    json["description"] = description();
    json["style"] = static_cast<int>(m_style);
    json["rotation"] = rotation();

    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }

    QJsonArray primArray;
    for (const auto& prim : m_primitives) {
        primArray.append(prim->toJson());
    }
    json["primitives"] = primArray;
    json["pinPadMapping"] = pinPadMappingToJson();
    
    return json;
}

bool CapacitorItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    setManufacturer(json["manufacturer"].toString());
    setMpn(json["mpn"].toString());
    setDescription(json["description"].toString());
    if (json.contains("style")) {
        m_style = static_cast<CapacitorStyle>(json["style"].toInt());
    }
    if (json.contains("rotation")) {
        setRotation(json["rotation"].toDouble());
    }
    loadPinPadMappingFromJson(json);

    buildPrimitives();
    createLabels(QPointF(-18, -37.5), QPointF(-18, 37.5));
    
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

SchematicItem* CapacitorItem::clone() const {
    CapacitorItem* newItem = new CapacitorItem(pos(), m_value, m_style, parentItem());
    newItem->setName(name());
    return newItem;
}

QList<QPointF> CapacitorItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0) };
}

void CapacitorItem::setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>&) {
    QString n1 = pinNet(0);
    QString n2 = pinNet(1);
    if (n1.isEmpty() || n2.isEmpty()) { m_powerDissipation = 0; return; }
    // Capacitors ideally dissipate 0W (reactive only)
    // Small ESR loss: P = I² × ESR (assume 0.01Ω default ESR)
    double v1 = nodeVoltages.value(n1, 0.0);
    double v2 = nodeVoltages.value(n2, 0.0);
    double dv = v1 - v2;
    double c = parseValue(value());
    double esr = 0.01; // Default ESR
    if (c > 1e-12) {
        // Simplified: P ≈ V² × ω² × C² × ESR (for AC)
        // For DC, P ≈ 0. Use voltage drop as rough indicator.
        m_powerDissipation = (dv * dv) * esr * 1e-6; // Very small
    } else {
        m_powerDissipation = 0;
    }
    update();
}
