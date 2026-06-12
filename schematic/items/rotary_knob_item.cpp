/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rotary_knob_item.h"
#include "../core/simulation/simulation_manager.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation_panel.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QJsonObject>
#include <QApplication>
#include <QtMath>

RotaryKnobItem::RotaryKnobItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setReference("KN1");
    setValue("50");
    setExcludeFromPcb(true);
}

QRectF RotaryKnobItem::boundingRect() const {
    return QRectF(-35, -35, 70, 80); // Circular knob + label area
}

void RotaryKnobItem::rebuildPrimitives() {}

void RotaryKnobItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Outer Ring (Silver/Chrome)
    painter->setBrush(QColor(180, 180, 190));
    painter->setPen(QPen(Qt::black, 1));
    painter->drawEllipse(QRectF(-m_radius, -m_radius, m_radius*2, m_radius*2));

    // Knob Body (Dark Gray with Gradient)
    QRadialGradient grad(0, 0, m_radius - 2);
    grad.setColorAt(0, QColor(60, 60, 70));
    grad.setColorAt(1, QColor(40, 40, 50));
    painter->setBrush(grad);
    painter->drawEllipse(QRectF(-m_radius+2, -m_radius+2, (m_radius-2)*2, (m_radius-2)*2));

    // Dial Indicator (Neon Green)
    painter->save();
    // Map current value (min..max) to angle (-135 to 135 degrees)
    double ratio = (m_current - m_min) / (m_max - m_min);
    double drawAngle = -135.0 + ratio * 270.0;
    painter->rotate(drawAngle);
    
    painter->setPen(QPen(QColor(0, 255, 100), 3, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(0, -10, 0, -m_radius + 5);
    painter->restore();

    // Value Display
    painter->setPen(Qt::white);
    QFont f = painter->font(); f.setPointSize(8); f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(-35, m_radius + 5, 70, 15), Qt::AlignCenter, QString::number(m_current, 'g', 4));
}

void RotaryKnobItem::setCurrentValue(double v) {
    m_current = qBound(m_min, v, m_max);
    setValue(QString::number(m_current));
    triggerSimulationUpdate();
    update();
}

void RotaryKnobItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        event->accept();
    } else {
        SchematicItem::mousePressEvent(event);
    }
}

void RotaryKnobItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        double angle = angleFromPos(event->pos());
        // Map -135..135 range to 0..1 ratio
        double ratio = (angle + 135.0) / 270.0;
        ratio = qBound(0.0, ratio, 1.0);
        setCurrentValue(m_min + ratio * (m_max - m_min));
        event->accept();
    } else {
        SchematicItem::mouseMoveEvent(event);
    }
}

void RotaryKnobItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_dragging = false;
    SchematicItem::mouseReleaseEvent(event);
}

double RotaryKnobItem::angleFromPos(const QPointF& pos) const {
    double angle = qRadiansToDegrees(qAtan2(pos.y(), pos.x())) + 90.0;
    if (angle > 180) angle -= 360;
    // Standardize to -135..135 range for the dial
    return qBound(-135.0, angle, 135.0);
}

void RotaryKnobItem::triggerSimulationUpdate() {
    if (!m_targetParam.isEmpty()) {
        SimulationManager::instance().queueParameterUpdate(m_targetParam, m_current);
        
        if (!SimulationManager::instance().isRunning()) {
            auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
            if (editor && editor->getSimulationPanel() && editor->getSimulationPanel()->isRealTimeMode()) {
                editor->getSimulationPanel()->onRunSimulation();
            }
        }
    }
}

QJsonObject RotaryKnobItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["min"] = m_min;
    j["max"] = m_max;
    j["current"] = m_current;
    j["target"] = m_targetParam;
    return j;
}

bool RotaryKnobItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_min = json.value("min").toDouble(0.0);
    m_max = json.value("max").toDouble(100.0);
    m_current = json.value("current").toDouble(50.0);
    m_targetParam = json.value("target").toString();
    return true;
}

SchematicItem* RotaryKnobItem::clone() const {
    auto* item = new RotaryKnobItem(pos());
    item->fromJson(toJson());
    return item;
}
