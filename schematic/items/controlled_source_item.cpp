/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "controlled_source_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QStyleOptionGraphicsItem>

ControlledSourceItem::ControlledSourceItem(Type type, QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_type(type) {
    setExcludeFromPcb(true);
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

#include "theme_manager.h"

QRectF ControlledSourceItem::boundingRect() const {
    return QRectF(-55, -55, 110, 110);
}

void ControlledSourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    const QColor wireColor = theme ? theme->schematicLine() : QColor(220, 220, 220);
    const QColor accentColor = theme ? theme->accentColor() : QColor(59, 130, 246);

    const bool isVoltageControlled = (m_type == VCVS || m_type == VCCS);

    if (isVoltageControlled) {
        // Main circle body at (0, 0), radius 22.5 (matching Voltage Source)
        QPen circlePen(wireColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (option && (option->state & QStyle::State_Selected)) {
            circlePen.setColor(accentColor);
            circlePen.setWidthF(2.2);
        }
        painter->setPen(circlePen);
        painter->setBrush(QBrush(theme ? theme->panelBackground() : QColor(30, 30, 35)));
        painter->drawEllipse(QPointF(0.0000, 0.0000), 22.5000, 22.5000);

        // Control leads: (-45.0000, -16.8750) -> (-22.5000, -16.8750) -> (-16.8750, -14.0625)
        //               (-45.0000, 16.8750) -> (-22.5000, 16.8750) -> (-16.8750, 14.0625)
        QPen leadPen(wireColor, 1.6, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(leadPen);
        painter->drawLine(QPointF(-45.0000, -16.8750), QPointF(-22.5000, -16.8750));
        painter->drawLine(QPointF(-22.5000, -16.8750), QPointF(-16.8750, -14.0625));
        painter->drawLine(QPointF(-45.0000, 16.8750), QPointF(-22.5000, 16.8750));
        painter->drawLine(QPointF(-22.5000, 16.8750), QPointF(-16.8750, 14.0625));

        // Control polarity indicators:
        // '+' sign next to top input (y = -16.875):
        painter->drawLine(QPointF(-33.7500, -11.2500), QPointF(-28.1250, -11.2500));
        painter->drawLine(QPointF(-30.9375, -14.0625), QPointF(-30.9375, -8.4375));
        // '-' sign next to bottom input (y = 16.875):
        painter->drawLine(QPointF(-33.7500, 11.2500), QPointF(-28.1250, 11.2500));

        // Output leads: (0, -45) -> (0, -22.5), (0, 45) -> (0, 22.5)
        painter->drawLine(QPointF(0.0000, -45.0000), QPointF(0.0000, -22.5000));
        painter->drawLine(QPointF(0.0000, 45.0000), QPointF(0.0000, 22.5000));

        if (m_type == VCVS) {
            // E source: internal '+' at top, '-' at bottom
            painter->drawLine(QPointF(-2.8125, -11.2500), QPointF(2.8125, -11.2500));
            painter->drawLine(QPointF(0.0000, -14.0625), QPointF(0.0000, -8.4375));
            painter->drawLine(QPointF(-2.8125, 11.2500), QPointF(2.8125, 11.2500));
        } else {
            // G source: internal arrow pointing UP
            painter->drawLine(QPointF(0.0000, -2.8125), QPointF(0.0000, 11.2500));
            painter->drawLine(QPointF(2.8125, -2.8125), QPointF(0.0000, -11.2500));
            painter->drawLine(QPointF(-2.8125, -2.8125), QPointF(0.0000, -11.2500));
            painter->drawLine(QPointF(-2.8125, -2.8125), QPointF(2.8125, -2.8125));
        }
    } else {
        // Current-controlled (F, H):
        // Main circle at (0, 0), radius 22.5 (matching Voltage Source)
        QPen circlePen(wireColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (option && (option->state & QStyle::State_Selected)) {
            circlePen.setColor(accentColor);
            circlePen.setWidthF(2.2);
        }
        painter->setPen(circlePen);
        painter->setBrush(QBrush(theme ? theme->panelBackground() : QColor(30, 30, 35)));
        painter->drawEllipse(QPointF(0.0000, 0.0000), 22.5000, 22.5000);

        // Terminal leads: (0, -45) -> (0, -22.5), (0, 45) -> (0, 22.5)
        QPen leadPen(wireColor, 1.6, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(leadPen);
        painter->drawLine(QPointF(0.0000, -45.0000), QPointF(0.0000, -22.5000));
        painter->drawLine(QPointF(0.0000, 45.0000), QPointF(0.0000, 22.5000));

        if (m_type == CCCS) {
            // F source: internal arrow pointing DOWN
            painter->drawLine(QPointF(0.0000, -11.2500), QPointF(0.0000, 2.8125));
            painter->drawLine(QPointF(0.0000, 11.2500), QPointF(2.8125, 2.8125));
            painter->drawLine(QPointF(0.0000, 11.2500), QPointF(-2.8125, 2.8125));
            painter->drawLine(QPointF(-2.8125, 2.8125), QPointF(2.8125, 2.8125));
        } else {
            // H source: internal '+' at top, '-' at bottom
            painter->drawLine(QPointF(-5.6250, -14.0625), QPointF(5.6250, -14.0625));
            painter->drawLine(QPointF(0.0000, -19.6875), QPointF(0.0000, -8.4375));
            painter->drawLine(QPointF(-5.6250, 14.0625), QPointF(5.6250, 14.0625));
        }
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> ControlledSourceItem::connectionPoints() const {
    if (m_type == CCCS || m_type == CCVS) {
        // 2 pins (OUT+ at top 0, -45; OUT- at bottom 0, 45)
        return {
            QPointF(0.0000, -45.0000),
            QPointF(0.0000, 45.0000)
        };
    }
    // 4 pins for E and G:
    if (m_type == VCCS) {
        return {
            QPointF(0.0000, 45.0000),
            QPointF(0.0000, -45.0000),
            QPointF(-45.0000, -16.8750),
            QPointF(-45.0000, 16.8750)
        };
    }
    return {
        QPointF(0.0000, -45.0000),
        QPointF(0.0000, 45.0000),
        QPointF(-45.0000, -16.8750),
        QPointF(-45.0000, 16.8750)
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
