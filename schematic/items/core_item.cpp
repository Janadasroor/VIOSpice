/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core_item.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonObject>

CoreItem::CoreItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setReference("U1");
    setName("Magnetic Core");
}

QRectF CoreItem::boundingRect() const {
    return QRectF(-70, -40, 140, 80);
}

void CoreItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QColor accent(251, 191, 36);
    QColor dim(161, 161, 170);
    QColor body(30, 30, 35);
    QPen selPen(QColor(99, 102, 241), 2);

    QPen wirePen(Qt::white, 1.5);
    if (isSelected()) wirePen = selPen;

    // Pins
    painter->setPen(wirePen);
    painter->drawLine(-70, -5, -55, -5);
    painter->drawLine(55, -5, 70, -5);

    // EI core shape — two C-cores facing each other
    painter->setBrush(QColor(55, 55, 60));
    painter->setPen(QPen(Qt::white, 1.2));
    if (isSelected()) painter->setPen(selPen);

    int cw = 16, ch = 40, gap = 8;
    int cy = -5;

    // Left C-core
    painter->drawRect(-55, cy - ch/2, cw, ch);
    painter->drawRect(-55, cy - ch/2, cw + 10, 8);
    painter->drawRect(-55, cy + ch/2 - 8, cw + 10, 8);

    // Right C-core
    int rx = 55 - cw;
    painter->drawRect(rx, cy - ch/2, cw, ch);
    painter->drawRect(rx - 10, cy - ch/2, cw + 10, 8);
    painter->drawRect(rx - 10, cy + ch/2 - 8, cw + 10, 8);

    // Gap lines (flux path)
    QPen gapPen(QColor(251, 191, 36), 1, Qt::DashLine);
    painter->setPen(gapPen);
    int gapL = -55 + cw + 10;
    int gapR = rx - 10;
    painter->drawLine(gapL, cy - 10, gapR, cy - 10);
    painter->drawLine(gapL, cy + 10, gapR, cy + 10);

    // Pin labels
    QFont pinFont("Inter", 7);
    painter->setFont(pinFont);
    painter->setPen(Qt::white);
    painter->drawText(QRectF(-70, -25, 60, 16), Qt::AlignLeft | Qt::AlignVCenter, "+");
    painter->drawText(QRectF(10, -25, 60, 16), Qt::AlignRight | Qt::AlignVCenter, "-");

    // Parameters below
    QFont detFont("Inter", 6);
    painter->setFont(detFont);
    painter->setPen(dim);
    painter->drawText(QRectF(-55, cy + ch/2 + 6, 110, 14), Qt::AlignCenter,
                      QString("A=%1 L=%2 m").arg(m_area, 0, 'g', 2).arg(m_length, 0, 'g', 2));

    drawConnectionPointHighlights(painter);
}

QList<QPointF> CoreItem::connectionPoints() const {
    return {QPointF(-70, -5), QPointF(70, -5)};
}

QString CoreItem::pinName(int index) const {
    if (index == 0) return "PLUS";
    if (index == 1) return "MINUS";
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> CoreItem::pinElectricalTypes() const {
    return {PassivePin, PassivePin};
}

QJsonObject CoreItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["area"] = m_area;
    j["length"] = m_length;
    j["mode"] = m_mode;
    j["H_array"] = m_hArray;
    j["B_array"] = m_bArray;
    j["input_domain"] = m_inputDomain;
    j["fraction"] = m_fraction;
    j["in_low"] = m_inLow;
    j["in_high"] = m_inHigh;
    j["hyst"] = m_hyst;
    j["out_lower_limit"] = m_outLowerLimit;
    j["out_upper_limit"] = m_outUpperLimit;
    return j;
}

bool CoreItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_area = json.value("area").toDouble(1e-4);
    m_length = json.value("length").toDouble(1e-2);
    m_mode = json.value("mode").toInt(1);
    m_hArray = json.value("H_array").toString("-200 -100 100 200");
    m_bArray = json.value("B_array").toString("-1.26 -0.63 0.63 1.26");
    m_inputDomain = json.value("input_domain").toDouble(0.01);
    m_fraction = json.value("fraction").toBool(true);
    m_inLow = json.value("in_low").toDouble(-1.0);
    m_inHigh = json.value("in_high").toDouble(1.0);
    m_hyst = json.value("hyst").toDouble(0.1);
    m_outLowerLimit = json.value("out_lower_limit").toDouble(-1.0);
    m_outUpperLimit = json.value("out_upper_limit").toDouble(1.0);
    return true;
}

SchematicItem* CoreItem::clone() const {
    auto* item = new CoreItem(pos(), parentItem());
    item->m_area = m_area;
    item->m_length = m_length;
    item->m_mode = m_mode;
    item->m_hArray = m_hArray;
    item->m_bArray = m_bArray;
    item->m_inputDomain = m_inputDomain;
    item->m_fraction = m_fraction;
    item->m_inLow = m_inLow;
    item->m_inHigh = m_inHigh;
    item->m_hyst = m_hyst;
    item->m_outLowerLimit = m_outLowerLimit;
    item->m_outUpperLimit = m_outUpperLimit;
    return item;
}
