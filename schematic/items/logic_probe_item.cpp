#include "logic_probe_item.h"
#include <QPainter>
#include <QJsonObject>

LogicProbeItem::LogicProbeItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setReference("LP1");
    setValue("0V");
    setExcludeFromPcb(true);
}

QRectF LogicProbeItem::boundingRect() const {
    return QRectF(-45, -25, 75, 50);
}

void LogicProbeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Instrument Body
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(QRectF(-25, -25, 50, 50), 4, 4);

    // Screen Area
    painter->setBrush(m_logicState ? QColor(10, 20, 10) : QColor(20, 10, 10));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawRect(-20, -20, 40, 40);

    // Logic Label
    painter->setPen(m_logicState ? QColor(0, 255, 100) : QColor(255, 50, 50));
    QFont f = painter->font();
    f.setBold(true);
    f.setPointSize(20);
    painter->setFont(f);
    painter->drawText(QRectF(-20, -20, 40, 40), Qt::AlignCenter, m_logicState ? "1" : "0");

    // Pin line (Left side for probe)
    painter->setPen(QPen(QColor(100, 100, 105), 1.5));
    painter->drawLine(-40, 0, -25, 0);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LogicProbeItem::connectionPoints() const {
    return { QPointF(-40, 0) }; // 1 Input Pin on the Left
}

void LogicProbeItem::updateFromSimulation(double voltage) {
    m_lastVoltage = voltage;
    bool newState = (voltage >= 2.5); // Logic threshold
    if (newState != m_logicState) {
        m_logicState = newState;
        update();
    }
}

QJsonObject LogicProbeItem::toJson() const {
    return SchematicItem::toJson();
}

bool LogicProbeItem::fromJson(const QJsonObject& json) {
    return SchematicItem::fromJson(json);
}

SchematicItem* LogicProbeItem::clone() const {
    return new LogicProbeItem(pos());
}
