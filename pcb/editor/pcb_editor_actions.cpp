/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mainwindow.h"
#include "pcb_panelizer.h"
#include "pcb_export_manager.h"
#include "pcb_api.h"
#include "pcb_eco_resolver.h"
#include "pcb_via_stitcher.h"
#include "pcb_view.h"
#include "pcb_item_registry.h"
#include "pcb_tool_registry_builtin.h"
#include "pcb_component_tool.h"
#include "pcb_plugin_manager.h"
#include "pcb_layer.h"
#include "pcb_layer_panel.h"
#include "pcb_drc_panel.h"
#include "../dialogs/board_setup_dialog.h"
#include "pcb_sync_dialog.h"
#include "../dialogs/via_stitching_dialog.h"
#include "../dialogs/gerber_export_dialog.h"
#include "../dialogs/netlist_import_dialog.h"
#include "../dialogs/pick_place_export_dialog.h"
#include "../dialogs/auto_router_dialog.h"
#include "../dialogs/length_matching_dialog.h"
#include "../dialogs/pcb_diff_viewer.h"
#include "../dialogs/design_report_dialog.h"
#include "../dialogs/pdf_viewer_dialog.h"
#include "../analysis/pcb_diff_engine.h"
#include "../gerber/gerber_exporter.h"
#include "../manufacturing/manufacturing_exporter.h"
#include "../mcad/mcad_exporter.h"
#include "../gerber/gerber_parser.h"
#include "../gerber/gerber_view.h"
#include <QPdfWriter>
#include <QPrinter>
#include <QStandardPaths>
#include <QSvgGenerator>
#include "ui/selection_filter_widget.h"
#include "theme_manager.h"
#include "../../footprints/footprint_editor.h"
#include "../../footprints/footprint_library.h"
#include "../gerber/gerber_viewer_window.h"
#include "../ui/pcb_components_widget.h"
#include "../../python/cpp/dialogs/gemini_dialog.h"
#include "../../python/cpp/gemini/gemini_panel.h"
#include "component_item.h"
#include "pcb_commands.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include "pad_item.h"
#include "trace_item.h"
#include "via_item.h"
#include "../ui/pcb_3d_window.h"
#include "copper_pour_item.h"
#include "shape_item.h"
#include "image_item.h"
#include "ui/command_palette.h"
#include <QMenuBar>
#include <QMenu>
#include <QScreen>
#include <QFile>
#include "ratsnest_item.h"
#include "sync_manager.h"
#include "../io/pcb_file_io.h"
#include "settings_dialog.h"
#include "config_manager.h"
#include <QTreeWidget>
#include <QLineEdit>
#include <QActionGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QCloseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QFrame>
#include <QTimer>
#include <QSet>
#include <QApplication>
#include <QPainter>
#include <QProcess>
#include <QLineF>
#include <QPixmap>
#include <QImageReader>
#include <QTemporaryDir>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <vector>
#include <algorithm>
#include <cmath>

void MainWindow::onToolSelected() {
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;
    QString toolName = action->data().toString();
    if (toolName.isEmpty()) return;
    m_view->setCurrentTool(toolName);
    
    // Update properties editor with tool settings if no selection
    if (m_scene->selectedItems().isEmpty()) {
        m_propertyEditor->setPCBTool(m_view->currentTool());
    }
    
    if (toolName == "Pad") statusBar()->showMessage("Click to place pads", 3000);
    else if (toolName == "Select") statusBar()->showMessage("Select and move items", 3000);
    else {
        auto& registry = PCBToolRegistry::instance();
        PCBTool* tool = registry.getTool(toolName);
        if (tool) statusBar()->showMessage(tool->tooltip(), 3000);
    }
}

void MainWindow::onZoomIn() {
    if (m_view) m_view->scale(1.2, 1.2);
}

void MainWindow::onZoomOut() {
    if (m_view) m_view->scale(1/1.2, 1/1.2);
}

void MainWindow::onZoomFit() {
    if (!m_view || !m_scene) return;
    QRectF bounds = m_scene->itemsBoundingRect();
    if (!bounds.isValid()) return;
    QRectF fitRect = bounds.adjusted(-50, -50, 50, 50);
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });
}

void MainWindow::onZoomAllComponents() {
    if (!m_scene || !m_view) return;

    QRectF rect;
    int count = 0;
    for (auto* item : m_scene->items()) {
        if (item->type() == PCBItem::ComponentType) {
            rect = rect.united(item->sceneBoundingRect());
            count++;
        }
    }

    if (!rect.isValid() || count == 0) {
        statusBar()->showMessage("No components found to zoom to.", 2000);
        return;
    }

    // Add comfortable margin
    QRectF fitRect = rect.adjusted(-10, -10, 10, 10); // PCB units are mm usually

    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(QString("Zoomed to %1 components").arg(count), 2000);
}

void MainWindow::onZoomSelection() {
    if (!m_view || !m_scene) return;

    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        statusBar()->showMessage("No items selected to zoom to.", 2000);
        return;
    }

    QRectF rect;
    for (auto* item : selected) {
        rect = rect.united(item->sceneBoundingRect());
    }

    if (!rect.isValid()) return;

    const qreal minSize = 50.0; // PCB uses mm-scale units
    if (rect.width() < minSize) {
        qreal d = (minSize - rect.width()) / 2.0;
        rect.adjust(-d, 0, d, 0);
    }
    if (rect.height() < minSize) {
        qreal d = (minSize - rect.height()) / 2.0;
        rect.adjust(0, -d, 0, d);
    }

    QRectF fitRect = rect.adjusted(-15, -15, 15, 15);
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(
        QString("Zoomed to %1 selected item(s)").arg(selected.size()), 2000);
}

void MainWindow::onActiveLayerChanged(int layerId) {
    if (m_layerCombo) {
        m_layerCombo->blockSignals(true);
        int idx = m_layerCombo->findData(layerId);
        if (idx != -1) m_layerCombo->setCurrentIndex(idx);
        
        QString colorStr = (layerId == 0) ? "#ef4444" : "#3b82f6";
        m_layerCombo->setStyleSheet(QString(
            "QComboBox { border: none; padding: 2px 10px; background: transparent; font-weight: 500; min-width: 140px; color: %1; }"
            "QComboBox:hover { background: #2d2d30; }"
            "QComboBox::drop-down { border: none; }"
        ).arg(colorStr));
        m_layerCombo->blockSignals(false);
    }

    // Sync active tool with the newly selected layer
    if (m_view && m_view->currentTool()) {
        m_view->currentTool()->setToolProperty("Active Layer", layerId);
    }

    // Force refresh of pads to show/hide active layer highlight
    if (m_scene) {
        m_scene->update();
    }
}

void MainWindow::onRunDRC() {
    if (m_drcDock) {
        m_drcDock->show();
        m_drcDock->raise();
    }
    if (m_drcPanel) m_drcPanel->runCheck();
}

void MainWindow::onRunCourtyardValidation() {
    if (!m_scene) return;

    QList<ComponentItem*> comps;
    for (QGraphicsItem* item : m_scene->items()) {
        if (ComponentItem* c = dynamic_cast<ComponentItem*>(item)) {
            if (c->isVisible()) comps.append(c);
        }
    }

    QList<ComponentItem*> offenders;
    int overlapPairs = 0;
    for (int i = 0; i < comps.size(); ++i) {
        for (int j = i + 1; j < comps.size(); ++j) {
            ComponentItem* a = comps[i];
            ComponentItem* b = comps[j];
            if (a->layer() != b->layer()) continue; // opposite side allowed
            if (!a->sceneBoundingRect().intersects(b->sceneBoundingRect())) continue;
            overlapPairs++;
            if (!offenders.contains(a)) offenders.append(a);
            if (!offenders.contains(b)) offenders.append(b);
        }
    }

    m_scene->clearSelection();
    for (ComponentItem* c : offenders) c->setSelected(true);

    if (overlapPairs == 0) {
        statusBar()->showMessage("Courtyard Validation: no overlaps found.", 3000);
        return;
    }

    if (m_view && !offenders.isEmpty()) {
        m_view->centerOn(offenders.first());
    }
    statusBar()->showMessage(
        QString("Courtyard Validation: %1 overlap pair(s), %2 component(s) selected.")
            .arg(overlapPairs)
            .arg(offenders.size()),
        5000);
}

void MainWindow::onCreateLinearArray() {
    if (!m_scene) return;

    QList<PCBItem*> seeds;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (PCBItem* p = dynamic_cast<PCBItem*>(item)) {
            if (dynamic_cast<PCBItem*>(p->parentItem()) == nullptr) {
                seeds.append(p);
            }
        }
    }
    if (seeds.isEmpty()) {
        statusBar()->showMessage("Linear Array: select at least one top-level item.", 3000);
        return;
    }

    bool ok = false;
    int copies = QInputDialog::getInt(this, "Linear Array", "Number of copies:", 3, 1, 1000, 1, &ok);
    if (!ok || copies <= 0) return;

    double dx = QInputDialog::getDouble(this, "Linear Array", "Step X (mm):", 5.0, -10000.0, 10000.0, 3, &ok);
    if (!ok) return;
    double dy = QInputDialog::getDouble(this, "Linear Array", "Step Y (mm):", 0.0, -10000.0, 10000.0, 3, &ok);
    if (!ok) return;

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * copies);

    for (int i = 1; i <= copies; ++i) {
        const QPointF offset(dx * i, dy * i);
        for (PCBItem* seed : seeds) {
            PCBItem* clone = seed->clone();
            if (!clone) continue;
            clone->setPos(seed->pos() + offset);
            newItems.append(clone);
        }
    }

    if (newItems.isEmpty()) {
        statusBar()->showMessage("Linear Array: no items created.", 3000);
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newItems));
    } else {
        for (PCBItem* item : newItems) m_scene->addItem(item);
    }
    statusBar()->showMessage(QString("Linear Array: created %1 item(s).").arg(newItems.size()), 4000);
}

void MainWindow::onCreateCircularArray() {
    if (!m_scene) return;

    QList<PCBItem*> seeds;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (PCBItem* p = dynamic_cast<PCBItem*>(item)) {
            if (dynamic_cast<PCBItem*>(p->parentItem()) == nullptr) {
                seeds.append(p);
            }
        }
    }
    if (seeds.isEmpty()) {
        statusBar()->showMessage("Circular Array: select at least one top-level item.", 3000);
        return;
    }

    bool ok = false;
    int copies = QInputDialog::getInt(this, "Circular Array", "Number of copies:", 6, 1, 1000, 1, &ok);
    if (!ok || copies <= 0) return;

    double stepDeg = QInputDialog::getDouble(this, "Circular Array", "Angle step (deg):", 30.0, -360.0, 360.0, 3, &ok);
    if (!ok) return;

    // Default center at selected items' bounding center.
    QRectF selBounds;
    for (PCBItem* s : seeds) selBounds = selBounds.united(s->sceneBoundingRect());
    QPointF center = selBounds.center();

    double cx = QInputDialog::getDouble(this, "Circular Array", "Center X (mm):", center.x(), -100000.0, 100000.0, 3, &ok);
    if (!ok) return;
    double cy = QInputDialog::getDouble(this, "Circular Array", "Center Y (mm):", center.y(), -100000.0, 100000.0, 3, &ok);
    if (!ok) return;
    center = QPointF(cx, cy);

    bool rotateWithArray = (QMessageBox::question(
        this, "Circular Array",
        "Rotate copied items by the same array angle?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * copies);

    for (int i = 1; i <= copies; ++i) {
        const double angleDeg = stepDeg * i;
        const double rad = angleDeg * M_PI / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);

        for (PCBItem* seed : seeds) {
            PCBItem* clone = seed->clone();
            if (!clone) continue;

            QPointF v = seed->pos() - center;
            QPointF vr(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
            clone->setPos(center + vr);

            if (rotateWithArray) {
                clone->setRotation(seed->rotation() + angleDeg);
            }

            newItems.append(clone);
        }
    }

    if (newItems.isEmpty()) {
        statusBar()->showMessage("Circular Array: no items created.", 3000);
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newItems));
    } else {
        for (PCBItem* item : newItems) m_scene->addItem(item);
    }
    statusBar()->showMessage(QString("Circular Array: created %1 item(s).").arg(newItems.size()), 4000);
}

void MainWindow::onPanelizeBoard() {
    PCBPanelizer::panelize(m_scene, statusBar(), m_undoStack, this);
}

void MainWindow::onDRCViolationSelected(QPointF location) {
    if (m_view) m_view->centerOn(location);
}

void MainWindow::onFilterChanged() {
    if (!m_scene || !m_selectionFilter) return;

    bool componentsEnabled = m_selectionFilter->isFilterEnabled("Symbols"); 
    bool tracesEnabled = m_selectionFilter->isFilterEnabled("Traces");
    bool padsEnabled = m_selectionFilter->isFilterEnabled("Pads/Vias");
    bool poursEnabled = m_selectionFilter->isFilterEnabled("Pours");
    bool ratsnestEnabled = m_selectionFilter->isFilterEnabled("Ratsnest");
    bool activeLayerOnly = m_selectionFilter->isFilterEnabled("Active Layer");
    const int activeLayer = PCBLayerManager::instance().activeLayerId();

    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pItem = dynamic_cast<PCBItem*>(item);
        if (!pItem) continue;

        bool selectable = true;
        QString type = pItem->itemTypeName();

        if (type == "Component") selectable = componentsEnabled;
        else if (type == "Trace") selectable = tracesEnabled;
        else if (type == "Pad" || type == "Via") selectable = padsEnabled;
        else if (type == "CopperPour") selectable = poursEnabled;
        else if (type == "Ratsnest") selectable = ratsnestEnabled;

        if (selectable && activeLayerOnly) {
            if (ViaItem* via = dynamic_cast<ViaItem*>(pItem)) {
                selectable = via->spansLayer(activeLayer);
            } else if (type == "Ratsnest") {
                selectable = true; // airwires are net-level guides
            } else {
                selectable = (pItem->layer() == activeLayer);
            }
        }

        pItem->setFlag(QGraphicsItem::ItemIsSelectable, selectable);
        
        if (!selectable && pItem->isSelected()) {
            pItem->setSelected(false);
        }
    }
}

void MainWindow::onOpenCommandPalette() {
    CommandPalette* palette = new CommandPalette(this);
    palette->setPlaceholderText("Search footprints, nets, or run PCB commands...");

    // 1. Add all menu actions
    for (auto menu : menuBar()->findChildren<QMenu*>()) {
        for (auto action : menu->actions()) {
            if (!action->text().isEmpty() && !action->isSeparator()) {
                palette->addAction(action);
            }
        }
    }

    // 2. Add all items in the PCB
    for (auto gItem : m_scene->items()) {
        if (auto pItem = dynamic_cast<PCBItem*>(gItem)) {
            if (auto comp = dynamic_cast<ComponentItem*>(pItem)) {
                PaletteResult res;
                res.title = QString("%1 (%2)").arg(comp->name(), comp->componentType());
                res.description = QString("Footprint: %1 on Layer %2").arg(comp->componentType()).arg(comp->layer());
                res.icon = getThemeIcon(":/icons/comp_ic.svg");
                res.action = [this, comp]() {
                    m_view->centerOn(comp);
                    m_scene->clearSelection();
                    comp->setSelected(true);
                };
                palette->addResult(res);
            } else if (auto trace = dynamic_cast<TraceItem*>(pItem)) {
                if (!trace->netName().isEmpty()) {
                    PaletteResult res;
                    res.title = QString("Net: %1").arg(trace->netName());
                    res.description = QString("Trace on Layer %1").arg(trace->layer());
                    res.icon = getThemeIcon(":/icons/tool_trace.svg");
                    res.action = [this, trace]() {
                        m_view->centerOn(trace);
                        m_scene->clearSelection();
                        trace->setSelected(true);
                    };
                    palette->addResult(res);
                }
            }
        }
    }

    palette->show();
}

void MainWindow::onAlignLeft() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignLeft));
        statusBar()->showMessage("Aligned Left", 2000);
    }
}

void MainWindow::onAlignRight() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignRight));
        statusBar()->showMessage("Aligned Right", 2000);
    }
}

void MainWindow::onAlignTop() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignTop));
        statusBar()->showMessage("Aligned Top", 2000);
    }
}

void MainWindow::onAlignBottom() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignBottom));
        statusBar()->showMessage("Aligned Bottom", 2000);
    }
}

void MainWindow::onAlignCenterX() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignCenterX));
        statusBar()->showMessage("Aligned Center X", 2000);
    }
}

void MainWindow::onAlignCenterY() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignCenterY));
        statusBar()->showMessage("Aligned Center Y", 2000);
    }
}

void MainWindow::onDistributeH() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 2) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::DistributeH));
        statusBar()->showMessage("Distributed Horizontally", 2000);
    }
}

void MainWindow::onDistributeV() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 2) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::DistributeV));
        statusBar()->showMessage("Distributed Vertically", 2000);
    }
}

void MainWindow::onRotate() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    for (auto* item : selected) {
        item->setRotation(item->rotation() + 90);
    }
    statusBar()->showMessage("Rotated selected items", 2000);
}

void MainWindow::onMirror() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    for (auto* item : selected) {
        item->setTransform(QTransform().scale(-1, 1), true);
    }
    statusBar()->showMessage("Mirrored selected items", 2000);
}

void MainWindow::onDeleteSelection() {
    if (!m_scene) return;

    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QSet<PCBItem*> itemsToDelete;
    for (QGraphicsItem* it : selected) {
        PCBItem* pcbItem = nullptr;
        QGraphicsItem* current = it;
        while (current) {
            if (PCBItem* candidate = dynamic_cast<PCBItem*>(current)) pcbItem = candidate;
            current = current->parentItem();
        }

        if (pcbItem && !pcbItem->isLocked()) {
            itemsToDelete.insert(pcbItem);
        } else if (!pcbItem) {
            if (it->flags() & QGraphicsItem::ItemIsSelectable) {
                m_scene->removeItem(it);
                delete it;
            }
        }
    }

    QList<PCBItem*> finalItems;
    for (PCBItem* item : itemsToDelete) {
        bool parentInSet = false;
        QGraphicsItem* p = item->parentItem();
        while (p) {
            if (PCBItem* pi = dynamic_cast<PCBItem*>(p)) {
                if (itemsToDelete.contains(pi)) {
                    parentInSet = true;
                    break;
                }
            }
            p = p->parentItem();
        }
        if (!parentInSet) {
            finalItems.append(item);
        }
    }

    if (finalItems.isEmpty()) return;

    m_scene->clearSelection();

    if (m_undoStack) {
        m_undoStack->push(new PCBRemoveItemCommand(m_scene, finalItems));
    } else {
        for (PCBItem* item : finalItems) {
            m_scene->removeItem(item);
            delete item;
        }
    }

    statusBar()->showMessage(QString("Deleted %1 item(s)").arg(finalItems.size()), 2000);
    m_view->update();
    if (m_propertyEditor) m_propertyEditor->clear();
}

void MainWindow::clearNetHighlighting() {
    if (!m_scene) return;
    for (QGraphicsItem* item : m_scene->items()) {
        if (dynamic_cast<PCBItem*>(item) || dynamic_cast<RatsnestItem*>(item)) {
            item->setOpacity(1.0);
        }
    }
    m_highlightedNet.clear();
}

void MainWindow::applyNetHighlighting() {
    if (!m_scene) return;

    QString targetNet;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* gi : selected) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(gi);
        if (!pcb) continue;
        if (!pcb->netName().isEmpty() && pcb->netName() != "No Net") {
            targetNet = pcb->netName();
            break;
        }
        for (QGraphicsItem* child : pcb->childItems()) {
            if (PCBItem* cp = dynamic_cast<PCBItem*>(child)) {
                if (!cp->netName().isEmpty() && cp->netName() != "No Net") {
                    targetNet = cp->netName();
                    break;
                }
            }
        }
        if (!targetNet.isEmpty()) break;
    }

    if (targetNet.isEmpty()) {
        clearNetHighlighting();
        statusBar()->showMessage("Select an item with a net to highlight", 2000);
        return;
    }

    m_highlightedNet = targetNet;

    for (QGraphicsItem* gi : m_scene->items()) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(gi);
        RatsnestItem* air = dynamic_cast<RatsnestItem*>(gi);
        if (!pcb && !air) continue;

        bool match = false;
        if (pcb) {
            if (pcb->netName() == targetNet) {
                match = true;
            } else if (pcb->itemType() == PCBItem::ComponentType) {
                for (QGraphicsItem* child : pcb->childItems()) {
                    if (PCBItem* cp = dynamic_cast<PCBItem*>(child)) {
                        if (cp->netName() == targetNet) {
                            match = true;
                            break;
                        }
                    }
                }
            }
        }

        if (air) match = true;

        gi->setOpacity(match ? 1.0 : 0.18);
    }

    statusBar()->showMessage(QString("Highlighting net: %1").arg(targetNet), 2000);
}

void MainWindow::onBringToFront() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    qreal maxZ = -999999;
    for (auto* item : m_scene->items()) maxZ = qMax(maxZ, item->zValue());
    
    for (auto* item : selected) item->setZValue(maxZ + 1);
}

void MainWindow::onSendToBack() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    qreal minZ = 999999;
    for (auto* item : m_scene->items()) minZ = qMin(minZ, item->zValue());
    
    for (auto* item : selected) item->setZValue(minZ - 1);
}

void MainWindow::onAutoRoute() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    AutoRouterDialog dlg(m_scene, this);
    dlg.exec();
}

void MainWindow::onLengthMatching() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    LengthMatchingDialog dlg(m_scene, this);
    dlg.exec();
}

void MainWindow::onCompareBoard() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    if (m_currentFilePath.isEmpty() || !QFileInfo::exists(m_currentFilePath)) {
        QMessageBox::information(this, "Unsaved Board",
            "Please save the current board first, then compare with another file.\n\n"
            "Use File → Save, then Tools → Compare Board...");
        return;
    }

    QString boardB = QFileDialog::getOpenFileName(this, "Select Board to Compare Against",
                                                   "", "PCB Files (*.pcb);;All Files (*)");
    if (boardB.isEmpty()) return;

    QString boardA = m_currentFilePath;

    DiffReport report = PCBDiffEngine::compareFiles(boardA, boardB, PCBDiffEngine::CompareOptions());

    PCBDiffViewer viewer(report, m_scene, nullptr, this);
    viewer.exec();
}

void MainWindow::onViaStitching() {
    PCBViaStitcher::performViaStitching(m_scene, m_undoStack, statusBar(), this);
}

void MainWindow::onOpenFootprintEditor() {
    FootprintEditor* editor = new FootprintEditor(this);
    connect(editor, &FootprintEditor::footprintSaved, this, [this](const FootprintDefinition& def) {
        auto libs = FootprintLibraryManager::instance().libraries();
        if (!libs.isEmpty()) {
            libs.first()->saveFootprint(def);
        }
        if (m_componentsPanel) m_componentsPanel->populate();
    });
    editor->show();
}

void MainWindow::onOpenGeminiAI() {
    GeminiDialog* dialog = new GeminiDialog(m_scene, this);
    dialog->show();
}

void MainWindow::onToggle3DView() {
    if (!m_3dWindow) {
        m_3dWindow = new PCB3DWindow(m_scene, this);
        m_3dWindow->setAttribute(Qt::WA_DeleteOnClose, false); // Keep it alive for toggling
        connect(m_3dWindow, &PCB3DWindow::componentPicked, this, [this](const QUuid& id) {
            if (!m_scene || !m_view) return;

            ComponentItem* hit = nullptr;
            for (QGraphicsItem* item : m_scene->items()) {
                if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                    if (comp->id() == id) {
                        hit = comp;
                        break;
                    }
                }
            }
            if (!hit) return;

            m_scene->clearSelection();
            hit->setSelected(true);
            m_view->centerOn(hit);
            m_view->setFocus();

            const QString refDes = hit->name().trimmed();
            const QString netName = hit->netName().trimmed();
            if (!refDes.isEmpty() || !netName.isEmpty()) {
                SyncManager::instance().pushCrossProbe(refDes, netName);
            }

            if (statusBar()) {
                statusBar()->showMessage(
                    QString("3D cross-probe: %1").arg(refDes.isEmpty() ? hit->componentType() : refDes),
                    2500);
            }
        });
    }
    
    if (m_3dWindow->isVisible()) {
        m_3dWindow->hide();
    } else {
        m_3dWindow->show();
        m_3dWindow->raise();
        m_3dWindow->activateWindow();
        m_3dWindow->updateView();
    }
}
