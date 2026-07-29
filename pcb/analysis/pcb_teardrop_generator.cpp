/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_teardrop_generator.h"
#include "../items/teardrop_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../editor/pcb_commands.h"
#include <QLineF>
#include <QPainterPath>
#include <cmath>

namespace {
bool pointsNear(const QPointF& a, const QPointF& b, double eps = 0.5) {
    return QLineF(a, b).length() <= eps;
}

QPolygonF createTeardropPolygon(const QPointF& center, const QPointF& traceOther, double radius, double traceWidth, const PCBTeardropGenerator::Options& opts) {
    QLineF dirLine(center, traceOther);
    if (dirLine.length() < 0.001) return QPolygonF();

    double length = traceWidth * opts.lengthRatio;
    if (length > dirLine.length() * 0.8) length = dirLine.length() * 0.8;
    if (length < 0.05) length = 0.05;

    QPointF apex = dirLine.pointAt(length / dirLine.length());

    double angleRad = std::atan2(dirLine.dy(), dirLine.dx());
    double perpRad = angleRad + M_PI_2;

    double baseWidth = std::max(traceWidth * 1.2, radius * 2.0 * opts.widthRatio);
    QPointF pLeft = center + QPointF(std::cos(perpRad) * baseWidth * 0.5, std::sin(perpRad) * baseWidth * 0.5);
    QPointF pRight = center - QPointF(std::cos(perpRad) * baseWidth * 0.5, std::sin(perpRad) * baseWidth * 0.5);

    QPainterPath path;
    path.moveTo(pLeft);

    if (opts.shape == PCBTeardropGenerator::TeardropShape::Curved) {
        QPointF ctrl1 = pLeft + QPointF(std::cos(angleRad) * length * 0.5, std::sin(angleRad) * length * 0.5);
        QPointF ctrl2 = pRight + QPointF(std::cos(angleRad) * length * 0.5, std::sin(angleRad) * length * 0.5);
        path.quadTo(ctrl1, apex);
        path.quadTo(ctrl2, pRight);
    } else {
        path.lineTo(apex);
        path.lineTo(pRight);
    }
    path.closeSubpath();

    return path.toFillPolygon();
}
} // namespace

PCBTeardropGenerator::Report PCBTeardropGenerator::addTeardrops(QGraphicsScene* scene, const Options& opts, QUndoStack* undoStack) {
    Report report;
    if (!scene) return report;

    // Remove existing teardrops first to avoid overlaps
    removeTeardrops(scene, undoStack);

    QList<PCBItem*> teardropsToAdd;

    QList<TraceItem*> traces;
    QList<PadItem*> pads;
    QList<ViaItem*> vias;

    for (auto* item : scene->items()) {
        if (auto* t = dynamic_cast<TraceItem*>(item)) traces.append(t);
        else if (auto* p = dynamic_cast<PadItem*>(item)) pads.append(p);
        else if (auto* v = dynamic_cast<ViaItem*>(item)) vias.append(v);
    }

    for (TraceItem* trace : traces) {
        if (trace->width() < opts.minTraceWidth) continue;

        QPointF startPt = trace->mapToScene(trace->startPoint());
        QPointF endPt = trace->mapToScene(trace->endPoint());

        // Check start endpoint
        if (opts.includePads) {
            for (PadItem* pad : pads) {
                if (pad->layer() == trace->layer() || pad->drillSize() > 0.001) {
                    if (pointsNear(startPt, pad->scenePos(), 1.0)) {
                        double rad = std::max(pad->size().width(), pad->size().height()) * 0.5;
                        QPolygonF poly = createTeardropPolygon(pad->scenePos(), endPt, rad, trace->width(), opts);
                        if (!poly.isEmpty()) {
                            teardropsToAdd.append(new TeardropItem(poly, trace->layer(), trace->netName()));
                            report.padTeardropsAdded++;
                        }
                    }
                    if (pointsNear(endPt, pad->scenePos(), 1.0)) {
                        double rad = std::max(pad->size().width(), pad->size().height()) * 0.5;
                        QPolygonF poly = createTeardropPolygon(pad->scenePos(), startPt, rad, trace->width(), opts);
                        if (!poly.isEmpty()) {
                            teardropsToAdd.append(new TeardropItem(poly, trace->layer(), trace->netName()));
                            report.padTeardropsAdded++;
                        }
                    }
                }
            }
        }

        if (opts.includeVias) {
            for (ViaItem* via : vias) {
                if (pointsNear(startPt, via->scenePos(), 0.5)) {
                    QPolygonF poly = createTeardropPolygon(via->scenePos(), endPt, via->diameter() * 0.5, trace->width(), opts);
                    if (!poly.isEmpty()) {
                        teardropsToAdd.append(new TeardropItem(poly, trace->layer(), trace->netName()));
                        report.viaTeardropsAdded++;
                    }
                }
                if (pointsNear(endPt, via->scenePos(), 0.5)) {
                    QPolygonF poly = createTeardropPolygon(via->scenePos(), startPt, via->diameter() * 0.5, trace->width(), opts);
                    if (!poly.isEmpty()) {
                        teardropsToAdd.append(new TeardropItem(poly, trace->layer(), trace->netName()));
                        report.viaTeardropsAdded++;
                    }
                }
            }
        }
    }

    if (!teardropsToAdd.isEmpty()) {
        if (undoStack) {
            undoStack->push(new PCBAddItemsCommand(scene, teardropsToAdd));
        } else {
            for (auto* item : teardropsToAdd) {
                scene->addItem(item);
            }
        }
    }

    return report;
}

int PCBTeardropGenerator::removeTeardrops(QGraphicsScene* scene, QUndoStack* undoStack) {
    if (!scene) return 0;

    QList<PCBItem*> teardropsToRemove;
    for (auto* item : scene->items()) {
        if (dynamic_cast<TeardropItem*>(item)) {
            teardropsToRemove.append(static_cast<PCBItem*>(item));
        }
    }

    int count = teardropsToRemove.size();
    if (count > 0) {
        if (undoStack) {
            undoStack->push(new PCBRemoveItemCommand(scene, teardropsToRemove));
        } else {
            for (auto* item : teardropsToRemove) {
                scene->removeItem(item);
                delete item;
            }
        }
    }
    return count;
}
