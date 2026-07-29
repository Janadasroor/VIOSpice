/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_tracks_cleaner.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../editor/pcb_commands.h"
#include <QLineF>
#include <QSet>
#include <QMap>
#include <cmath>

namespace {
bool pointsNear(const QPointF& a, const QPointF& b, double eps = 1e-4) {
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

QString pointKey(const QPointF& p) {
    return QString("%1,%2").arg(qRound(p.x() * 1000.0)).arg(qRound(p.y() * 1000.0));
}

bool shareEndpoint(TraceItem* a, TraceItem* b, QPointF* outShared, QPointF* outAOther, QPointF* outBOther) {
    const QPointF aStart = a->mapToScene(a->startPoint());
    const QPointF aEnd = a->mapToScene(a->endPoint());
    const QPointF bStart = b->mapToScene(b->startPoint());
    const QPointF bEnd = b->mapToScene(b->endPoint());

    if (pointsNear(aStart, bStart)) {
        if (outShared) *outShared = aStart;
        if (outAOther) *outAOther = aEnd;
        if (outBOther) *outBOther = bEnd;
        return true;
    }
    if (pointsNear(aStart, bEnd)) {
        if (outShared) *outShared = aStart;
        if (outAOther) *outAOther = aEnd;
        if (outBOther) *outBOther = bStart;
        return true;
    }
    if (pointsNear(aEnd, bStart)) {
        if (outShared) *outShared = aEnd;
        if (outAOther) *outAOther = aStart;
        if (outBOther) *outBOther = bEnd;
        return true;
    }
    if (pointsNear(aEnd, bEnd)) {
        if (outShared) *outShared = aEnd;
        if (outAOther) *outAOther = aStart;
        if (outBOther) *outBOther = bStart;
        return true;
    }
    return false;
}

bool collinearAroundShared(const QPointF& shared, const QPointF& aOther, const QPointF& bOther) {
    QLineF l1(shared, aOther);
    QLineF l2(shared, bOther);
    if (l1.length() < 1e-4 || l2.length() < 1e-4) return false;
    double diff = std::abs(l1.angleTo(l2) - 180.0);
    return diff < 0.1 || std::abs(diff - 360.0) < 0.1;
}
} // namespace

PCBTracksCleaner::Report PCBTracksCleaner::cleanBoard(QGraphicsScene* scene, const Options& opts, QUndoStack* undoStack) {
    Report report;
    if (!scene) return report;

    QList<PCBItem*> itemsToDelete;

    // 1. Delete Zero-Length Traces
    if (opts.deleteZeroLengthTraces) {
        for (auto* item : scene->items()) {
            if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                QPointF s = trace->mapToScene(trace->startPoint());
                QPointF e = trace->mapToScene(trace->endPoint());
                if (QLineF(s, e).length() < 1e-4) {
                    itemsToDelete.append(trace);
                    report.zeroLengthTracesRemoved++;
                }
            }
        }
    }

    // 2. Delete Duplicate Vias
    if (opts.deleteDuplicateVias) {
        QList<ViaItem*> vias;
        for (auto* item : scene->items()) {
            if (auto* via = dynamic_cast<ViaItem*>(item)) vias.append(via);
        }

        QSet<ViaItem*> toRemove;
        for (int i = 0; i < vias.size(); ++i) {
            if (toRemove.contains(vias[i])) continue;
            for (int j = i + 1; j < vias.size(); ++j) {
                if (toRemove.contains(vias[j])) continue;
                if (pointsNear(vias[i]->scenePos(), vias[j]->scenePos(), 1e-3)) {
                    toRemove.insert(vias[j]);
                    report.duplicateViasRemoved++;
                }
            }
        }
        for (auto* via : toRemove) itemsToDelete.append(via);
    }

    // 3. Delete Dangling Traces (Iterative sweep)
    if (opts.deleteDanglingTraces) {
        bool progress = true;
        while (progress) {
            progress = false;
            QList<TraceItem*> allTraces;
            for (auto* item : scene->items()) {
                if (auto* t = dynamic_cast<TraceItem*>(item)) {
                    if (!itemsToDelete.contains(t)) allTraces.append(t);
                }
            }

            for (TraceItem* t : allTraces) {
                if (t->netName().isEmpty() || t->netName() == "No Net") continue;

                QPointF startPt = t->mapToScene(t->startPoint());
                QPointF endPt = t->mapToScene(t->endPoint());

                int startConnections = 0;
                int endConnections = 0;

                for (auto* otherItem : scene->items()) {
                    if (otherItem == t) continue;
                    PCBItem* pcb = dynamic_cast<PCBItem*>(otherItem);
                    if (!pcb || itemsToDelete.contains(pcb)) continue;

                    if (PadItem* pad = dynamic_cast<PadItem*>(pcb)) {
                        if (pointsNear(startPt, pad->scenePos(), 1.0)) startConnections++;
                        if (pointsNear(endPt, pad->scenePos(), 1.0)) endConnections++;
                    } else if (ViaItem* via = dynamic_cast<ViaItem*>(pcb)) {
                        if (pointsNear(startPt, via->scenePos(), 0.5)) startConnections++;
                        if (pointsNear(endPt, via->scenePos(), 0.5)) endConnections++;
                    } else if (TraceItem* ot = dynamic_cast<TraceItem*>(pcb)) {
                        if (ot->netName() == t->netName()) {
                            QPointF os = ot->mapToScene(ot->startPoint());
                            QPointF oe = ot->mapToScene(ot->endPoint());
                            if (pointsNear(startPt, os) || pointsNear(startPt, oe)) startConnections++;
                            if (pointsNear(endPt, os) || pointsNear(endPt, oe)) endConnections++;
                        }
                    }
                }

                if (startConnections == 0 || endConnections == 0) {
                    itemsToDelete.append(t);
                    report.danglingTracesRemoved++;
                    progress = true;
                    break;
                }
            }
        }
    }

    // 4. Delete Dangling Vias
    if (opts.deleteDanglingVias) {
        for (auto* item : scene->items()) {
            if (auto* via = dynamic_cast<ViaItem*>(item)) {
                if (itemsToDelete.contains(via)) continue;
                int connections = 0;
                for (auto* other : scene->items()) {
                    if (other == via) continue;
                    if (auto* trace = dynamic_cast<TraceItem*>(other)) {
                        if (itemsToDelete.contains(trace)) continue;
                        QPointF s = trace->mapToScene(trace->startPoint());
                        QPointF e = trace->mapToScene(trace->endPoint());
                        if (pointsNear(via->scenePos(), s, 0.5) || pointsNear(via->scenePos(), e, 0.5)) {
                            connections++;
                        }
                    }
                }
                if (connections <= 1) {
                    itemsToDelete.append(via);
                    report.danglingViasRemoved++;
                }
            }
        }
    }

    // Perform deletions via Undo command or direct scene removal
    if (!itemsToDelete.isEmpty()) {
        if (undoStack) {
            undoStack->push(new PCBRemoveItemCommand(scene, itemsToDelete));
        } else {
            for (auto* item : itemsToDelete) {
                scene->removeItem(item);
                delete item;
            }
        }
    }

    // 5. Merge Collinear Trace Segments
    if (opts.mergeCollinearTraces) {
        bool mergedAny = true;
        while (mergedAny) {
            mergedAny = false;
            QList<TraceItem*> traces;
            for (auto* item : scene->items()) {
                if (auto* t = dynamic_cast<TraceItem*>(item)) traces.append(t);
            }

            QMap<QString, int> endpointCounts;
            for (TraceItem* t : traces) {
                endpointCounts[pointKey(t->mapToScene(t->startPoint()))]++;
                endpointCounts[pointKey(t->mapToScene(t->endPoint()))]++;
            }

            for (int i = 0; i < traces.size() && !mergedAny; ++i) {
                TraceItem* a = traces[i];
                for (int j = i + 1; j < traces.size() && !mergedAny; ++j) {
                    TraceItem* b = traces[j];
                    if (a->layer() != b->layer()) continue;
                    if (a->netName() != b->netName() || a->netName().isEmpty()) continue;
                    if (std::abs(a->width() - b->width()) > 1e-4) continue;

                    QPointF shared, aOther, bOther;
                    if (!shareEndpoint(a, b, &shared, &aOther, &bOther)) continue;
                    if (endpointCounts.value(pointKey(shared), 0) != 2) continue;
                    if (!collinearAroundShared(shared, aOther, bOther)) continue;

                    TraceItem* merged = new TraceItem(aOther, bOther, a->width());
                    merged->setLayer(a->layer());
                    merged->setNetName(a->netName());
                    scene->addItem(merged);

                    scene->removeItem(a);
                    scene->removeItem(b);
                    delete a;
                    delete b;

                    report.collinearTracesMerged++;
                    mergedAny = true;
                }
            }
        }
    }

    return report;
}
