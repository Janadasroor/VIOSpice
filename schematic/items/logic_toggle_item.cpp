#include "logic_toggle_item.h"
#include "../core/simulation/simulation_manager.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation_panel.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QJsonObject>
#include <QApplication>

LogicToggleItem::LogicToggleItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setReference("LT1");
    setValue("0V");
    setExcludeFromPcb(true);
}

QRectF LogicToggleItem::boundingRect() const {
    // Matches the symbol proportions (50x50 block + pin line)
    return QRectF(-45, -25, 90, 50);
}

void LogicToggleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Instrument Body
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(QRectF(-25, -25, 50, 50), 4, 4);

    // Screen Area
    // Color depends on state: Black/Red for 0, DarkGreen/Green for 1
    painter->setBrush(m_state ? QColor(10, 20, 10) : QColor(20, 10, 10));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawRect(-20, -20, 40, 40);

    // Label
    painter->setPen(m_state ? QColor(0, 255, 100) : QColor(255, 50, 50));
    QFont f = painter->font();
    f.setBold(true);
    f.setPointSize(20);
    painter->setFont(f);
    painter->drawText(QRectF(-20, -20, 40, 40), Qt::AlignCenter, m_state ? "1" : "0");

    // Pin line
    painter->setPen(QPen(QColor(100, 100, 105), 1.5));
    painter->drawLine(25, 0, 40, 0);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LogicToggleItem::connectionPoints() const {
    return { QPointF(40, 0) }; // 1 Output Pin on the Right
}

void LogicToggleItem::setState(bool high) {
    if (m_state != high) {
        m_state = high;
        setValue(m_state ? "5V" : "0V");
        updateSimulationValue();
        update();
    }
}

void LogicToggleItem::onInteractivePress(const QPointF&) {
    setState(!m_state); // Toggle!
}

void LogicToggleItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // If we click the "body", toggle it
    if (QRectF(-25, -25, 50, 50).contains(event->pos())) {
        onInteractivePress(event->scenePos());
        event->accept();
    } else {
        SchematicItem::mousePressEvent(event);
    }
}

void LogicToggleItem::updateSimulationValue() {
    // Inject the value into the running SPICE engine
    QString ref = reference();
    QString targetName = ref.startsWith("V", Qt::CaseInsensitive) ? ref : "V" + ref;
    SimulationManager::instance().queueParameterUpdate(targetName, m_state ? 5.0 : 0.0);

    // If not currently running but in Real-Time mode, auto-restart the simulation
    if (!SimulationManager::instance().isRunning()) {
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (editor && editor->getSimulationPanel() && editor->getSimulationPanel()->isRealTimeMode()) {
            editor->getSimulationPanel()->onRunSimulation();
        }
    }
}

QJsonObject LogicToggleItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["logicState"] = m_state;
    return j;
}

bool LogicToggleItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    if (json.contains("logicState")) {
        m_state = json["logicState"].toBool();
        setValue(m_state ? "5V" : "0V");
    }
    return true;
}

SchematicItem* LogicToggleItem::clone() const {
    auto* item = new LogicToggleItem(pos());
    item->setState(m_state);
    return item;
}
