/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "analog_function_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>

AnalogFunctionItem::AnalogFunctionItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setReference("U1");
    setName("Analog Function");
    setFunctionType("gain");
}

QStringList AnalogFunctionItem::availableFunctions() {
    return {"gain", "hyst", "int", "d_dt", "limit", "slew"};
}

void AnalogFunctionItem::setFunctionType(const QString& type) {
    m_functionType = type;
    setParamExpression("functionType", type);
    m_params.clear();

    if (type == "gain") {
        m_params["gain"] = 1.0;
        m_params["in_offset"] = 0.0;
        m_params["out_offset"] = 0.0;
    } else if (type == "hyst") {
        m_params["in_low"] = 0.0;
        m_params["in_high"] = 1.0;
        m_params["hyst"] = 0.1;
        m_params["out_low"] = 0.0;
        m_params["out_high"] = 5.0;
    } else if (type == "int") {
        m_params["gain"] = 1.0;
        m_params["in_offset"] = 0.0;
        m_params["out_lower_limit"] = -1e6;
        m_params["out_upper_limit"] = 1e6;
    } else if (type == "d_dt") {
        m_params["gain"] = 1.0;
        m_params["out_offset"] = 0.0;
    } else if (type == "limit") {
        m_params["gain"] = 1.0;
        m_params["in_offset"] = 0.0;
        m_params["out_lower_limit"] = -1.0;
        m_params["out_upper_limit"] = 1.0;
        m_params["limit_range"] = 0.01;
    } else if (type == "slew") {
        m_params["rise_slope"] = 1e-9;
        m_params["fall_slope"] = 1e-9;
    }

    setParamExpression("functionType", type);
    for (auto it = m_params.begin(); it != m_params.end(); ++it)
        setParamExpression(it.key(), QString::number(it.value()));

    updateSize();
    update();
}

void AnalogFunctionItem::updateSize() {
    prepareGeometryChange();
}

QRectF AnalogFunctionItem::boundingRect() const {
    return QRectF(-70, -30, 140, 60);
}

static QColor colorForType(const QString& type) {
    if (type == "gain")    return QColor(99, 102, 241);
    if (type == "hyst")    return QColor(239, 68, 68);
    if (type == "int")     return QColor(34, 211, 238);
    if (type == "d_dt")    return QColor(168, 85, 247);
    if (type == "limit")   return QColor(251, 191, 36);
    if (type == "slew")    return QColor(52, 211, 153);
    return QColor(161, 161, 170);
}

void AnalogFunctionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect(-60, -25, 120, 50);

    QPen bodyPen(Qt::white, 2);
    if (isSelected()) bodyPen.setColor(QColor(99, 102, 241));
    painter->setPen(bodyPen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRoundedRect(rect, 4, 4);

    QColor accent = colorForType(m_functionType);
    painter->setPen(accent);
    QFont titleFont("Inter", 8, QFont::Bold);
    painter->setFont(titleFont);

    QString label = m_functionType;
    label[0] = label[0].toUpper();
    painter->drawText(rect.adjusted(5, 2, -5, -22), Qt::AlignCenter, label);

    painter->setPen(QPen(Qt::white, 1.5));
    painter->drawLine(-70, -5, -60, -5);
    painter->drawLine(60, -5, 70, -5);

    QFont pinFont("Inter", 7);
    painter->setFont(pinFont);
    painter->setPen(Qt::white);
    painter->drawText(QRectF(-70, -20, 60, 16), Qt::AlignLeft | Qt::AlignVCenter, "In");
    painter->drawText(QRectF(10, -20, 60, 16), Qt::AlignRight | Qt::AlignVCenter, "Out");

    drawConnectionPointHighlights(painter);
}

QList<QPointF> AnalogFunctionItem::connectionPoints() const {
    return {QPointF(-70, -5), QPointF(70, -5)};
}

QString AnalogFunctionItem::pinName(int index) const {
    if (index == 0) return "In";
    if (index == 1) return "Out";
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> AnalogFunctionItem::pinElectricalTypes() const {
    return {InputPin, OutputPin};
}

QJsonObject AnalogFunctionItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["functionType"] = m_functionType;
    QJsonObject p;
    for (auto it = m_params.begin(); it != m_params.end(); ++it)
        p[it.key()] = it.value();
    j["params"] = p;
    return j;
}

bool AnalogFunctionItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_functionType = json.value("functionType").toString("gain");
    setParamExpression("functionType", m_functionType);
    m_params.clear();
    QJsonObject p = json.value("params").toObject();
    for (auto it = p.begin(); it != p.end(); ++it) {
        m_params[it.key()] = it.value().toDouble();
        setParamExpression(it.key(), QString::number(it.value().toDouble()));
    }
    update();
    return true;
}

SchematicItem* AnalogFunctionItem::clone() const {
    auto* item = new AnalogFunctionItem(pos(), parentItem());
    item->m_functionType = m_functionType;
    item->m_params = m_params;
    return item;
}
