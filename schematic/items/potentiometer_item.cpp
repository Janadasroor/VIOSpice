#include "potentiometer_item.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation_panel.h"
#include <QApplication>
#include "../../simulator/core/sim_value_parser.h"
#include <QPainter>
#include <QJsonObject>
#include <QGraphicsSceneMouseEvent>
#include <cmath>
#include <algorithm>

namespace {
void triggerInteractiveSimulationUpdateIfNeeded() {
    auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
    if (!editor || !editor->getSimulationPanel()) return;

    const auto cfg = editor->getSimulationPanel()->getAnalysisConfig();
    if (cfg.type == SimAnalysisType::Transient || cfg.type == SimAnalysisType::RealTime) {
        editor->getSimulationPanel()->onRunSimulation();
    }
}
}

PotentiometerItem::PotentiometerItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    setReference("RV1");
    setValue("10k");
    m_wiperPos = 0.5;
}

void PotentiometerItem::setWiperPosition(double pos) {
    m_wiperPos = std::clamp(pos, 0.0, 1.0);
    
    // Update simulation parameters
    QMap<QString, QString> pe = paramExpressions();
    pe["pot.position"] = QString::number(m_wiperPos);
    pe["pot.log"] = m_log ? "true" : "false";
    pe["pot.log_multiplier"] = QString::number(m_logMultiplier);
    
    // Legacy support for internal engine (splitting resistors)
    double totalR = 10000.0;
    double parsed = 0.0;
    if (SimValueParser::parseSpiceNumber(m_value, parsed)) {
        totalR = parsed;
    }
    
    double r1, r2;
    if (m_log) {
        // Simple log approximation for legacy engine
        double ratio = (std::pow(10, m_wiperPos * m_logMultiplier) - 1.0) / (std::pow(10, m_logMultiplier) - 1.0);
        r2 = std::max(0.001, totalR * ratio);
        r1 = std::max(0.001, totalR - r2);
    } else {
        r1 = std::max(0.001, totalR * (1.0 - m_wiperPos));
        r2 = std::max(0.001, totalR * m_wiperPos);
    }
    
    pe["r_upper"] = QString::number(r1);
    pe["r_lower"] = QString::number(r2);
    for (auto it = pe.constBegin(); it != pe.constEnd(); ++it)
        setParamExpression(it.key(), it.value());

    Q_EMIT interactiveStateChanged();
    triggerInteractiveSimulationUpdateIfNeeded();
    update();
}

void PotentiometerItem::onInteractivePress(const QPointF& pos) {
    QPointF local = mapFromScene(pos);
    if (QRectF(-10, -35, 20, 20).contains(local)) {
        m_isDraggingWiper = true;
    }
}

void PotentiometerItem::onInteractiveClick(const QPointF& pos) {
    QPointF local = mapFromScene(pos);
    if (local.x() >= -30 && local.x() <= 30) {
        setWiperPosition((local.x() + 30) / 60.0);
    }
}

QRectF PotentiometerItem::boundingRect() const {
    return QRectF(-50, -50, 100, 100);
}

void PotentiometerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(Qt::NoBrush);

    // Resistor Body (Zigzag)
    QPolygonF zigzag;
    zigzag << QPointF(-30, 0)
           << QPointF(-25, -22.5)
           << QPointF(-15, 22.5)
           << QPointF(-5, -22.5)
           << QPointF(5, 22.5)
           << QPointF(15, -22.5)
           << QPointF(25, 22.5)
           << QPointF(30, 0);
    painter->drawPolyline(zigzag);

    // Terminal pins
    painter->drawLine(-45, 0, -30, 0);
    painter->drawLine(30, 0, 45, 0);

    // Wiper Arrow
    double wx = -30 + (m_wiperPos * 60.0);
    painter->setPen(QPen(QColor(0, 255, 100), 2)); // Green wiper
    painter->drawLine(wx, -18, wx, -35);
    painter->drawLine(wx, -18, wx - 4, -26);
    painter->drawLine(wx, -18, wx + 4, -26);
    
    // Wiper pin line
    painter->drawLine(wx, -35, 0, -35);
    painter->drawLine(0, -35, 0, -45);

    // Knob handle
    painter->setBrush(m_isDraggingWiper ? QColor(0, 200, 80) : QColor(0, 100, 40));
    painter->drawEllipse(wx - 6, -40, 12, 12);

    painter->setPen(QPen(Qt::white, 1));
    painter->setFont(QFont("Inter", 6));
    QString label = QString::number((int)(m_wiperPos * 100)) + "%";
    if (m_log) label += " (Log)";
    painter->drawText(QRectF(-40, 25, 80, 10), Qt::AlignCenter, label);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> PotentiometerItem::connectionPoints() const {
    return {
        QPointF(-45, 0),  // Pin 1 (A)
        QPointF(0, -45),  // Pin 2 (Wiper)
        QPointF(45, 0)    // Pin 3 (B)
    };
}

QString PotentiometerItem::pinName(int index) const {
    switch(index) {
        case 0: return "1";
        case 1: return "2";
        case 2: return "3";
        default: return QString::number(index+1);
    }
}

QJsonObject PotentiometerItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Potentiometer";
    j["wiper"] = m_wiperPos;
    j["log"] = m_log;
    j["logMultiplier"] = m_logMultiplier;
    return j;
}

bool PotentiometerItem::fromJson(const QJsonObject& j) {
    if (j["type"].toString() != "Potentiometer") return false;
    SchematicItem::fromJson(j);
    m_wiperPos = j["wiper"].toDouble(0.5);
    m_log = j["log"].toBool(false);
    m_logMultiplier = j["logMultiplier"].toDouble(1.0);
    return true;
}

SchematicItem* PotentiometerItem::clone() const {
    PotentiometerItem* p = new PotentiometerItem(pos());
    p->setLogarithmic(m_log);
    p->setLogMultiplier(m_logMultiplier);
    p->setWiperPosition(m_wiperPos);
    return p;
}
