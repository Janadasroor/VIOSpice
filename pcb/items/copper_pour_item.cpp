/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "copper_pour_item.h"
#include "trace_item.h"
#include "pad_item.h"
#include "via_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QDebug>

using namespace Flux::Model;

CopperPourItem::CopperPourItem(QGraphicsItem* parent)
    : PCBItem(parent)
    , m_model(new CopperPourModel())
    , m_ownsModel(true) {
    setZValue(-10 - m_layer);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

CopperPourItem::CopperPourItem(CopperPourModel* model, QGraphicsItem* parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    setZValue(-10 - m_layer);
    setPos(0, 0); // Polygons are usually in scene coordinates
    setLayer(model->layer());
    setId(model->id());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    updatePath();
}

CopperPourItem::~CopperPourItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void CopperPourItem::setPolygon(const QPolygonF& polygon) {
    prepareGeometryChange();
    m_model->setPolygon(polygon);
    updatePath();
}

void CopperPourItem::addPoint(const QPointF& point) {
    prepareGeometryChange();
    QPolygonF poly = m_model->polygon();
    poly << point;
    m_model->setPolygon(poly);
    updatePath();
}

void CopperPourItem::closePolygon() {
    QPolygonF poly = m_model->polygon();
    if (poly.size() > 2 && poly.first() != poly.last()) {
        prepareGeometryChange();
        poly << poly.first();
        m_model->setPolygon(poly);
        updatePath();
    }
}

void CopperPourItem::rebuild() {
    updatePath();
    update();
}

void CopperPourItem::recalculatePour() {
    rebuild();
}

void CopperPourItem::updatePath() {
    m_path = QPainterPath();
    if (m_model->polygon().isEmpty()) return;
    
    QPainterPath rawPath;
    rawPath.addPolygon(m_model->polygon());

    double clearanceVal = m_model->clearance();
    if (clearanceVal < 0.05) clearanceVal = 0.3;

    if (scene()) {
        QPainterPath keepoutPath;
        
        for (auto* item : scene()->items()) {
            if (!item || item == this) continue;

            if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                if (trace->layer() == layer() && trace->netName() != netName()) {
                    QPainterPathStroker stroker;
                    stroker.setWidth(trace->width() + 2.0 * clearanceVal);
                    stroker.setCapStyle(Qt::RoundCap);
                    stroker.setJoinStyle(Qt::RoundJoin);

                    QPainterPath linePath;
                    linePath.moveTo(trace->startPoint());
                    linePath.lineTo(trace->endPoint());

                    keepoutPath.addPath(stroker.createStroke(linePath));
                }
            } else if (auto* pad = dynamic_cast<PadItem*>(item)) {
                bool padOnLayer = (pad->layer() == layer() || pad->drillSize() > 0.001);
                if (padOnLayer) {
                    QPointF padPos = pad->scenePos();
                    QSizeF sz = pad->size();
                    double rW = (sz.width() > 0.01 ? sz.width() : 1.5) / 2.0 + clearanceVal;
                    double rH = (sz.height() > 0.01 ? sz.height() : 1.5) / 2.0 + clearanceVal;
                    QRectF bounds(padPos.x() - rW, padPos.y() - rH, rW * 2.0, rH * 2.0);
                    
                    QPainterPath padKeepout;
                    if (pad->padShape() == "Round" || pad->padShape() == "Circle" || pad->drillSize() > 0.001) {
                        padKeepout.addEllipse(bounds);
                    } else {
                        padKeepout.addRect(bounds);
                    }

                    if (pad->netName() == netName() && !netName().isEmpty() && m_model->useThermalReliefs()) {
                        // KiCad 4-Spoke Thermal Relief Generation
                        double spokeW = m_model->thermalSpokeWidth() > 0.01 ? m_model->thermalSpokeWidth() : 0.5;
                        double baseAngleRad = m_model->thermalSpokeAngleDeg() * M_PI / 180.0;
                        int spokeCount = m_model->thermalSpokeCount() > 0 ? m_model->thermalSpokeCount() : 4;
                        double angleStep = 2.0 * M_PI / spokeCount;

                        QPainterPath spokesPath;
                        for (int k = 0; k < spokeCount; ++k) {
                            double ang = baseAngleRad + k * angleStep;
                            QPointF dir(std::cos(ang), std::sin(ang));
                            QPointF norm(-dir.y(), dir.x());

                            double reach = std::max(rW, rH) * 2.5;
                            QPolygonF spokePoly;
                            spokePoly << (padPos + norm * (spokeW * 0.5));
                            spokePoly << (padPos + norm * (spokeW * 0.5) + dir * reach);
                            spokePoly << (padPos - norm * (spokeW * 0.5) + dir * reach);
                            spokePoly << (padPos - norm * (spokeW * 0.5));

                            QPainterPath spokePath;
                            spokePath.addPolygon(spokePoly);
                            spokesPath.addPath(spokePath);
                        }

                        // Thermal relief moat = pad keepout moat minus the 4 spoke bridges!
                        QPainterPath thermalMoat = padKeepout.subtracted(spokesPath);
                        keepoutPath.addPath(thermalMoat);
                    } else if (pad->netName() != netName()) {
                        keepoutPath.addPath(padKeepout);
                    }
                }
            } else if (auto* via = dynamic_cast<ViaItem*>(item)) {
                if (via->netName() != netName()) {
                    QPointF viaPos = via->scenePos();
                    double r = (via->diameter() > 0.01 ? via->diameter() : 0.8) / 2.0 + clearanceVal;
                    QRectF bounds(viaPos.x() - r, viaPos.y() - r, r * 2.0, r * 2.0);
                    QPainterPath viaKeepout;
                    viaKeepout.addEllipse(bounds);
                    keepoutPath.addPath(viaKeepout);
                }
            }
        }

        m_path = rawPath.subtracted(keepoutPath);
    } else {
        m_path = rawPath;
    }
    
    if (m_model->pourType() == CopperPourModel::HatchPour) {
        generateHatchPattern();
    }
}

void CopperPourItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        setZValue(-10 - m_layer);
        update();
    }
}

void CopperPourItem::setPriority(int priority) {
    if (m_model->priority() != priority) {
        m_model->setPriority(priority);
        update();
    }
}

void CopperPourItem::setRemoveIslands(bool remove) {
    if (m_model->removeIslands() != remove) {
        m_model->setRemoveIslands(remove);
        update();
    }
}

void CopperPourItem::setThermalSpokeWidth(double width) {
    if (m_model->thermalSpokeWidth() != width) {
        m_model->setThermalSpokeWidth(width);
        rebuild();
    }
}

void CopperPourItem::setThermalSpokeCount(int count) {
    if (m_model->thermalSpokeCount() != count) {
        m_model->setThermalSpokeCount(count);
        rebuild();
    }
}

void CopperPourItem::setThermalSpokeAngleDeg(double deg) {
    if (m_model->thermalSpokeAngleDeg() != deg) {
        m_model->setThermalSpokeAngleDeg(deg);
        rebuild();
    }
}

QRectF CopperPourItem::boundingRect() const {
    return shape().boundingRect().adjusted(-0.5, -0.5, 0.5, 0.5);
}

QPainterPath CopperPourItem::shape() const {
    if (!m_model->filled()) {
        QPainterPathStroker stroker;
        stroker.setWidth(0.6);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(m_path);
    }
    return m_path;
}

void CopperPourItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget *widget) {
    Q_UNUSED(widget);
    
    PCBTheme* theme = ThemeManager::theme();
    PCBLayer* l = PCBLayerManager::instance().layer(layer());
    QColor baseColor = l ? l->color() : theme->trace();
    
    // Fill with semi-transparent copper color
    QColor fillColor = baseColor;
    fillColor.setAlpha(120);
    
    painter->setPen(QPen(baseColor, 0.1, Qt::SolidLine));

    if (m_model->pourType() == CopperPourModel::SolidPour) {
        painter->setBrush(m_model->filled() ? QBrush(fillColor) : Qt::NoBrush);
        painter->drawPath(m_path);
    } else {
        painter->setBrush(m_model->filled() ? QBrush(fillColor) : Qt::NoBrush);
        painter->drawPath(m_path);
        painter->setPen(QPen(baseColor, m_model->hatchWidth(), Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_hatchPath);
    }

    if (option->state & QStyle::State_Selected) {
        drawSelectionGlow(painter);
    }
}



void CopperPourItem::generateHatchPattern() {
    m_hatchPath = QPainterPath();
    QRectF rect = m_path.boundingRect();
    double step = m_model->hatchWidth() * 3.0;
    if (step <= 0) return;
    
    // Simple 45-degree hatching
    for (double x = rect.left() - rect.height(); x < rect.right(); x += step) {
        QLineF line(x, rect.top(), x + rect.height(), rect.bottom());
        m_hatchPath.moveTo(line.p1());
        m_hatchPath.lineTo(line.p2());
    }
    for (double x = rect.left(); x < rect.right() + rect.height(); x += step) {
        QLineF line(x, rect.top(), x - rect.height(), rect.bottom());
        m_hatchPath.moveTo(line.p1());
        m_hatchPath.lineTo(line.p2());
    }
    
    m_hatchPath = m_hatchPath.intersected(m_path);
}

QJsonObject CopperPourItem::toJson() const {
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["name"] = name();
    return json;
}

bool CopperPourItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;
    
    m_model->fromJson(json);
    setId(m_model->id());
    setName(json["name"].toString());
    setLayer(m_model->layer());
    
    updatePath();
    update();
    return true;
}

PCBItem* CopperPourItem::clone() const {
    CopperPourModel* newModel = m_model->clone();
    QUuid newId = QUuid::createUuid();
    newModel->setId(newId);
    
    CopperPourItem* newItem = new CopperPourItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setId(newId);
    newItem->setName(name());
    newItem->setLayer(layer());
    return newItem;
}
