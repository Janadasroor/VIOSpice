/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "instrument_probe_item.h"
#include <QPainter>
#include <QJsonObject>

InstrumentProbeItem::InstrumentProbeItem(Kind kind, QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_kind(kind) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    
    switch (m_kind) {
        case Kind::Voltmeter: setReference("V_MET1"); setValue("0V"); break;
        case Kind::Ammeter: setReference("A_MET1"); setValue("0A"); break;
        case Kind::Wattmeter: setReference("W_MET1"); setValue("0W"); break;
        case Kind::Oscilloscope: setReference("OSC1"); break;
        default: setReference("P1"); break;
    }
}

QString InstrumentProbeItem::itemTypeName() const {
    switch (m_kind) {
        case Kind::Voltmeter: return "Voltmeter (DC)";
        case Kind::Ammeter: return "Ammeter (DC)";
        case Kind::Wattmeter: return "Wattmeter";
        case Kind::Oscilloscope: return "Oscilloscope Instrument";
        default: return "InstrumentProbe";
    }
}

QRectF InstrumentProbeItem::boundingRect() const {
    if (m_kind == Kind::Oscilloscope) {
        return QRectF(-60, -75, 105, 175);
    }
    // Standard 2-terminal instrument
    return QRectF(-60, -30, 120, 80);
}

QList<QPointF> InstrumentProbeItem::connectionPoints() const {
    if (m_kind == Kind::Oscilloscope) {
        return {
            QPointF(-50, 20), // CH1
            QPointF(-50, 40), // CH2
            QPointF(-50, 60), // CH3
            QPointF(-50, 80)  // CH4
        };
    }

    // Two-terminal instrument symbol.
    return {
        QPointF(-50, 20),  // pin 1
        QPointF(50, 20)    // pin 2
    };
}

void InstrumentProbeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF body = QRectF(-45, -75, 90, 175).adjusted(2, 2, -2, -2);
    const QRectF bodySmall = QRectF(-45, -30, 90, 80).adjusted(2, 2, -2, -2);
    
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    
    if (m_kind == Kind::Oscilloscope) {
        painter->drawRoundedRect(body, 5, 5);
    } else {
        painter->drawRoundedRect(bodySmall, 5, 5);
    }

    painter->setPen(QColor(0, 255, 100));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(8);
    painter->setFont(titleFont);

    const QString title = displayName().toUpper();
    const QRectF titleRect = (m_kind == Kind::Oscilloscope) ? body : bodySmall;
    painter->drawText(QRectF(titleRect.left(), titleRect.top() + 2, titleRect.width(), 16), Qt::AlignCenter, title);

    if (m_kind == Kind::Oscilloscope) {
        painter->setBrush(QColor(10, 20, 10));
        painter->setPen(QPen(Qt::white, 1));
        painter->drawRect(-30, -50, 60, 50);

        painter->setPen(QPen(QColor(30, 50, 30), 1));
        for (int i = -20; i <= 20; i += 10) painter->drawLine(i, -50, i, 0);
        for (int i = -40; i <= -10; i += 10) painter->drawLine(-30, i, 30, i);

        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize(7);
        painter->setFont(f);
        
        // Pin lines (sticking out to the left)
        painter->setPen(QPen(QColor(100, 100, 105), 1.5));
        painter->drawLine(-50, 20, -30, 20);
        painter->drawLine(-50, 40, -30, 40);
        painter->drawLine(-50, 60, -30, 60);
        painter->drawLine(-50, 80, -30, 80);

        // Labels centered inside the body near pins
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-28, 15, 30, 10), Qt::AlignLeft | Qt::AlignVCenter, "CH1");
        painter->drawText(QRectF(-28, 35, 30, 10), Qt::AlignLeft | Qt::AlignVCenter, "CH2");
        painter->drawText(QRectF(-28, 55, 30, 10), Qt::AlignLeft | Qt::AlignVCenter, "CH3");
        painter->drawText(QRectF(-28, 75, 30, 10), Qt::AlignLeft | Qt::AlignVCenter, "CH4");
    } else {
        // Professional Rectangular Meter Design
        // Body already drawn

        // LCD Display Area
        QRectF lcdRect = QRectF(-32, 5, 64, 30);
        painter->setBrush(QColor(15, 20, 15)); // Deep green/black background
        painter->setPen(QPen(QColor(0, 255, 100, 100), 1)); // Faint green border
        painter->drawRect(lcdRect);

        // Subtle glow effect behind text
        QRadialGradient glow(0, 20, 30);
        glow.setColorAt(0, QColor(0, 255, 100, 40));
        glow.setColorAt(1, Qt::transparent);
        painter->setBrush(glow);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(lcdRect);

        // Display Value
        painter->setPen(QColor(0, 255, 100));
        QFont mono("Monospace", 10, QFont::Bold);
        painter->setFont(mono);
        painter->drawText(lcdRect, Qt::AlignCenter, "0.00");

        // Units / Kind Glyph
        painter->setPen(QColor(255, 220, 120));
        QFont unitFont("Inter", 6, QFont::Bold);
        painter->setFont(unitFont);
        QString unit = "V";
        if (m_kind == Kind::Ammeter) unit = "A";
        else if (m_kind == Kind::Wattmeter) unit = "W";
        else if (m_kind == Kind::FrequencyCounter) unit = "Hz";
        else if (m_kind == Kind::LogicProbe) unit = "LOGIC";
        painter->drawText(QRectF(lcdRect.right() - 25, lcdRect.bottom() - 12, 20, 10), Qt::AlignRight, unit);

        // Pins
        painter->setPen(QPen(QColor(100, 100, 105), 1.5));
        painter->drawLine(-50, 20, -32, 20); // Input
        painter->drawLine(50, 20, 32, 20);   // Output

        // Polarized indicators (+ / -)
        painter->setPen(QColor(200, 200, 205));
        painter->setFont(QFont("Inter", 6, QFont::Bold));
        painter->drawText(QRectF(-40, 10, 10, 10), Qt::AlignCenter, "+");
        painter->drawText(QRectF(30, 10, 10, 10), Qt::AlignCenter, "-");
    }

    drawConnectionPointHighlights(painter);
}

QJsonObject InstrumentProbeItem::toJson() const {
    QJsonObject j;
    if (m_kind == Kind::Oscilloscope) j["type"] = "Oscilloscope Instrument";
    else if (m_kind == Kind::Voltmeter) j["type"] = "Voltmeter (DC)";
    else if (m_kind == Kind::Ammeter) j["type"] = "Ammeter (DC)";
    else if (m_kind == Kind::Wattmeter) j["type"] = "Wattmeter";
    else if (m_kind == Kind::FrequencyCounter) j["type"] = "Frequency Counter";
    else j["type"] = "Logic Probe";
    j["x"] = pos().x();
    j["y"] = pos().y();
    j["id"] = id().toString();
    j["reference"] = reference();
    return j;
}

bool InstrumentProbeItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    return true;
}

SchematicItem* InstrumentProbeItem::clone() const {
    auto* item = new InstrumentProbeItem(m_kind, pos(), parentItem());
    item->fromJson(toJson());
    return item;
}

QString InstrumentProbeItem::referencePrefix() const {
    switch (m_kind) {
        case Kind::Voltmeter: return "V_MET";
        case Kind::Ammeter: return "A_MET";
        case Kind::Wattmeter: return "W_MET";
        case Kind::Oscilloscope: return "OSC";
        default: return "P";
    }
}

QString InstrumentProbeItem::displayName() const {
    switch (m_kind) {
        case Kind::Voltmeter: return "Voltmeter";
        case Kind::Ammeter: return "Ammeter";
        case Kind::Wattmeter: return "Wattmeter";
        case Kind::FrequencyCounter: return "Frequency";
        case Kind::LogicProbe: return "Logic";
        case Kind::Oscilloscope: return "Oscilloscope";
    }
    return "Probe";
}
