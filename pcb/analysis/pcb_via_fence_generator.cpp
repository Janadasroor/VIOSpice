/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_via_fence_generator.h"
#include "../items/via_item.h"
#include "../items/trace_item.h"
#include "../editor/pcb_commands.h"
#include <QLineF>
#include <cmath>

PCBViaFenceGenerator::Report PCBViaFenceGenerator::generateViaFence(QGraphicsScene* scene, const QList<TraceItem*>& targetTraces, const Options& opts, QUndoStack* undoStack) {
    Report report;
    if (!scene || targetTraces.isEmpty()) return report;

    QList<PCBItem*> viasToAdd;

    for (TraceItem* trace : targetTraces) {
        QPointF start = trace->mapToScene(trace->startPoint());
        QPointF end = trace->mapToScene(trace->endPoint());

        QLineF line(start, end);
        double len = line.length();
        if (len < 0.1) continue;

        int viaCount = std::max(2, qRound(len / opts.viaPitch));
        double actualStep = len / (viaCount - 1);

        double angleRad = std::atan2(line.dy(), line.dx());
        double perpRad = angleRad + M_PI_2;

        double offset = (trace->width() * 0.5) + opts.offsetDistance;

        for (int i = 0; i < viaCount; ++i) {
            QPointF center = line.pointAt((i * actualStep) / len);

            // Side 1 (+)
            QPointF p1 = center + QPointF(std::cos(perpRad) * offset, std::sin(perpRad) * offset);
            ViaItem* v1 = new ViaItem(p1, opts.viaDiameter);
            v1->setDrillSize(opts.drillDiameter);
            v1->setNetName(opts.netName);
            viasToAdd.append(v1);
            report.viasPlaced++;

            // Side 2 (-)
            if (opts.dualSided) {
                QPointF p2 = center - QPointF(std::cos(perpRad) * offset, std::sin(perpRad) * offset);
                ViaItem* v2 = new ViaItem(p2, opts.viaDiameter);
                v2->setDrillSize(opts.drillDiameter);
                v2->setNetName(opts.netName);
                viasToAdd.append(v2);
                report.viasPlaced++;
            }
        }
    }

    if (!viasToAdd.isEmpty()) {
        if (undoStack) {
            undoStack->push(new PCBAddItemsCommand(scene, viasToAdd));
        } else {
            for (auto* item : viasToAdd) {
                scene->addItem(item);
            }
        }
    }

    return report;
}
