#include "adc_bridge_item.h"
#include <QPainter>
#include <QJsonObject>

AdcBridgeItem::AdcBridgeItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setReference("U1");
    setName("ADC Bridge");
}

QRectF AdcBridgeItem::boundingRect() const {
    return QRectF(-70, -30, 140, 60);
}

void AdcBridgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect(-60, -25, 120, 50);

    QPen bodyPen(Qt::white, 2);
    if (isSelected()) bodyPen.setColor(QColor(99, 102, 241));
    painter->setPen(bodyPen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRoundedRect(rect, 4, 4);

    painter->setPen(QColor(251, 191, 36));
    QFont titleFont("Inter", 8, QFont::Bold);
    painter->setFont(titleFont);
    painter->drawText(rect.adjusted(5, 2, -5, -22), Qt::AlignCenter, "ADC");

    QFont detailFont("Inter", 6);
    painter->setFont(detailFont);
    painter->setPen(QColor(161, 161, 170));
    painter->drawText(rect.adjusted(5, 20, -5, -5), Qt::AlignCenter,
                      QString("A: %1/%2V").arg(m_inLow).arg(m_inHigh));

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

QList<QPointF> AdcBridgeItem::connectionPoints() const {
    return {QPointF(-70, -5), QPointF(70, -5)};
}

QString AdcBridgeItem::pinName(int index) const {
    if (index == 0) return "In";
    if (index == 1) return "Out";
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> AdcBridgeItem::pinElectricalTypes() const {
    return {InputPin, OutputPin};
}

QJsonObject AdcBridgeItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["in_low"] = m_inLow;
    j["in_high"] = m_inHigh;
    j["rise_delay"] = m_riseDelay;
    j["fall_delay"] = m_fallDelay;
    return j;
}

bool AdcBridgeItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_inLow = json.value("in_low").toDouble(0.1);
    m_inHigh = json.value("in_high").toDouble(0.9);
    m_riseDelay = json.value("rise_delay").toDouble(1e-9);
    m_fallDelay = json.value("fall_delay").toDouble(1e-9);
    return true;
}

SchematicItem* AdcBridgeItem::clone() const {
    auto* item = new AdcBridgeItem(pos(), parentItem());
    item->m_inLow = m_inLow;
    item->m_inHigh = m_inHigh;
    item->m_riseDelay = m_riseDelay;
    item->m_fallDelay = m_fallDelay;
    return item;
}
