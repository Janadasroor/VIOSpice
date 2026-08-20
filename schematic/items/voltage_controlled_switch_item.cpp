/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "voltage_controlled_switch_item.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

VoltageControlledSwitchItem::VoltageControlledSwitchItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent),
      m_modelName("MySwitchName"),
      m_ron("0.1"),
      m_roff("1Meg"),
      m_vt("0.5"),
      m_vh("0.1") {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    setReference("S1");
    syncParamExpressions();
}

#include "theme_manager.h"

QRectF VoltageControlledSwitchItem::boundingRect() const { return QRectF(-55, -55, 110, 110); }

void VoltageControlledSwitchItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    const QColor wireColor = theme ? theme->schematicLine() : QColor(220, 220, 220);
    const QColor accentColor = theme ? theme->accentColor() : QColor(59, 130, 246);

    // Main Circle body at (0, 0), radius 22.5 (matching Voltage Source)
    QPen mainPen(wireColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (option && (option->state & QStyle::State_Selected)) {
        mainPen.setColor(accentColor);
        mainPen.setWidthF(2.2);
    }
    painter->setPen(mainPen);
    painter->setBrush(QBrush(theme ? theme->panelBackground() : QColor(30, 30, 35)));
    painter->drawEllipse(QPointF(0.0000, 0.0000), 22.5000, 22.5000);

    // Terminal contact dots
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF(0.0000, 11.2500), 2.8125, 2.8125);
    painter->drawEllipse(QPointF(14.0625, 2.8125), 2.8125, 2.8125);

    // Switch blade: (0.0000, -14.0625) -> (14.0625, 2.8125)
    painter->setPen(QPen(accentColor, 2.2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(0.0000, -14.0625), QPointF(14.0625, 2.8125));

    // Control leads and symbols on left
    painter->setPen(QPen(wireColor, 1.6, Qt::SolidLine, Qt::RoundCap));
    // NC+ lead (bottom left): (-45.0000, 16.8750) -> (-22.5000, 16.8750) -> (-16.8750, 14.0625)
    painter->drawLine(QPointF(-45.0000, 16.8750), QPointF(-22.5000, 16.8750));
    painter->drawLine(QPointF(-22.5000, 16.8750), QPointF(-16.8750, 14.0625));
    // NC- lead (top left): (-45.0000, -16.8750) -> (-22.5000, -16.8750) -> (-16.8750, -14.0625)
    painter->drawLine(QPointF(-45.0000, -16.8750), QPointF(-22.5000, -16.8750));
    painter->drawLine(QPointF(-22.5000, -16.8750), QPointF(-16.8750, -14.0625));

    // Polarity signs for control inputs:
    // '+' sign next to NC+ (y = 16.875):
    painter->drawLine(QPointF(-33.7500, 11.2500), QPointF(-28.1250, 11.2500));
    painter->drawLine(QPointF(-30.9375, 14.0625), QPointF(-30.9375, 8.4375));
    // '-' sign next to NC- (y = -16.875):
    painter->drawLine(QPointF(-33.7500, -11.2500), QPointF(-28.1250, -11.2500));

    // Main terminals: A (top) and B (bottom) leads
    painter->drawLine(QPointF(0.0000, -45.0000), QPointF(0.0000, -14.0625));
    painter->drawLine(QPointF(0.0000, 11.2500), QPointF(0.0000, 45.0000));

    drawConnectionPointHighlights(painter);
}

QList<QPointF> VoltageControlledSwitchItem::connectionPoints() const {
    // 1:A (top 0, -45), 2:B (bottom 0, 45), 3:NC+ (left bottom -45, 16.88), 4:NC- (left top -45, -16.88)
    return {
        QPointF(0.0000, -45.0000),
        QPointF(0.0000, 45.0000),
        QPointF(-45.0000, 16.8750),
        QPointF(-45.0000, -16.8750)
    };
}

QJsonObject VoltageControlledSwitchItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Voltage Controlled Switch";
    return j;
}

bool VoltageControlledSwitchItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    applyParamExpressions();
    syncParamExpressions();
    return true;
}

SchematicItem* VoltageControlledSwitchItem::clone() const {
    auto* item = new VoltageControlledSwitchItem(pos());
    item->m_modelName = m_modelName;
    item->m_ron = m_ron;
    item->m_roff = m_roff;
    item->m_vt = m_vt;
    item->m_vh = m_vh;
    item->syncParamExpressions();
    return item;
}

void VoltageControlledSwitchItem::setModelName(const QString& name) {
    m_modelName = name.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setRon(const QString& value) {
    m_ron = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setRoff(const QString& value) {
    m_roff = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setVt(const QString& value) {
    m_vt = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setVh(const QString& value) {
    m_vh = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::syncParamExpressions() {
    setParamExpression("switch.model_name", m_modelName);
    setParamExpression("switch.ron", m_ron);
    setParamExpression("switch.roff", m_roff);
    setParamExpression("switch.vt", m_vt);
    setParamExpression("switch.vh", m_vh);
}

void VoltageControlledSwitchItem::applyParamExpressions() {
    const QString modelNameExpr = paramExpressions().value("switch.model_name").trimmed();
    if (!modelNameExpr.isEmpty()) m_modelName = modelNameExpr;

    const QString ronExpr = paramExpressions().value("switch.ron").trimmed();
    if (!ronExpr.isEmpty()) m_ron = ronExpr;

    const QString roffExpr = paramExpressions().value("switch.roff").trimmed();
    if (!roffExpr.isEmpty()) m_roff = roffExpr;

    const QString vtExpr = paramExpressions().value("switch.vt").trimmed();
    if (!vtExpr.isEmpty()) m_vt = vtExpr;

    const QString vhExpr = paramExpressions().value("switch.vh").trimmed();
    if (!vhExpr.isEmpty()) m_vh = vhExpr;
}
