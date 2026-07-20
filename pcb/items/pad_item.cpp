/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pad_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

using namespace Flux::Model;

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

PadItem::PadItem(QPointF pos, double diameter, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(new PadModel())
    , m_ownsModel(true) {
    
    m_model->setPos(pos);
    m_model->setSize(QSizeF(diameter, diameter));
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->padFill());
    m_pen = QPen(theme->padStroke(), 0.1);
    m_pen.setCosmetic(true);
    setPos(pos);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

PadItem::PadItem(PadModel* model, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->padFill());
    m_pen = QPen(theme->padStroke(), 0.1);
    m_pen.setCosmetic(true);
    setPos(model->pos());
    setRotation(model->rotation());
    setNetName(model->netName());
    setLayer(model->layer());
    setId(model->id());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

PadItem::~PadItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void PadItem::setDiameter(double diameter) {
    setSize(QSizeF(diameter, diameter));
}

void PadItem::setSize(const QSizeF& size) {
    if (m_model->size() != size) {
        prepareGeometryChange();
        m_model->setSize(size);
        update();
    }
}

void PadItem::setPadShape(const QString& shape) {
    if (m_model->shape() != shape) {
        prepareGeometryChange();
        m_model->setShape(shape);
        update();
    }
}

void PadItem::setDrillSize(double size) {
    if (m_model->drillSize() != size) {
        m_model->setDrillSize(size);
        update();
    }
}

void PadItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        update();
    }
}

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

QRectF PadItem::boundingRect() const {
    double border = m_pen.widthF() / 2.0;
    QSizeF size = m_model->size();
    QRectF rect(-size.width()/2 - border, -size.height()/2 - border,
                  size.width() + 2*border, size.height() + 2*border);
    if (m_model->shape().toLower() == "custom" && !m_model->customPrimitives().isEmpty()) {
        QRectF primsRect = getCustomPrimitivesBoundingRect(m_model->customPrimitives());
        if (primsRect.isValid()) {
            rect = rect.united(primsRect);
        }
    }
    return rect;
}

QPainterPath PadItem::shape() const {
    QPainterPath path;
    QSizeF size = m_model->size();
    QString shape = m_model->shape().toLower();
    const QString padNumber = m_model->number().trimmed();
    
    if (shape == "rect" || shape == "rectangle" || shape == "square") {
        path.addRect(-size.width()/2, -size.height()/2, size.width(), size.height());
    } else if (shape == "oblong" || shape == "oval") {
        double r = std::min(size.width(), size.height()) / 2.0;
        path.addRoundedRect(-size.width()/2, -size.height()/2, 
                           size.width(), size.height(), r, r);
    } else if (shape == "roundedrect" || shape == "roundrect") {
        double r = std::min(size.width(), size.height()) * 0.25;
        path.addRoundedRect(-size.width()/2, -size.height()/2, 
                           size.width(), size.height(), r, r);
    } else if (shape == "custom" && !m_model->customPrimitives().isEmpty()) {
        for (const auto& val : m_model->customPrimitives()) {
            QJsonObject primObj = val.toObject();
            QString type = primObj.value("type").toString().toLower();
            QJsonObject data = primObj.value("data").toObject();
            if (type == "line") {
                qreal x1 = data.value("x1").toDouble();
                qreal y1 = data.value("y1").toDouble();
                qreal x2 = data.value("x2").toDouble();
                qreal y2 = data.value("y2").toDouble();
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
                qreal x = data.value("x").toDouble();
                qreal y = data.value("y").toDouble();
                qreal w = data.value("width").toDouble();
                qreal h = data.value("height").toDouble();
                path.addRect(x, y, w, h);
            } else if (type == "circle") {
                qreal cx = data.value("cx").toDouble();
                qreal cy = data.value("cy").toDouble();
                qreal r = data.value("radius").toDouble();
                path.addEllipse(QPointF(cx, cy), r, r);
            } else if (type == "arc") {
                qreal cx = data.value("cx").toDouble();
                qreal cy = data.value("cy").toDouble();
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
                    poly << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
                }
                path.addPolygon(poly);
            }
        }
    } else {
        path.addEllipse(-size.width()/2, -size.height()/2, size.width(), size.height());
    }
    return path;
}

void PadItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    PCBTheme* theme = ThemeManager::theme();
    PCBLayerManager& layerMgr = PCBLayerManager::instance();
    int activeLayerId = layerMgr.activeLayerId();
    PCBLayer* activeLayer = layerMgr.layer(activeLayerId);
    
    QColor baseColor;
    bool isTH = (m_model->drillSize() > 0.001);
    bool onActiveLayer = false;

    if (isTH) {
        baseColor = theme->multiLayer();
        // Through-hole pads exist on all copper layers
        if (activeLayer && activeLayer->isCopperLayer()) {
            onActiveLayer = true;
        }
    } else {
        PCBLayer* l = layerMgr.layer(layer());
        baseColor = l ? l->color() : theme->padFill();
        if (layer() == activeLayerId) {
            onActiveLayer = true;
        }
    }
    
    // Draw the main pad body
    painter->setPen(QPen(baseColor.darker(150), 0));
    painter->setBrush(QBrush(baseColor));

    QSizeF size = m_model->size();
    QString shape = m_model->shape().toLower();
    const QString padNumber = m_model->number().trimmed();
    QRectF rect(-size.width()/2, -size.height()/2, size.width(), size.height());
    
    if (shape == "rect" || shape == "rectangle" || shape == "square") {
        painter->drawRect(rect);
    } else if (shape == "oblong" || shape == "oval") {
        double r = std::min(size.width(), size.height()) / 2.0;
        painter->drawRoundedRect(rect, r, r);
    } else if (shape == "roundedrect" || shape == "roundrect") {
        double r = std::min(size.width(), size.height()) * 0.25;
        painter->drawRoundedRect(rect, r, r);
    } else if (shape == "custom" && !m_model->customPrimitives().isEmpty()) {
        drawCustomPadPrimitives(painter, m_model->customPrimitives(), baseColor);
    } else {
        painter->drawEllipse(rect);
    }

    // KiCad-style Active Layer Highlight (Outline)
    if (onActiveLayer && activeLayer) {
        QColor highlightColor = activeLayer->color();
        painter->setBrush(Qt::NoBrush);
        // Bright, cosmetic outline to show connectability
        QPen highlightPen(highlightColor, 1.0); 
        highlightPen.setCosmetic(true);
        painter->setPen(highlightPen);
        
        // Draw exactly on the pad boundary to prevent overlap
        QRectF highlightRect = rect;
        
        if (shape == "rect" || shape == "rectangle" || shape == "square") {
            painter->drawRect(highlightRect);
        } else if (shape == "oblong" || shape == "oval") {
            double r = std::min(highlightRect.width(), highlightRect.height()) / 2.0;
            painter->drawRoundedRect(highlightRect, r, r);
        } else if (shape == "roundedrect" || shape == "roundrect") {
            double r = std::min(highlightRect.width(), highlightRect.height()) * 0.25;
            painter->drawRoundedRect(highlightRect, r, r);
        } else if (shape == "custom" && !m_model->customPrimitives().isEmpty()) {
            drawCustomPadPrimitives(painter, m_model->customPrimitives(), highlightColor);
        } else {
            painter->drawEllipse(highlightRect);
        }
    }

    // Draw the drill hole
    if (isTH) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(QColor(30, 30, 30)));
        painter->drawEllipse(QPointF(0, 0), m_model->drillSize()/2, m_model->drillSize()/2);
    }
    
    painter->setPen(QPen(baseColor.lighter(130), 0.02));
    double crossSize = std::min(size.width(), size.height()) * 0.2;
    painter->drawLine(QLineF(-crossSize, 0, crossSize, 0));
    painter->drawLine(QLineF(0, -crossSize, 0, crossSize));

    if (!padNumber.isEmpty()) {
        double w = size.width();
        double h = size.height();
        double maxDim = std::max(w, h);
        double minDim = std::min(w, h);
        if (maxDim > 0.5) {
            painter->setPen(baseColor.lightness() < 140 ? Qt::white : Qt::black);
            double fontSize = qMax(minDim * 0.5, 0.7); // 50% of min dimension, but at least 0.7 points
            fontSize = qMin(fontSize, maxDim * 0.8);   // Ensure it fits within the pad length
            
            // Safety check: skip text drawing if effective pixel size is too small to avoid Qt/FreeType crash
            QTransform trans = painter->transform();
            qreal viewScale = QLineF(trans.map(QPointF(0,0)), trans.map(QPointF(1,0))).length();
            if (fontSize * viewScale >= 2.0) {
                QFont font("Monospace");
                font.setPointSizeF(fontSize);
                painter->setFont(font);
                painter->drawText(rect, Qt::AlignCenter, padNumber);
            }
        }
    }

    drawSelectionGlow(painter);
}

QJsonObject PadItem::toJson() const {
    m_model->setNetName(netName());
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["name"] = name();
    return json;
}

bool PadItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    m_model->fromJson(json);
    
    setId(m_model->id());
    setName(json["name"].toString());
    setNetName(m_model->netName());
    setLayer(m_model->layer());
    setPos(m_model->pos());
    setRotation(m_model->rotation());

    update();
    return true;
}

PCBItem* PadItem::clone() const {
    m_model->setNetName(netName());
    PadModel* newModel = m_model->clone();
    
    PadItem* newItem = new PadItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setName(name());
    newItem->setNetName(netName());
    newItem->setLayer(layer());
    newItem->setRotation(rotation());
    newItem->m_brush = m_brush;
    newItem->m_pen = m_pen;
    return newItem;
}
