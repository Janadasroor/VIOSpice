#include "virtual_terminal_item.h"
#include <QPainter>
#include <QJsonObject>

VirtualTerminalItem::VirtualTerminalItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    setReference("TERM1");
    setValue("UART 9600");
}

QRectF VirtualTerminalItem::boundingRect() const {
    return QRectF(-45, -35, 90, 70);
}

void VirtualTerminalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    
    // Main body box (leave room for pins on left)
    QRectF bodyRect = QRectF(-30, -30, 70, 60);

    QPen pen(Qt::white, 2);
    if (isSelected()) pen.setColor(QColor(0, 120, 255));
    painter->setPen(pen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRect(bodyRect);

    // Terminal Screen Area (smaller, centered inside body)
    QRectF screenRect = bodyRect.adjusted(8, 8, -8, -20);
    painter->setBrush(QColor(10, 10, 12));
    painter->drawRect(screenRect);

    // Decorative "Text" lines in the screen
    painter->setPen(QColor(0, 255, 100, 150));
    painter->drawLine(screenRect.left() + 5, screenRect.top() + 8, screenRect.left() + 25, screenRect.top() + 8);
    painter->drawLine(screenRect.left() + 5, screenRect.top() + 14, screenRect.left() + 35, screenRect.top() + 14);
    painter->drawLine(screenRect.left() + 5, screenRect.top() + 20, screenRect.left() + 20, screenRect.top() + 20);

    // "VIRTUAL TERMINAL" Header at bottom of body
    painter->setPen(Qt::white);
    painter->setFont(QFont("Inter", 6, QFont::DemiBold));
    painter->drawText(QRectF(bodyRect.left(), bodyRect.bottom() - 15, bodyRect.width(), 15), Qt::AlignCenter, "SERIAL TERM");

    // Pin lines (sticking out to the left)
    painter->setPen(QPen(QColor(100, 100, 105), 1));
    painter->drawLine(-45, 10, -30, 10); // RX
    painter->drawLine(-45, 30, -30, 30); // TX

    // Labels for Pins (inside the box, aligned left near pins)
    painter->setPen(Qt::white);
    painter->setFont(QFont("Inter", 7, QFont::Bold));
    // The Y coordinate for drawText when AlignVCenter needs a rect that surrounds the pin line
    painter->drawText(QRectF(-28, 5, 20, 10), Qt::AlignLeft | Qt::AlignVCenter, "RX");
    painter->drawText(QRectF(-28, 25, 20, 10), Qt::AlignLeft | Qt::AlignVCenter, "TX");

    drawConnectionPointHighlights(painter);
}

QList<QPointF> VirtualTerminalItem::connectionPoints() const {
    return {
        QPointF(-45, 10), // RX
        QPointF(-45, 30)  // TX
    };
}

void VirtualTerminalItem::setConfig(const Config& cfg) {
    m_config = cfg;
    QString p = m_config.parity.isEmpty() ? "N" : m_config.parity.left(1).toUpper();
    setValue(QString("%1 %2%3%4").arg(m_config.baudRate).arg(m_config.dataBits).arg(p).arg(m_config.stopBits));
    update();
}

QJsonObject VirtualTerminalItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "VirtualTerminalInstrument";
    j["baudRate"] = m_config.baudRate;
    j["dataBits"] = m_config.dataBits;
    j["parity"] = m_config.parity;
    j["stopBits"] = m_config.stopBits;
    j["hexMode"] = m_config.hexMode;
    j["autoScroll"] = m_config.autoScroll;
    return j;
}

bool VirtualTerminalItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_config.baudRate = json.value("baudRate").toInt(9600);
    m_config.dataBits = json.value("dataBits").toInt(8);
    m_config.parity = json.value("parity").toString("None");
    m_config.stopBits = json.value("stopBits").toDouble(1.0);
    m_config.hexMode = json.value("hexMode").toBool(false);
    m_config.autoScroll = json.value("autoScroll").toBool(true);
    
    QString p = m_config.parity.isEmpty() ? "N" : m_config.parity.left(1).toUpper();
    setValue(QString("%1 %2%3%4").arg(m_config.baudRate).arg(m_config.dataBits).arg(p).arg(m_config.stopBits));
    return true;
}

SchematicItem* VirtualTerminalItem::clone() const {
    auto* item = new VirtualTerminalItem(pos());
    item->setConfig(m_config);
    return item;
}
