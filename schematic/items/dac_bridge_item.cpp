/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "dac_bridge_item.h"
#include <QPainter>
#include <QJsonObject>

DacBridgeItem::DacBridgeItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setReference("U1");
    setName("DAC Bridge");
}

QRectF DacBridgeItem::boundingRect() const {
    return QRectF(-70, -30, 140, 60);
}

void DacBridgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect(-60, -25, 120, 50);

    QPen bodyPen(Qt::white, 2);
    if (isSelected()) bodyPen.setColor(QColor(99, 102, 241));
    painter->setPen(bodyPen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRoundedRect(rect, 4, 4);

    painter->setPen(QColor(52, 211, 153));
    QFont titleFont("Inter", 8, QFont::Bold);
    painter->setFont(titleFont);
    painter->drawText(rect.adjusted(5, 2, -5, -22), Qt::AlignCenter, "DAC");

    QFont detailFont("Inter", 6);
    painter->setFont(detailFont);
    painter->setPen(QColor(161, 161, 170));
    painter->drawText(rect.adjusted(5, 20, -5, -5), Qt::AlignCenter,
                      QString("0:%1 / 1:%2V").arg(m_outLow).arg(m_outHigh));

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

QList<QPointF> DacBridgeItem::connectionPoints() const {
    return {QPointF(-70, -5), QPointF(70, -5)};
}

QString DacBridgeItem::pinName(int index) const {
    if (index == 0) return "In";
    if (index == 1) return "Out";
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> DacBridgeItem::pinElectricalTypes() const {
    return {InputPin, OutputPin};
}

QJsonObject DacBridgeItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["out_low"] = m_outLow;
    j["out_high"] = m_outHigh;
    j["out_undef"] = m_outUndef;
    j["input_load"] = m_inputLoad;
    j["t_rise"] = m_tRise;
    j["t_fall"] = m_tFall;
    return j;
}

bool DacBridgeItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_outLow = json.value("out_low").toDouble(0.0);
    m_outHigh = json.value("out_high").toDouble(5.0);
    m_outUndef = json.value("out_undef").toDouble(2.5);
    m_inputLoad = json.value("input_load").toDouble(1e-12);
    m_tRise = json.value("t_rise").toDouble(1e-9);
    m_tFall = json.value("t_fall").toDouble(1e-9);
    return true;
}

SchematicItem* DacBridgeItem::clone() const {
    auto* item = new DacBridgeItem(pos(), parentItem());
    item->m_outLow = m_outLow;
    item->m_outHigh = m_outHigh;
    item->m_outUndef = m_outUndef;
    item->m_inputLoad = m_inputLoad;
    item->m_tRise = m_tRise;
    item->m_tFall = m_tFall;
    return item;
}
