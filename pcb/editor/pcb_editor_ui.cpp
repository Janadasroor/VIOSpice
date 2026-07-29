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

void MainWindow::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;
    theme->applyToWidget(this);
    
    // Apply specific toolbars and dock styling for premium look
    for (auto toolbar : findChildren<QToolBar*>()) {
        toolbar->setStyleSheet(theme->toolbarStylesheet());
    }
    for (auto dock : findChildren<QDockWidget*>()) {
        dock->setStyleSheet(theme->dockStylesheet());
    }
    
    if (statusBar()) {
        statusBar()->setStyleSheet(theme->statusBarStylesheet());
    }
    
    if (m_view) {
        m_view->setBackgroundBrush(QBrush(theme->canvasBackground()));
    }
}

void MainWindow::setupCanvas() {
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    if (m_api) m_api->setScene(m_scene);
    updateGrid();
    
    // Initialize Ratsnest
    PCBRatsnestManager::instance().setScene(m_scene);

    m_view = new PCBView(this);
    m_view->setScene(m_scene);
    m_view->setUndoStack(m_undoStack);
    connect(m_scene, &QGraphicsScene::selectionChanged, m_view, &PCBView::selectionChanged);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyBar);
    
    // Live Ratsnest and Copper Pour Updates
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](){
        PCBRatsnestManager::instance().update();
        for (auto* item : m_scene->items()) {
            if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                pour->rebuild();
                pour->update();
            }
        }
        if (m_3dWindow) m_3dWindow->updateView();
    });

    setCentralWidget(m_view);
    connect(m_view, &PCBView::toolChanged, this, &MainWindow::updateOptionsBar);
    connect(m_view, &PCBView::coordinatesChanged, this, &MainWindow::updateCoordinates);
    connect(m_view, &PCBView::statusMessage, this, [this](const QString& msg){
        statusBar()->showMessage(msg, 3000);
    });
}

void MainWindow::updateGrid() {
    // Grid size is managed by m_gridCombo selection
}

void MainWindow::createMenuBar() {
    QMenuBar *menuBar = this->menuBar();

    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New", QKeySequence::New, this, &MainWindow::onNewProject);
    fileMenu->addAction("&Open", QKeySequence::Open, this, &MainWindow::onOpenProject);
    fileMenu->addAction("Open Gerber Viewer...", QKeySequence(), this, &MainWindow::onOpenGerberViewer);
    fileMenu->addAction("&Save", QKeySequence::Save, this, &MainWindow::onSaveProject);
    fileMenu->addAction("Save &As...", QKeySequence::SaveAs, this, &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();
    fileMenu->addAction("Import Netlist...", QKeySequence("Ctrl+I"), this, &MainWindow::onImportNetlist);
    fileMenu->addAction("Import Image...", QKeySequence("Ctrl+Shift+I"), this, &MainWindow::onImportImage);
    fileMenu->addAction("Import KiCad PCB...", QKeySequence("Ctrl+K"), this, &MainWindow::onImportKiCadPCB);

    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu("Export");
    exportMenu->addAction("Generate One-Click Manufacturing Package (ZIP)...", QKeySequence("Ctrl+Shift+M"), this, &MainWindow::onExportManufacturingPackage);
    exportMenu->addSeparator();
    exportMenu->addAction("Export KiCad PCB (.kicad_pcb)...", QKeySequence("Ctrl+Shift+K"), this, &MainWindow::onExportKiCadPCB);
    exportMenu->addSeparator();
    exportMenu->addAction("Export as Image...", QKeySequence(), this, &MainWindow::onExportImage);
    exportMenu->addAction("Export as PDF...", QKeySequence(), this, &MainWindow::onExportPDF);
    exportMenu->addAction("Export as SVG...", QKeySequence(), this, &MainWindow::onExportSVG);
    exportMenu->addAction("Export Assembly Drawing...", QKeySequence(), this, &MainWindow::onExportAssemblyDrawing);
    exportMenu->addSeparator();
    exportMenu->addAction("Export IPC-2581...", QKeySequence(), this, &MainWindow::onExportIPC2581);
    exportMenu->addAction("Export ODB++ Package...", QKeySequence(), this, &MainWindow::onExportODBpp);
    exportMenu->addSeparator();
    exportMenu->addAction("Export Pick and Place...", QKeySequence(), this, &MainWindow::onExportPickPlace);
    exportMenu->addSeparator();
    exportMenu->addAction("Export STEP (Wireframe)...", QKeySequence(), this, &MainWindow::onExportSTEP);
    exportMenu->addAction("Export IGES (Wireframe)...", QKeySequence(), this, &MainWindow::onExportIGES);
    
    fileMenu->addAction("Generate Gerber Files...", QKeySequence(), this, &MainWindow::onGenerateGerbers);
    
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar->addMenu("&Edit");
    m_undoAction = m_undoStack->createUndoAction(this, "&Undo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(m_undoAction);
    addAction(m_undoAction);

    m_redoAction = m_undoStack->createRedoAction(this, "&Redo");
    m_redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(m_redoAction);
    addAction(m_redoAction);
    editMenu->addSeparator();

    QAction* cutAct = editMenu->addAction("Cu&t", QKeySequence::Cut, this, &MainWindow::onCut);
    addAction(cutAct);
    QAction* copyAct = editMenu->addAction("&Copy", QKeySequence::Copy, this, &MainWindow::onCopy);
    addAction(copyAct);
    QAction* pasteAct = editMenu->addAction("&Paste", QKeySequence::Paste, this, &MainWindow::onPaste);
    addAction(pasteAct);
    QAction* duplicateAct = editMenu->addAction("Duplicate", QKeySequence("Ctrl+D"), this, &MainWindow::onDuplicate);
    addAction(duplicateAct);
    editMenu->addSeparator();
    editMenu->addAction("&Delete", QKeySequence::Delete, this, &MainWindow::onDeleteSelection);
    QAction* bringToFrontAct = editMenu->addAction("Bring To Front");
    bringToFrontAct->setShortcut(QKeySequence("Ctrl+]"));
    connect(bringToFrontAct, &QAction::triggered, this, &MainWindow::onBringToFront);
    QAction* sendToBackAct = editMenu->addAction("Send To Back");
    sendToBackAct->setShortcut(QKeySequence("Ctrl+["));
    connect(sendToBackAct, &QAction::triggered, this, &MainWindow::onSendToBack);
    editMenu->addSeparator();

    QAction* selectAllAct = editMenu->addAction("Select &All");
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, this, [this]() {
        if (!m_scene) return;
        int count = 0;
        for (QGraphicsItem* item : m_scene->items()) {
            PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
            if (!pcbItem) continue;
            if (!pcbItem->isVisible()) continue;

            pcbItem->setSelected(true);
            ++count;
        }

        statusBar()->showMessage(QString("Selected %1 item(s)").arg(count), 2000);
    });
    editMenu->addSeparator();
    editMenu->addAction("Settings...", QKeySequence(), this, &MainWindow::onSettings);

    QMenu *viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("Zoom &In", QKeySequence::ZoomIn, this, &MainWindow::onZoomIn);
    viewMenu->addAction("Zoom &Out", QKeySequence::ZoomOut, this, &MainWindow::onZoomOut);
    viewMenu->addAction("&Fit to Window", QKeySequence("F"), this, &MainWindow::onZoomFit);
    viewMenu->addAction("Zoom to &Components", QKeySequence("Alt+F"), this, &MainWindow::onZoomAllComponents);
    viewMenu->addSeparator();
    QAction* netFocusAct = viewMenu->addAction("Highlight Selected Net");
    netFocusAct->setCheckable(true);
    netFocusAct->setShortcut(QKeySequence("H"));
    connect(netFocusAct, &QAction::toggled, this, [this](bool on) {
        m_netHighlightEnabled = on;
        if (!on) {
            clearNetHighlighting();
            statusBar()->showMessage("Net highlighting disabled", 2000);
            return;
        }
        applyNetHighlighting();
    });
    viewMenu->addAction("3D View", QKeySequence("Alt+3"), this, &MainWindow::onToggle3DView);
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Default Layout", this, [this]() {
        ensureRightBottomDockTabs();
        
        if (m_libraryDock) m_libraryDock->show();

        // Clear saved state so it sticks
        ConfigManager::instance().saveWindowState("PCBEditor", saveGeometry(), saveState());
        statusBar()->showMessage("Workspace layout reset to default tabs", 3000);
    });
    
    QMenu *routeMenu = menuBar->addMenu("&Route");
    QAction* singleRouteAct = routeMenu->addAction("Single Track");
    singleRouteAct->setShortcut(QKeySequence("X"));
    connect(singleRouteAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setCurrentTool("Trace");
    });

    QAction* diffPairAct = routeMenu->addAction("Differential Pair");
    diffPairAct->setShortcut(QKeySequence("Alt+X"));
    connect(diffPairAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setCurrentTool("Diff Pair");
    });

    QAction* lengthTuningAct = routeMenu->addAction("Interactive Length Tuning / Meander");
    lengthTuningAct->setShortcut(QKeySequence("Shift+U"));
    connect(lengthTuningAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setCurrentTool("Length Tuning");
    });

    QAction* viaAct = routeMenu->addAction("Via Placement");
    viaAct->setShortcut(QKeySequence("V"));
    connect(viaAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setCurrentTool("Via");
    });

    routeMenu->addSeparator();
    QAction* routeLenMatchAct = routeMenu->addAction("Length Matching Manager...");
    routeLenMatchAct->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(routeLenMatchAct, &QAction::triggered, this, &MainWindow::onLengthMatching);

    QAction* routeAutoRouteAct = routeMenu->addAction("Auto-Router...");
    routeAutoRouteAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(routeAutoRouteAct, &QAction::triggered, this, &MainWindow::onAutoRoute);

    QMenu *toolsMenu = menuBar->addMenu("&Tools");
    QAction* drcAction = toolsMenu->addAction("Design Rule Check");
    drcAction->setShortcut(QKeySequence("Shift+D"));
    connect(drcAction, &QAction::triggered, this, &MainWindow::onRunDRC);
    QAction* courtyardAction = toolsMenu->addAction("Courtyard Validation");
    courtyardAction->setShortcut(QKeySequence("Shift+C"));
    connect(courtyardAction, &QAction::triggered, this, &MainWindow::onRunCourtyardValidation);
    QAction* arrayAction = toolsMenu->addAction("Create Linear Array...");
    arrayAction->setShortcut(QKeySequence("Ctrl+Shift+A"));
    connect(arrayAction, &QAction::triggered, this, &MainWindow::onCreateLinearArray);
    QAction* circularArrayAction = toolsMenu->addAction("Create Circular Array...");
    circularArrayAction->setShortcut(QKeySequence("Ctrl+Alt+A"));
    connect(circularArrayAction, &QAction::triggered, this, &MainWindow::onCreateCircularArray);
    QAction* panelizeAction = toolsMenu->addAction("Panelize Board...");
    panelizeAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(panelizeAction, &QAction::triggered, this, &MainWindow::onPanelizeBoard);
    QAction* measureAction = toolsMenu->addAction("Measure Distance");
    measureAction->setShortcut(QKeySequence("M"));
    connect(measureAction, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setCurrentTool("Measure");
    });
    toolsMenu->addAction("Board Setup", this, &MainWindow::onBoardSetup);
    toolsMenu->addAction("Board Layer Stackup & Impedance...", this, &MainWindow::onLayerStackup);
    toolsMenu->addAction("Via Stitching...", this, &MainWindow::onViaStitching);
    toolsMenu->addSeparator();
    QAction* autoRouteAction = toolsMenu->addAction("Auto-Router...");
    autoRouteAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(autoRouteAction, &QAction::triggered, this, &MainWindow::onAutoRoute);

    QAction* lengthMatchAction = toolsMenu->addAction("Length Matching...");
    lengthMatchAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(lengthMatchAction, &QAction::triggered, this, &MainWindow::onLengthMatching);

    QAction* compareBoardAction = toolsMenu->addAction("Compare Board...");
    compareBoardAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(compareBoardAction, &QAction::triggered, this, &MainWindow::onCompareBoard);

    QAction* reportAction = toolsMenu->addAction("Generate Design Report...");
    reportAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(reportAction, &QAction::triggered, this, &MainWindow::onGenerateDesignReport);

    toolsMenu->addSeparator();
    QAction* syncAction = toolsMenu->addAction("🔄 Check for Schematic Updates");
    syncAction->setShortcut(QKeySequence("Ctrl+Shift+U"));
    connect(syncAction, &QAction::triggered, this, &MainWindow::handleIncomingECO);

    toolsMenu->addSeparator();
    QAction* paletteAction = toolsMenu->addAction("Command Palette...");
    paletteAction->setShortcut(QKeySequence("Ctrl+K"));
    connect(paletteAction, &QAction::triggered, this, &MainWindow::onOpenCommandPalette);
}

void MainWindow::createToolBar() {
    QToolBar *toolbar = addToolBar("Main Toolbar");
    toolbar->setObjectName("MainToolbar");
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setMovable(false);
    toolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, toolbar);
    toolbar->setStyleSheet(
        "QToolBar#MainToolbar {"
        "  background-color: #1a1a1c;"
        "  border-right: 1px solid #101010;"
        "  padding: 8px 6px;"
        "  spacing: 8px;"
        "}"
        "QToolBar#MainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#MainToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#MainToolbar QToolButton:checked, QToolBar#MainToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
    );

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    
    QStringList coreTools = {"Select", "Erase", "Zoom Area", "Trace", "Diff Pair", "Length Tuning", "Pad", "Via"};
    QStringList shapeTools = {"Rectangle", "Filled Zone", "Line", "Circle", "Arc"};
    QStringList extraTools = {"Measure"};

    QMap<QString, QIcon> toolIcons;
    toolIcons["Select"] = getThemeIcon(":/icons/tool_select.svg");
    toolIcons["Erase"] = getThemeIcon(":/icons/tool_erase.svg");
    toolIcons["Zoom Area"] = getThemeIcon(":/icons/tool_zoom_area.svg");
    toolIcons["Trace"] = getThemeIcon(":/icons/tool_wire.svg");
    toolIcons["Diff Pair"] = getThemeIcon(":/icons/tool_diff_pair.svg");
    toolIcons["Length Tuning"] = getThemeIcon(":/icons/tool_meander.svg");
    toolIcons["Pad"] = getThemeIcon(":/icons/tool_pad.svg");
    toolIcons["Via"] = getThemeIcon(":/icons/tool_via.svg");
    toolIcons["Rectangle"] = getThemeIcon(":/icons/tool_rect.svg");
    toolIcons["Filled Zone"] = getThemeIcon(":/icons/tool_polygon.svg");
    toolIcons["Line"] = getThemeIcon(":/icons/tool_line.svg");
    toolIcons["Circle"] = getThemeIcon(":/icons/tool_circle.svg");
    toolIcons["Arc"] = getThemeIcon(":/icons/tool_arc.svg");
    toolIcons["Measure"] = getThemeIcon(":/icons/tool_measure.svg");
    
    auto availableTools = PCBToolRegistry::instance().registeredTools();
    
    // 1. Add Core Tools
    for (const QString& toolName : coreTools) {
        if (availableTools.contains(toolName)) {
            QIcon icon = toolIcons.value(toolName);
            if (icon.isNull()) icon = getThemeIcon(":/icons/tool_generic.svg");
            
            QAction* action = toolbar->addAction(icon, toolName);
            action->setCheckable(true);
            action->setData(toolName);
            
            m_toolActions[toolName] = action;
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
        }
    }

    // 2. Add Shape Tools Group
    QToolButton* shapesBtn = new QToolButton(toolbar);
    shapesBtn->setIcon(getThemeIcon(":/icons/tool_rect.svg"));
    shapesBtn->setToolTip("Geometric Shapes...");
    shapesBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* shapesMenu = new QMenu(shapesBtn);
    for (const QString& toolName : shapeTools) {
        if (availableTools.contains(toolName)) {
            QAction* action = new QAction(toolIcons.value(toolName), toolName, this);
            action->setCheckable(true);
            action->setData(toolName);
            m_toolActions[toolName] = action;
            toolGroup->addAction(action);
            shapesMenu->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
            
            // If sub-tool selected, update the main button icon
            connect(action, &QAction::triggered, this, [shapesBtn, action](){
                shapesBtn->setIcon(action->icon());
            });
        }
    }
    shapesBtn->setMenu(shapesMenu);
    toolbar->addWidget(shapesBtn);

    // 3. Add Extra Tools
    for (const QString& toolName : extraTools) {
        if (availableTools.contains(toolName)) {
            QAction* action = toolbar->addAction(toolIcons.value(toolName), toolName);
            action->setCheckable(true);
            action->setData(toolName);
            m_toolActions[toolName] = action;
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
        }
    }

    if (m_toolActions.contains("Select")) {
        m_toolActions["Select"]->setChecked(true);
    }

    toolbar->addSeparator();

    // 4. Component Tool
    if (availableTools.contains("Component")) {
        QAction* action = toolbar->addAction(getThemeIcon(":/icons/comp_ic.svg"), "Component");
        action->setCheckable(true);
        action->setData("Component");
        m_toolActions["Component"] = action;
        toolGroup->addAction(action);
        connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
        toolbar->addSeparator();
    }

    // 5. More Tools Button (Global Access)
    QToolButton* moreBtn = new QToolButton(toolbar);
    moreBtn->setObjectName("MoreToolsButton");
    moreBtn->setIcon(getThemeIcon(":/icons/chevron_down.svg"));
    moreBtn->setToolTip("All Tools...");
    moreBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* moreMenu = new QMenu(moreBtn);
    connect(moreMenu, &QMenu::aboutToShow, this, [this, moreMenu, coreTools, shapeTools, extraTools]() {
        moreMenu->clear();
        QStringList all = coreTools + shapeTools + extraTools;
        all << "Component";
        for (const QString& key : all) {
            QAction* a = m_toolActions.value(key, nullptr);
            if (!a) continue;
            QAction* proxy = moreMenu->addAction(a->icon(), a->text(), [a]() { a->trigger(); });
            proxy->setCheckable(true);
            proxy->setChecked(a->isChecked());
        }
    });
    moreBtn->setMenu(moreMenu);
    toolbar->addWidget(moreBtn);

    // 6. Explicitly style the standard Qt extension button if it appears
    QTimer::singleShot(0, this, [this, toolbar]() {
        if (QToolButton* extBtn = toolbar->findChild<QToolButton*>("qt_toolbar_ext_button")) {
            extBtn->setIcon(getThemeIcon(":/icons/chevron_down.svg"));
            extBtn->setToolTip("Hidden Tools");
            extBtn->setStyleSheet("QToolButton { background: #2d2d30; border-radius: 4px; padding: 2px; }");
        }
    });

    // Undo/Redo buttons for quick access
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);

    QAction* deleteAction = toolbar->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete");
    deleteAction->setToolTip("Delete selected items (Del / Bksp)");
    deleteAction->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelection);

    toolbar->addSeparator();
    QAction* drcToolbarAction = toolbar->addAction(getThemeIcon(":/icons/check.svg"), "DRC");
    drcToolbarAction->setToolTip("Run Design Rule Check (Shift+D)");
    connect(drcToolbarAction, &QAction::triggered, this, &MainWindow::onRunDRC);

    // Layer shortcuts
    QAction* layer1Act = new QAction(this);
    layer1Act->setShortcut(QKeySequence("1"));
    connect(layer1Act, &QAction::triggered, this, [this](){ onActiveLayerChanged(0); m_layerPanel->selectLayer(0); });
    addAction(layer1Act);

    QAction* layer2Act = new QAction(this);
    layer2Act->setShortcut(QKeySequence("2"));
    connect(layer2Act, &QAction::triggered, this, [this](){ onActiveLayerChanged(1); m_layerPanel->selectLayer(1); });
    addAction(layer2Act);
    
    QAction* rotateAction = toolbar->addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate");
    rotateAction->setToolTip("Rotate selected items (R)");
    rotateAction->setShortcut(QKeySequence("R"));
    connect(rotateAction, &QAction::triggered, this, &MainWindow::onRotate);
    addAction(rotateAction);
    
    QAction* mirrorAction = toolbar->addAction(getThemeIcon(":/icons/flip_h.svg"), "Mirror");
    mirrorAction->setToolTip("Mirror selected items (H)");
    mirrorAction->setShortcut(QKeySequence("H"));
    connect(mirrorAction, &QAction::triggered, this, &MainWindow::onMirror);
    addAction(mirrorAction);

    QAction* flipAction = toolbar->addAction(getThemeIcon(":/icons/flip_v.svg"), "Flip Layer");
    flipAction->setToolTip("Flip selected items to opposite layer (F)");
    flipAction->setShortcut(QKeySequence("F"));
    connect(flipAction, &QAction::triggered, this, &MainWindow::onFlip);
    addAction(flipAction);

    toolbar->addSeparator();

    QAction* snapAction = toolbar->addAction(getThemeIcon(":/icons/snap_grid.svg"), "Snap Grid");
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    snapAction->setToolTip("Enable/Disable Grid Snapping (S)");
    snapAction->setShortcut(QKeySequence("S"));
    connect(snapAction, &QAction::toggled, this, [this](bool checked){
        if (m_view) m_view->setSnapToGrid(checked);
    });

    toolbar->addSeparator();

    QAction* zoomInAction = toolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In"); 
    zoomInAction->setToolTip("Zoom in (+)");
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);
    QAction* zoomOutAction = toolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out"); 
    zoomOutAction->setToolTip("Zoom out (-)");
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
    QAction* zoomFitAction = toolbar->addAction(getThemeIcon(":/icons/view_fit.svg"), "Fit All"); 
    zoomFitAction->setToolTip("Fit all items to window (F)");
    zoomFitAction->setShortcut(QKeySequence("F"));
    connect(zoomFitAction, &QAction::triggered, this, &MainWindow::onZoomFit);

    QAction* zoomCompAction = toolbar->addAction(getThemeIcon(":/icons/view_zoom_components.svg"), "Zoom Components");
    zoomCompAction->setToolTip("Zoom to fit all components (Alt+F)");
    zoomCompAction->setShortcut(QKeySequence("Alt+F"));
    connect(zoomCompAction, &QAction::triggered, this, &MainWindow::onZoomAllComponents);

    QAction* zoomSelAction = toolbar->addAction(getThemeIcon(":/icons/view_zoom_selection.svg"), "Zoom Selection");
    zoomSelAction->setToolTip("Zoom to fit selected items (Ctrl+0)");
    zoomSelAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(zoomSelAction, &QAction::triggered, this, &MainWindow::onZoomSelection);

    // ─── Property Bar (Dynamic Ribbon) ──────────────────────────────────────
    m_propertyBar = new QToolBar("Selection Properties", this);
    m_propertyBar->setObjectName("PropertyBar");
    m_propertyBar->setIconSize(QSize(18, 18));
    m_propertyBar->setMovable(false);
    m_propertyBar->setFixedHeight(40);
    m_propertyBar->setStyleSheet(
        "QToolBar#PropertyBar {"
        "  background: #1e1e20;"
        "  border-bottom: 1px solid #333336;"
        "  spacing: 15px;"
        "  padding-left: 15px;"
        "}"
        "QLabel { color: #3b82f6; font-weight: 600; font-size: 11px; text-transform: uppercase; }"
        "QLineEdit, QComboBox, QDoubleSpinBox {"
        "  background: #121214;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 4px;"
        "  padding: 3px 8px;"
        "  color: #ffffff;"
        "  min-width: 80px;"
        "}"
        "QLineEdit:focus { border-color: #3b82f6; }"
    );
    addToolBar(Qt::TopToolBarArea, m_propertyBar);
    
    updatePropertyBar();

    toolbar->addSeparator();

    QAction* view3DAct = toolbar->addAction(getThemeIcon(":/icons/tool_3d.svg"), "3D View");
    view3DAct->setToolTip("Open PCB 3D Preview (Alt+3)");
    view3DAct->setShortcut(QKeySequence("Alt+3"));
    connect(view3DAct, &QAction::triggered, this, &MainWindow::onToggle3DView);

    // ─── Top Main Menu Bar Replacement ──────────────────────────────────────────
    QToolBar *topToolbar = addToolBar("Top Main Config");
    topToolbar->setObjectName("TopMainToolbar");
    topToolbar->setIconSize(QSize(20, 20));
    topToolbar->setMovable(false);
    topToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    
    if (m_undoAction) {
        m_undoAction->setToolTip("Undo last action (Ctrl+Z)");
        topToolbar->addAction(m_undoAction);
    }
    if (m_redoAction) {
        m_redoAction->setToolTip("Redo last action (Ctrl+Shift+Z)");
        topToolbar->addAction(m_redoAction);
    }
    topToolbar->addSeparator();
    topToolbar->setStyleSheet(
        "QToolBar#TopMainToolbar {"
        "  background-color: #2d2d30;"
        "  border-bottom: 1px solid #1e1e1e;"
        "  padding: 4px 8px;"
        "  spacing: 3px;"
        "}"
        "QToolBar#TopMainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 3px;"
        "  padding: 4px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#TopMainToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#TopMainToolbar QToolButton:checked, QToolBar#TopMainToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "}"
        "QToolBar#TopMainToolbar QLabel {"
        "  color: #888;"
        "  font-size: 11px;"
        "}"
        "QToolBar#TopMainToolbar QComboBox {"
        "  background-color: #1e1e1e;"
        "  color: #cccccc;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 3px;"
        "  padding: 2px 6px;"
        "  font-size: 11px;"
        "}"
    );

    // Grid size to Top Toolbar
    topToolbar->addSeparator();
    topToolbar->addWidget(new QLabel("  Grid: "));
    auto* gridCombo = new QComboBox();
    gridCombo->addItems({"0.1", "0.5", "1.0", "2.5", "5.0", "10.0", "25.0", "50.0"});
    gridCombo->setCurrentText(QString::number(1.0, 'f', 1));
    gridCombo->setFixedWidth(60);
    connect(gridCombo, &QComboBox::currentTextChanged, this, [this](const QString& text){
        if (m_view) {
            m_view->setGridSize(text.toDouble());
        }
    });
    topToolbar->addWidget(gridCombo);

    // Zoom & View controls (match schematic editor experience)
    topToolbar->addSeparator();

    QAction* topZoomInAct = topToolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    connect(topZoomInAct, &QAction::triggered, this, &MainWindow::onZoomIn);

    QAction* topZoomOutAct = topToolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    connect(topZoomOutAct, &QAction::triggered, this, &MainWindow::onZoomOut);

    QAction* topFitAct = topToolbar->addAction(getThemeIcon(":/icons/view_fit.svg"), "Fit All");
    connect(topFitAct, &QAction::triggered, this, &MainWindow::onZoomFit);

    QAction* topCompAct = topToolbar->addAction(getThemeIcon(":/icons/view_zoom_components.svg"), "Zoom Components");
    connect(topCompAct, &QAction::triggered, this, &MainWindow::onZoomAllComponents);

    QAction* topZoomSelAct = topToolbar->addAction(getThemeIcon(":/icons/view_zoom_selection.svg"), "Zoom Selection");
    connect(topZoomSelAct, &QAction::triggered, this, &MainWindow::onZoomSelection);

    QAction* topZoomAreaAct = topToolbar->addAction(getThemeIcon(":/icons/tool_zoom_area.svg"), "Zoom Area");
    topZoomAreaAct->setToolTip("Drag a rectangle to zoom in (Z)");
    connect(topZoomAreaAct, &QAction::triggered, this, [this]() {
        if (m_view) {
            m_view->setCurrentTool("Zoom Area");
        }
    });

    topToolbar->addSeparator();

    QAction* top3DAct = topToolbar->addAction(getThemeIcon(":/icons/tool_3d.svg"), "3D View");
    top3DAct->setToolTip("Open PCB 3D Preview (Alt+3)");
    connect(top3DAct, &QAction::triggered, this, &MainWindow::onToggle3DView);

    // --- PANEL TOGGLES (VS CODE STYLE) ---
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topToolbar->addWidget(spacer);

    auto addPanelToggle = [&](const QString& iconName, const QString& tooltip, auto slot) {
        QToolButton* btn = new QToolButton(this);
        btn->setIcon(createPCBIcon(iconName));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setChecked(true);
        connect(btn, &QToolButton::clicked, this, slot);
        topToolbar->addWidget(btn);
        return btn;
    };

    addPanelToggle("Panel Sidebar Left", "Toggle Left Sidebar", &MainWindow::onToggleLeftSidebar);
    addPanelToggle("Panel Bottom", "Toggle Bottom Panel (DRC)", &MainWindow::onToggleBottomPanel);
    addPanelToggle("Panel Sidebar Right", "Toggle Right Sidebar", &MainWindow::onToggleRightSidebar);

    // ─── Options Toolbar (Context Settings) ──────────────────────────────────
    m_optionsToolbar = new QToolBar("Tool Settings", this);
    m_optionsToolbar->setObjectName("OptionsToolbar");
    m_optionsToolbar->setIconSize(QSize(18, 18));
    m_optionsToolbar->setMovable(false);
    m_optionsToolbar->setFixedHeight(40);
    m_optionsToolbar->setStyleSheet(
        "QToolBar#OptionsToolbar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #252528, stop:1 #1e1e20);"
        "  border-bottom: 1px solid #333336;"
        "  spacing: 12px;"
        "  padding-left: 10px;"
        "}"
        "QLabel { color: #aaaaaa; font-weight: 500; font-size: 11px; }"
        "QComboBox, QSpinBox, QDoubleSpinBox {"
        "  background: #2d2d30;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  color: #eeeeee;"
        "}"
    );
    addToolBar(Qt::TopToolBarArea, m_optionsToolbar);
    insertToolBarBreak(m_optionsToolbar);

    // ─── Layout Toolbar (Alignment & Distribution) ──────────────────────────
    QToolBar *layoutToolbar = addToolBar("Layout");
    layoutToolbar->setObjectName("LayoutToolbar");
    layoutToolbar->setIconSize(QSize(20, 20));
    layoutToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    layoutToolbar->setMovable(false);
    layoutToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, layoutToolbar);
    layoutToolbar->setStyleSheet(
        "QToolBar#LayoutToolbar {"
        "  background-color: #1a1a1c;"
        "  border-right: 1px solid #101010;"
        "  padding: 8px 6px;"
        "  spacing: 8px;"
        "}"
        "QToolBar#LayoutToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#LayoutToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
    );
    auto addAlignAct = [this, layoutToolbar](const QString& text, const QString& tooltip, const QKeySequence& shortcut, auto slot) {
        QAction* act = layoutToolbar->addAction(createPCBIcon(text), text);
        act->setToolTip(tooltip);
        if (!shortcut.isEmpty()) {
            act->setShortcut(shortcut);
            addAction(act);
        }
        connect(act, &QAction::triggered, this, slot);
    };

    addAlignAct("Align Left", "Align Left (Ctrl+Alt+Left)", QKeySequence("Ctrl+Alt+Left"), &MainWindow::onAlignLeft);
    addAlignAct("Align Right", "Align Right (Ctrl+Alt+Right)", QKeySequence("Ctrl+Alt+Right"), &MainWindow::onAlignRight);
    addAlignAct("Align Top", "Align Top (Ctrl+Alt+Up)", QKeySequence("Ctrl+Alt+Up"), &MainWindow::onAlignTop);
    addAlignAct("Align Bottom", "Align Bottom (Ctrl+Alt+Down)", QKeySequence("Ctrl+Alt+Down"), &MainWindow::onAlignBottom);
    
    layoutToolbar->addSeparator();

    addAlignAct("Center X", "Center Horizontal (Ctrl+Alt+H)", QKeySequence("Ctrl+Alt+H"), &MainWindow::onAlignCenterX);
    addAlignAct("Center Y", "Center Vertical (Ctrl+Alt+V)", QKeySequence("Ctrl+Alt+V"), &MainWindow::onAlignCenterY);
    
    layoutToolbar->addSeparator();

    addAlignAct("Distribute H", "Distribute Horizontally", QKeySequence(), &MainWindow::onDistributeH);
    addAlignAct("Distribute V", "Distribute Vertically", QKeySequence(), &MainWindow::onDistributeV);
}

void MainWindow::ensureRightBottomDockTabs() {
    if (!m_layerDock || !m_propertiesDock || !m_drcDock || !m_geminiDock) {
        return;
    }

    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);
    tabifyDockWidget(m_layerDock, m_propertiesDock);
    tabifyDockWidget(m_layerDock, m_drcDock);
    tabifyDockWidget(m_layerDock, m_geminiDock);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::South);
    m_layerDock->raise();
}

void MainWindow::ensureGeminiPanelInitialized() {
    if (m_geminiPanel) return;

    auto* scroll = qobject_cast<QScrollArea*>(m_geminiDock->widget());
    if (!scroll) return;

    if (QWidget* oldWidget = scroll->takeWidget()) {
        oldWidget->deleteLater();
    }

    m_geminiPanel = new GeminiPanel(m_scene, this);
    m_geminiPanel->setMode("pcb");
    m_geminiPanel->setUndoStack(m_undoStack);
    connect(m_geminiPanel, &GeminiPanel::snippetGenerated, this, &MainWindow::onSnippetGenerated);

    scroll->setWidget(m_geminiPanel);
}

void MainWindow::createDockWidgets() {
    // === Layer Dock ===
    m_layerDock = new QDockWidget("Layers", this);
    m_layerDock->setObjectName("LayerDock");
    m_layerPanel = new PCBLayerPanel(m_layerDock);

    QWidget* layerContainer = new QWidget(this);
    QVBoxLayout* layerLayout = new QVBoxLayout(layerContainer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(0);

    m_selectionFilter = new SelectionFilterWidget(this);
    connect(m_selectionFilter, &SelectionFilterWidget::filterChanged, this, &MainWindow::onFilterChanged);

    layerLayout->addWidget(m_layerPanel, 1);
    layerLayout->addWidget(m_selectionFilter);

    m_layerDock->setWidget(layerContainer);

    connect(m_layerPanel, &PCBLayerPanel::activeLayerChanged, 
            this, &MainWindow::onActiveLayerChanged);
    connect(m_layerPanel, &PCBLayerPanel::layerVisibilityChanged,
            this, [this](int, bool) { updateGrid(); });

    // === DRC Dock ===
    m_drcDock = new QDockWidget("Design Rule Check", this);
    m_drcDock->setObjectName("DRCDock");
    m_drcPanel = new PCBDRCPanel(m_drcDock);
    m_drcPanel->setScene(m_scene);
    m_drcDock->setWidget(m_drcPanel);

    connect(m_drcPanel, &PCBDRCPanel::violationSelected,
            this, &MainWindow::onDRCViolationSelected);

    // === Properties Dock ===
    m_propertiesDock = new QDockWidget("Properties", this);
    m_propertiesDock->setObjectName("PropertiesDock");
    m_propertyEditor = new Flux::PCBPropertyEditor();
    m_propertiesDock->setWidget(m_propertyEditor);

    connect(m_propertyEditor, &Flux::PCBPropertyEditor::propertyChanged, this, &MainWindow::onPropertyChanged);

    // === Gemini Assistant Dock (Lazy-initialized on visibility) ===
    m_geminiDock = new QDockWidget("Gemini Assistant", this);
    m_geminiDock->setObjectName("GeminiDock");
    m_geminiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    QScrollArea* geminiScroll = new QScrollArea(this);
    geminiScroll->setWidgetResizable(true);
    geminiScroll->setFrameShape(QFrame::NoFrame);
    geminiScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QLabel* placeholder = new QLabel("AI Assistant panel will initialize when opened.", geminiScroll);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    placeholder->setStyleSheet("color: #888888; padding: 24px;");

    geminiScroll->setWidget(placeholder);
    m_geminiDock->setWidget(geminiScroll);

    connect(m_geminiDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            ensureGeminiPanelInitialized();
        }
    });

    // === Tabify All Right Docks ===
    ensureRightBottomDockTabs();

    // === Left Side: Library ===
    m_libraryDock = new QDockWidget("Component Library", this);
    m_libraryDock->setObjectName("LibraryDock");
    m_componentsPanel = new PCBComponentsWidget(this);
    m_libraryDock->setWidget(m_componentsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_libraryDock);

    connect(m_componentsPanel, &PCBComponentsWidget::footprintSelected, this, [this](const QString& fpName){
        m_view->setCurrentTool("Component");
        PCBTool* current = m_view->currentTool();
        if (PCBComponentTool* compTool = dynamic_cast<PCBComponentTool*>(current)) {
             compTool->setComponentType(fpName);
             statusBar()->showMessage("Selected component: " + fpName, 3000);
        }

        if (m_toolActions.contains("Component")) {
             m_toolActions["Component"]->setChecked(true);
        }
    });

    connect(m_componentsPanel, &PCBComponentsWidget::footprintCreated, this, &MainWindow::onOpenFootprintEditor);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this](){
        if (!m_scene) return;
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        QList<PCBItem*> pItems;
        for (auto* item : selected) {
            if (auto* pi = dynamic_cast<PCBItem*>(item)) {
                if (dynamic_cast<PCBItem*>(pi->parentItem()) == nullptr || pi->itemType() == PCBItem::PadType) {
                    pItems.append(pi);
                }
            }
        }

        if (!pItems.isEmpty()) {
            m_propertyEditor->setPCBItems(pItems);
            if (m_propertiesDock) {
                m_propertiesDock->show();
                m_propertiesDock->raise();
            }

            // Cross-probing trigger from 2D PCB view to Schematic
            QString refDes;
            QString netName;
            for (auto* pi : pItems) {
                if (auto* comp = dynamic_cast<ComponentItem*>(pi)) {
                    refDes = comp->name().trimmed();
                    if (!refDes.isEmpty()) break;
                } else if (netName.isEmpty() && !pi->netName().trimmed().isEmpty()) {
                    netName = pi->netName().trimmed();
                }
            }
            if (!refDes.isEmpty() || !netName.isEmpty()) {
                SyncManager::instance().pushCrossProbe(refDes, netName);
            }
        } else {
            m_propertyEditor->clear();
        }

        if (m_netHighlightEnabled) {
            applyNetHighlighting();
        }

        updateSelectionQuickInfo(pItems);
    });
}

void MainWindow::createStatusBar() {
    m_coordLabel = new QLabel("X: 0.0mm  Y: 0.0mm");
    m_coordLabel->setMinimumWidth(180);
    m_coordLabel->setStyleSheet("QLabel { padding: 4px 12px; font-weight: 500; }");
    
    // Grid Size Selector
    m_gridCombo = new QComboBox();
    m_gridCombo->addItem("Grid: 0.01mm", 0.01);
    m_gridCombo->addItem("Grid: 0.05mm", 0.05);
    m_gridCombo->addItem("Grid: 0.1mm", 0.1);
    m_gridCombo->addItem("Grid: 0.25mm", 0.25);
    m_gridCombo->addItem("Grid: 0.5mm", 0.5);
    m_gridCombo->addItem("Grid: 1.0mm", 1.0);
    m_gridCombo->addItem("Grid: 1.27mm", 1.27);
    m_gridCombo->addItem("Grid: 2.54mm", 2.54);
    m_gridCombo->addItem("Grid: 5.0mm", 5.0);
    m_gridCombo->setCurrentIndex(7); // Default to 2.54mm
    m_gridCombo->setToolTip("Select Snap Grid Size");
    m_gridCombo->setStyleSheet(
        "QComboBox { border: none; padding: 2px 10px; background: transparent; font-weight: 500; min-width: 120px; }"
        "QComboBox:hover { background: #2d2d30; }"
        "QComboBox::drop-down { border: none; }"
    );
    
    connect(m_gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
        double size = m_gridCombo->itemData(index).toDouble();
        if (m_view) m_view->setGridSize(size);
    });

    // Layer Switcher
    m_layerCombo = new QComboBox();
    for (const auto& layer : PCBLayerManager::instance().layers()) {
        if (layer.isCopperLayer()) {
            m_layerCombo->addItem(layer.name(), layer.id());
        }
    }
    m_layerCombo->setToolTip("Select Active Routing Layer");
    m_layerCombo->setStyleSheet(
        "QComboBox { border: none; padding: 2px 10px; background: transparent; font-weight: 500; min-width: 140px; color: #ef4444; }"
        "QComboBox:hover { background: #2d2d30; }"
        "QComboBox::drop-down { border: none; }"
    );
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
        int layerId = m_layerCombo->itemData(index).toInt();
        onActiveLayerChanged(layerId);
        if (m_layerPanel) m_layerPanel->selectLayer(layerId);
    });

    m_layerLabel = new QLabel("Layer:");
    m_layerLabel->setStyleSheet("QLabel { padding: 0 0 0 12px; }");

    m_selectionInfoLabel = new QLabel("Selection: none");
    m_selectionInfoLabel->setMinimumWidth(320);
    m_selectionInfoLabel->setStyleSheet("QLabel { padding: 4px 12px; }");

    // Track Width Quick Preset Selector
    QComboBox* widthCombo = new QComboBox();
    widthCombo->addItem("Trace: 0.15mm (Signal)", 0.15);
    widthCombo->addItem("Trace: 0.25mm (Std)", 0.25);
    widthCombo->addItem("Trace: 0.50mm (Medium)", 0.50);
    widthCombo->addItem("Trace: 1.00mm (Power)", 1.00);
    widthCombo->addItem("Trace: 2.00mm (High Power)", 2.00);
    widthCombo->setCurrentIndex(1);
    widthCombo->setToolTip("Quick Track Width Selection");
    widthCombo->setStyleSheet("QComboBox { border: none; padding: 2px 8px; background: transparent; font-weight: 500; min-width: 140px; color: #10b981; } QComboBox:hover { background: #2d2d30; }");
    connect(widthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, widthCombo](int index){
        double w = widthCombo->itemData(index).toDouble();
        if (m_view && m_view->currentTool()) {
            m_view->currentTool()->setToolProperty("Width (mm)", w);
        }
    });

    // Via Size Quick Preset Selector
    QComboBox* viaCombo = new QComboBox();
    viaCombo->addItem("Via: 0.6 / 0.3 mm (Micro)", QPointF(0.6, 0.3));
    viaCombo->addItem("Via: 0.8 / 0.4 mm (Std)", QPointF(0.8, 0.4));
    viaCombo->addItem("Via: 1.0 / 0.5 mm (Large)", QPointF(1.0, 0.5));
    viaCombo->addItem("Via: 1.2 / 0.6 mm (Power)", QPointF(1.2, 0.6));
    viaCombo->setCurrentIndex(1);
    viaCombo->setToolTip("Quick Via Diameter & Drill Selection");
    viaCombo->setStyleSheet("QComboBox { border: none; padding: 2px 8px; background: transparent; font-weight: 500; min-width: 145px; color: #f59e0b; } QComboBox:hover { background: #2d2d30; }");
    connect(viaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, viaCombo](int index){
        QPointF sizes = viaCombo->itemData(index).toPointF();
        if (m_view && m_view->currentTool()) {
            m_view->currentTool()->setToolProperty("Via Diameter (mm)", sizes.x());
            m_view->currentTool()->setToolProperty("Via Drill (mm)", sizes.y());
        }
    });

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addWidget(createStatusSeparator());
    statusBar()->addPermanentWidget(m_gridCombo);
    statusBar()->addPermanentWidget(widthCombo);
    statusBar()->addPermanentWidget(viaCombo);
    statusBar()->addWidget(createStatusSeparator());
    statusBar()->addPermanentWidget(m_layerLabel);
    statusBar()->addPermanentWidget(m_layerCombo);
    statusBar()->addWidget(createStatusSeparator());
    statusBar()->addPermanentWidget(m_selectionInfoLabel, 1);
    statusBar()->addWidget(createStatusSeparator());

    QPushButton* themeBtn = new QPushButton("Theme");
    themeBtn->setFlat(true);
    themeBtn->setCursor(Qt::PointingHandCursor);
    themeBtn->setStyleSheet("QPushButton { color: #a1a1aa; font-weight: bold; border: none; padding: 0 5px; } QPushButton:hover { color: white; }");
    connect(themeBtn, &QPushButton::clicked, this, []() {
        auto& tm = ThemeManager::instance();
        if (tm.currentTheme()->type() == PCBTheme::Engineering) tm.setTheme(PCBTheme::Dark);
        else if (tm.currentTheme()->type() == PCBTheme::Dark) tm.setTheme(PCBTheme::Light);
        else tm.setTheme(PCBTheme::Engineering);
    });
    statusBar()->addPermanentWidget(themeBtn);
}

void MainWindow::updateSelectionQuickInfo(const QList<PCBItem*>& items) {
    if (!m_selectionInfoLabel) {
        return;
    }

    if (items.isEmpty()) {
        m_selectionInfoLabel->setText("Selection: none");
        return;
    }

    if (items.size() == 1 && items.first()) {
        m_selectionInfoLabel->setText(selectionQuickInfoText(items.first()));
        return;
    }

    QMap<QString, int> typeCounts;
    for (PCBItem* item : items) {
        if (item) {
            typeCounts[item->itemTypeName()] += 1;
        }
    }

    QStringList parts;
    for (auto it = typeCounts.cbegin(); it != typeCounts.cend(); ++it) {
        parts << QString("%1 %2").arg(it.value()).arg(it.key());
    }

    m_selectionInfoLabel->setText(QString("Selection: %1 item(s) | %2")
        .arg(items.size())
        .arg(parts.join(", ")));
}

QString MainWindow::selectionQuickInfoText(PCBItem* item) const {
    if (!item) {
        return "Selection: none";
    }

    auto layerNameFor = [](int layerId) -> QString {
        if (PCBLayer* layer = PCBLayerManager::instance().layer(layerId)) {
            return layer->name();
        }
        return QString("Layer %1").arg(layerId);
    };
    auto fmt = [](double value) -> QString {
        return QString::number(value, 'f', 2);
    };

    const QString layerName = layerNameFor(item->layer());

    if (auto* trace = dynamic_cast<TraceItem*>(item)) {
        const double length = QLineF(trace->startPoint(), trace->endPoint()).length();
        return QString("Trace | L: %1 mm | W: %2 mm | Layer: %3")
            .arg(fmt(length))
            .arg(fmt(trace->width()))
            .arg(layerName);
    }

    if (auto* shape = dynamic_cast<PCBShapeItem*>(item)) {
        const QSizeF size = shape->sizeMm();
        QString text = QString("%1 | W: %2 mm | H: %3 mm | Stroke: %4 mm | Layer: %5")
            .arg(shape->shapeKindName())
            .arg(fmt(size.width()))
            .arg(fmt(size.height()))
            .arg(fmt(shape->strokeWidth()))
            .arg(layerName);
        if (shape->shapeKind() == PCBShapeItem::ShapeKind::Arc) {
            text += QString(" | Start: %1° | Span: %2°")
                .arg(fmt(shape->startAngleDeg()))
                .arg(fmt(shape->spanAngleDeg()));
        }
        return text;
    }

    if (auto* pour = dynamic_cast<CopperPourItem*>(item)) {
        const QRectF bounds = pour->polygon().boundingRect();
        return QString("Shape | W: %1 mm | H: %2 mm | Clearance: %3 mm | Layer: %4")
            .arg(fmt(bounds.width()))
            .arg(fmt(bounds.height()))
            .arg(fmt(pour->clearance()))
            .arg(layerName);
    }

    if (auto* image = dynamic_cast<PCBImageItem*>(item)) {
        const QSizeF size = image->sizeMm();
        return QString("Image | W: %1 mm | H: %2 mm | Layer: %3")
            .arg(fmt(size.width()))
            .arg(fmt(size.height()))
            .arg(layerName);
    }

    if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
        const QSizeF size = comp->size();
        return QString("Component %1 | W: %2 mm | H: %3 mm | Layer: %4")
            .arg(comp->componentType())
            .arg(fmt(size.width()))
            .arg(fmt(size.height()))
            .arg(layerName);
    }

    if (auto* pad = dynamic_cast<PadItem*>(item)) {
        const QSizeF size = pad->size();
        return QString("Pad | W: %1 mm | H: %2 mm | Drill: %3 mm | Layer: %4")
            .arg(fmt(size.width()))
            .arg(fmt(size.height()))
            .arg(fmt(pad->drillSize()))
            .arg(layerName);
    }

    if (auto* via = dynamic_cast<ViaItem*>(item)) {
        return QString("Via | Dia: %1 mm | Drill: %2 mm | %3-%4")
            .arg(fmt(via->diameter()))
            .arg(fmt(via->drillSize()))
            .arg(layerNameFor(via->startLayer()))
            .arg(layerNameFor(via->endLayer()));
    }

    return QString("%1 | Layer: %2").arg(item->itemTypeName()).arg(layerName);
}

QWidget* MainWindow::createStatusSeparator() {
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet("QFrame { color: #3f3f46; margin: 4px 0px; }");
    return separator;
}

QIcon MainWindow::getThemeIcon(const QString& path) {
    QIcon icon(path);
    if (!ThemeManager::theme()) return icon;

    // List of icons that should keep their original multi-color design
    static const QStringList multiColorIcons = {
        "probe", "ammeter", "voltmeter", "power_meter", "scissor", "n-v-probe", "p-v-probe", "tool_pad.svg"
    };

    bool isMultiColor = false;
    for (const auto& tag : multiColorIcons) {
        if (path.contains(tag, Qt::CaseInsensitive)) {
            isMultiColor = true;
            break;
        }
    }

    if (isMultiColor) {
        return icon;
    }

    // Tint monochrome icons for the active theme so they remain visible on both
    // light and dark backgrounds.
    QPixmap pixmap = icon.pixmap(QSize(32, 32));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), ThemeManager::theme()->textColor());
    painter.end();
    return QIcon(pixmap);
}

QIcon MainWindow::createPCBIcon(const QString& name) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor color = Qt::white;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->textColor();
    }
    QPen pen(color, 2);
    painter.setPen(pen);

    if (name == "Align Left") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawRect(6, 8, 10, 4);
        painter.drawRect(6, 16, 20, 4);
    } else if (name == "Align Right") {
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(18, 8, 10, 4);
        painter.drawRect(8, 16, 20, 4);
    } else if (name == "Align Top") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawRect(8, 6, 4, 10);
        painter.drawRect(16, 6, 4, 20);
    } else if (name == "Align Bottom") {
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(8, 18, 4, 10);
        painter.drawRect(16, 8, 4, 20);
    } else if (name == "Center X") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(16, 2, 16, 30);
        painter.setPen(pen);
        painter.drawRect(10, 8, 12, 4);
        painter.drawRect(6, 18, 20, 4);
    } else if (name == "Center Y") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(2, 16, 30, 16);
        painter.setPen(pen);
        painter.drawRect(8, 10, 4, 12);
        painter.drawRect(18, 6, 4, 20);
    } else if (name == "Distribute H") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(10, 12, 4, 8);
        painter.drawRect(18, 12, 4, 8);
    } else if (name == "Distribute V") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(12, 10, 8, 4);
        painter.drawRect(12, 18, 8, 4);
    } else if (name == "Panel Sidebar Left") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(6, 8, 6, 16);
    } else if (name == "Panel Bottom") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(6, 18, 20, 6);
    } else if (name == "Panel Sidebar Right") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(20, 8, 6, 16);
    } else {
        painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1));
    }

    return QIcon(pixmap);
}

void MainWindow::updateOptionsBar(const QString& toolName) {
    if (!m_optionsToolbar) return;
    
    // Sync toolbar buttons visually
    if (m_toolActions.contains(toolName)) {
        m_toolActions[toolName]->setChecked(true);
    }
    
    m_optionsToolbar->clear();
    
    QLabel* title = new QLabel("<b>" + toolName.toUpper() + " SETTINGS:</b>  ");
    title->setStyleSheet("color: #ec4899; margin-right: 5px;"); // Pink accent for PCB
    m_optionsToolbar->addWidget(title);

    if (toolName == "Trace") {
        PCBTool* tool = m_view->currentTool();
        double currentWidth = tool->toolProperties().value("Trace Width (mm)", 0.25).toDouble();
        int currentLayer = tool->toolProperties().value("Active Layer", 0).toInt();

        m_optionsToolbar->addWidget(new QLabel("Width: "));
        QDoubleSpinBox* widthSpin = new QDoubleSpinBox();
        widthSpin->setRange(0.05, 10.0);
        widthSpin->setSingleStep(0.05);
        widthSpin->setValue(currentWidth);
        widthSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(widthSpin);
        
        connect(widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
            if (m_view && m_view->currentTool()) {
                m_view->currentTool()->setToolProperty("Trace Width (mm)", val);
                // If we have items selected, apply to them too
                if (!m_scene->selectedItems().isEmpty()) {
                    onPropertyChanged("Width (mm)", val);
                }
            }
        });
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Layer: "));
        QComboBox* layerCombo = new QComboBox();
        layerCombo->addItems({"Top Copper", "Bottom Copper"});
        layerCombo->setCurrentIndex(currentLayer);
        m_optionsToolbar->addWidget(layerCombo);
        
        connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
            onActiveLayerChanged(idx);
            if (m_layerPanel) m_layerPanel->selectLayer(idx);
            // If we have items selected, apply to them too
            if (!m_scene->selectedItems().isEmpty()) {
                onPropertyChanged("Layer", idx);
            }
        });
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Mode: "));
        QComboBox* modeCombo = new QComboBox();
        modeCombo->addItems({"45 Degree", "90 Degree", "Free"});
        m_optionsToolbar->addWidget(modeCombo);

        m_optionsToolbar->addSeparator();
        
        QCheckBox* shoveCheck = new QCheckBox("Shove Traces");
        bool isShoveEnabled = tool->toolProperties().value("Enable Shoving", true).toBool();
        shoveCheck->setChecked(isShoveEnabled);
        m_optionsToolbar->addWidget(shoveCheck);
        connect(shoveCheck, &QCheckBox::toggled, this, [tool](bool checked){
            tool->setToolProperty("Enable Shoving", checked);
        });
    } 
    else if (toolName == "Pad") {
        m_optionsToolbar->addWidget(new QLabel("Size X: "));
        QDoubleSpinBox* sx = new QDoubleSpinBox();
        sx->setRange(0.1, 10.0);
        sx->setValue(1.5);
        m_optionsToolbar->addWidget(sx);
        
        m_optionsToolbar->addWidget(new QLabel("Size Y: "));
        QDoubleSpinBox* sy = new QDoubleSpinBox();
        sy->setRange(0.1, 10.0);
        sy->setValue(1.5);
        m_optionsToolbar->addWidget(sy);
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Shape: "));
        QComboBox* shape = new QComboBox();
        shape->addItems({"Rect", "Round", "Oval"});
        m_optionsToolbar->addWidget(shape);
    }
    else if (toolName == "Via") {
        m_optionsToolbar->addWidget(new QLabel("Diameter: "));
        QDoubleSpinBox* d = new QDoubleSpinBox();
        d->setRange(0.1, 5.0);
        d->setValue(0.6);
        m_optionsToolbar->addWidget(d);
        
        m_optionsToolbar->addWidget(new QLabel("Drill: "));
        QDoubleSpinBox* dr = new QDoubleSpinBox();
        dr->setRange(0.1, 5.0);
        dr->setValue(0.3);
        m_optionsToolbar->addWidget(dr);
    }
    else if (toolName == "Length Tuning") {
        PCBTool* tool = m_view->currentTool();
        double currentTarget = tool->toolProperties().value("Target Length (mm)", 50.0).toDouble();
        double currentAmp = tool->toolProperties().value("Amplitude (mm)", 2.0).toDouble();

        m_optionsToolbar->addWidget(new QLabel("Target: "));
        QDoubleSpinBox* targetSpin = new QDoubleSpinBox();
        targetSpin->setRange(1.0, 1000.0);
        targetSpin->setValue(currentTarget);
        targetSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(targetSpin);
        connect(targetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
            tool->setToolProperty("Target Length (mm)", val);
        });

        m_optionsToolbar->addSeparator();

        m_optionsToolbar->addWidget(new QLabel("Amplitude: "));
        QDoubleSpinBox* ampSpin = new QDoubleSpinBox();
        ampSpin->setRange(0.1, 20.0);
        ampSpin->setValue(currentAmp);
        ampSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(ampSpin);
        connect(ampSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
            tool->setToolProperty("Amplitude (mm)", val);
        });
    }
    else if (toolName == "Rectangle" || toolName == "Filled Zone" || toolName == "Polygon Pour") {
        PCBTool* tool = m_view->currentTool();
        int currentLayer = tool->toolProperties().value("Active Layer", 0).toInt();

        m_optionsToolbar->addWidget(new QLabel("Layer: "));
        QComboBox* layerCombo = new QComboBox();
        
        // Add all available layers
        for (const auto& layer : PCBLayerManager::instance().layers()) {
            layerCombo->addItem(layer.name(), layer.id());
        }
        
        // Set current selection
        int initialIdx = layerCombo->findData(currentLayer);
        if (initialIdx != -1) layerCombo->setCurrentIndex(initialIdx);
        
        m_optionsToolbar->addWidget(layerCombo);
        
        connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, tool, layerCombo](int index){
            int layerId = layerCombo->itemData(index).toInt();
            tool->setToolProperty("Active Layer", layerId);
            
            // Also sync active layer global state if user expects it to follow
            onActiveLayerChanged(layerId);
            if (m_layerPanel) m_layerPanel->selectLayer(layerId);
        });

        if (toolName == "Filled Zone" || toolName == "Polygon Pour") {
            m_optionsToolbar->addSeparator();
            m_optionsToolbar->addWidget(new QLabel("Net: "));
            QLineEdit* netEdit = new QLineEdit(tool->toolProperties().value("Net Name", "GND").toString());
            netEdit->setMaximumWidth(80);
            m_optionsToolbar->addWidget(netEdit);
            connect(netEdit, &QLineEdit::textChanged, this, [tool](const QString& text){
                tool->setToolProperty("Net Name", text);
            });

            m_optionsToolbar->addSeparator();
            m_optionsToolbar->addWidget(new QLabel("Clearance: "));
            QDoubleSpinBox* clearSpin = new QDoubleSpinBox();
            clearSpin->setRange(0.05, 5.0);
            clearSpin->setSingleStep(0.05);
            clearSpin->setValue(tool->toolProperties().value("Clearance (mm)", 0.3).toDouble());
            m_optionsToolbar->addWidget(clearSpin);
            connect(clearSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
                tool->setToolProperty("Clearance (mm)", val);
            });
        }
    }
    else if (toolName == "Line" || toolName == "Circle" || toolName == "Arc") {
        PCBTool* tool = m_view->currentTool();
        int currentLayer = tool->toolProperties().value("Active Layer", 0).toInt();
        double stroke = tool->toolProperties().value("Stroke Width (mm)", 0.25).toDouble();

        m_optionsToolbar->addWidget(new QLabel("Layer: "));
        QComboBox* layerCombo = new QComboBox();
        for (const auto& layer : PCBLayerManager::instance().layers()) {
            layerCombo->addItem(layer.name(), layer.id());
        }
        int initialIdx = layerCombo->findData(currentLayer);
        if (initialIdx != -1) layerCombo->setCurrentIndex(initialIdx);
        m_optionsToolbar->addWidget(layerCombo);
        connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, tool, layerCombo](int index){
            int layerId = layerCombo->itemData(index).toInt();
            tool->setToolProperty("Active Layer", layerId);
            onActiveLayerChanged(layerId);
            if (m_layerPanel) m_layerPanel->selectLayer(layerId);
            if (!m_scene->selectedItems().isEmpty()) onPropertyChanged("Layer", layerId);
        });

        m_optionsToolbar->addSeparator();
        m_optionsToolbar->addWidget(new QLabel("Stroke: "));
        QDoubleSpinBox* strokeSpin = new QDoubleSpinBox();
        strokeSpin->setRange(0.05, 10.0);
        strokeSpin->setSingleStep(0.05);
        strokeSpin->setValue(stroke);
        strokeSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(strokeSpin);
        connect(strokeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, tool](double val){
            tool->setToolProperty("Stroke Width (mm)", val);
            if (!m_scene->selectedItems().isEmpty()) onPropertyChanged("Shape Stroke Width (mm)", val);
        });

        if (toolName == "Arc") {
            m_optionsToolbar->addSeparator();
            m_optionsToolbar->addWidget(new QLabel("Start: "));
            QDoubleSpinBox* startSpin = new QDoubleSpinBox();
            startSpin->setRange(-360.0, 360.0);
            startSpin->setValue(tool->toolProperties().value("Arc Start Angle (deg)", 0.0).toDouble());
            m_optionsToolbar->addWidget(startSpin);
            connect(startSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
                tool->setToolProperty("Arc Start Angle (deg)", val);
            });

            m_optionsToolbar->addWidget(new QLabel("Span: "));
            QDoubleSpinBox* spanSpin = new QDoubleSpinBox();
            spanSpin->setRange(-360.0, 360.0);
            spanSpin->setValue(tool->toolProperties().value("Arc Span Angle (deg)", 180.0).toDouble());
            m_optionsToolbar->addWidget(spanSpin);
            connect(spanSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
                tool->setToolProperty("Arc Span Angle (deg)", val);
            });
        }
    }
    else if (toolName == "Select") {
        m_optionsToolbar->addWidget(new QLabel("Select footprints and traces to edit properties."));
    }
    else {
        m_optionsToolbar->addWidget(new QLabel("Ready."));
    }

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_optionsToolbar->addWidget(spacer);
}

void MainWindow::updatePropertyBar() {
    if (!m_propertyBar || !m_scene) return;

    m_propertyBar->clear();
    QList<QGraphicsItem*> selected = m_scene->selectedItems();

    if (selected.isEmpty()) {
        m_propertyBar->addWidget(new QLabel(" NO SELECTION"));
        return;
    }

    if (selected.size() == 1) {
        PCBItem* pItem = dynamic_cast<PCBItem*>(selected.first());
        if (!pItem) return;

        if (pItem->itemType() == PCBItem::TraceType) {
            TraceItem* trace = static_cast<TraceItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" TRACE: "));

            m_propertyBar->addWidget(new QLabel(" Width:"));
            QDoubleSpinBox* wSpin = new QDoubleSpinBox();
            wSpin->setRange(0.1, 10.0);
            wSpin->setSingleStep(0.05);
            wSpin->setValue(trace->width());
            connect(wSpin, &QDoubleSpinBox::valueChanged, this, [this, trace](double val) {
                onPropertyChanged("Width (mm)", val);
            });
            m_propertyBar->addWidget(wSpin);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(trace->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, trace, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            for (const auto& layer : PCBLayerManager::instance().layers()) {
                layerCombo->addItem(layer.name(), layer.id());
            }
            layerCombo->setCurrentIndex(layerCombo->findData(trace->layer()));
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, trace, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

        } else if (pItem->itemType() == PCBItem::ViaType) {
            ViaItem* via = static_cast<ViaItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" VIA: "));

            m_propertyBar->addWidget(new QLabel(" Outer:"));
            QDoubleSpinBox* dSpin = new QDoubleSpinBox();
            dSpin->setRange(0.2, 5.0);
            dSpin->setValue(via->diameter());
            connect(dSpin, &QDoubleSpinBox::valueChanged, this, [this, via](double val) {
                onPropertyChanged("Diameter (mm)", val);
            });
            m_propertyBar->addWidget(dSpin);

            m_propertyBar->addWidget(new QLabel(" Drill:"));
            QDoubleSpinBox* drSpin = new QDoubleSpinBox();
            drSpin->setRange(0.1, 4.0);
            drSpin->setValue(via->drillSize());
            connect(drSpin, &QDoubleSpinBox::valueChanged, this, [this, via](double val) {
                onPropertyChanged("Drill Size (mm)", val);
            });
            m_propertyBar->addWidget(drSpin);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(via->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, via, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

        } else if (pItem->itemType() == PCBItem::ComponentType) {
            ComponentItem* comp = static_cast<ComponentItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" COMPONENT: "));

            m_propertyBar->addWidget(new QLabel(" Ref:"));
            QLineEdit* refEdit = new QLineEdit(comp->name());
            refEdit->setMaximumWidth(80);
            connect(refEdit, &QLineEdit::editingFinished, this, [this, comp, refEdit]() {
                onPropertyChanged("Name", refEdit->text());
            });
            m_propertyBar->addWidget(refEdit);

            m_propertyBar->addWidget(new QLabel(" Rot:"));
            QDoubleSpinBox* rotSpin = new QDoubleSpinBox();
            rotSpin->setRange(-360, 360);
            rotSpin->setValue(comp->rotation());
            connect(rotSpin, &QDoubleSpinBox::valueChanged, this, [this, comp](double val) {
                onPropertyChanged("Rotation (deg)", val);
            });
            m_propertyBar->addWidget(rotSpin);

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            layerCombo->addItem("Top", 0);
            layerCombo->addItem("Bottom", 1);
            layerCombo->setCurrentIndex(comp->layer() == 0 ? 0 : 1);
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, comp, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

        } else if (pItem->itemType() == PCBItem::CopperPourType) {
            CopperPourItem* pour = static_cast<CopperPourItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" SHAPE/POUR: "));

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            for (const auto& layer : PCBLayerManager::instance().layers()) {
                layerCombo->addItem(layer.name(), layer.id());
            }
            layerCombo->setCurrentIndex(layerCombo->findData(pour->layer()));
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pour, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(pour->netName());
            netEdit->setMaximumWidth(80);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, pour, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

            m_propertyBar->addWidget(new QLabel(" Clearance:"));
            QDoubleSpinBox* cSpin = new QDoubleSpinBox();
            cSpin->setRange(0.0, 5.0);
            cSpin->setSingleStep(0.05);
            cSpin->setValue(pour->clearance());
            connect(cSpin, &QDoubleSpinBox::valueChanged, this, [this, pour](double val) {
                onPropertyChanged("Clearance (mm)", val);
            });
            m_propertyBar->addWidget(cSpin);

        } else if (pItem->itemType() == PCBItem::ShapeType) {
            PCBShapeItem* shape = static_cast<PCBShapeItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(QString(" %1: ").arg(shape->shapeKindName().toUpper())));

            m_propertyBar->addWidget(new QLabel(" W:"));
            QDoubleSpinBox* wSpin = new QDoubleSpinBox();
            wSpin->setRange(0.2, 1000.0);
            wSpin->setValue(shape->sizeMm().width());
            connect(wSpin, &QDoubleSpinBox::valueChanged, this, [this](double val) {
                onPropertyChanged("Shape Width (mm)", val);
            });
            m_propertyBar->addWidget(wSpin);

            m_propertyBar->addWidget(new QLabel(" H:"));
            QDoubleSpinBox* hSpin = new QDoubleSpinBox();
            hSpin->setRange(0.2, 1000.0);
            hSpin->setValue(shape->sizeMm().height());
            connect(hSpin, &QDoubleSpinBox::valueChanged, this, [this](double val) {
                onPropertyChanged("Shape Height (mm)", val);
            });
            m_propertyBar->addWidget(hSpin);

            m_propertyBar->addWidget(new QLabel(" Stroke:"));
            QDoubleSpinBox* sSpin = new QDoubleSpinBox();
            sSpin->setRange(0.05, 10.0);
            sSpin->setSingleStep(0.05);
            sSpin->setValue(shape->strokeWidth());
            connect(sSpin, &QDoubleSpinBox::valueChanged, this, [this](double val) {
                onPropertyChanged("Shape Stroke Width (mm)", val);
            });
            m_propertyBar->addWidget(sSpin);

            if (shape->shapeKind() == PCBShapeItem::ShapeKind::Arc) {
                m_propertyBar->addWidget(new QLabel(" Start:"));
                QDoubleSpinBox* startSpin = new QDoubleSpinBox();
                startSpin->setRange(-360.0, 360.0);
                startSpin->setValue(shape->startAngleDeg());
                connect(startSpin, &QDoubleSpinBox::valueChanged, this, [this](double val) {
                    onPropertyChanged("Arc Start Angle (deg)", val);
                });
                m_propertyBar->addWidget(startSpin);

                m_propertyBar->addWidget(new QLabel(" Span:"));
                QDoubleSpinBox* spanSpin = new QDoubleSpinBox();
                spanSpin->setRange(-360.0, 360.0);
                spanSpin->setValue(shape->spanAngleDeg());
                connect(spanSpin, &QDoubleSpinBox::valueChanged, this, [this](double val) {
                    onPropertyChanged("Arc Span Angle (deg)", val);
                });
                m_propertyBar->addWidget(spanSpin);
            }

        } else if (pItem->itemType() == PCBItem::PadType) {
            PadItem* pad = static_cast<PadItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(QString(" PAD (%1)").arg(pad->padShape())));
            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(pad->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, pad, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);
        }
    } else {
        m_propertyBar->addWidget(new QLabel(QString(" MULTI-SELECTION (%1 items)").arg(selected.size())));
    }
}
