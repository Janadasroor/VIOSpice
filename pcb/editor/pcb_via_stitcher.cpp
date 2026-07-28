/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_via_stitcher.h"
#include "copper_pour_item.h"
#include "via_item.h"
#include "trace_item.h"
#include "pcb_commands.h"
#include "config_manager.h"
#include "../drc/pcb_drc.h"
#include "../dialogs/via_stitching_dialog.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include <QGraphicsScene>
#include <QUndoStack>
#include <QStatusBar>
#include <QWidget>
#include <QPolygonF>
#include <QRectF>
#include <QList>
#include <QLineF>

void PCBViaStitcher::performViaStitching(QGraphicsScene* scene, QUndoStack* undoStack, QStatusBar* statusBar, QWidget* parent) {
    if (!scene) return;
    if (!undoStack) return;

    // 1. Find selected copper pour
    CopperPourItem* selectedPour = nullptr;
    for (auto* gItem : scene->selectedItems()) {
        if (auto* pour = dynamic_cast<CopperPourItem*>(gItem)) {
            selectedPour = pour;
            break;
        }
    }

    if (!selectedPour) {
        if (statusBar) {
            statusBar->showMessage("Error: Please select a copper pour first.", 3000);
        }
        return;
    }

    // 2. Open settings dialog
    ViaStitchingDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return;

    // 3. Generate Grid
    QRectF bounds = selectedPour->polygon().boundingRect();
    QPolygonF poly = selectedPour->polygon();
    double stepX = dlg.gridSpacingX();
    double stepY = dlg.gridSpacingY();
    const QString netName = dlg.netName().trimmed().isEmpty() ? selectedPour->netName() : dlg.netName().trimmed();
    const int startLayer = dlg.startLayer();
    const int endLayer = dlg.endLayer();
    const bool microvia = dlg.microviaMode();
    
    QList<PCBItem*> newVias;
    PCBDRC drc;
    double minClearance = drc.rules().minClearance();

    undoStack->beginMacro("Via Stitching");

    for (double x = bounds.left() + stepX/2; x < bounds.right(); x += stepX) {
        for (double y = bounds.top() + stepY/2; y < bounds.bottom(); y += stepY) {
            QPointF pos(x, y);
            
            // Check if inside polygon
            if (!poly.containsPoint(pos, Qt::OddEvenFill)) continue;

            // Check DRC clearance
            ViaItem* tempVia = new ViaItem(pos, dlg.viaDiameter());
            tempVia->setDrillSize(dlg.viaDrill());
            tempVia->setNetName(netName);
            tempVia->setStartLayer(startLayer);
            tempVia->setEndLayer(endLayer);
            tempVia->setLayer(startLayer);
            tempVia->setMicrovia(microvia);

            // Skip duplicates on same location/net.
            bool duplicate = false;
            for (QGraphicsItem* existing : scene->items(QRectF(pos.x() - 0.05, pos.y() - 0.05, 0.1, 0.1))) {
                ViaItem* existingVia = dynamic_cast<ViaItem*>(existing);
                if (!existingVia) continue;
                if (QLineF(existingVia->scenePos(), pos).length() <= 0.05 &&
                    existingVia->netName() == netName &&
                    existingVia->startLayer() == startLayer &&
                    existingVia->endLayer() == endLayer &&
                    existingVia->isMicrovia() == microvia) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                delete tempVia;
                continue;
            }
            
            // Fast bounding box check for obstacles
            QRectF searchRect = tempVia->sceneBoundingRect().adjusted(-minClearance, -minClearance, minClearance, minClearance);
            bool collision = false;
            for (auto* obstacle : scene->items(searchRect)) {
                PCBItem* pItem = dynamic_cast<PCBItem*>(obstacle);
                if (!pItem || pItem == selectedPour) continue;
                
                QPointF dummy;
                if (drc.checkItemClearance(tempVia, pItem, minClearance, dummy)) {
                    collision = true;
                    break;
                }
            }

            if (!collision) {
                scene->addItem(tempVia);
                tempVia->setNetName(netName);
                newVias.append(tempVia);
            } else {
                delete tempVia;
            }
        }
    }

    if (!newVias.isEmpty()) {
        undoStack->push(new PCBAddItemsCommand(scene, newVias));
        if (statusBar) {
            statusBar->showMessage(QString("Successfully generated %1 stitching vias.").arg(newVias.size()), 5000);
        }
    } else {
        if (statusBar) {
            statusBar->showMessage("No vias could be placed without DRC violations.", 3000);
        }
    }

    undoStack->endMacro();
    PCBRatsnestManager::instance().update();
}

void PCBViaStitcher::performViaFencing(QGraphicsScene* scene, QUndoStack* undoStack, QStatusBar* statusBar, QWidget* parent) {
    if (!scene || !undoStack) return;

    // 1. Find selected trace items
    QList<TraceItem*> selectedTraces;
    for (auto* gItem : scene->selectedItems()) {
        if (auto* trace = dynamic_cast<TraceItem*>(gItem)) {
            selectedTraces.append(trace);
        }
    }

    if (selectedTraces.isEmpty()) {
        if (statusBar) {
            statusBar->showMessage("Error: Please select one or more traces first for via fencing.", 3000);
        }
        return;
    }

    // 2. Open via stitching dialog
    ViaStitchingDialog dlg(parent);
    dlg.setWindowTitle("RF Via Fencing Settings");
    if (dlg.exec() != QDialog::Accepted) return;

    double pitch = dlg.gridSpacingX();
    double offset = dlg.gridSpacingY();
    const QString netName = dlg.netName().trimmed().isEmpty() ? "GND" : dlg.netName().trimmed();
    const int startLayer = dlg.startLayer();
    const int endLayer = dlg.endLayer();
    const bool microvia = dlg.microviaMode();

    QList<PCBItem*> newVias;
    PCBDRC drc;
    double minClearance = drc.rules().minClearance();

    undoStack->beginMacro("RF Via Fencing");

    for (TraceItem* trace : selectedTraces) {
        QPointF p0 = trace->startPoint();
        QPointF p1 = trace->endPoint();
        QLineF line(p0, p1);
        double len = line.length();
        if (len < 0.1) continue;

        QPointF dir(line.dx() / len, line.dy() / len);
        QPointF norm(-dir.y(), dir.x());

        int numSteps = std::max(1, static_cast<int>(std::round(len / pitch)));
        double stepDist = len / numSteps;

        for (int i = 0; i <= numSteps; ++i) {
            QPointF centerPt = p0 + dir * (i * stepDist);

            // Both left and right fencing points
            QList<QPointF> fencePts;
            fencePts << (centerPt + norm * offset);
            fencePts << (centerPt - norm * offset);

            for (const QPointF& pos : fencePts) {
                ViaItem* tempVia = new ViaItem(pos, dlg.viaDiameter());
                tempVia->setDrillSize(dlg.viaDrill());
                tempVia->setNetName(netName);
                tempVia->setStartLayer(startLayer);
                tempVia->setEndLayer(endLayer);
                tempVia->setLayer(startLayer);
                tempVia->setMicrovia(microvia);

                // Skip duplicates
                bool duplicate = false;
                for (QGraphicsItem* existing : scene->items(QRectF(pos.x() - 0.05, pos.y() - 0.05, 0.1, 0.1))) {
                    ViaItem* existingVia = dynamic_cast<ViaItem*>(existing);
                    if (!existingVia) continue;
                    if (QLineF(existingVia->scenePos(), pos).length() <= 0.05) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    delete tempVia;
                    continue;
                }

                // Check DRC clearance
                QRectF searchRect = tempVia->sceneBoundingRect().adjusted(-minClearance, -minClearance, minClearance, minClearance);
                bool collision = false;
                for (auto* obstacle : scene->items(searchRect)) {
                    PCBItem* pItem = dynamic_cast<PCBItem*>(obstacle);
                    if (!pItem || pItem == trace) continue;
                    
                    QPointF dummy;
                    if (drc.checkItemClearance(tempVia, pItem, minClearance, dummy)) {
                        collision = true;
                        break;
                    }
                }

                if (!collision) {
                    scene->addItem(tempVia);
                    tempVia->setNetName(netName);
                    newVias.append(tempVia);
                } else {
                    delete tempVia;
                }
            }
        }
    }

    if (!newVias.isEmpty()) {
        undoStack->push(new PCBAddItemsCommand(scene, newVias));
        if (statusBar) {
            statusBar->showMessage(QString("Successfully generated %1 RF fencing vias.").arg(newVias.size()), 5000);
        }
    } else {
        if (statusBar) {
            statusBar->showMessage("No fencing vias could be placed without DRC violations.", 3000);
        }
    }

    undoStack->endMacro();
    PCBRatsnestManager::instance().update();
}
