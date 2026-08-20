/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "push_button_item.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation/simulation_panel.h"
#include "simulation_manager.h"
#include <QApplication>
#include <QPainter>
#include <QJsonObject>

namespace {
void updatePushButtonRealTime(const QString& ref, bool pressed) {
    // Phase 1: Use alter command (proven working)
    auto& sim = SimulationManager::instance();
    sim.alterSwitchResistance(ref.startsWith("R") ? ref : "R" + ref, pressed ? 0.001 : 1e12);
}
}

PushButtonItem::PushButtonItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    m_isPressed = false;
    setReference("SW1");
    setValue("1e12"); // Open resistance
}

void PushButtonItem::onInteractivePress(const QPointF&) {
    m_isPressed = true;
    setValue("0.001"); // Closed resistance
    setParamExpression("resistance", "0.001");
    Q_EMIT interactiveStateChanged();
    updatePushButtonRealTime(reference(), m_isPressed);
    update();
}

void PushButtonItem::onInteractiveRelease(const QPointF&) {
    m_isPressed = false;
    setValue("1e12"); // Open resistance
    setParamExpression("resistance", "1e12");
    Q_EMIT interactiveStateChanged();
    updatePushButtonRealTime(reference(), m_isPressed);
    update();
}

#include "theme_manager.h"

QRectF PushButtonItem::boundingRect() const { return QRectF(-50, -30, 100, 60); }

void PushButtonItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    const QColor wireColor = theme ? theme->schematicLine() : QColor(220, 220, 220);
    const QColor accentColor = theme ? theme->accentColor() : QColor(59, 130, 246);
    const QColor activeColor = QColor(0, 230, 118);

    // 1. Leads
    QPen leadPen(wireColor, 2.0, Qt::SolidLine, Qt::RoundCap);
    painter->setPen(leadPen);
    painter->drawLine(QPointF(-45, 0), QPointF(-25, 0));
    painter->drawLine(QPointF(25, 0), QPointF(45, 0));

    // 2. Terminals (Contact circles)
    painter->setPen(QPen(accentColor, 1.8));
    painter->setBrush(QBrush(theme ? theme->panelBackground() : QColor(30, 30, 30)));
    painter->drawEllipse(QPointF(-25, 0), 3.5, 3.5);
    painter->drawEllipse(QPointF(25, 0), 3.5, 3.5);

    // 3. Push Button Bar and Plunger
    if (m_isPressed) {
        // Connected short bar across contacts
        painter->setPen(QPen(activeColor, 2.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-25, 0), QPointF(25, 0));
        // Pressed plunger stem
        painter->setPen(QPen(activeColor, 2.0));
        painter->drawLine(QPointF(0, 0), QPointF(0, -10));
        // Tactile cap
        painter->setBrush(QBrush(activeColor));
        painter->drawRoundedRect(QRectF(-12, -14, 24, 6), 2, 2);
    } else {
        // Raised bar above contacts
        painter->setPen(QPen(accentColor, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-25, -10), QPointF(25, -10));
        // Plunger stem
        painter->drawLine(QPointF(0, -10), QPointF(0, -18));
        // Tactile cap
        painter->setBrush(QBrush(theme ? theme->panelBackground() : QColor(40, 40, 45)));
        painter->drawRoundedRect(QRectF(-12, -22, 24, 6), 2, 2);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> PushButtonItem::connectionPoints() const {
    return { QPointF(-15, 0), QPointF(15, 0) };
}

QJsonObject PushButtonItem::toJson() const { 
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "PushButton";
    return j; 
}

bool PushButtonItem::fromJson(const QJsonObject& j) { 
    return SchematicItem::fromJson(j);
}

SchematicItem* PushButtonItem::clone() const {
    return new PushButtonItem(pos());
}
