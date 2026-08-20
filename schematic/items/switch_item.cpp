/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "switch_item.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation/simulation_panel.h"
#include "simulation_manager.h"
#include <QApplication>
#include <QPainter>
#include <QJsonObject>

namespace {
void updateSwitchRealTime(const QString& switchRef, bool open, double vt, double vh) {
    // Phase 1: Use alter command (proven working)
    // bg_halt → alter R=value → bg_resume
    auto& sim = SimulationManager::instance();
    sim.alterSwitch(switchRef, open, vt, vh);
}
}

SwitchItem::SwitchItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setExcludeFromPcb(true); // Switches are excluded from PCB by default
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    m_isOpen = true;
    m_useModel = false;
    m_modelName = "MySwitchName";
    m_ron = "0.1";
    m_roff = "1Meg";
    m_vt = "0.5";
    m_vh = "0.1";
    setReference("SW1");
    setValue("1e12"); // Open resistance
    syncParamExpressions();
}

void SwitchItem::onInteractiveClick(const QPointF&) {
    setOpen(!m_isOpen);

    Q_EMIT interactiveStateChanged();

    // Use real-time switch update instead of full simulation restart
    // This preserves simulation state (capacitor voltages, inductor currents)
    double vtVal = 0.5, vhVal = 0.1;
    bool okVt = false, okVh = false;
    vtVal = m_vt.toDouble(&okVt);
    vhVal = m_vh.toDouble(&okVh);
    updateSwitchRealTime(reference(), m_isOpen, vtVal, vhVal);

    update();
}

#include "theme_manager.h"

QRectF SwitchItem::boundingRect() const { return QRectF(-50, -25, 100, 50); }

void SwitchItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    const QColor wireColor = theme ? theme->schematicLine() : QColor(220, 220, 220);
    const QColor accentColor = theme ? theme->accentColor() : QColor(59, 130, 246);

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

    // 3. Switch arm / blade
    if (m_isOpen) {
        painter->setPen(QPen(accentColor, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-25, 0), QPointF(20, -15));
    } else {
        painter->setPen(QPen(accentColor, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-25, 0), QPointF(25, 0));
    }

    // Pivot dot at left terminal
    painter->setBrush(QBrush(accentColor));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(-25, 0), 2.0, 2.0);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> SwitchItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0) };
}

QJsonObject SwitchItem::toJson() const { 
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Switch";
    j["open"] = m_isOpen;
    return j; 
}

bool SwitchItem::fromJson(const QJsonObject& j) { 
    SchematicItem::fromJson(j);
    m_isOpen = j["open"].toBool(m_isOpen);
    applyParamExpressions();
    syncParamExpressions();
    return true; 
}

SchematicItem* SwitchItem::clone() const {
    auto* item = new SwitchItem(pos());
    item->m_isOpen = m_isOpen;
    item->m_useModel = m_useModel;
    item->m_modelName = m_modelName;
    item->m_ron = m_ron;
    item->m_roff = m_roff;
    item->m_vt = m_vt;
    item->m_vh = m_vh;
    item->syncParamExpressions();
    return item;
}

void SwitchItem::setOpen(bool open) {
    m_isOpen = open;
    setValue(m_isOpen ? "1Meg" : "0.1");
    setParamExpression("resistance", m_isOpen ? "1Meg" : "0.1");
    syncParamExpressions();
    update();
}

void SwitchItem::setUseModel(bool useModel) {
    m_useModel = useModel;
    syncParamExpressions();
}

void SwitchItem::setModelName(const QString& name) {
    m_modelName = name.trimmed();
    syncParamExpressions();
}

void SwitchItem::setRon(const QString& value) {
    m_ron = value.trimmed();
    syncParamExpressions();
}

void SwitchItem::setRoff(const QString& value) {
    m_roff = value.trimmed();
    syncParamExpressions();
}

void SwitchItem::setVt(const QString& value) {
    m_vt = value.trimmed();
    syncParamExpressions();
}

void SwitchItem::setVh(const QString& value) {
    m_vh = value.trimmed();
    syncParamExpressions();
}

void SwitchItem::syncParamExpressions() {
    setParamExpression("switch.use_model", m_useModel ? "1" : "0");
    setParamExpression("switch.model_name", m_modelName);
    setParamExpression("switch.ron", m_ron);
    setParamExpression("switch.roff", m_roff);
    setParamExpression("switch.vt", m_vt);
    setParamExpression("switch.vh", m_vh);
    setParamExpression("switch.state", m_isOpen ? "open" : "closed");
}

void SwitchItem::applyParamExpressions() {
    const QString useModelExpr = paramExpressions().value("switch.use_model").trimmed();
    if (!useModelExpr.isEmpty()) {
        m_useModel = (useModelExpr == "1" || useModelExpr.compare("true", Qt::CaseInsensitive) == 0);
    }

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

    const QString stateExpr = paramExpressions().value("switch.state").trimmed().toLower();
    if (stateExpr == "open" || stateExpr == "closed") {
        m_isOpen = (stateExpr == "open");
    }
}
