/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_panelizer.h"
#include "../items/pcb_item.h"
#include "../items/ratsnest_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "pcb_layer.h"
#include "pcb_commands.h"

#include <QGraphicsScene>
#include <QStatusBar>
#include <QUndoStack>
#include <QWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QList>
#include <QRectF>
#include <QPointF>
#include <QPair>
#include <algorithm>
#include <cmath>

void PCBPanelizer::panelize(QGraphicsScene* scene, QStatusBar* statusBar, QUndoStack* undoStack, QWidget* parentWidget) {
    if (!scene) return;

    QList<PCBItem*> seeds;
    QRectF boardBounds;

    for (QGraphicsItem* item : scene->items()) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) continue;
        if (dynamic_cast<PCBItem*>(pcbItem->parentItem()) != nullptr) continue;
        if (!pcbItem->isVisible()) continue;
        if (dynamic_cast<RatsnestItem*>(pcbItem) != nullptr) continue;

        seeds.append(pcbItem);
        boardBounds = boardBounds.united(pcbItem->sceneBoundingRect());
    }

    if (seeds.isEmpty()) {
        if (statusBar) statusBar->showMessage("Panelize Board: nothing to panelize.", 3000);
        return;
    }
    if (!boardBounds.isValid() || boardBounds.isEmpty()) {
        if (statusBar) statusBar->showMessage("Panelize Board: invalid board bounds.", 3000);
        return;
    }

    bool ok = false;
    int rows = QInputDialog::getInt(parentWidget, "Panelize Board", "Rows:", 2, 1, 100, 1, &ok);
    if (!ok) return;
    int cols = QInputDialog::getInt(parentWidget, "Panelize Board", "Columns:", 2, 1, 100, 1, &ok);
    if (!ok) return;
    if (rows == 1 && cols == 1) {
        if (statusBar) statusBar->showMessage("Panelize Board: rows/columns are both 1; nothing to create.", 3000);
        return;
    }

    const double defaultSpacing = 5.0;
    double spacingX = QInputDialog::getDouble(
        parentWidget, "Panelize Board", "Spacing X (mm):", defaultSpacing, 0.0, 100000.0, 3, &ok);
    if (!ok) return;
    double spacingY = QInputDialog::getDouble(
        parentWidget, "Panelize Board", "Spacing Y (mm):", defaultSpacing, 0.0, 100000.0, 3, &ok);
    if (!ok) return;

    const double pitchX = std::max(0.001, boardBounds.width() + spacingX);
    const double pitchY = std::max(0.001, boardBounds.height() + spacingY);

    const bool addRails = (QMessageBox::question(
        parentWidget, "Panelize Board",
        "Add panel frame rails (edge-cuts frame)?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);

    double railWidth = 5.0;
    if (addRails) {
        railWidth = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Rail width (mm):", 5.0, 0.5, 100000.0, 3, &ok);
        if (!ok) return;
    }

    const bool addToolingHoles = (QMessageBox::question(
        parentWidget, "Panelize Board",
        "Add 4 tooling holes?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    double toolingDiameter = 3.0;
    double toolingDrill = 2.0;
    if (addToolingHoles) {
        toolingDiameter = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Tooling hole diameter (mm):", 3.0, 0.2, 100000.0, 3, &ok);
        if (!ok) return;
        toolingDrill = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Tooling hole drill (mm):", 2.0, 0.1, toolingDiameter, 3, &ok);
        if (!ok) return;
    }

    const bool addFiducials = (QMessageBox::question(
        parentWidget, "Panelize Board",
        "Add 3 global fiducials?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    double fidDiameter = 1.0;
    if (addFiducials) {
        fidDiameter = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Fiducial copper diameter (mm):", 1.0, 0.1, 100000.0, 3, &ok);
        if (!ok) return;
    }

    const bool addTabsWithMouseBites = (QMessageBox::question(
        parentWidget, "Panelize Board",
        "Add breakaway tabs with mouse-bite holes between boards?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    int tabsPerSeam = 2;
    double tabWidth = 4.0;
    double biteDiameter = 0.5;
    double biteDrill = 0.4;
    double bitePitch = 0.9;
    bool addRoutedTabCuts = true;
    if (addTabsWithMouseBites) {
        tabsPerSeam = QInputDialog::getInt(
            parentWidget, "Panelize Board", "Tabs per seam:", 2, 1, 20, 1, &ok);
        if (!ok) return;
        tabWidth = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Tab width (mm):", 4.0, 0.5, 100000.0, 3, &ok);
        if (!ok) return;
        biteDiameter = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Mouse-bite hole diameter (mm):", 0.5, 0.1, 10.0, 3, &ok);
        if (!ok) return;
        biteDrill = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Mouse-bite drill (mm):", 0.4, 0.05, biteDiameter, 3, &ok);
        if (!ok) return;
        bitePitch = QInputDialog::getDouble(
            parentWidget, "Panelize Board", "Mouse-bite hole pitch (mm):", 0.9, 0.1, 50.0, 3, &ok);
        if (!ok) return;
        addRoutedTabCuts = (QMessageBox::question(
            parentWidget, "Panelize Board",
            "Add routed seam cuts with tab bridges?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    }

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * (rows * cols - 1) + 1024);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) continue;

            const QPointF offset(c * pitchX, r * pitchY);
            for (PCBItem* seed : seeds) {
                PCBItem* clone = seed->clone();
                if (!clone) continue;
                clone->setPos(seed->pos() + offset);
                newItems.append(clone);
            }
        }
    }

    if (newItems.isEmpty()) {
        if (statusBar) statusBar->showMessage("Panelize Board: no copies created.", 3000);
        return;
    }

    QRectF panelBounds = boardBounds.united(
        boardBounds.translated((cols - 1) * pitchX, (rows - 1) * pitchY));
    QRectF frameBounds = addRails
        ? panelBounds.adjusted(-railWidth, -railWidth, railWidth, railWidth)
        : panelBounds;

    if (addRails) {
        const double edgeWidth = 0.2;
        TraceItem* top = new TraceItem(frameBounds.topLeft(), frameBounds.topRight(), edgeWidth);
        TraceItem* right = new TraceItem(frameBounds.topRight(), frameBounds.bottomRight(), edgeWidth);
        TraceItem* bottom = new TraceItem(frameBounds.bottomRight(), frameBounds.bottomLeft(), edgeWidth);
        TraceItem* left = new TraceItem(frameBounds.bottomLeft(), frameBounds.topLeft(), edgeWidth);
        top->setLayer(PCBLayerManager::EdgeCuts);
        right->setLayer(PCBLayerManager::EdgeCuts);
        bottom->setLayer(PCBLayerManager::EdgeCuts);
        left->setLayer(PCBLayerManager::EdgeCuts);
        newItems.append(top);
        newItems.append(right);
        newItems.append(bottom);
        newItems.append(left);
    }

    if (addToolingHoles) {
        const double margin = std::max(2.0, railWidth * 0.5);
        const QPointF tl(frameBounds.left() + margin, frameBounds.top() + margin);
        const QPointF tr(frameBounds.right() - margin, frameBounds.top() + margin);
        const QPointF br(frameBounds.right() - margin, frameBounds.bottom() - margin);
        const QPointF bl(frameBounds.left() + margin, frameBounds.bottom() - margin);
        const QList<QPointF> corners = {tl, tr, br, bl};

        int idx = 1;
        for (const QPointF& p : corners) {
            ViaItem* hole = new ViaItem(p, toolingDiameter);
            hole->setName(QString("TOOL_%1").arg(idx++));
            hole->setDrillSize(toolingDrill);
            hole->setStartLayer(PCBLayerManager::TopCopper);
            hole->setEndLayer(PCBLayerManager::BottomCopper);
            hole->setLayer(PCBLayerManager::TopCopper);
            hole->setNetName(QString());
            newItems.append(hole);
        }
    }

    if (addFiducials) {
        const double margin = std::max(4.0, railWidth);
        const QPointF tl(frameBounds.left() + margin, frameBounds.top() + margin);
        const QPointF tr(frameBounds.right() - margin, frameBounds.top() + margin);
        const QPointF bl(frameBounds.left() + margin, frameBounds.bottom() - margin);
        const QList<QPointF> fidPts = {tl, tr, bl};

        int idx = 1;
        for (const QPointF& p : fidPts) {
            PadItem* fid = new PadItem(p, fidDiameter);
            fid->setName(QString("FID_%1").arg(idx++));
            fid->setPadShape("Round");
            fid->setLayer(PCBLayerManager::TopCopper);
            fid->setNetName(QString());
            newItems.append(fid);
        }
    }

    int mouseBiteCount = 0;
    int routedCutCount = 0;
    if (addTabsWithMouseBites && (rows > 1 || cols > 1)) {
        auto tabCentersAlongSpan = [&](double start, double length) {
            QList<double> centers;
            for (int t = 0; t < tabsPerSeam; ++t) {
                const double frac = static_cast<double>(t + 1) / static_cast<double>(tabsPerSeam + 1);
                centers.append(start + frac * length);
            }
            return centers;
        };

        auto addMouseBiteChain = [&](const QPointF& center, bool alongX) {
            const int holeCount = std::max(2, static_cast<int>(std::floor(tabWidth / bitePitch)) + 1);
            const double step = (holeCount > 1) ? (tabWidth / static_cast<double>(holeCount - 1)) : 0.0;
            const double start = -tabWidth * 0.5;

            for (int i = 0; i < holeCount; ++i) {
                QPointF p = center;
                const double d = start + i * step;
                if (alongX) {
                    p.setX(center.x() + d);
                } else {
                    p.setY(center.y() + d);
                }

                ViaItem* bite = new ViaItem(p, biteDiameter);
                bite->setName(QString("MB_%1").arg(mouseBiteCount + 1));
                bite->setDrillSize(biteDrill);
                bite->setLayer(PCBLayerManager::Drills);
                bite->setStartLayer(PCBLayerManager::TopCopper);
                bite->setEndLayer(PCBLayerManager::BottomCopper);
                bite->setNetName(QString());
                newItems.append(bite);
                ++mouseBiteCount;
            }
        };

        auto addRoutedSeamSegments = [&](double fixedCoord, double spanStart, double spanLen, bool alongX) {
            if (!addRoutedTabCuts) return;
            const double minSegLen = 0.2;
            QList<QPair<double, double>> blocked;
            const QList<double> centers = tabCentersAlongSpan(spanStart, spanLen);
            for (double c : centers) {
                blocked.append({c - tabWidth * 0.5, c + tabWidth * 0.5});
            }
            std::sort(blocked.begin(), blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            double cursor = spanStart;
            const double spanEnd = spanStart + spanLen;
            for (const auto& interval : blocked) {
                const double a = std::max(spanStart, interval.first);
                const double b = std::min(spanEnd, interval.second);
                if (a > cursor && (a - cursor) >= minSegLen) {
                    TraceItem* seg = alongX
                        ? new TraceItem(QPointF(cursor, fixedCoord), QPointF(a, fixedCoord), 0.2)
                        : new TraceItem(QPointF(fixedCoord, cursor), QPointF(fixedCoord, a), 0.2);
                    seg->setLayer(PCBLayerManager::EdgeCuts);
                    newItems.append(seg);
                    ++routedCutCount;
                }
                cursor = std::max(cursor, b);
            }
            if (spanEnd > cursor && (spanEnd - cursor) >= minSegLen) {
                TraceItem* seg = alongX
                    ? new TraceItem(QPointF(cursor, fixedCoord), QPointF(spanEnd, fixedCoord), 0.2)
                    : new TraceItem(QPointF(fixedCoord, cursor), QPointF(fixedCoord, spanEnd), 0.2);
                seg->setLayer(PCBLayerManager::EdgeCuts);
                newItems.append(seg);
                ++routedCutCount;
            }
        };

        if (cols > 1) {
            for (int r = 0; r < rows; ++r) {
                const double rowTop = boardBounds.top() + r * pitchY;
                const QList<double> yTabs = tabCentersAlongSpan(rowTop, boardBounds.height());
                for (int c = 1; c < cols; ++c) {
                    const double seamX = boardBounds.right() + (c - 1) * pitchX + spacingX * 0.5;
                    for (double cy : yTabs) {
                        addMouseBiteChain(QPointF(seamX, cy), false);
                    }
                    addRoutedSeamSegments(seamX, rowTop, boardBounds.height(), false);
                }
            }
        }

        if (rows > 1) {
            for (int c = 0; c < cols; ++c) {
                const double colLeft = boardBounds.left() + c * pitchX;
                const QList<double> xTabs = tabCentersAlongSpan(colLeft, boardBounds.width());
                for (int r = 1; r < rows; ++r) {
                    const double seamY = boardBounds.bottom() + (r - 1) * pitchY + spacingY * 0.5;
                    for (double cx : xTabs) {
                        addMouseBiteChain(QPointF(cx, seamY), true);
                    }
                    addRoutedSeamSegments(seamY, colLeft, boardBounds.width(), true);
                }
            }
        }
    }

    if (undoStack) {
        undoStack->push(new PCBAddItemsCommand(scene, newItems));
    } else {
        for (PCBItem* item : newItems) scene->addItem(item);
    }

    if (statusBar) {
        statusBar->showMessage(
            QString("Panelize Board: created %1 board copy/copies (%2 new item(s), mouse-bites: %3, routed cuts: %4).")
                .arg(rows * cols - 1)
                .arg(newItems.size())
                .arg(mouseBiteCount)
                .arg(routedCutCount),
            5000);
    }
}
