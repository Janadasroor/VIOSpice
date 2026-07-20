/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_primitive_item.h"
#include "theme_manager.h"
#include <QPainterPathStroker>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>

namespace Flux {
namespace Item {

static QRectF getCustomPrimitivesBoundingRect(const QJsonArray& customPrims) {
    if (customPrims.isEmpty()) return QRectF();
    QRectF rect;
    bool first = true;
    for (const auto& val : customPrims) {
        QJsonObject primObj = val.toObject();
        QString type = primObj.value("type").toString().toLower();
        QJsonObject data = primObj.value("data").toObject();
        QRectF primRect;
        if (type == "line") {
            qreal x1 = data.value("x1").toDouble();
            qreal y1 = data.value("y1").toDouble();
            qreal x2 = data.value("x2").toDouble();
            qreal y2 = data.value("y2").toDouble();
            qreal w = data.value("width").toDouble(0.15);
            primRect = QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
            primRect.adjust(-w/2, -w/2, w/2, w/2);
        } else if (type == "rect") {
            qreal x = data.value("x").toDouble();
            qreal y = data.value("y").toDouble();
            qreal w = data.value("width").toDouble();
            qreal h = data.value("height").toDouble();
            primRect = QRectF(x, y, w, h);
        } else if (type == "circle") {
            qreal cx = data.value("cx").toDouble();
            qreal cy = data.value("cy").toDouble();
            qreal r = data.value("radius").toDouble();
            primRect = QRectF(cx - r, cy - r, r * 2.0, r * 2.0);
        } else if (type == "arc") {
            qreal cx = data.value("cx").toDouble();
            qreal cy = data.value("cy").toDouble();
            qreal r = data.value("radius").toDouble();
            primRect = QRectF(cx - r, cy - r, r * 2.0, r * 2.0);
        } else if (type == "polygon") {
            QJsonArray pts = data.value("points").toArray();
            if (!pts.isEmpty()) {
                QJsonObject pt0 = pts.first().toObject();
                primRect = QRectF(pt0["x"].toDouble(), pt0["y"].toDouble(), 0, 0);
                for (const auto& v : pts) {
                    QJsonObject pt = v.toObject();
                    primRect = primRect.united(QRectF(pt["x"].toDouble(), pt["y"].toDouble(), 0, 0));
                }
            }
        }
        if (primRect.isValid()) {
            if (first) {
                rect = primRect;
                first = false;
            } else {
                rect = rect.united(primRect);
            }
        }
    }
    return rect;
}

static void drawCustomPadPrimitives(QPainter* painter, const QJsonArray& customPrims, const QColor& padColor) {
    auto parsePoints = [](const QJsonArray& arr) {
        QPolygonF poly;
        for (const auto& val : arr) {
            QJsonObject pt = val.toObject();
            poly << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
        }
        return poly;
    };

    for (const auto& val : customPrims) {
        QJsonObject primObj = val.toObject();
        QString type = primObj.value("type").toString().toLower();
        QJsonObject data = primObj.value("data").toObject();
        
        if (type == "line") {
            qreal x1 = data.value("x1").toDouble();
            qreal y1 = data.value("y1").toDouble();
            qreal x2 = data.value("x2").toDouble();
            qreal y2 = data.value("y2").toDouble();
            qreal w = data.value("width").toDouble(0.15);
            painter->setPen(QPen(padColor, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        } else if (type == "rect") {
            qreal x = data.value("x").toDouble();
            qreal y = data.value("y").toDouble();
            qreal w = data.value("width").toDouble();
            qreal h = data.value("height").toDouble();
            qreal lw = data.value("lineWidth").toDouble(0.15);
            bool filled = data.value("filled").toBool(true);
            painter->setPen(QPen(padColor, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (filled) painter->setBrush(padColor);
            else painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(x, y, w, h));
        } else if (type == "circle") {
            qreal cx = data.value("cx").toDouble();
            qreal cy = data.value("cy").toDouble();
            qreal r = data.value("radius").toDouble();
            qreal lw = data.value("lineWidth").toDouble(0.15);
            bool filled = data.value("filled").toBool(true);
            painter->setPen(QPen(padColor, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (filled) painter->setBrush(padColor);
            else painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(cx, cy), r, r);
        } else if (type == "arc") {
            qreal cx = data.value("cx").toDouble();
            qreal cy = data.value("cy").toDouble();
            qreal r = data.value("radius").toDouble();
            qreal lw = data.value("lineWidth").toDouble(0.15);
            qreal startAngle = data.value("startAngle").toDouble();
            qreal spanAngle = data.value("spanAngle").toDouble();
            painter->setPen(QPen(padColor, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawArc(QRectF(cx - r, cy - r, r * 2.0, r * 2.0), qRound(startAngle * 16.0), qRound(spanAngle * 16.0));
        } else if (type == "polygon") {
            QPolygonF poly = parsePoints(data.value("points").toArray());
            bool filled = data.value("filled").toBool(true);
            qreal lw = data.value("lineWidth").toDouble(0.15);
            painter->setPen(QPen(padColor, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (filled) {
                painter->setBrush(padColor);
                painter->drawPolygon(poly);
            } else {
                painter->setBrush(Qt::NoBrush);
                painter->drawPolyline(poly);
            }
        }
    }
}

FootprintPrimitiveItem::FootprintPrimitiveItem(const Model::FootprintPrimitive& model, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_model(model) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
}

void FootprintPrimitiveItem::paintSelectionBorder(QPainter* painter, const QStyleOptionGraphicsItem* option) const {
    if (option->state & QStyle::State_Selected) {
        painter->save();
        QColor selectionColor(56, 189, 248, 180);
        QPen selectionPen(selectionColor, 0.0, Qt::DashLine);
        selectionPen.setDashPattern({2.0, 2.0});
        painter->setPen(selectionPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
        painter->restore();
    }
}

// Helper to get color for a footprint layer
static QColor getLayerColor(Model::FootprintPrimitive::Layer layer) {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return QColor(255, 255, 100);

    switch (layer) {
        case Model::FootprintPrimitive::Top_Copper: return theme->topCopper();
        case Model::FootprintPrimitive::Bottom_Copper: return theme->bottomCopper();
        case Model::FootprintPrimitive::Top_Silkscreen: return theme->topSilkscreen();
        case Model::FootprintPrimitive::Bottom_Silkscreen: return theme->bottomSilkscreen();
        case Model::FootprintPrimitive::Top_SolderMask: return theme->topSoldermask();
        case Model::FootprintPrimitive::Bottom_SolderMask: return theme->bottomSoldermask();
        case Model::FootprintPrimitive::Top_Courtyard: return theme->edgeCuts().lighter(115);
        case Model::FootprintPrimitive::Bottom_Courtyard: return theme->edgeCuts().darker(110);
        case Model::FootprintPrimitive::Top_Fabrication: return theme->componentOutline();
        case Model::FootprintPrimitive::Bottom_Fabrication: return theme->componentOutline().darker(120);
        case Model::FootprintPrimitive::Top_SolderPaste: return theme->padFill().lighter(125);
        case Model::FootprintPrimitive::Bottom_SolderPaste: return theme->padFill().darker(105);
        case Model::FootprintPrimitive::Top_Adhesive: return QColor(168, 85, 247);
        case Model::FootprintPrimitive::Bottom_Adhesive: return QColor(217, 70, 239);
        case Model::FootprintPrimitive::Inner_Copper_1: return theme->topCopper().lighter(120);
        case Model::FootprintPrimitive::Inner_Copper_2: return theme->bottomCopper().lighter(120);
        case Model::FootprintPrimitive::Inner_Copper_3: return theme->topCopper().darker(115);
        case Model::FootprintPrimitive::Inner_Copper_4: return theme->bottomCopper().darker(115);
        default: return theme->multiLayer();
    }
}

// --- Line ---
QRectF FootprintLineItem::boundingRect() const {
    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();
    qreal w = m_model.data.value("width").toDouble(0.1);
    return QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized().adjusted(-w, -w, w, w);
}

QPainterPath FootprintLineItem::shape() const {
    QPainterPath path;
    path.moveTo(m_model.data.value("x1").toDouble(), m_model.data.value("y1").toDouble());
    path.lineTo(m_model.data.value("x2").toDouble(), m_model.data.value("y2").toDouble());
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(0.2, m_model.data.value("width").toDouble(0.1)));
    return stroker.createStroke(path);
}

void FootprintLineItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();
    qreal w = m_model.data.value("width").toDouble(0.1);

    painter->setPen(QPen(getLayerColor(m_model.layer), w, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));

    paintSelectionBorder(painter, option);
}

// --- Rect ---
QRectF FootprintRectItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    return QRectF(x, y, w, h).adjusted(-lw, -lw, lw, lw);
}

void FootprintRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    bool filled = m_model.data.value("filled").toBool();

    QColor color = getLayerColor(m_model.layer);
    painter->setPen(QPen(color, lw));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawRect(QRectF(x, y, w, h));
    paintSelectionBorder(painter, option);
}

// --- Circle ---
QRectF FootprintCircleItem::boundingRect() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    return QRectF(cx - r, cy - r, r * 2, r * 2).adjusted(-lw, -lw, lw, lw);
}

void FootprintCircleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    bool filled = m_model.data.value("filled").toBool();

    QColor color = getLayerColor(m_model.layer);
    painter->setPen(QPen(color, lw));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawEllipse(QPointF(cx, cy), r, r);
    paintSelectionBorder(painter, option);
}

// --- Arc ---
QRectF FootprintArcItem::boundingRect() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("width").toDouble(0.1);
    return QRectF(cx - r, cy - r, r * 2, r * 2).adjusted(-lw, -lw, lw, lw);
}

QPainterPath FootprintArcItem::shape() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal start = m_model.data.value("startAngle").toDouble();
    qreal span = m_model.data.value("spanAngle").toDouble();
    
    QPainterPath path;
    path.arcMoveTo(cx - r, cy - r, r * 2, r * 2, start);
    path.arcTo(cx - r, cy - r, r * 2, r * 2, start, span);
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(0.2, m_model.data.value("width").toDouble(0.1)));
    return stroker.createStroke(path);
}

void FootprintArcItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal start = m_model.data.value("startAngle").toDouble();
    qreal span = m_model.data.value("spanAngle").toDouble();
    qreal lw = m_model.data.value("width").toDouble(0.1);

    painter->setPen(QPen(getLayerColor(m_model.layer), lw, Qt::SolidLine, Qt::RoundCap));
    painter->drawArc(QRectF(cx - r, cy - r, r * 2, r * 2), start * 16, span * 16);
    paintSelectionBorder(painter, option);
}

// --- Pad ---
QRectF FootprintPadItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    QRectF rect(x - w/2, y - h/2, w, h);
    QString shape = m_model.data.value("shape").toString();
    if (shape.toLower() == "custom" && m_model.data.contains("custom_primitives")) {
        QRectF primsRect = getCustomPrimitivesBoundingRect(m_model.data.value("custom_primitives").toArray());
        if (primsRect.isValid()) {
            primsRect.translate(x, y);
            rect = rect.united(primsRect);
        }
    }
    return rect.adjusted(-0.1, -0.1, 0.1, 0.1);
}

QPainterPath FootprintPadItem::shape() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    QString shape = m_model.data.value("shape").toString();
    
    QPainterPath path;
    if (shape == "Circle" || shape == "Round") {
        path.addEllipse(QPointF(x, y), w/2, h/2);
    } else if (shape.toLower() == "custom" && m_model.data.contains("custom_primitives")) {
        for (const auto& val : m_model.data.value("custom_primitives").toArray()) {
            QJsonObject primObj = val.toObject();
            QString type = primObj.value("type").toString().toLower();
            QJsonObject data = primObj.value("data").toObject();
            if (type == "line") {
                qreal x1 = data.value("x1").toDouble() + x;
                qreal y1 = data.value("y1").toDouble() + y;
                qreal x2 = data.value("x2").toDouble() + x;
                qreal y2 = data.value("y2").toDouble() + y;
                qreal w = data.value("width").toDouble(0.15);
                QPainterPathStroker stroker;
                stroker.setWidth(w);
                stroker.setCapStyle(Qt::RoundCap);
                stroker.setJoinStyle(Qt::RoundJoin);
                QPainterPath lp;
                lp.moveTo(x1, y1);
                lp.lineTo(x2, y2);
                path.addPath(stroker.createStroke(lp));
            } else if (type == "rect") {
                qreal rx = data.value("x").toDouble() + x;
                qreal ry = data.value("y").toDouble() + y;
                qreal rw = data.value("width").toDouble();
                qreal rh = data.value("height").toDouble();
                path.addRect(rx, ry, rw, rh);
            } else if (type == "circle") {
                qreal cx = data.value("cx").toDouble() + x;
                qreal cy = data.value("cy").toDouble() + y;
                qreal r = data.value("radius").toDouble();
                path.addEllipse(QPointF(cx, cy), r, r);
            } else if (type == "arc") {
                qreal cx = data.value("cx").toDouble() + x;
                qreal cy = data.value("cy").toDouble() + y;
                qreal r = data.value("radius").toDouble();
                qreal lw = data.value("lineWidth").toDouble(0.15);
                QPainterPathStroker stroker;
                stroker.setWidth(lw);
                QPainterPath ap;
                qreal startAngle = data.value("startAngle").toDouble();
                qreal spanAngle = data.value("spanAngle").toDouble();
                ap.arcMoveTo(QRectF(cx - r, cy - r, r * 2.0, r * 2.0), startAngle);
                ap.arcTo(QRectF(cx - r, cy - r, r * 2.0, r * 2.0), startAngle, spanAngle);
                path.addPath(stroker.createStroke(ap));
            } else if (type == "polygon") {
                QPolygonF poly;
                for (const auto& v : data.value("points").toArray()) {
                    QJsonObject pt = v.toObject();
                    poly << QPointF(pt["x"].toDouble() + x, pt["y"].toDouble() + y);
                }
                path.addPolygon(poly);
            }
        }
    } else {
        path.addRect(x - w/2, y - h/2, w, h);
    }
    return path;
}

void FootprintPadItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    QString shape = m_model.data.value("shape").toString();
    QString num = m_model.data.value("number").toString();
    const bool isThroughHole = m_model.data.value("drill_size").toDouble() > 0.001;
    const QColor color = isThroughHole && ThemeManager::theme()
        ? ThemeManager::theme()->multiLayer()
        : getLayerColor(m_model.layer);
    painter->setPen(QPen(color.darker(150), 0));
    painter->setBrush(color);

    if (shape == "Circle" || shape == "Round") {
        painter->drawEllipse(QPointF(x, y), w/2, h/2);
    } else if (shape == "Oblong") {
        qreal r = std::min(w, h) / 2.0;
        painter->drawRoundedRect(QRectF(x - w/2, y - h/2, w, h), r, r);
    } else if (shape == "RoundedRect") {
        qreal r = std::min(w, h) * 0.25;
        painter->drawRoundedRect(QRectF(x - w/2, y - h/2, w, h), r, r);
    } else if (shape.toLower() == "custom" && m_model.data.contains("custom_primitives")) {
        painter->save();
        painter->translate(x, y);
        drawCustomPadPrimitives(painter, m_model.data.value("custom_primitives").toArray(), color);
        painter->restore();
    } else {
        painter->drawRect(QRectF(x - w/2, y - h/2, w, h));
    }

    if (isThroughHole) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(30, 30, 30));
        const qreal drill = m_model.data.value("drill_size").toDouble();
        painter->drawEllipse(QPointF(x, y), drill / 2.0, drill / 2.0);
    }

    // Draw number
    if (!num.isEmpty()) {
        double maxDim = std::max(w, h);
        double minDim = std::min(w, h);
        if (maxDim > 0.5) {
            painter->setPen(color.lightness() < 140 ? Qt::white : Qt::black);
            double fontSize = qMax(minDim * 0.5, 0.7); // 50% of min dimension, but at least 0.7 points
            fontSize = qMin(fontSize, maxDim * 0.8);   // Ensure it fits within the pad length
            
            // Safety check: skip text drawing if effective pixel size is too small to avoid Qt/FreeType crash
            QTransform trans = painter->transform();
            qreal viewScale = QLineF(trans.map(QPointF(0,0)), trans.map(QPointF(1,0))).length();
            if (fontSize * viewScale >= 2.0) {
                QFont font("Monospace");
                font.setPointSizeF(fontSize);
                painter->setFont(font);
                painter->drawText(QRectF(x - w/2, y - h/2, w, h), Qt::AlignCenter, num);
            }
        }
    }

    paintSelectionBorder(painter, option);
}

// --- Text ---
QRectF FootprintTextItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal h = m_model.data.value("height").toDouble(1.0);
    return QRectF(x, y, h * 5, h); // Approx
}

void FootprintTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal h = m_model.data.value("height").toDouble(1.0);
    QString text = m_model.data.value("text").toString();

    painter->setPen(getLayerColor(m_model.layer));
    
    // Safety check: skip text drawing if effective pixel size is too small to avoid Qt/FreeType crash
    QTransform trans = painter->transform();
    qreal viewScale = QLineF(trans.map(QPointF(0,0)), trans.map(QPointF(1,0))).length();
    if (h * viewScale >= 2.0) {
        painter->setFont(QFont("SansSerif", h));
        painter->drawText(QPointF(x, y + h), text);
    }

    paintSelectionBorder(painter, option);
}

// --- Polygon ---
FootprintPolygonItem::FootprintPolygonItem(const Model::FootprintPrimitive& model, QGraphicsItem* parent)
    : FootprintPrimitiveItem(model, parent)
{
}

QRectF FootprintPolygonItem::boundingRect() const {
    QJsonArray arr = m_model.data.value("points").toArray();
    if (arr.isEmpty()) return QRectF();
    QJsonObject firstPt = arr.first().toObject();
    QRectF r(firstPt["x"].toDouble(), firstPt["y"].toDouble(), 0, 0);
    for (const auto& val : arr) {
        QJsonObject pt = val.toObject();
        r = r.united(QRectF(pt["x"].toDouble(), pt["y"].toDouble(), 0, 0));
    }
    qreal w = m_model.data.value("lineWidth").toDouble(0.15);
    r.adjust(-w/2, -w/2, w/2, w/2);
    return r;
}

QPainterPath FootprintPolygonItem::shape() const {
    QPainterPath path;
    QJsonArray arr = m_model.data.value("points").toArray();
    if (arr.isEmpty()) return path;
    QJsonObject firstPt = arr.first().toObject();
    path.moveTo(firstPt["x"].toDouble(), firstPt["y"].toDouble());
    for (int i = 1; i < arr.size(); ++i) {
        QJsonObject pt = arr[i].toObject();
        path.lineTo(pt["x"].toDouble(), pt["y"].toDouble());
    }
    bool filled = m_model.data.value("filled").toBool(true);
    if (filled) {
        path.closeSubpath();
        return path;
    } else {
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(0.2, m_model.data.value("lineWidth").toDouble(0.15)));
        return stroker.createStroke(path);
    }
}

void FootprintPolygonItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    QJsonArray arr = m_model.data.value("points").toArray();
    if (arr.isEmpty()) return;
    
    QPolygonF poly;
    for (const auto& val : arr) {
        QJsonObject pt = val.toObject();
        poly << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
    }
    
    QColor color = getLayerColor(m_model.layer);
    bool filled = m_model.data.value("filled").toBool(true);
    qreal lw = m_model.data.value("lineWidth").toDouble(0.15);
    
    painter->setPen(QPen(color, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (filled) {
        painter->setBrush(color);
        painter->drawPolygon(poly);
    } else {
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(poly);
    }
    
    paintSelectionBorder(painter, option);
}

} // namespace Item
} // namespace Flux
