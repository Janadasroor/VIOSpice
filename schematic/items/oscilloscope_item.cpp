/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oscilloscope_item.h"
#include "net_manager.h"
#include <QPainter>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

static const QColor s_defaultColors[8] = {
    Qt::yellow, Qt::cyan, Qt::magenta, QColor(0, 255, 100),
    QColor(255, 165, 0), QColor(147, 112, 219), QColor(255, 105, 180), QColor(0, 191, 255)
};

OscilloscopeItem::OscilloscopeItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setExcludeFromPcb(true); // Instruments are excluded from PCB by default
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    setReference("OSC1");
    setValue("Oscilloscope");
}

void OscilloscopeItem::setConfig(const Config& cfg) {
    m_config = cfg;
    if (m_config.channelCount < 1) m_config.channelCount = 1;
    if (m_config.channelCount > 8) m_config.channelCount = 8;
    if (m_config.channels.size() < m_config.channelCount) {
        int oldSize = m_config.channels.size();
        m_config.channels.resize(m_config.channelCount);
        for (int i = oldSize; i < m_config.channelCount; ++i) {
            m_config.channels[i].enabled = true;
            m_config.channels[i].scale = 1.0;
            m_config.channels[i].offset = 0.0;
            m_config.channels[i].color = s_defaultColors[i % 8];
            m_config.channels[i].floatingGround = false;
        }
    }
    prepareGeometryChange();
    Q_EMIT configChanged();
    update();
}

void OscilloscopeItem::setChannelCount(int count) {
    count = std::clamp(count, 1, 8);
    if (m_config.channelCount == count) return;
    m_config.channelCount = count;
    setConfig(m_config);
}

QRectF OscilloscopeItem::boundingRect() const {
    int count = m_config.channelCount;
    qreal height = 75 + count * 22 + 25;
    return QRectF(-65, -75, 130, height);
}

void OscilloscopeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    int count = m_config.channelCount;
    qreal bodyHeight = 70 + count * 22 + 15;

    // Instrument Body (Professional Slate Gray)
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(QRectF(-50, -75, 100, bodyHeight).adjusted(2, 2, -2, -2), 5, 5);

    // Title
    painter->setPen(QColor(0, 255, 100)); // Glowing green
    QFont titleFont = painter->font(); titleFont.setBold(true); titleFont.setPointSize(8);
    painter->setFont(titleFont);
    painter->drawText(QRectF(-50, -73, 100, 15), Qt::AlignCenter, "OSCILLOSCOPE");

    // Screen area
    painter->setBrush(QColor(10, 20, 10));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawRect(-35, -52, 70, 48);
    
    // Fake grid on screen
    painter->setPen(QPen(QColor(30, 50, 30), 1));
    for (int i = -25; i <= 25; i += 10) painter->drawLine(i, -52, i, -4);
    for (int i = -42; i <= -12; i += 10) painter->drawLine(-35, i, 35, i);

    // Channel Labels and Pins
    QFont f = painter->font(); f.setPointSize(7); painter->setFont(f);

    for (int i = 0; i < count; ++i) {
        qreal y = 10 + i * 22;
        QColor chColor = (i < m_config.channels.size()) ? m_config.channels[i].color : s_defaultColors[i % 8];

        // Pin + line (Left: Positive Probe)
        painter->setPen(QPen(chColor, 2));
        painter->drawLine(-58, y, -42, y);

        // Label
        painter->setPen(chColor);
        painter->drawText(QRectF(-40, y - 7, 30, 14), Qt::AlignLeft | Qt::AlignVCenter, QString("CH%1+").arg(i + 1));

        // Pin - line (Right: Floating Ground / Reference)
        painter->setPen(QPen(QColor(150, 150, 150), 1.5));
        painter->drawLine(42, y, 58, y);
        painter->setPen(QColor(180, 180, 180));
        painter->drawText(QRectF(10, y - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QString("CH%1-").arg(i + 1));
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> OscilloscopeItem::connectionPoints() const {
    QList<QPointF> pts;
    int count = m_config.channelCount;
    // Left side: Positive Pins
    for (int i = 0; i < count; ++i) {
        pts.append(QPointF(-58, 10 + i * 22));
    }
    // Right side: Negative/Floating Ground Pins
    for (int i = 0; i < count; ++i) {
        pts.append(QPointF(58, 10 + i * 22));
    }
    return pts;
}

QJsonObject OscilloscopeItem::toJson() const {
    QJsonObject j;
    j["type"] = itemTypeName();
    j["id"] = id().toString();
    j["x"] = pos().x();
    j["y"] = pos().y();
    j["reference"] = reference();
    j["channelCount"] = m_config.channelCount;
    
    QJsonArray chs;
    for (int i = 0; i < m_config.channels.size(); ++i) {
        QJsonObject c;
        c["enabled"] = m_config.channels[i].enabled;
        c["scale"] = m_config.channels[i].scale;
        c["offset"] = m_config.channels[i].offset;
        c["color"] = m_config.channels[i].color.name();
        c["floatingGround"] = m_config.channels[i].floatingGround;
        chs.append(c);
    }
    j["channels"] = chs;
    j["timebase"] = m_config.timebase;
    j["triggerSource"] = m_config.triggerSource;
    j["triggerLevel"] = m_config.triggerLevel;
    
    return j;
}

bool OscilloscopeItem::fromJson(const QJsonObject& j) {
    const QString type = j["type"].toString();
    if (type != itemTypeName() && type != "Oscilloscope" && type != "Oscilloscope Instrument") {
        return false;
    }
    
    setId(QUuid::fromString(j["id"].toString()));
    setPos(j["x"].toDouble(), j["y"].toDouble());
    setReference(j["reference"].toString());

    if (j.contains("channelCount")) {
        m_config.channelCount = std::clamp(j["channelCount"].toInt(), 1, 8);
    } else {
        m_config.channelCount = 4;
    }
    
    m_config.channels.resize(m_config.channelCount);
    if (j.contains("channels")) {
        QJsonArray chs = j["channels"].toArray();
        for (int i = 0; i < m_config.channelCount && i < chs.size(); ++i) {
            QJsonObject c = chs[i].toObject();
            m_config.channels[i].enabled = c.value("enabled").toBool(true);
            m_config.channels[i].scale = c.value("scale").toDouble(1.0);
            m_config.channels[i].offset = c.value("offset").toDouble(0.0);
            if (c.contains("color")) m_config.channels[i].color = QColor(c["color"].toString());
            m_config.channels[i].floatingGround = c.value("floatingGround").toBool(false);
        }
    }
    
    if (j.contains("timebase")) m_config.timebase = j["timebase"].toDouble(1e-3);
    if (j.contains("triggerSource")) m_config.triggerSource = j["triggerSource"].toString("CH1");
    if (j.contains("triggerLevel")) m_config.triggerLevel = j["triggerLevel"].toDouble(0.0);
    
    prepareGeometryChange();
    update();
    return true;
}

SchematicItem* OscilloscopeItem::clone() const {
    auto* osc = new OscilloscopeItem(pos());
    osc->setConfig(m_config);
    return osc;
}

QString OscilloscopeItem::channelNet(int chIdx) const {
    if (chIdx < 0 || chIdx >= m_config.channelCount) return QString();
    return QString("Node_%1_%2_P").arg(reference()).arg(chIdx + 1);
}

QString OscilloscopeItem::channelNegNet(int chIdx) const {
    if (chIdx < 0 || chIdx >= m_config.channelCount) return QString();
    return QString("Node_%1_%2_N").arg(reference()).arg(chIdx + 1);
}

void OscilloscopeItem::configChanged() {}
