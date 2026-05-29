#include "controlled_source_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QStyleOptionGraphicsItem>

ControlledSourceItem::ControlledSourceItem(Type type, QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_type(type) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    
    switch (m_type) {
        case VCVS: setReference("E1"); break;
        case VCCS: setReference("G1"); break;
        case CCCS: setReference("F1"); break;
        case CCVS: setReference("H1"); break;
    }
}

QString ControlledSourceItem::itemTypeName() const {
    switch (m_type) {
        case VCVS: return "VCVS";
        case VCCS: return "VCCS";
        case CCCS: return "CCCS";
        case CCVS: return "CCVS";
    }
    return "ControlledSource";
}

QString ControlledSourceItem::referencePrefix() const {
    switch (m_type) {
        case VCVS: return "E";
        case VCCS: return "G";
        case CCCS: return "F";
        case CCVS: return "H";
    }
    return "B";
}

QRectF ControlledSourceItem::boundingRect() const {
    return QRectF(-55, -35, 110, 70);
}

void ControlledSourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF diamondBounds(-30, -30, 60, 60);
    QPolygonF diamond;
    diamond << QPointF(0, -30) << QPointF(30, 0) << QPointF(0, 30) << QPointF(-30, 0);

    // Background Gradient (Diamond)
    QLinearGradient bgGrad(0, -30, 0, 30);
    bgGrad.setColorAt(0, QColor(55, 55, 60));
    bgGrad.setColorAt(1, QColor(35, 35, 40));
    
    if (option->state & QStyle::State_Selected) {
        bgGrad.setColorAt(0, QColor(100, 80, 50));
        bgGrad.setColorAt(1, QColor(60, 40, 20));
    }
    
    painter->setBrush(bgGrad);
    
    // Accent Border (Golden/Orange for Controlled Sources)
    QColor accentColor(249, 115, 22); // Orange 500
    painter->setPen(QPen(accentColor, 2));
    painter->drawPolygon(diamond);

    // Glowing Label in the middle
    painter->setPen(QColor(255, 200, 100));
    QFont f("Monospace", 10, QFont::Bold);
    painter->setFont(f);
    
    QString label;
    switch (m_type) {
        case VCVS: label = "E"; break;
        case VCCS: label = "G"; break;
        case CCCS: label = "F"; break;
        case CCVS: label = "H"; break;
    }
    
    // Subtle glow behind text
    QRadialGradient glow(0, 0, 20);
    glow.setColorAt(0, QColor(255, 180, 50, 40));
    glow.setColorAt(1, Qt::transparent);
    painter->setBrush(glow);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QRectF(-15, -15, 30, 30));

    painter->setPen(QColor(255, 180, 50));
    painter->drawText(diamondBounds, Qt::AlignCenter, label);

    // Pin tails
    painter->setPen(QPen(QColor(100, 100, 105), 1.5));
    
    const bool hasControlPins = (m_type == VCVS || m_type == VCCS);

    if (hasControlPins) {
        // Control pins (Left)
        painter->drawLine(-50, -10, -20, -10);
        painter->drawLine(-50, 10, -20, 10);

        // Control Polarized indicators
        painter->setPen(QColor(200, 200, 205));
        painter->setFont(QFont("Inter", 6, QFont::Bold));
        painter->drawText(QRectF(-45, -20, 10, 10), Qt::AlignCenter, "+");
        painter->drawText(QRectF(-45, 10, 10, 10), Qt::AlignCenter, "-");
    }
    
    // Output pins (Right)
    painter->setPen(QPen(QColor(100, 100, 105), 1.5));
    painter->drawLine(50, -10, 20, -10);
    painter->drawLine(50, 10, 20, 10);

    // Output Polarized indicators
    painter->setPen(QColor(200, 200, 205));
    painter->setFont(QFont("Inter", 6, QFont::Bold));
    painter->drawText(QRectF(35, -20, 10, 10), Qt::AlignCenter, "+");
    painter->drawText(QRectF(35, 10, 10, 10), Qt::AlignCenter, "-");

    drawConnectionPointHighlights(painter);
}

QList<QPointF> ControlledSourceItem::connectionPoints() const {
    if (m_type == CCCS || m_type == CCVS) {
        // 2 pins only (Output+, Output-)
        return {
            QPointF(50, -10),
            QPointF(50, 10)
        };
    }
    // 4 pins (Output+, Output-, Control+, Control-)
    return {
        QPointF(50, -10),
        QPointF(50, 10),
        QPointF(-50, -10),
        QPointF(-50, 10)
    };
}

QString ControlledSourceItem::pinName(int index) const {
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> ControlledSourceItem::pinElectricalTypes() const {
    if (m_type == CCCS || m_type == CCVS) {
        return { OutputPin, OutputPin };
    }
    return { OutputPin, OutputPin, InputPin, InputPin };
}

QJsonObject ControlledSourceItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["sourceType"] = (int)m_type;
    return j;
}

bool ControlledSourceItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_type = static_cast<Type>(json["sourceType"].toInt(VCVS));
    return true;
}

SchematicItem* ControlledSourceItem::clone() const {
    auto* item = new ControlledSourceItem(m_type, pos(), parentItem());
    item->fromJson(toJson());
    return item;
}
