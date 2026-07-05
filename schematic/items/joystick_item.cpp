/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "joystick_item.h"
#include "../core/simulation/simulation_manager.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation/simulation_panel.h"
#include <QApplication>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QJsonObject>

JoystickItem::JoystickItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setReference("JS1");
    setExcludeFromPcb(true);
}

QRectF JoystickItem::boundingRect() const {
    return QRectF(-5, -5, m_padSize + 10, m_padSize + 30);
}

void JoystickItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Background Pad
    painter->setBrush(QColor(30, 30, 35));
    painter->setPen(QPen(QColor(100, 100, 110), 2));
    painter->drawRoundedRect(0, 0, m_padSize, m_padSize, 5, 5);

    // Grid Lines
    painter->setPen(QPen(QColor(50, 50, 60), 1));
    painter->drawLine(m_padSize / 2, 0, m_padSize / 2, m_padSize);
    painter->drawLine(0, m_padSize / 2, m_padSize, m_padSize / 2);

    double hX = ((m_xCurr - m_xMin) / (m_xMax - m_xMin)) * m_padSize;
    double hY = m_padSize - (((m_yCurr - m_yMin) / (m_yMax - m_yMin)) * m_padSize); // Flip Y for screen coord

    painter->setBrush(QColor(0, 200, 255));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawEllipse(QPointF(hX, hY), 8, 8);

    // Values Label
    painter->setPen(Qt::white);
    painter->setFont(QFont("Inter", 7));
    painter->drawText(QRectF(0, m_padSize + 5, m_padSize, 15), Qt::AlignCenter, 
                      QString("X: %1, Y: %2").arg(m_xCurr, 0, 'f', 2).arg(m_yCurr, 0, 'f', 2));
}

void JoystickItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (QRectF(0, 0, m_padSize, m_padSize).contains(event->pos())) {
        m_dragging = true;
        mouseMoveEvent(event);
        event->accept();
    } else {
        SchematicItem::mousePressEvent(event);
    }
}

void JoystickItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        double px = qBound(0.0, event->pos().x(), m_padSize);
        double py = qBound(0.0, event->pos().y(), m_padSize);

        m_xCurr = m_xMin + (px / m_padSize) * (m_xMax - m_xMin);
        m_yCurr = m_yMin + ((m_padSize - py) / m_padSize) * (m_yMax - m_yMin);

        triggerSimulationUpdate();
        update();
        event->accept();
    } else {
        SchematicItem::mouseMoveEvent(event);
    }
}

void JoystickItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_dragging = false;
    SchematicItem::mouseReleaseEvent(event);
}

void JoystickItem::triggerSimulationUpdate() {
    if (!m_targetX.isEmpty()) SimulationManager::instance().queueParameterUpdate(m_targetX, m_xCurr);
    if (!m_targetY.isEmpty()) SimulationManager::instance().queueParameterUpdate(m_targetY, m_yCurr);
}

QJsonObject JoystickItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["xMin"] = m_xMin; j["xMax"] = m_xMax;
    j["yMin"] = m_yMin; j["yMax"] = m_yMax;
    j["xCurr"] = m_xCurr; j["yCurr"] = m_yCurr;
    j["targetX"] = m_targetX; j["targetY"] = m_targetY;
    return j;
}

bool JoystickItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_xMin = json.value("xMin").toDouble(0.0); m_xMax = json.value("xMax").toDouble(1.0);
    m_yMin = json.value("yMin").toDouble(0.0); m_yMax = json.value("yMax").toDouble(1.0);
    m_xCurr = json.value("xCurr").toDouble(0.5); m_yCurr = json.value("yCurr").toDouble(0.5);
    m_targetX = json.value("targetX").toString(); m_targetY = json.value("targetY").toString();
    return true;
}

SchematicItem* JoystickItem::clone() const {
    auto* item = new JoystickItem(pos());
    item->fromJson(toJson());
    return item;
}
