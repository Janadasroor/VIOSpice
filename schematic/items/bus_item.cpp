/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bus_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineF>

BusItem::BusItem(QPointF start, QPointF end, QGraphicsItem *parent)
    : SchematicItem(parent) {
    if (!start.isNull() || !end.isNull()) {
        m_points << start << end;
    }
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setZValue(1); // Standard layer for buses
    updatePen();
}

void BusItem::updatePen() {
    QColor color = Qt::blue;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->schematicBus();
        if (color == Qt::transparent) color = Qt::blue; // Fallback
    }
    m_pen = QPen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void BusItem::setStartPoint(const QPointF& point) {
    if (m_points.isEmpty()) m_points << point << point;
    else m_points[0] = point;
    prepareGeometryChange();
    update();
}

void BusItem::setEndPoint(const QPointF& point) {
    if (m_points.isEmpty()) m_points << point << point;
    else m_points.last() = point;
    prepareGeometryChange();
    update();
}

void BusItem::addSegment(const QPointF& point) {
    m_points << point;
    prepareGeometryChange();
    update();
}

void BusItem::setPoints(const QList<QPointF>& points) {
    m_points = points;
    prepareGeometryChange();
    update();
}

void BusItem::addJunction(const QPointF& point) {
    for (const QPointF& existing : m_junctions) {
        if (QLineF(existing, point).length() < 0.75) return;
    }
    m_junctions.append(point);
    update();
}

void BusItem::removeJunction(const QPointF& point) {
    for (int i = m_junctions.size() - 1; i >= 0; --i) {
        if (QLineF(m_junctions[i], point).length() < 2.0) {
            m_junctions.removeAt(i);
        }
    }
    update();
}

QPointF BusItem::closestPointOnBus(const QPointF& pt, qreal* outDistSq, int* outSegmentIdx) const {
    if (m_points.size() < 2) {
        if (outDistSq) *outDistSq = std::numeric_limits<qreal>::max();
        if (outSegmentIdx) *outSegmentIdx = -1;
        return pt;
    }

    qreal bestDistSq = std::numeric_limits<qreal>::max();
    QPointF bestPoint = m_points.first();
    int bestSegment = 0;

    for (int i = 0; i < m_points.size() - 1; ++i) {
        const QPointF a = m_points[i];
        const QPointF b = m_points[i + 1];
        const QPointF ab = b - a;
        const qreal abLenSq = ab.x() * ab.x() + ab.y() * ab.y();
        
        qreal t = 0.0;
        if (abLenSq > 1e-9) {
            t = ((pt.x() - a.x()) * ab.x() + (pt.y() - a.y()) * ab.y()) / abLenSq;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
        }
        const QPointF proj(a.x() + ab.x() * t, a.y() + ab.y() * t);
        const QPointF diff = pt - proj;
        const qreal dSq = diff.x() * diff.x() + diff.y() * diff.y();

        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestPoint = proj;
            bestSegment = i;
        }
    }

    if (outDistSq) *outDistSq = bestDistSq;
    if (outSegmentIdx) *outSegmentIdx = bestSegment;
    return bestPoint;
}

bool BusItem::isNearBus(const QPointF& pt, qreal tolerance) const {
    qreal dSq = 0.0;
    closestPointOnBus(pt, &dSq);
    return dSq <= (tolerance * tolerance);
}

QRectF BusItem::labelRect() const {
    if (netName().trimmed().isEmpty() || m_points.size() < 2) return QRectF();

    // Find the longest segment to place the label badge
    int longestIdx = 0;
    qreal maxLenSq = 0.0;
    for (int i = 0; i < m_points.size() - 1; ++i) {
        const QPointF diff = m_points[i + 1] - m_points[i];
        const qreal lenSq = diff.x() * diff.x() + diff.y() * diff.y();
        if (lenSq > maxLenSq) {
            maxLenSq = lenSq;
            longestIdx = i;
        }
    }

    const QPointF a = m_points[longestIdx];
    const QPointF b = m_points[longestIdx + 1];
    const QPointF mid = (a + b) * 0.5;

    QFont font("Consolas", 8, QFont::Bold);
    QFontMetrics fm(font);
    const int textW = fm.horizontalAdvance(netName()) + 8;
    const int textH = fm.height() + 4;

    return QRectF(mid.x() - textW * 0.5, mid.y() - textH * 0.5 - 10, textW, textH);
}

QRectF BusItem::boundingRect() const {
    if (m_points.isEmpty()) return QRectF();
    
    qreal minX = m_points[0].x();
    qreal minY = m_points[0].y();
    qreal maxX = minX;
    qreal maxY = minY;
    
    for (const QPointF& p : m_points) {
        minX = qMin(minX, p.x());
        minY = qMin(minY, p.y());
        maxX = qMax(maxX, p.x());
        maxY = qMax(maxY, p.y());
    }
    
    qreal w = m_pen.widthF() + 4;
    QRectF rect = QRectF(minX, minY, maxX - minX, maxY - minY).adjusted(-w, -w, w, w);
    const QRectF lr = labelRect();
    if (lr.isValid()) {
        rect = rect.united(lr.adjusted(-2, -2, 2, 2));
    }
    return rect;
}

void BusItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    if (m_points.size() < 2) return;
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen p = m_pen;
    if (isSelected()) {
        p.setColor(QColor("#facc15")); // Amber yellow highlight on select
        p.setStyle(Qt::SolidLine);
    }
    
    // Draw glowing selection underlay if selected
    if (isSelected()) {
        QPen glowPen(QColor(250, 204, 21, 50), m_pen.widthF() + 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(glowPen);
        for (int i = 0; i < m_points.size() - 1; ++i) {
            painter->drawLine(m_points[i], m_points[i+1]);
        }
    }

    // Draw highlight glow if enabled by net manager
    if (m_isHighlighted) {
        QPen highlightPen(QColor(255, 215, 0, 120), m_pen.widthF() + 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(highlightPen);
        for (int i = 0; i < m_points.size() - 1; ++i) {
            painter->drawLine(m_points[i], m_points[i+1]);
        }
    }

    painter->setPen(p);
    for (int i = 0; i < m_points.size() - 1; ++i) {
        painter->drawLine(m_points[i], m_points[i+1]);
    }

    // Draw bus junction dots
    if (!m_junctions.isEmpty()) {
        QColor dotColor = ThemeManager::theme() ? ThemeManager::theme()->schematicBus() : QColor("#3b82f6");
        if (dotColor == Qt::transparent) dotColor = QColor("#3b82f6");
        painter->setPen(QPen(dotColor.lighter(130), 1.0));
        painter->setBrush(QBrush(dotColor));
        for (const QPointF& junction : m_junctions) {
            painter->drawEllipse(junction, 3.5, 3.5);
        }
    }

    // Draw Bus Label Badge if set
    const QString label = netName().trimmed();
    if (!label.isEmpty()) {
        const QRectF lr = labelRect();
        if (lr.isValid()) {
            painter->setPen(QPen(QColor("#334155"), 1.0));
            painter->setBrush(QBrush(QColor("#0f172a")));
            painter->drawRoundedRect(lr, 3.0, 3.0);

            QFont font("Consolas", 8, QFont::Bold);
            painter->setFont(font);
            painter->setPen(QColor("#38bdf8"));
            painter->drawText(lr, Qt::AlignCenter, label);
        }
    }
    
    drawConnectionPointHighlights(painter);
}

QJsonObject BusItem::toJson() const {
    QJsonObject json;
    json["type"] = "Bus";
    json["id"] = m_id.toString();
    
    // Save visual properties
    json["color"] = m_pen.color().name();
    json["width"] = m_pen.widthF();
    json["lineStyle"] = static_cast<int>(m_pen.style());

    QJsonArray pointsArray;
    for (const QPointF& p : m_points) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        pointsArray.append(pt);
    }
    json["points"] = pointsArray;

    QJsonArray junctionsArray;
    for (const QPointF& junction : m_junctions) {
        QJsonObject jo;
        jo["x"] = junction.x();
        jo["y"] = junction.y();
        junctionsArray.append(jo);
    }
    json["junctions"] = junctionsArray;
    
    return json;
}

bool BusItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    
    // Restore visual properties
    updatePen(); // Set defaults first
    if (json.contains("color")) {
        QColor color(json["color"].toString());
        qreal width = json["width"].toDouble(m_pen.widthF());
        Qt::PenStyle style = static_cast<Qt::PenStyle>(json["lineStyle"].toInt(Qt::SolidLine));
        m_pen = QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin);
    }

    m_points.clear();
    QJsonArray pointsArray = json["points"].toArray();
    for (int i = 0; i < pointsArray.size(); ++i) {
        QJsonObject pt = pointsArray[i].toObject();
        m_points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
    }

    m_junctions.clear();
    const QJsonArray junctionsArray = json["junctions"].toArray();
    for (const QJsonValue& jv : junctionsArray) {
        const QJsonObject jo = jv.toObject();
        m_junctions.append(QPointF(jo["x"].toDouble(), jo["y"].toDouble()));
    }
    
    updatePen();
    return true;
}

SchematicItem* BusItem::clone() const {
    BusItem* item = new BusItem();
    item->setPoints(m_points);
    return item;
}
