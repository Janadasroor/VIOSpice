#include "logic_analyzer_item.h"
#include <QPainter>
#include <QJsonObject>

LogicAnalyzerItem::LogicAnalyzerItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_channelCount(8) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    setReference("LOGIC1");
    setValue("8-Channel");
}

QRectF LogicAnalyzerItem::boundingRect() const {
    double height = (m_channelCount + 1) * 20.0 + 20.0;
    return QRectF(-60, -20, 105, height);
}

void LogicAnalyzerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF r = QRectF(-45, -20, 90, (m_channelCount + 1) * 20.0 + 20.0).adjusted(2, 2, -2, -2);
    
    // Body (Professional Slate Gray)
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(r, 5, 5);

    // Title
    painter->setPen(QColor(0, 255, 100));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(8);
    painter->setFont(titleFont);
    painter->drawText(QRectF(r.left(), r.top() + 5, r.width(), 16), Qt::AlignCenter, "LOGIC ANALYZER");

    // Channels
    painter->setFont(QFont("Inter", 7));
    painter->setPen(QPen(QColor(100, 100, 105), 1.5));
    for (int i = 0; i < m_channelCount; ++i) {
        double y = (i + 1) * 20.0;
        
        // Pin connection line (Standardized to 50)
        painter->setPen(QPen(QColor(100, 100, 105), 1.5));
        painter->drawLine(-50, y, -30, y);
        
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-28, y - 7.5, 20, 15), Qt::AlignVCenter, QString("D%1").arg(i));
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LogicAnalyzerItem::connectionPoints() const {
    QList<QPointF> pts;
    for (int i = 0; i < m_channelCount; ++i) {
        pts << QPointF(-50, (i + 1) * 20.0);
    }
    return pts;
}

void LogicAnalyzerItem::setChannelCount(int count) {
    if (m_channelCount != count) {
        prepareGeometryChange();
        m_channelCount = qBound(1, count, 16);
        setValue(QString("%1-Channel").arg(m_channelCount));
        update();
    }
}

QJsonObject LogicAnalyzerItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Logic Analyzer";
    j["channels"] = m_channelCount;
    return j;
}

bool LogicAnalyzerItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    if (json.contains("channels")) m_channelCount = json["channels"].toInt();
    return true;
}

SchematicItem* LogicAnalyzerItem::clone() const {
    auto* item = new LogicAnalyzerItem(pos());
    item->setChannelCount(m_channelCount);
    return item;
}
