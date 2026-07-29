/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_array_generator.h"
#include "../items/pcb_item.h"
#include "../editor/pcb_commands.h"
#include <cmath>

PCBArrayGenerator::Report PCBArrayGenerator::createArray(QGraphicsScene* scene, const QList<PCBItem*>& sourceItems, const Options& opts, QUndoStack* undoStack) {
    Report report;
    if (!scene || sourceItems.isEmpty()) return report;

    QList<PCBItem*> createdItems;

    if (opts.mode == ArrayMode::Rectangular) {
        for (int r = 0; r < opts.rows; ++r) {
            for (int c = 0; c < opts.cols; ++c) {
                if (r == 0 && c == 0) continue; // Keep original item at (0,0)
                double offsetX = c * opts.deltaX;
                double offsetY = r * opts.deltaY;

                for (PCBItem* src : sourceItems) {
                    PCBItem* clone = src->clone();
                    clone->setPos(src->pos() + QPointF(offsetX, offsetY));
                    createdItems.append(clone);
                    report.itemsCreated++;
                }
            }
        }
    } else if (opts.mode == ArrayMode::Circular) {
        QPointF center = sourceItems.first()->pos();
        double stepAngleRad = (opts.count > 1) 
            ? (opts.spanAngleDeg * M_PI / 180.0) / (opts.spanAngleDeg == 360.0 ? opts.count : (opts.count - 1)) 
            : 0.0;

        double startRad = opts.startAngleDeg * M_PI / 180.0;

        for (int i = 1; i < opts.count; ++i) {
            double angle = startRad + (i * stepAngleRad);
            double posX = center.x() + (std::cos(angle) * opts.radius);
            double posY = center.y() + (std::sin(angle) * opts.radius);

            for (PCBItem* src : sourceItems) {
                PCBItem* clone = src->clone();
                clone->setPos(QPointF(posX, posY));
                if (opts.rotateItems) {
                    clone->setRotation(src->rotation() + (angle * 180.0 / M_PI));
                }
                createdItems.append(clone);
                report.itemsCreated++;
            }
        }
    }

    if (!createdItems.isEmpty()) {
        if (undoStack) {
            undoStack->push(new PCBAddItemsCommand(scene, createdItems));
        } else {
            for (auto* item : createdItems) {
                scene->addItem(item);
            }
        }
    }

    return report;
}
