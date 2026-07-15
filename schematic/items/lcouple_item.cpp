/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lcouple_item.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonObject>

LcoupleItem::LcoupleItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setReference("U1");
    setName("Inductive Coupling");
}

QRectF LcoupleItem::boundingRect() const {
    return QRectF(-90, -50, 180, 100);
}

void LcoupleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QColor dim(161, 161, 170);
    QPen selPen(QColor(99, 102, 241), 2);

    QPen wirePen(Qt::white, 1.5);
    if (isSelected()) wirePen = selPen;

    int cy = -5;

    // Pins
    painter->setPen(wirePen);
    painter->drawLine(-90, cy - 10, -75, cy - 10);
    painter->drawLine(-90, cy + 10, -75, cy + 10);
    painter->drawLine(75, cy - 10, 90, cy - 10);
    painter->drawLine(75, cy + 10, 90, cy + 10);

    // Coil arcs (inductor symbol) on the electrical side
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(34, 211, 238), 1.8));
    if (isSelected()) painter->setPen(selPen);

    int arcR = 7;
    int coilStart = -65;
    for (int i = 0; i < 4; i++) {
        int cx = coilStart + i * arcR * 2;
        painter->drawArc(cx, cy - arcR, arcR * 2, arcR * 2, 0, -180 * 16);
    }

    // Horizontal bars connecting coil to magnetic port
    int barEnd = 45;
    painter->setPen(QPen(QColor(34, 211, 238), 1.2));
    int coilEnd = coilStart + 4 * arcR * 2;
    painter->drawLine(coilEnd, cy - arcR, barEnd, cy - arcR);
    painter->drawLine(coilEnd, cy + arcR, barEnd, cy + arcR);

    // Core bar on the magnetic side
    painter->setBrush(QColor(55, 55, 60));
    painter->setPen(QPen(Qt::white, 1.2));
    if (isSelected()) painter->setPen(selPen);
    painter->drawRect(barEnd, cy - 16, 28, 32);

    // Small core gap lines
    QPen gapPen(QColor(251, 191, 36), 1, Qt::DashLine);
    painter->setPen(gapPen);
    painter->drawLine(barEnd + 4, cy - 6, barEnd + 24, cy - 6);
    painter->drawLine(barEnd + 4, cy + 6, barEnd + 24, cy + 6);

    // Turns label
    QFont detFont("Inter", 6);
    painter->setFont(detFont);
    painter->setPen(dim);
    painter->drawText(QRectF(-40, cy + 18, 80, 14), Qt::AlignCenter,
                      QString("N=%1").arg(m_numTurns, 0, 'g', 3));

    // Pin labels
    QFont pinFont("Inter", 7);
    painter->setFont(pinFont);
    painter->setPen(Qt::white);
    painter->drawText(QRectF(-90, cy - 26, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, "L+");
    painter->drawText(QRectF(-90, cy - 4, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, "L-");
    painter->drawText(QRectF(30, cy - 26, 60, 14), Qt::AlignRight | Qt::AlignVCenter, "MMF+");
    painter->drawText(QRectF(30, cy - 4, 60, 14), Qt::AlignRight | Qt::AlignVCenter, "MMF-");

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LcoupleItem::connectionPoints() const {
    return {QPointF(-80, -15), QPointF(-80, 5), QPointF(80, -15), QPointF(80, 5)};
}

QString LcoupleItem::pinName(int index) const {
    if (index == 0) return "L+";
    if (index == 1) return "L-";
    if (index == 2) return "MMF+";
    if (index == 3) return "MMF-";
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> LcoupleItem::pinElectricalTypes() const {
    return {PassivePin, PassivePin, PassivePin, PassivePin};
}

QJsonObject LcoupleItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["num_turns"] = m_numTurns;
    return j;
}

bool LcoupleItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_numTurns = json.value("num_turns").toDouble(100.0);
    return true;
}

SchematicItem* LcoupleItem::clone() const {
    auto* item = new LcoupleItem(pos(), parentItem());
    item->m_numTurns = m_numTurns;
    return item;
}
