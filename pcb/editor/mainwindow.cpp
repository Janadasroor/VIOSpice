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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(nullptr)
    , m_view(nullptr)
    , m_layerDock(nullptr)
    , m_propertiesDock(nullptr)
    , m_libraryDock(nullptr)
    , m_drcDock(nullptr)
    , m_layerPanel(nullptr)
    , m_drcPanel(nullptr)
    , m_propertyEditor(nullptr)
    , m_componentsPanel(nullptr)
    , m_geminiDock(nullptr)
    , m_geminiPanel(nullptr)
    , m_componentTool(nullptr)
    , m_coordLabel(nullptr)
    , m_gridCombo(nullptr)
    , m_layerLabel(nullptr)
    , m_undoStack(new QUndoStack(this))
    , m_api(nullptr)
    , m_selectionFilter(nullptr) {

    setWindowTitle("Viora EDA - PCB Editor");
    setMinimumSize(800, 600);
    resize(1100, 800);
    setObjectName("PCBEditor");
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    PCBItemRegistry::registerBuiltInItems();
    PCBToolRegistryBuiltIn::registerBuiltInTools();

    setupCanvas();
    m_api = new PCBAPI(m_scene, m_undoStack, this);
    createMenuBar();
    createToolBar();
    createDockWidgets();
    createStatusBar();
    applyTheme();

    connect(&SyncManager::instance(), &SyncManager::ecoAvailable, this, &MainWindow::handleIncomingECO);
    
    // Smart Cross-Probing
    connect(&SyncManager::instance(), &SyncManager::crossProbeReceived, this, [this](const QString& refDes, const QString& netName) {
        if (!m_scene) return;
        const bool autoFocus = ConfigManager::instance().autoFocusOnCrossProbe();
        
        bool found = false;
        
        for (QGraphicsItem* item : m_scene->items()) {
            if (auto* comp = dynamic_cast<class ComponentItem*>(item)) {
                if (comp->name() == refDes) {
                    // Fix infinite loop: if already selected, don't re-select
                    if (comp->isSelected() && m_scene->selectedItems().size() == 1) {
                         if (autoFocus) m_view->centerOn(comp);
                         return;
                    }
                    
                    m_scene->clearSelection();
                    comp->setSelected(true);
                    if (autoFocus) m_view->centerOn(comp);
                    found = true;
                    statusBar()->showMessage("Cross-probed: " + refDes, 2000);
                    break;
                }
            }
        }
        
        if (!found && !netName.isEmpty()) {
            // Highlight all items on net
            m_netHighlightEnabled = true;
            m_scene->clearSelection();
            bool netFound = false;
            QList<QGraphicsItem*> netItems;
            for (auto* item : m_scene->items()) {
                if (auto* pi = dynamic_cast<PCBItem*>(item)) {
                    if (pi->netName() == netName) {
                        pi->setSelected(true);
                        netItems.append(pi);
                        netFound = true;
                    }
                }
            }
            if (netFound) {
                if (autoFocus && !netItems.isEmpty()) {
                    m_view->centerOn(netItems.first());
                }
                statusBar()->showMessage("Cross-probed net: " + netName, 2500);
            }
        }
    });

    if (SyncManager::instance().hasPendingECO()) {
        QTimer::singleShot(500, this, &MainWindow::handleIncomingECO);
    }

    // Restore UI State (Deferred to prevent startup crashes)
    QTimer::singleShot(0, this, [this](){
        QByteArray geom = ConfigManager::instance().windowGeometry("PCBEditor");
        QByteArray state = ConfigManager::instance().windowState("PCBEditor");
        bool restoredState = false;
        if (!geom.isEmpty()) restoreGeometry(geom);
        if (!state.isEmpty()) restoredState = restoreState(state);

        // Only enforce default tabbed docks if no saved window state was restored
        if (!restoredState) {
            ensureRightBottomDockTabs();
        }

        // Restore last open file if no valid file was set by caller
        if (m_currentFilePath.isEmpty() || !QFile::exists(m_currentFilePath)) {
            QString lastFile = ConfigManager::instance().toolProperty("PCBEditor", "openFile").toString();
            if (!lastFile.isEmpty() && QFile::exists(lastFile)) {
                openFile(lastFile);
            }
        }

        qDebug() << "PCB Editor UI state restored";
    });
}

MainWindow::~MainWindow() {
    this->disconnect();
    SyncManager::instance().disconnect(this);
    if (m_scene) {
        m_scene->blockSignals(true);
        m_scene->disconnect(this);
    }
    if (m_view) {
        m_view->blockSignals(true);
        m_view->disconnect(this);
    }
    if (m_propertyEditor) {
        m_propertyEditor->blockSignals(true);
        m_propertyEditor->disconnect(this);
    }
}

void MainWindow::updateCoordinates(QPointF pos) {
    if (m_coordLabel)
        m_coordLabel->setText(QString("X: %1mm  Y: %2mm").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
}

void MainWindow::handleIncomingECO() {
    if (m_isProcessingECO) {
        qDebug() << "[PCB handleIncomingECO] Already processing, skipping.";
        return;
    }
    qDebug() << "[PCB handleIncomingECO] hasPendingECO:" << SyncManager::instance().hasPendingECO()
             << "target:" << (int)SyncManager::instance().pendingECOTarget();
    if (!SyncManager::instance().hasPendingECO()) return;
    const SyncManager::ECOTarget target = SyncManager::instance().pendingECOTarget();
    if (target == SyncManager::ECOTarget::Schematic) return;

    ECOPackage pkg = SyncManager::instance().pendingECO();
    qDebug() << "[PCB handleIncomingECO] ECO package:" << pkg.components.size() << "components," << pkg.nets.size() << "nets";

    // Clear immediately to prevent re-entry
    SyncManager::instance().clearPendingECO();
    m_isProcessingECO = true;

    // Show review dialog before applying
    PCBSyncDialog reviewDlg(pkg, this);
    if (reviewDlg.exec() == QDialog::Accepted) {
        // User approved the changes
        qDebug() << "[PCB handleIncomingECO] User accepted, applying ECO...";
        applyECO(pkg);
    } else {
        // User rejected
        statusBar()->showMessage("ECO review cancelled — no changes applied", 3000);
    }

    m_isProcessingECO = false;
}

void MainWindow::applyECO(const ECOPackage& package) {
    PCBECOResolver::applyECO(package, m_scene, statusBar(), m_view);
}

void MainWindow::onPropertyChanged(const QString& name, const QVariant& value) {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        // If no items are selected, check if we are editing tool properties
        if (m_view && m_view->currentTool()) {
            m_view->currentTool()->setToolProperty(name, value);
        }
        return;
    }
    
    m_undoStack->beginMacro(QString("Change PCB Property: %1").arg(name));

    for (QGraphicsItem* gItem : selected) {
        PCBItem* item = dynamic_cast<PCBItem*>(gItem);
        if (!item) continue;

        PCBItem* commandTarget = item;

        QVariant oldValue;
        bool found = false;

        if (name == "ID") { /* Read-only */ }
        else if (name == "Name") { oldValue = item->name(); found = true; }
        else if (name == "Value") {
            if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                oldValue = comp->value();
                found = true;
            }
        }
        else if (name == "Component Type") {
            if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                oldValue = comp->componentType();
                found = true;
            }
        }
        else if (name == "Pad Number") {
            if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                oldValue = pad->model() ? pad->model()->number() : "";
                found = true;
            }
        }
        else if (name == "Net") { oldValue = item->netName(); found = true; }
        else if (name == "Component Net") {
            if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                QList<PadItem*> pads;
                for (QGraphicsItem* child : comp->childItems()) {
                    if (PadItem* pad = dynamic_cast<PadItem*>(child)) {
                        pads.append(pad);
                    }
                }
                if (pads.size() == 1) {
                    commandTarget = pads.first();
                    oldValue = pads.first()->netName();
                    found = true;
                }
            }
        }
        else if (name == "Layer") { oldValue = item->layer(); found = true; }
        else if (name == "Height (mm)") { oldValue = item->height(); found = true; }
        else if (name == "3D Model Path") { oldValue = item->modelPath(); found = true; }
        else if (name == "3D Model Scale") { oldValue = item->modelScale(); found = true; }
        else if (name == "Locked") { oldValue = item->isLocked(); found = true; }
        else if (name == "Width (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = trace->width();
                found = true;
            }
        }
        else if (name == "Start X (mm)" || name == "Start Y (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = (name == "Start X (mm)") ? trace->startPoint().x() : trace->startPoint().y();
                found = true;
            }
        }
        else if (name == "End X (mm)" || name == "End Y (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = (name == "End X (mm)") ? trace->endPoint().x() : trace->endPoint().y();
                found = true;
            }
        }
        else if (name == "Position X (mm)" || name == "Position Y (mm)") {
            oldValue = (name == "Position X (mm)") ? item->pos().x() : item->pos().y();
            found = true;
        }
        else if (name == "Rotation (deg)") {
            oldValue = item->rotation();
            found = true;
        }
        else if (name == "Image Width (mm)" || name == "Image Height (mm)") {
            if (PCBImageItem* image = dynamic_cast<PCBImageItem*>(item)) {
                oldValue = (name == "Image Width (mm)") ? image->sizeMm().width() : image->sizeMm().height();
                found = true;
            }
        }
        else if (name == "Shape Stroke Width (mm)") {
            if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) {
                oldValue = shape->strokeWidth();
                found = true;
            }
        }
        else if (selected.size() == 1) {
            if (name == "Pad Shape") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->padShape();
                    found = true;
                }
            }
            else if (name == "Size X (mm)" || name == "Size Y (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = (name == "Size X (mm)") ? pad->size().width() : pad->size().height();
                    found = true;
                }
            }
            else if (name == "Diameter (mm)") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->diameter();
                    found = true;
                }
            }
            else if (name == "Drill Size (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->drillSize();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->drillSize();
                    found = true;
                }
            }
            else if (name == "Via Start Layer") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->startLayer();
                    found = true;
                }
            }
            else if (name == "Via End Layer") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->endLayer();
                    found = true;
                }
            }
            else if (name == "Microvia") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->isMicrovia();
                    found = true;
                }
            }
            else if (name == "Mask Expansion Mode") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->maskExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->maskExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                }
            }
            else if (name == "Mask Expansion (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->maskExpansion();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->maskExpansion();
                    found = true;
                }
            }
            else if (name == "Paste Expansion Mode") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->pasteExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->pasteExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                }
            }
            else if (name == "Paste Expansion (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->pasteExpansion();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->pasteExpansion();
                    found = true;
                }
            }
            else if (name == "Clearance (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->clearance();
                    found = true;
                }
            }
            else if (name == "Shape Width (mm)" || name == "Shape Height (mm)") {
                if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) {
                    oldValue = (name == "Shape Width (mm)") ? shape->sizeMm().width() : shape->sizeMm().height();
                    found = true;
                }
                else if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    const QRectF bounds = pour->polygon().boundingRect();
                    oldValue = (name == "Shape Width (mm)") ? bounds.width() : bounds.height();
                    found = true;
                }
            }
            else if (name == "Arc Start Angle (deg)") {
                if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) {
                    oldValue = shape->startAngleDeg();
                    found = true;
                }
            }
            else if (name == "Arc Span Angle (deg)") {
                if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) {
                    oldValue = shape->spanAngleDeg();
                    found = true;
                }
            }
            else if (name == "Priority") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->priority();
                    found = true;
                }
            }
            else if (name == "Remove Islands") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->removeIslands();
                    found = true;
                }
            }
            else if (name == "Min Island Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->minWidth();
                    found = true;
                }
            }
            else if (name == "Filled") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->filled();
                    found = true;
                }
            }
            else if (name == "Pour Type") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = static_cast<int>(pour->pourType());
                    found = true;
                }
            }
            else if (name == "Hatch Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->hatchWidth();
                    found = true;
                }
            }
            else if (name == "Solid") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->isSolid();
                    found = true;
                }
            }
            else if (name == "Use Thermal Reliefs") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->useThermalReliefs();
                    found = true;
                }
            }
            else if (name == "Thermal Spoke Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeWidth();
                    found = true;
                }
            }
            else if (name == "Thermal Spoke Count") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeCount();
                    found = true;
                }
            }
            else if (name == "Thermal Angle (deg)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeAngleDeg();
                    found = true;
                }
            }
        }

        if (found && oldValue != value) {
            if (m_undoStack) {
                const QString effectiveName = (name == "Component Net") ? QString("Net") : name;
                m_undoStack->push(new PCBPropertyCommand(m_scene, commandTarget, effectiveName, oldValue, value));
            } else {
                // Manual fallback
                if (name == "Name") item->setName(value.toString());
                else if (name == "Net") item->setNetName(value.toString());
                else if (name == "Component Net") {
                    if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                        for (QGraphicsItem* child : comp->childItems()) {
                            if (PadItem* pad = dynamic_cast<PadItem*>(child)) {
                                pad->setNetName(value.toString());
                            }
                        }
                    }
                }
                else if (name == "Layer") item->setLayer(value.toInt());
                else if (name == "Height (mm)") item->setHeight(value.toDouble());
                else if (name == "3D Model Path") item->setModelPath(value.toString());
                else if (name == "3D Model Scale") item->setModelScale(value.toDouble());
                else if (name == "Locked") item->setLocked(value.toBool());
                else if (name == "Width (mm)") {
                    if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) trace->setWidth(value.toDouble());
                }
                else if (name == "Position X (mm)") item->setX(value.toDouble());
                else if (name == "Position Y (mm)") item->setY(value.toDouble());
                else if (name == "Rotation (deg)") item->setRotation(value.toDouble());
                else if (name == "Image Width (mm)" || name == "Image Height (mm)") {
                    if (PCBImageItem* image = dynamic_cast<PCBImageItem*>(item)) {
                        QSizeF size = image->sizeMm();
                        if (name == "Image Width (mm)") size.setWidth(value.toDouble());
                        else size.setHeight(value.toDouble());
                        image->setSizeMm(size);
                    }
                }
                else if (name == "Shape Stroke Width (mm)") {
                    if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) shape->setStrokeWidth(value.toDouble());
                }
                else if (name == "Shape Width (mm)" || name == "Shape Height (mm)") {
                    if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) {
                        QSizeF size = shape->sizeMm();
                        if (name == "Shape Width (mm)") size.setWidth(value.toDouble());
                        else size.setHeight(value.toDouble());
                        shape->setSizeMm(size);
                    }
                    else if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                        QPolygonF poly = pour->polygon();
                        const QRectF bounds = poly.boundingRect();
                        const QPointF center = bounds.center();
                        const qreal target = std::max(0.01, value.toDouble());
                        const qreal current = (name == "Shape Width (mm)") ? bounds.width() : bounds.height();
                        if (current > 1e-9) {
                            const qreal scaleX = (name == "Shape Width (mm)") ? (target / current) : 1.0;
                            const qreal scaleY = (name == "Shape Height (mm)") ? (target / current) : 1.0;
                            for (QPointF& p : poly) {
                                p.setX(center.x() + (p.x() - center.x()) * scaleX);
                                p.setY(center.y() + (p.y() - center.y()) * scaleY);
                            }
                            pour->setPolygon(poly);
                        }
                    }
                }
                else if (name == "Arc Start Angle (deg)") {
                    if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) shape->setStartAngleDeg(value.toDouble());
                }
                else if (name == "Arc Span Angle (deg)") {
                    if (PCBShapeItem* shape = dynamic_cast<PCBShapeItem*>(item)) shape->setSpanAngleDeg(value.toDouble());
                }
                else if (name == "Pad Shape") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPadShape(value.toString());
                }
                else if (name == "Diameter (mm)") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setDiameter(value.toDouble());
                }
                else if (name == "Via Start Layer") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setStartLayer(value.toInt());
                }
                else if (name == "Via End Layer") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setEndLayer(value.toInt());
                }
                else if (name == "Microvia") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMicrovia(value.toBool() || value.toString() == "True");
                }
                else if (name == "Mask Expansion Mode") {
                    const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setMaskExpansionOverrideEnabled(custom);
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMaskExpansionOverrideEnabled(custom);
                }
                else if (name == "Mask Expansion (mm)") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setMaskExpansion(value.toDouble());
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMaskExpansion(value.toDouble());
                }
                else if (name == "Paste Expansion Mode") {
                    const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPasteExpansionOverrideEnabled(custom);
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setPasteExpansionOverrideEnabled(custom);
                }
                else if (name == "Paste Expansion (mm)") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPasteExpansion(value.toDouble());
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setPasteExpansion(value.toDouble());
                }
                else if (name == "Use Thermal Reliefs") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setUseThermalReliefs(value.toBool() || value.toString() == "True");
                }
                else if (name == "Priority") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setPriority(qRound(value.toDouble()));
                }
                else if (name == "Remove Islands") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setRemoveIslands(value.toBool() || value.toString() == "True");
                }
                else if (name == "Min Island Width (mm)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setMinWidth(value.toDouble());
                }
                else if (name == "Filled") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setFilled(value.toBool() || value.toString() == "True");
                }
                else if (name == "Thermal Spoke Width (mm)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeWidth(value.toDouble());
                }
                else if (name == "Thermal Spoke Count") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeCount(value.toInt());
                }
                else if (name == "Thermal Angle (deg)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeAngleDeg(value.toDouble());
                }
            }
            
            // Sync tool default values with the last edited item properties
            if (m_view && m_view->currentTool()) {
                if (name == "Width (mm)") {
                    m_view->currentTool()->setToolProperty("Trace Width (mm)", value);
                    updateOptionsBar(m_view->currentTool()->name());
                } else if (name == "Shape Stroke Width (mm)") {
                    m_view->currentTool()->setToolProperty("Stroke Width (mm)", value);
                    updateOptionsBar(m_view->currentTool()->name());
                } else if (name == "Layer") {
                    m_view->currentTool()->setToolProperty("Active Layer", value);
                    updateOptionsBar(m_view->currentTool()->name());
                }
            }
        }
        item->update();
    }

    m_undoStack->endMacro();
}

void MainWindow::onSnippetGenerated(const QString& jsonSnippet) {
    if (!m_scene || !m_api) return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonSnippet.toUtf8());
    if (!doc.isArray() && !doc.isObject()) return;

    if (doc.isObject() && doc.object().contains("commands")) {
        m_undoStack->beginMacro("AI Generated Changes");
        m_api->executeBatch(doc.object()["commands"].toArray());
        m_undoStack->endMacro();
        statusBar()->showMessage("Executed AI generated PCB commands", 3000);
    }
}
