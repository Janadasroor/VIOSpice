/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_editor.h"
#include "footprint_library.h"
#include "footprint_commands.h"
#include "kicad_footprint_importer.h"
#include "ui/footprint_wizard_dialog.h"
#include "ui/footprint_library_browser_panel.h"
#include "ui/footprint_wizard_panel.h"
#include "ui/footprint_model_3d_panel.h"
#include "../core/visuals/theme_manager.h"
#include "../core/project/config_manager.h"
#include "../pcb/ui/pcb_3d_window.h"
#include "../pcb/items/component_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QActionGroup>
#include <QHeaderView>
#include <QInputDialog>
#include <cmath>
#include <algorithm>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileDialog>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QCloseEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QCursor>
#include <QSignalBlocker>
#include "items/footprint_primitive_item.h"
#include "analysis/footprint_engine.h"

using namespace Flux::Model;
using namespace Flux::Item;
using namespace Flux::Analysis;

namespace {
constexpr const char* kFootprintEditorStateKey = "FootprintEditor";

QIcon getThemeIcon(const QString& path, bool tinted = true, const QColor& overrideColor = QColor()) {
    QIcon icon(path);
    if (!tinted || !ThemeManager::theme()) {
        return icon;
    }

    const QPixmap pixmap = icon.pixmap(QSize(32, 32));
    if (pixmap.isNull()) {
        return icon;
    }

    QPixmap result = pixmap;
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    
    QColor tintColor = overrideColor.isValid() ? overrideColor : ThemeManager::theme()->textColor();
    painter.fillRect(result.rect(), tintColor);
    painter.end();
    return QIcon(result);
}

QString footprintLayerName(FootprintPrimitive::Layer layer) {
    switch (layer) {
        case FootprintPrimitive::Top_Silkscreen: return "Top Silk";
        case FootprintPrimitive::Top_Courtyard: return "Top Court";
        case FootprintPrimitive::Top_Fabrication: return "Top Fab";
        case FootprintPrimitive::Top_Copper: return "Top Cu";
        case FootprintPrimitive::Bottom_Copper: return "Bottom Cu";
        case FootprintPrimitive::Bottom_Silkscreen: return "Bottom Silk";
        case FootprintPrimitive::Top_SolderMask: return "Top Mask";
        case FootprintPrimitive::Bottom_SolderMask: return "Bottom Mask";
        case FootprintPrimitive::Top_SolderPaste: return "Top Paste";
        case FootprintPrimitive::Bottom_SolderPaste: return "Bottom Paste";
        case FootprintPrimitive::Top_Adhesive: return "Top Adhesive";
        case FootprintPrimitive::Bottom_Adhesive: return "Bottom Adhesive";
        case FootprintPrimitive::Bottom_Courtyard: return "Bottom Court";
        case FootprintPrimitive::Bottom_Fabrication: return "Bottom Fab";
        case FootprintPrimitive::Inner_Copper_1: return "In Cu 1";
        case FootprintPrimitive::Inner_Copper_2: return "In Cu 2";
        case FootprintPrimitive::Inner_Copper_3: return "In Cu 3";
        case FootprintPrimitive::Inner_Copper_4: return "In Cu 4";
    }
    return "Layer";
}

QColor footprintLayerColor(FootprintPrimitive::Layer layer) {
    if (PCBTheme* theme = ThemeManager::theme()) {
        switch (layer) {
            case FootprintPrimitive::Top_Copper: return theme->topCopper();
            case FootprintPrimitive::Bottom_Copper: return theme->bottomCopper();
            case FootprintPrimitive::Top_Silkscreen: return theme->topSilkscreen();
            case FootprintPrimitive::Bottom_Silkscreen: return theme->bottomSilkscreen();
            case FootprintPrimitive::Top_SolderMask: return theme->topSoldermask();
            case FootprintPrimitive::Bottom_SolderMask: return theme->bottomSoldermask();
            case FootprintPrimitive::Top_Courtyard:
            case FootprintPrimitive::Bottom_Courtyard: return QColor(148, 163, 184);
            case FootprintPrimitive::Top_Fabrication:
            case FootprintPrimitive::Bottom_Fabrication: return QColor(203, 213, 225);
            default: break;
        }
    }
    switch (layer) {
        case FootprintPrimitive::Top_Copper: return QColor("#ef4444");
        case FootprintPrimitive::Bottom_Copper: return QColor("#3b82f6");
        case FootprintPrimitive::Top_Silkscreen: return QColor("#f8fafc");
        case FootprintPrimitive::Bottom_Silkscreen: return QColor("#94a3b8");
        case FootprintPrimitive::Top_Courtyard:
        case FootprintPrimitive::Bottom_Courtyard: return QColor("#94a3b8");
        case FootprintPrimitive::Top_Fabrication:
        case FootprintPrimitive::Bottom_Fabrication: return QColor("#cbd5e1");
        default: return QColor("#fbbf24");
    }
}

QList<FootprintPrimitive::Layer> quickLayerChipOrder() {
    return {
        FootprintPrimitive::Top_Copper,
        FootprintPrimitive::Bottom_Copper,
        FootprintPrimitive::Top_Silkscreen,
        FootprintPrimitive::Bottom_Silkscreen,
        FootprintPrimitive::Top_Fabrication,
        FootprintPrimitive::Top_Courtyard
    };
}
}

void FootprintEditor::setupUI() {
    setWindowTitle("Footprint Editor - Design Mode");
    resize(1240, 850);
    setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    setAcceptDrops(true);
    for (int layer = int(FootprintPrimitive::Top_Silkscreen); layer <= int(FootprintPrimitive::Inner_Copper_4); ++layer) {
        m_visibleLayers.insert(layer);
    }
    
    // Global Dark Style patterned after components-library-panel.html
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; color: #cccccc; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }"
        "QGroupBox { border: 1px solid #1e1e1e; margin-top: 15px; padding-top: 15px; color: #cccccc; font-size: 13px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; }"
        "QLineEdit, QComboBox, QTreeWidget, QSpinBox, QDoubleSpinBox { background-color: #1e1e1e; border: 1px solid #3c3c3c; padding: 4px 8px; color: #cccccc; selection-background-color: #094771; font-size: 12px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #007acc; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox::down-arrow { image: url(:/icons/arrow_down.svg); width: 12px; height: 12px; }"
        "QTreeWidget::item { padding: 3px 0; border: none; }"
        "QTreeWidget::item:hover { background-color: #2a2d2e; }"
        "QTreeWidget::item:selected { background-color: #094771; color: white; }"
        "QPushButton { background-color: #2d2d30; border: 1px solid #555; padding: 6px 12px; color: #cccccc; }"
        "QPushButton:hover { background-color: #3c3c3c; }"
        "QPushButton:pressed { background-color: #094771; color: white; }"
        "QToolBar { background-color: #2d2d30; border-bottom: 1px solid #1e1e1e; padding: 8px 10px; spacing: 4px; }"
        "QToolBar#LeftToolBar { border-bottom: none; border-right: 1px solid #1e1e1e; padding: 4px; }"
        "QToolButton { background: transparent; border: 1px solid transparent; padding: 4px; color: #cccccc; }"
        "QToolButton:hover { border-color: #555; background-color: #3c3c3c; }"
        "QToolButton:checked { background-color: #094771; border-color: #094771; color: white; }"
        "QLabel { color: #cccccc; font-size: 13px; }"
        "QScrollBar:vertical { background: #2d2d30; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #555; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #666; }"
        "QScrollArea { border: none; background-color: #2b2b2b; }"
    );
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Top Utility ToolBar
    createToolBar();
    mainLayout->addWidget(m_toolbar);
    
    // Create separator
    QFrame* hLine = new QFrame();
    hLine->setFrameShape(QFrame::HLine);
    hLine->setStyleSheet("background-color: #1e1e1e; height: 1px; border: none;");
    mainLayout->addWidget(hLine);
    
    // Main Content (View + Lateral Toolbars + Side Panel)
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(0);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Lateral Drawing Bar
    contentLayout->addWidget(m_leftToolbar);
    
    // -- Left Navigator --
    m_leftTabWidget = new QTabWidget();
    m_leftTabWidget->setFixedWidth(300);
    m_leftTabWidget->setDocumentMode(true);
    m_leftTabWidget->setTabPosition(QTabWidget::North);
    m_leftTabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1e1e1e; background-color: #252526; }"
        "QTabBar::tab { background-color: #2d2d30; color: #9ca3af; padding: 8px 14px; border: 1px solid #1e1e1e; }"
        "QTabBar::tab:selected { background-color: #252526; color: #e5e7eb; border-bottom: 2px solid #10b981; }"
        "QTabBar::tab:hover:!selected { background-color: #3c3c3c; }");
    m_leftNavigatorPanel = m_leftTabWidget;

    m_libraryBrowserPanel = new FootprintLibraryBrowserPanel(this);
    connect(m_libraryBrowserPanel, &FootprintLibraryBrowserPanel::footprintSelected, this, &FootprintEditor::onLoadFootprint);
    m_leftTabWidget->addTab(m_libraryBrowserPanel, "Library Browser");

    m_wizardPanel = new FootprintWizardPanel(this);
    connect(m_wizardPanel, &FootprintWizardPanel::footprintGenerated, this, &FootprintEditor::onWizardGenerate);
    connect(m_wizardPanel, &FootprintWizardPanel::importKicadFootprintRequested, this, &FootprintEditor::onImportKicadFootprint);
    m_leftTabWidget->addTab(m_wizardPanel, "Wizard");
    contentLayout->addWidget(m_leftTabWidget);

    // -- Viewport Area --
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-200, -200, 400, 400); // 400x400 mm workspace
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &FootprintEditor::onSelectionChanged);
    
    m_view = new FootprintEditorView(this);
    m_view->setScene(m_scene);
    m_view->setCrosshairEnabled(ConfigManager::instance().toolProperty("FootprintEditor", "showCrosshair", true).toBool());
    connect(m_view, &FootprintEditorView::contextMenuRequested, this, &FootprintEditor::onContextMenu);

    QWidget* centerPane = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    m_layerChipsBar = new QWidget(centerPane);
    m_layerChipsBar->setStyleSheet("background-color: #17181b; border-bottom: 1px solid #26272b;");
    QHBoxLayout* chipsLayout = new QHBoxLayout(m_layerChipsBar);
    chipsLayout->setContentsMargins(10, 6, 10, 6);
    chipsLayout->setSpacing(6);

    auto makeChip = [this, chipsLayout](const QString& label, FootprintPrimitive::Layer layer) {
        auto* button = new QToolButton(m_layerChipsBar);
        button->setText(label);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setCursor(Qt::PointingHandCursor);
        chipsLayout->addWidget(button);
        m_layerChipButtons[int(layer)] = button;
        connect(button, &QToolButton::clicked, this, [this, layer]() {
            isolateLayer(layer);
        });
    };

    for (FootprintPrimitive::Layer layer : quickLayerChipOrder()) {
        makeChip(footprintLayerName(layer), layer);
    }

    chipsLayout->addStretch();

    QLabel* chipHint = new QLabel("Click a chip to set the active layer.", m_layerChipsBar);
    chipHint->setStyleSheet("color: #9ca3af; font-size: 11px;");
    chipHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    chipsLayout->addWidget(chipHint);

    centerLayout->addWidget(m_layerChipsBar);
    centerLayout->addWidget(m_view, 1);
    contentLayout->addWidget(centerPane, 1);
    
    // -- Right Side Configuration Panel --
    QScrollArea* sideScroll = new QScrollArea();
    sideScroll->setFixedWidth(360);
    sideScroll->setWidgetResizable(true);
    m_rightPanel = sideScroll;
    
    QWidget* sideWidget = new QWidget();
    sideWidget->setObjectName("SidePanel");
    sideWidget->setStyleSheet("#SidePanel { background-color: #3c3c3c; border-left: 1px solid #1e1e1e; }");
    
    QVBoxLayout* sideLayout = new QVBoxLayout(sideWidget);
    sideLayout->setContentsMargins(15, 15, 15, 15);
    sideLayout->setSpacing(15);
    
    m_rightTabWidget = new QTabWidget();
    m_rightTabWidget->setDocumentMode(true);
    m_rightTabWidget->setTabPosition(QTabWidget::East);
    m_rightTabWidget->tabBar()->setExpanding(false);
    m_rightTabWidget->tabBar()->setUsesScrollButtons(true);
    m_rightTabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_rightTabWidget->tabBar()->setFixedWidth(28);
    m_rightTabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1e1e1e; border-right: none; background-color: #2b2b2b; }"
        "QTabBar::tab { background-color: #2d2d2d; color: #9ca3af; border: 1px solid #3f3f46; border-left: none; "
        "border-top-right-radius: 4px; border-bottom-right-radius: 4px; padding: 2px 2px; min-width: 24px; min-height: 64px; font-weight: 600; }"
        "QTabBar::tab:selected { background-color: #2b2b2b; color: #e5e7eb; border-right: 3px solid #10b981; }"
        "QTabBar::tab:hover:!selected { background-color: #3f3f46; }");

    // 1. Identity Group
    QGroupBox* infoGroup = new QGroupBox("Footprint Metadata");
    QFormLayout* infoForm = new QFormLayout(infoGroup);
    infoForm->setSpacing(10);
    infoForm->setContentsMargins(15, 25, 15, 15);
    infoForm->setLabelAlignment(Qt::AlignRight);
    
    createInfoPanel();
    infoForm->addRow("Footprint Name", m_nameEdit);
    infoForm->addRow("Desc", m_descriptionEdit);
    infoForm->addRow("Category", m_categoryCombo);
    infoForm->addRow("Class", m_classificationCombo);
    infoForm->addRow("Keywords", m_keywordsEdit);
    infoForm->addRow("", m_excludeBOMCheck);
    infoForm->addRow("", m_excludePosCheck);
    infoForm->addRow("", m_dnpCheck);
    infoForm->addRow("", m_netTieCheck);

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_descriptionEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_categoryCombo, &QComboBox::currentTextChanged, this, [this]() { updatePreview(); });
    connect(m_classificationCombo, &QComboBox::currentTextChanged, this, [this]() { updatePreview(); });
    connect(m_keywordsEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_excludeBOMCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    connect(m_excludePosCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    connect(m_dnpCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    connect(m_netTieCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    QWidget* metadataPage = new QWidget();
    QVBoxLayout* metadataLayout = new QVBoxLayout(metadataPage);
    metadataLayout->setContentsMargins(0, 0, 0, 0);
    metadataLayout->addWidget(infoGroup);
    metadataLayout->addStretch();
    
    // 3D Model Group
    m_model3DPanel = new FootprintModel3DPanel(this);

    QWidget* modelPage = new QWidget();
    QVBoxLayout* modelPageLayout = new QVBoxLayout(modelPage);
    modelPageLayout->setContentsMargins(0, 0, 0, 0);
    modelPageLayout->addWidget(m_model3DPanel);
    
    // 2. Properties Editor Group
    QWidget* propsPage = new QWidget();
    QVBoxLayout* propsLayout = new QVBoxLayout(propsPage);
    propsLayout->setContentsMargins(5, 5, 5, 5);
    propsLayout->setSpacing(0);
    createPropertiesPanel();
    propsLayout->addWidget(m_propertyEditor, 1);

    m_rightTabWidget->addTab(propsPage, "Selection");
    m_rightTabWidget->addTab(metadataPage, "Metadata");
    m_rightTabWidget->addTab(modelPage, "3D");
    sideLayout->addWidget(m_rightTabWidget, 1);
    
    // Bottom Action Bar
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(10);
    
    QPushButton* closeBtn = new QPushButton("Close");
    QPushButton* saveAsBtn = new QPushButton("Clone...");
    QPushButton* saveBtn = new QPushButton("Save All");
    
    saveBtn->setStyleSheet("background-color: #007acc; color: white; border-color: #006bbd;");
    saveAsBtn->setStyleSheet("background-color: #333;");
    closeBtn->setStyleSheet("background-color: #1a1a1a;");

    connect(saveBtn, &QPushButton::clicked, this, &FootprintEditor::onSave);
    connect(saveAsBtn, &QPushButton::clicked, this, &FootprintEditor::onSaveToLibrary);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    actionLayout->addWidget(closeBtn);
    actionLayout->addWidget(saveAsBtn);
    actionLayout->addWidget(saveBtn);
    sideLayout->addLayout(actionLayout);
    
    sideScroll->setWidget(sideWidget);
    contentLayout->addWidget(sideScroll);

    mainLayout->addLayout(contentLayout);
    
    // Bottom Console
    QWidget* bottomConsole = new QWidget(this);
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomConsole);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    m_statusLabel = new QLabel("Ready | Grid: 1.27mm");
    m_statusLabel->setStyleSheet("background-color: #1e1e1e; border-top: 1px solid #3c3c3c; padding: 4px 15px; color: #cccccc; font-size: 11px;");
    bottomLayout->addWidget(m_statusLabel);

    m_bottomTabWidget = new QTabWidget(bottomConsole);
    m_bottomTabWidget->setDocumentMode(true);
    m_bottomTabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1e1e1e; border-top: none; background-color: #1f1f1f; }"
        "QTabBar::tab { background-color: #2d2d30; color: #9ca3af; padding: 6px 12px; border: 1px solid #1e1e1e; }"
        "QTabBar::tab:selected { background-color: #1f1f1f; color: #e5e7eb; border-bottom: 2px solid #10b981; }");
    m_codePreview = new QTextEdit(bottomConsole);
    m_codePreview->setReadOnly(true);
    m_codePreview->setFont(QFont("Monospace", 9));
    m_ruleList = new QListWidget(bottomConsole);
    m_bottomTabWidget->addTab(m_codePreview, "JSON Source");
    m_bottomTabWidget->addTab(m_ruleList, "Rule Checker");
    bottomLayout->addWidget(m_bottomTabWidget);

    m_bottomPanel = bottomConsole;
    mainLayout->addWidget(bottomConsole);
    
    // Connect View Signals
    connect(m_view, &FootprintEditorView::pointClicked, [this](QPointF pos){
         if (m_currentTool == ZoomArea) {
             m_view->scale(1/1.2, 1/1.2); 
             return;
         }
         
         if (m_currentTool == Pad) {
             QString num = getNextPadNumber();
             QString shape = m_currentPadShape.isEmpty() ? QString("Rect") : m_currentPadShape;
             FootprintPrimitive prim = FootprintPrimitive::createPad(pos, num, shape, QSizeF(1.5, 1.5));
             applyPadToolbarDefaults(prim);
             prim.layer = m_activeLayer;
             m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
         } else if (m_currentTool == Text) {
             bool ok;
             QString text = QInputDialog::getText(this, "Add Text", "Text:", QLineEdit::Normal, "Ref", &ok);
             if (ok && !text.isEmpty()) {
                 FootprintPrimitive prim = FootprintPrimitive::createText(text, pos);
                 prim.layer = m_activeLayer;
                 m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
             }
         } else if (m_currentTool == Line || m_currentTool == Rect || m_currentTool == Circle) {
             m_polyPoints.append(pos);
             if (m_polyPoints.size() == 2) {
                 const QPointF p1 = m_polyPoints[0];
                 const QPointF p2 = m_polyPoints[1];
                 FootprintPrimitive prim;

                 if (m_currentTool == Line) {
                     prim = FootprintPrimitive::createLine(p1, p2);
                 } else if (m_currentTool == Rect) {
                     prim = FootprintPrimitive::createRect(QRectF(p1, p2).normalized());
                 } else {
                     prim = FootprintPrimitive::createCircle(p1, QLineF(p1, p2).length());
                 }

                 prim.layer = m_activeLayer;
                 m_polyPoints.clear();
                 if (m_previewItem) {
                     m_scene->removeItem(m_previewItem);
                     delete m_previewItem;
                     m_previewItem = nullptr;
                 }
                 m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
             }
         } else if (m_currentTool == Anchor) {
             onSetAnchor(pos);
             // Revert to select tool after setting anchor
             if (m_view) {
                 m_currentTool = Select;
                 m_view->setCurrentTool(Select);
                 m_polyPoints.clear();
                 if (m_toolActions.contains("Select")) {
                     m_toolActions["Select"]->setChecked(true);
                 }
             }
         }
     });

    connect(m_view, &FootprintEditorView::drawingFinished, [this](QPointF start, QPointF end){
        if (m_currentTool == ZoomArea) {
             QRectF rect(start, end);
             m_view->fitInView(rect.normalized(), Qt::KeepAspectRatio);
             m_view->setCurrentTool(Select);
             m_currentTool = Select;
             return;
        }
        
        if (m_currentTool == Measure) {
             onMeasure(start, end);
             // Keep measurement tool active as per user request
             return;
        }

        Q_UNUSED(start);
        Q_UNUSED(end);
    });
    
    connect(m_view, &FootprintEditorView::toolCancelled, [this](){
        m_currentTool = Select;
        m_view->setCurrentTool(Select);
        m_polyPoints.clear();
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        // Find select action and check it
        if (m_toolActions.contains("Select")) {
             m_toolActions["Select"]->setChecked(true);
        }
    });

    connect(m_view, &FootprintEditorView::lineDragged, [this](QPointF start, QPointF end){
        if (m_currentTool == Select || m_currentTool == Pad || m_currentTool == Text ||
            m_currentTool == Line || m_currentTool == Rect || m_currentTool == Circle) return;
        
        // Remove old preview
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        
        QPen previewPen(Qt::yellow, 0, Qt::DashLine);
        
        if (m_currentTool == Line) {
            m_previewItem = m_scene->addLine(QLineF(start, end), previewPen);
        } else if (m_currentTool == Rect) {
            m_previewItem = m_scene->addRect(QRectF(start, end).normalized(), previewPen);
        } else if (m_currentTool == Circle) {
            qreal r = QLineF(start, end).length();
            m_previewItem = m_scene->addEllipse(start.x()-r, start.y()-r, r*2, r*2, previewPen);
        }
    });
    
    connect(m_view, &FootprintEditorView::mouseMoved, [this](QPointF pos){
        m_lastMouseScenePos = pos;
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }

        if (m_currentTool == Pad) {
            QString shape = m_currentPadShape.isEmpty() ? QString("Rect") : m_currentPadShape;
            FootprintPrimitive prim = FootprintPrimitive::createPad(pos, "", shape, QSizeF(1.5, 1.5));
            applyPadToolbarDefaults(prim);
            prim.layer = m_activeLayer;

            m_previewItem = buildVisual(prim, -1);
            if (m_previewItem) {
                m_previewItem->setOpacity(0.58);
                m_previewItem->setZValue(2500);
                m_previewItem->setAcceptedMouseButtons(Qt::NoButton);
                m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
                m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
                m_scene->addItem(m_previewItem);
            }
        } else if (m_currentTool == Line || m_currentTool == Rect || m_currentTool == Circle) {
            const QPen previewPen(Qt::cyan, 1, Qt::DashLine);
            const QPointF start = m_polyPoints.isEmpty() ? pos : m_polyPoints.first();

            if (m_currentTool == Line) {
                m_previewItem = m_scene->addLine(QLineF(start, pos), previewPen);
            } else if (m_currentTool == Rect) {
                m_previewItem = m_scene->addRect(QRectF(start, pos).normalized(), previewPen);
            } else if (m_currentTool == Circle) {
                const qreal radius = QLineF(start, pos).length();
                m_previewItem = m_scene->addEllipse(start.x() - radius, start.y() - radius,
                                                    radius * 2, radius * 2, previewPen);
            }
        }

        if (m_statusLabel) {
            m_statusLabel->setText(QString("X: %1 mm  Y: %2 mm | Grid: %3 mm | Snap: %4 | Alt = free")
                                   .arg(pos.x(), 0, 'f', 2)
                                   .arg(pos.y(), 0, 'f', 2)
                                   .arg(m_view->gridSize(), 0, 'f', 2)
                                   .arg(m_view->snapToGridEnabled() ? "ON" : "OFF"));
        }
    });

    connect(m_view, &FootprintEditorView::rectResizeStarted, this, &FootprintEditor::onRectResizeStarted);
    connect(m_view, &FootprintEditorView::rectResizeUpdated, this, &FootprintEditor::onRectResizeUpdated);
    connect(m_view, &FootprintEditorView::rectResizeFinished, this, &FootprintEditor::onRectResizeFinished);
    connect(m_view, &FootprintEditorView::originDragFinished, this, &FootprintEditor::onSetAnchor);
    updatePreview();
    refreshLayerChipStates();

    QByteArray geom = ConfigManager::instance().windowGeometry(kFootprintEditorStateKey);
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    }
    const QByteArray stateBytes = ConfigManager::instance().windowState(kFootprintEditorStateKey);
    if (!stateBytes.isEmpty()) {
        const QJsonObject state = QJsonDocument::fromJson(stateBytes).object();
        if (m_leftToolbar) m_leftToolbar->setVisible(state.value("leftToolbarVisible").toBool(true));
        if (m_leftNavigatorPanel) m_leftNavigatorPanel->setVisible(state.value("leftNavigatorVisible").toBool(true));
        if (m_bottomPanel) m_bottomPanel->setVisible(state.value("bottomPanelVisible").toBool(true));
        if (m_rightPanel) m_rightPanel->setVisible(state.value("rightPanelVisible").toBool(true));
        if (m_leftTabWidget) {
            const int idx = state.value("leftTabIndex").toInt(0);
            if (idx >= 0 && idx < m_leftTabWidget->count()) m_leftTabWidget->setCurrentIndex(idx);
            const int width = state.value("leftNavigatorWidth").toInt(m_leftTabWidget->width());
            if (width > 0) m_leftTabWidget->setFixedWidth(width);
        }
        if (m_rightTabWidget) {
            const int idx = state.value("rightTabIndex").toInt(0);
            if (idx >= 0 && idx < m_rightTabWidget->count()) m_rightTabWidget->setCurrentIndex(idx);
        }
        if (m_bottomTabWidget) {
            const int idx = state.value("bottomTabIndex").toInt(0);
            if (idx >= 0 && idx < m_bottomTabWidget->count()) m_bottomTabWidget->setCurrentIndex(idx);
        }
        if (QScrollArea* scroll = qobject_cast<QScrollArea*>(m_rightPanel)) {
            const int width = state.value("rightPanelWidth").toInt(scroll->width());
            if (width > 0) scroll->setFixedWidth(width);
        }
    }
}

void FootprintEditor::createInfoPanel() {
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("Footprint Name");
    
    m_descriptionEdit = new QLineEdit();
    m_descriptionEdit->setPlaceholderText("Description");
    
    m_categoryCombo = new QComboBox();
    m_categoryCombo->addItems({"Through-Hole", "SMD", "Connectors", "Discrete", "IC"});
    m_categoryCombo->setEditable(true);

    m_classificationCombo = new QComboBox();
    m_classificationCombo->addItems({"Unspecified", "SMD", "Through-Hole", "Virtual"});

    m_keywordsEdit = new QLineEdit();
    m_keywordsEdit->setPlaceholderText("Keywords, comma-separated");

    m_excludeBOMCheck = new QCheckBox("Exclude from BOM");
    m_excludePosCheck = new QCheckBox("Exclude from Position Files");
    m_dnpCheck = new QCheckBox("DNP (Do Not Populate)");
    m_netTieCheck = new QCheckBox("Net Tie Footprint");
}

void FootprintEditor::createToolBar() {
    // Top ToolBar (Settings, Utilities)
    m_toolbar = new QToolBar("Tools", this);
    m_toolbar->setObjectName("TopToolbar");
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setMovable(false);
    m_toolbar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    
    m_toolbar->setStyleSheet(
        "QToolBar#TopToolbar {"
        "  background: #252526;"
        "  border: 1px solid #3e3e42;"
        "  border-radius: 8px;"
        "  margin: 5px;"
        "  padding: 2px;"
        "}"
        "QToolBar#TopToolbar QToolButton {"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  margin: 1px;"
        "  background: transparent;"
        "}"
        "QToolBar#TopToolbar QToolButton:hover {"
        "  background: #3e3e42;"
        "}"
        "QToolBar#TopToolbar QToolButton:pressed {"
        "  background: #4e4e52;"
        "}"
    );
    
    // Left ToolBar (Drawing Tools)
    m_leftToolbar = new QToolBar("Drawing", this);
    m_leftToolbar->setObjectName("LeftToolBar");
    m_leftToolbar->setOrientation(Qt::Vertical);
    m_leftToolbar->setIconSize(QSize(22, 22));
    m_leftToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_leftToolbar->setMovable(false);

    QActionGroup* group = new QActionGroup(this);
    group->setExclusive(true);
    
    // Helper to add tool to appropriate toolbar
    auto addTool = [&](QToolBar* bar, const QString& name, Tool tool, const QString& iconFile, const QString& shortcut = "", const QColor& overrideColor = QColor(), bool tinted = true) {
        QAction* action = new QAction(getThemeIcon(":/icons/" + iconFile, tinted, overrideColor), name, this);
        action->setData(static_cast<int>(tool));
        action->setCheckable(true);
        if (!shortcut.isEmpty()) {
             action->setShortcut(QKeySequence(shortcut));
             action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
             action->setToolTip(name + " (" + shortcut + ")");
             this->addAction(action);
        }
        bar->addAction(action);
        group->addAction(action);
        if (tool == Select) action->setChecked(true);
        connect(action, &QAction::triggered, this, &FootprintEditor::onToolSelected);
        m_toolActions[name] = action;
        return action;
    };
    
    // Top Toolbar Items
    addTool(m_toolbar, "Select", Select, "tool_select.svg", "Esc");
    m_toolbar->addSeparator();
    
    m_undoAction = m_toolbar->addAction(getThemeIcon(":/icons/undo.svg"), "Undo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_undoAction, &QAction::triggered, this, &FootprintEditor::onUndo);
    this->addAction(m_undoAction);
    
    m_redoAction = m_toolbar->addAction(getThemeIcon(":/icons/redo.svg"), "Redo");
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_redoAction, &QAction::triggered, this, &FootprintEditor::onRedo);
    this->addAction(m_redoAction);

    QAction* saveAction = new QAction(getThemeIcon(":/icons/check.svg"), "Save", this);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(saveAction, &QAction::triggered, this, &FootprintEditor::onSave);
    this->addAction(saveAction);

    m_toolbar->addSeparator();

    QAction* deleteAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete");
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteAction, &QAction::triggered, this, &FootprintEditor::onDelete);
    this->addAction(deleteAction);
    
    m_toolbar->addSeparator();

    // Alignment Tools
    QToolButton* alignBtn = new QToolButton(this);
    alignBtn->setIcon(getThemeIcon(":/icons/tool_duplicate.svg"));
    alignBtn->setToolTip("Alignment & Distribution Tools");
    alignBtn->setPopupMode(QToolButton::InstantPopup);
    alignBtn->setAutoRaise(true);

    QMenu* alignMenu = new QMenu(alignBtn);
    auto addAlignAct = [&](const QString& text, const QString& tooltip, void (FootprintEditor::*member)()) {
        QAction* act = alignMenu->addAction(text, this, member);
        act->setToolTip(tooltip);
        connect(act, &QAction::triggered, this, member);
    };

    addAlignAct("←  Align Left",   "Align Left",   &FootprintEditor::onAlignLeft);
    addAlignAct("→  Align Right",  "Align Right",  &FootprintEditor::onAlignRight);
    addAlignAct("↑  Align Top",    "Align Top",    &FootprintEditor::onAlignTop);
    addAlignAct("↓  Align Bottom", "Align Bottom", &FootprintEditor::onAlignBottom);
    alignMenu->addSeparator();
    addAlignAct("⬌  Center H",     "Center H",     &FootprintEditor::onAlignCenterH);
    addAlignAct("⬍  Center V",     "Center V",     &FootprintEditor::onAlignCenterV);
    alignMenu->addSeparator();
    addAlignAct("⇥  Distribute H", "Distribute H", &FootprintEditor::onDistributeH);
    addAlignAct("⤒  Distribute V", "Distribute V", &FootprintEditor::onDistributeV);
    addAlignAct("↔  Match Spacing","Match Spacing",&FootprintEditor::onMatchSpacing);
    alignMenu->addSeparator();
    addAlignAct(" Arrows  Move Exactly", "Move Exactly", &FootprintEditor::onMoveExactly);

    alignBtn->setMenu(alignMenu);
    m_toolbar->addWidget(alignBtn);

    m_toolbar->addSeparator();

    m_padShapeCombo = new QComboBox(this);
    m_padShapeCombo->addItems({"Rect", "Round", "Oblong", "RoundedRect", "Trapezoid"});
    m_padShapeCombo->setCurrentText(m_currentPadShape.isEmpty() ? "Rect" : m_currentPadShape);

    m_padWidthSpin = new QDoubleSpinBox(this);
    m_padWidthSpin->setDecimals(3);
    m_padWidthSpin->setRange(0.1, 50.0);
    m_padWidthSpin->setValue(1.5);
    m_padWidthSpin->setPrefix("W ");
    m_padWidthSpin->setSuffix(" mm");
    m_padWidthSpin->setSingleStep(0.1);
    m_padWidthSpin->setToolTip("Pad width");

    m_padHeightSpin = new QDoubleSpinBox(this);
    m_padHeightSpin->setDecimals(3);
    m_padHeightSpin->setRange(0.1, 50.0);
    m_padHeightSpin->setValue(1.5);
    m_padHeightSpin->setPrefix("H ");
    m_padHeightSpin->setSuffix(" mm");
    m_padHeightSpin->setSingleStep(0.1);
    m_padHeightSpin->setToolTip("Pad height");

    m_padDrillSpin = new QDoubleSpinBox(this);
    m_padDrillSpin->setDecimals(3);
    m_padDrillSpin->setRange(0.0, 20.0);
    m_padDrillSpin->setValue(0.0);
    m_padDrillSpin->setPrefix("D ");
    m_padDrillSpin->setSuffix(" mm");
    m_padDrillSpin->setSingleStep(0.05);
    m_padDrillSpin->setToolTip("Drill size. Zero keeps the pad SMD.");

    m_padNumberStepSpin = new QSpinBox(this);
    m_padNumberStepSpin->setRange(1, 64);
    m_padNumberStepSpin->setValue(1);
    m_padNumberStepSpin->setPrefix("#+");
    m_padNumberStepSpin->setToolTip("Pad number increment for repeated placement");

    m_padSettingsButton = new QToolButton(this);
    m_padSettingsButton->setIcon(getThemeIcon(":/icons/tool_pad_settings.svg", false));
    m_padSettingsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_padSettingsButton->setPopupMode(QToolButton::DelayedPopup);
    m_padSettingsButton->setToolTip("Pad settings");
    m_padSettingsButton->setAutoRaise(true);
    connect(m_padSettingsButton, &QToolButton::clicked, this, &FootprintEditor::openPadSettingsDialog);
    m_toolbar->addWidget(m_padSettingsButton);

    connect(m_padShapeCombo, &QComboBox::currentTextChanged, this, [this](const QString& shape) {
        setPadShape(shape);
        m_currentTool = Pad;
        if (m_view) m_view->setCurrentTool(Pad);
        if (m_toolActions.contains("Pad")) m_toolActions["Pad"]->setChecked(true);
    });

    connect(m_padDrillSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyPadPresetFromDrill();
    });
    
    m_toolbar->addSeparator();

    // Zoom Tools
    addTool(m_toolbar, "Zoom Area", ZoomArea, "tool_zoom_area.svg", "Z");
    
    QAction* zoomIn = m_toolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    connect(zoomIn, &QAction::triggered, this, &FootprintEditor::onZoomIn);
    
    QAction* zoomOut = m_toolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    connect(zoomOut, &QAction::triggered, this, &FootprintEditor::onZoomOut);
    
    QAction* zoomFit = m_toolbar->addAction(getThemeIcon(":/icons/view_fit.svg"), "Zoom Fit");
    connect(zoomFit, &QAction::triggered, this, &FootprintEditor::onZoomFit);
    
    QAction* wizardAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_footprint_wizard.svg", false), "Footprint Wizard");
    wizardAction->setToolTip("Advanced Footprint Wizard — generate standard packages with live preview");
    wizardAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(wizardAction, &QAction::triggered, this, &FootprintEditor::onOpenWizard);
    
    m_toolbar->addSeparator();

    // Grid Selector 
    QLabel* gridLabel = new QLabel(" Grid:");
    gridLabel->setStyleSheet("color: #ccc; margin-left: 5px;");
    m_toolbar->addWidget(gridLabel);
    
    QComboBox* gridCombo = new QComboBox();
    gridCombo->addItem("0.1 mm", 0.1);
    gridCombo->addItem("0.25 mm", 0.25);
    gridCombo->addItem("0.5 mm", 0.5);
    gridCombo->addItem("1.0 mm", 1.0);
    gridCombo->addItem("1.27 mm (50mil)", 1.27);
    gridCombo->addItem("2.54 mm (100mil)", 2.54);
    gridCombo->setCurrentText("1.27 mm (50mil)");
    m_toolbar->addWidget(gridCombo);
    
    connect(gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, gridCombo](int index){
        onGridSizeChanged(gridCombo->itemData(index).toString());
    });

    m_toolbar->addSeparator();

    // Layer Selector
    QLabel* layerLabel = new QLabel(" Layer:");
    layerLabel->setStyleSheet("color: #ccc; margin-left: 5px;");
    m_toolbar->addWidget(layerLabel);

    m_layerCombo = new QComboBox();
    m_layerCombo->addItem("Top Silkscreen", FootprintPrimitive::Top_Silkscreen);
    m_layerCombo->addItem("Top Courtyard", FootprintPrimitive::Top_Courtyard);
    m_layerCombo->addItem("Top Fabrication", FootprintPrimitive::Top_Fabrication);
    m_layerCombo->addItem("Top Copper", FootprintPrimitive::Top_Copper);
    m_layerCombo->addItem("Bottom Copper", FootprintPrimitive::Bottom_Copper);
    m_layerCombo->addItem("Bottom Silkscreen", FootprintPrimitive::Bottom_Silkscreen);
    m_layerCombo->addItem("Top Solder Mask", FootprintPrimitive::Top_SolderMask);
    m_layerCombo->addItem("Bottom Solder Mask", FootprintPrimitive::Bottom_SolderMask);
    m_layerCombo->addItem("Top Solder Paste", FootprintPrimitive::Top_SolderPaste);
    m_layerCombo->addItem("Bottom Solder Paste", FootprintPrimitive::Bottom_SolderPaste);
    m_layerCombo->addItem("Top Adhesive", FootprintPrimitive::Top_Adhesive);
    m_layerCombo->addItem("Bottom Adhesive", FootprintPrimitive::Bottom_Adhesive);
    m_layerCombo->addItem("Bottom Courtyard", FootprintPrimitive::Bottom_Courtyard);
    m_layerCombo->addItem("Bottom Fabrication", FootprintPrimitive::Bottom_Fabrication);
    m_layerCombo->addItem("Inner Copper 1", FootprintPrimitive::Inner_Copper_1);
    m_layerCombo->addItem("Inner Copper 2", FootprintPrimitive::Inner_Copper_2);
    m_layerCombo->addItem("Inner Copper 3", FootprintPrimitive::Inner_Copper_3);
    m_layerCombo->addItem("Inner Copper 4", FootprintPrimitive::Inner_Copper_4);
    m_layerCombo->setCurrentIndex(0);
    m_toolbar->addWidget(m_layerCombo);

    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){
        m_activeLayer = static_cast<FootprintPrimitive::Layer>(m_layerCombo->itemData(index).toInt());
        refreshLayerChipStates();
    });

    m_toolbar->addSeparator();

    QAction* snapAction = m_toolbar->addAction(getThemeIcon(":/icons/snap_grid.svg"), "Snap");
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    snapAction->setToolTip("Toggle Grid Snapping (S)");
    snapAction->setShortcut(QKeySequence("S"));
    connect(snapAction, &QAction::toggled, this, [this](bool checked){
        if (m_view) m_view->setSnapToGrid(checked);
        if (m_statusLabel) {
            m_statusLabel->setText(QString("Grid snap %1. Alt temporarily bypasses snap while placing pads/shapes.")
                                   .arg(checked ? "enabled" : "disabled"));
        }
    });
    
    m_toolbar->addSeparator();
    
    // Alignment / Orientation
    QAction* rotate = m_toolbar->addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate");
    rotate->setShortcut(QKeySequence("Ctrl+R"));
    this->addAction(rotate);
    connect(rotate, &QAction::triggered, this, &FootprintEditor::onRotate);

    QAction* flipH = m_toolbar->addAction(getThemeIcon(":/icons/flip_h.svg"), "Flip H");
    connect(flipH, &QAction::triggered, this, &FootprintEditor::onFlipHorizontal);
    
    QAction* flipV = m_toolbar->addAction(getThemeIcon(":/icons/flip_v.svg"), "Flip V");
    connect(flipV, &QAction::triggered, this, &FootprintEditor::onFlipVertical);

    QAction* pairAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_array.png", false), "Mirror Pair");
    pairAct->setToolTip("Create mirrored copies (with optional top/bottom layer swap)");
    connect(pairAct, &QAction::triggered, this, &FootprintEditor::onCreateMirroredPair);

    m_toolbar->addSeparator();
    
    QAction* arrayAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_array.png", false), "Array Tool");
    arrayAct->setToolTip("Create Linear or Circular Array of items");
    connect(arrayAct, &QAction::triggered, this, &FootprintEditor::onArrayTool);

    QAction* polarAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_pad.svg"), "Polar Grid");
    polarAct->setToolTip("Generate pads arranged on a circular/radial grid");
    connect(polarAct, &QAction::triggered, this, &FootprintEditor::onPolarGridTool);

    QAction* fabOutlineAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_rect.svg"), "Fab From Selection");
    fabOutlineAct->setToolTip("Generate a fabrication outline rectangle from the current selection");
    connect(fabOutlineAct, &QAction::triggered, this, [this]() {
        generateOutlineFromSelection(FootprintPrimitive::Top_Fabrication, 0.25, "Generate Fab Outline");
    });

    QAction* courtOutlineAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_rect.svg"), "Courtyard From Selection");
    courtOutlineAct->setToolTip("Generate a courtyard rectangle from the current selection");
    connect(courtOutlineAct, &QAction::triggered, this, [this]() {
        generateOutlineFromSelection(FootprintPrimitive::Top_Courtyard, 0.5, "Generate Courtyard");
    });

    QAction* renumberAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_text.svg"), "Renumber Pads");
    renumberAct->setToolTip("Renumber selected pads by layout pattern");
    connect(renumberAct, &QAction::triggered, this, [this]() {
        QMenu menu(this);
        menu.addAction("Left to Right", this, [this]() { renumberPads("left-right"); });
        menu.addAction("Top to Bottom", this, [this]() { renumberPads("top-bottom"); });
        menu.addAction("Clockwise", this, [this]() { renumberPads("clockwise"); });
        menu.exec(QCursor::pos());
    });

    QAction* drcAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_drc.png", false), "Check Footprint");
    drcAct->setToolTip("Run Footprint Rule Check (DRC)");
    connect(drcAct, &QAction::triggered, this, &FootprintEditor::onRunDRC);

    QAction* importKiCadAct = m_toolbar->addAction(getThemeIcon(":/icons/folder_open.svg"), "Import KiCad");
    importKiCadAct->setToolTip("Import KiCad footprint (.kicad_mod)");
    connect(importKiCadAct, &QAction::triggered, this, &FootprintEditor::onImportKicadFootprint);

    QAction* view3DAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_3d.png", false), "3D View");
    view3DAct->setToolTip("Open 3D preview for current footprint");
    connect(view3DAct, &QAction::triggered, this, &FootprintEditor::onOpen3DPreview);

    auto makePanelIcon = [](const QString& type) -> QIcon {
        QPixmap px(32, 32);
        px.fill(Qt::transparent);
        QPainter painter(&px);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = Qt::white;
        if (ThemeManager::theme()) {
            color = ThemeManager::theme()->textColor();
        }
        painter.setPen(QPen(color, 2));
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(color);
        if (type == "left") {
            painter.drawRect(6, 8, 6, 16);
        } else if (type == "bottom") {
            painter.drawRect(6, 18, 20, 6);
        } else if (type == "right") {
            painter.drawRect(20, 8, 6, 16);
        }
        return QIcon(px);
    };

    m_toolbar->addSeparator();

    QAction* leftToggle = new QAction(makePanelIcon("left"), "Toggle Left Sidebar", this);
    leftToggle->setToolTip("Toggle Left Sidebar");
    leftToggle->setCheckable(true);
    leftToggle->setChecked(true);
    m_toolbar->addAction(leftToggle);
    connect(leftToggle, &QAction::toggled, this, [this](bool visible) {
        if (m_leftToolbar) m_leftToolbar->setVisible(visible);
        if (m_leftNavigatorPanel) m_leftNavigatorPanel->setVisible(visible);
    });

    QAction* bottomToggle = new QAction(makePanelIcon("bottom"), "Toggle Bottom Panel", this);
    bottomToggle->setToolTip("Toggle Bottom Panel");
    bottomToggle->setCheckable(true);
    bottomToggle->setChecked(true);
    m_toolbar->addAction(bottomToggle);
    connect(bottomToggle, &QAction::toggled, this, [this](bool visible) {
        if (m_bottomPanel) m_bottomPanel->setVisible(visible);
    });

    QAction* rightToggle = new QAction(makePanelIcon("right"), "Toggle Right Sidebar", this);
    rightToggle->setToolTip("Toggle Right Sidebar");
    rightToggle->setCheckable(true);
    rightToggle->setChecked(true);
    m_toolbar->addAction(rightToggle);
    connect(rightToggle, &QAction::toggled, this, [this](bool visible) {
        if (m_rightPanel) m_rightPanel->setVisible(visible);
    });

    // Left Toolbar Items (Drawing Tools)
    addTool(m_leftToolbar, "Pad", Pad, "tool_pad.svg", "P", QColor(), false);
    m_leftToolbar->addSeparator();
    addTool(m_leftToolbar, "Line", Line, "tool_line.svg", "L");
    addTool(m_leftToolbar, "Rect", Rect, "tool_rect.svg", "R");
    addTool(m_leftToolbar, "Circle", Circle, "tool_circle.svg", "C");
    // Text tool
    addTool(m_leftToolbar, "Text", Text, "tool_text.svg", "T");
    // Measure tool
    addTool(m_leftToolbar, "Measure", Measure, "tool_measure.svg", "M");
    // Anchor tool
    addTool(m_leftToolbar, "Set Anchor", Anchor, "tool_anchor.svg", "H");
    
    QAction* addExactAct = m_leftToolbar->addAction(getThemeIcon(":/icons/tool_line.svg"), "Exact Dimensions...");
    addExactAct->setToolTip("Add Primitive (Exact Dimensions)...");
    connect(addExactAct, &QAction::triggered, this, &FootprintEditor::onAddPrimitiveExact);
    
    // Spacer for left toolbar
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftToolbar->addWidget(spacer);
}

void FootprintEditor::createPropertiesPanel() {
    m_propertyEditor = new PropertyEditor();
    
    connect(m_propertyEditor, &PropertyEditor::propertyChanged, this, [this](const QString& name, const QVariant& value){
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        if (selected.size() != 1) {
            FootprintDefinition oldDef = m_footprint;
            FootprintDefinition newDef = oldDef;
            bool changed = false;

            if (name == "Footprint Name") {
                newDef.setName(value.toString());
                changed = true;
            } else if (name == "Description") {
                newDef.setDescription(value.toString());
                changed = true;
            } else if (name == "Category") {
                newDef.setCategory(value.toString());
                changed = true;
            } else if (name == "Classification") {
                newDef.setClassification(value.toString());
                changed = true;
            } else if (name == "Keywords") {
                QStringList keywords;
                for (const QString& token : value.toString().split(',', Qt::SkipEmptyParts)) {
                    const QString trimmed = token.trimmed();
                    if (!trimmed.isEmpty()) keywords.append(trimmed);
                }
                newDef.setKeywords(keywords);
                changed = true;
            } else if (name == "Exclude From BOM") {
                newDef.setExcludeFromBOM(value.toBool());
                changed = true;
            } else if (name == "Exclude From Position Files") {
                newDef.setExcludeFromPosFiles(value.toBool());
                changed = true;
            } else if (name == "DNP") {
                newDef.setDnp(value.toBool());
                changed = true;
            } else if (name == "Net Tie") {
                newDef.setIsNetTie(value.toBool());
                changed = true;
            }

            if (changed && m_undoStack) {
                m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Update Footprint Properties"));
            }
            return;
        }
        
        QGraphicsItem* item = selected.first();
        int index = m_drawnItems.indexOf(item);
        if (index == -1 || index >= m_footprint.primitives().size()) return;
        
        FootprintDefinition oldDef = m_footprint;
        FootprintDefinition newDef = oldDef;
        FootprintPrimitive& prim = newDef.primitives()[index];
        
        bool changed = false;
        
        if (name == "Layer") {
            QString val = value.toString();
            if (val == "Top Silkscreen") prim.layer = FootprintPrimitive::Top_Silkscreen;
            else if (val == "Top Courtyard") prim.layer = FootprintPrimitive::Top_Courtyard;
            else if (val == "Top Fabrication") prim.layer = FootprintPrimitive::Top_Fabrication;
            else if (val == "Top Copper") prim.layer = FootprintPrimitive::Top_Copper;
            else if (val == "Bottom Copper") prim.layer = FootprintPrimitive::Bottom_Copper;
            else if (val == "Bottom Silkscreen") prim.layer = FootprintPrimitive::Bottom_Silkscreen;
            else if (val == "Top Solder Mask") prim.layer = FootprintPrimitive::Top_SolderMask;
            else if (val == "Bottom Solder Mask") prim.layer = FootprintPrimitive::Bottom_SolderMask;
            else if (val == "Top Solder Paste") prim.layer = FootprintPrimitive::Top_SolderPaste;
            else if (val == "Bottom Solder Paste") prim.layer = FootprintPrimitive::Bottom_SolderPaste;
            else if (val == "Top Adhesive") prim.layer = FootprintPrimitive::Top_Adhesive;
            else if (val == "Bottom Adhesive") prim.layer = FootprintPrimitive::Bottom_Adhesive;
            else if (val == "Bottom Courtyard") prim.layer = FootprintPrimitive::Bottom_Courtyard;
            else if (val == "Bottom Fabrication") prim.layer = FootprintPrimitive::Bottom_Fabrication;
            else if (val == "Inner Copper 1") prim.layer = FootprintPrimitive::Inner_Copper_1;
            else if (val == "Inner Copper 2") prim.layer = FootprintPrimitive::Inner_Copper_2;
            else if (val == "Inner Copper 3") prim.layer = FootprintPrimitive::Inner_Copper_3;
            else if (val == "Inner Copper 4") prim.layer = FootprintPrimitive::Inner_Copper_4;
            changed = true;
        }

        if (prim.type == FootprintPrimitive::Pad) {
             if (name == "Number") { prim.data["number"] = value.toString(); changed = true; }
             else if (name == "Pad Type") {
                 const QString padType = value.toString();
                 prim.data["pad_type"] = padType;
                 if (padType == "SMD") {
                     prim.data["drill_size"] = 0.0;
                     prim.data["plated"] = true;
                 } else if (padType == "Through-Hole") {
                     if (prim.data["drill_size"].toDouble() <= 0.0) {
                         prim.data["drill_size"] = 0.8;
                     }
                     prim.data["plated"] = true;
                 } else if (padType == "Connector") {
                     prim.data["drill_size"] = 0.0;
                     prim.data["plated"] = true;
                 }
                 changed = true;
             }
             else if (name == "Shape") { prim.data["shape"] = value.toString(); changed = true; }
             else if (name == "Width") { prim.data["width"] = value.toDouble(); changed = true; }
             else if (name == "Height") { prim.data["height"] = value.toDouble(); changed = true; }
             else if (name == "X") { prim.data["x"] = value.toDouble(); changed = true; }
             else if (name == "Y") { prim.data["y"] = value.toDouble(); changed = true; }
             else if (name == "Rotation") { prim.data["rotation"] = value.toDouble(); changed = true; }
             else if (name == "Corner Radius") { prim.data["corner_radius"] = value.toDouble(); changed = true; }
             else if (name == "Trapezoid Delta X") { prim.data["trapezoid_delta_x"] = value.toDouble(); changed = true; }
             else if (name == "Drill Size") { prim.data["drill_size"] = value.toDouble(); changed = true; }
             else if (name == "Clearance Override") { prim.data["net_clearance_override_enabled"] = (value.toString() == "True"); changed = true; }
             else if (name == "Net Clearance") { prim.data["net_clearance"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Relief") { prim.data["thermal_relief_enabled"] = (value.toString() == "True"); changed = true; }
             else if (name == "Thermal Spoke Width") { prim.data["thermal_spoke_width"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Relief Gap") { prim.data["thermal_relief_gap"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Spoke Count") { prim.data["thermal_spoke_count"] = value.toInt(); changed = true; }
             else if (name == "Thermal Spoke Angle") { prim.data["thermal_spoke_angle_deg"] = value.toDouble(); changed = true; }
             else if (name == "Jumper Group") { prim.data["jumper_group"] = qMax(0, value.toInt()); changed = true; }
             else if (name == "Net Tie Group") { prim.data["net_tie_group"] = qMax(0, value.toInt()); changed = true; }
             else if (name == "Solder Mask Exp") { prim.data["solder_mask_expansion"] = value.toDouble(); changed = true; }
             else if (name == "Paste Mask Exp") { prim.data["paste_mask_expansion"] = value.toDouble(); changed = true; }
             else if (name == "Plated") { prim.data["plated"] = (value.toString() == "True"); changed = true; }
        } else if (prim.type == FootprintPrimitive::Text) {
             if (name == "Text") { prim.data["text"] = value.toString(); changed = true; }
             else if (name == "Height") { prim.data["height"] = value.toDouble(); changed = true; }
             else if (name == "X") { prim.data["x"] = value.toDouble(); changed = true; }
             else if (name == "Y") { prim.data["y"] = value.toDouble(); changed = true; }
             else if (name == "Rotation") { prim.data["rotation"] = value.toDouble(); changed = true; }
        } else if (prim.type == FootprintPrimitive::Line || prim.type == FootprintPrimitive::Dimension) {
             if (name == "X1") { prim.data["x1"] = value.toDouble(); changed = true; }
             else if (name == "Y1") { prim.data["y1"] = value.toDouble(); changed = true; }
             else if (name == "X2") { prim.data["x2"] = value.toDouble(); changed = true; }
             else if (name == "Y2") { prim.data["y2"] = value.toDouble(); changed = true; }
        } else if (prim.type == FootprintPrimitive::Rect) {
             if (name == "X") { prim.data["x"] = value.toDouble(); changed = true; }
             else if (name == "Y") { prim.data["y"] = value.toDouble(); changed = true; }
             else if (name == "Width") { prim.data["width"] = value.toDouble(); changed = true; }
             else if (name == "Height") { prim.data["height"] = value.toDouble(); changed = true; }
             else if (name == "Filled") { prim.data["filled"] = value.toBool(); changed = true; }
             else if (name == "Line Width") { prim.data["lineWidth"] = value.toDouble(); changed = true; }
        } else if (prim.type == FootprintPrimitive::Circle) {
             if (name == "Center X") { prim.data["cx"] = value.toDouble(); changed = true; }
             else if (name == "Center Y") { prim.data["cy"] = value.toDouble(); changed = true; }
             else if (name == "Radius") { prim.data["radius"] = value.toDouble(); changed = true; }
             else if (name == "Filled") { prim.data["filled"] = value.toBool(); changed = true; }
             else if (name == "Line Width") { prim.data["lineWidth"] = value.toDouble(); changed = true; }
        } else if (prim.type == FootprintPrimitive::Arc) {
             if (name == "Center X") { prim.data["cx"] = value.toDouble(); changed = true; }
             else if (name == "Center Y") { prim.data["cy"] = value.toDouble(); changed = true; }
             else if (name == "Radius") { prim.data["radius"] = value.toDouble(); changed = true; }
             else if (name == "Start Angle") { prim.data["startAngle"] = value.toDouble(); changed = true; }
             else if (name == "Span Angle") { prim.data["spanAngle"] = value.toDouble(); changed = true; }
             else if (name == "Line Width") { prim.data["width"] = value.toDouble(); changed = true; }
        }
        
        if (changed) {
            m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Update Primitive Properties"));
        }
    });
}

void FootprintEditor::updatePropertiesPanel() {
    if (!m_propertyEditor) return;

    m_propertyEditor->beginUpdate();
    const QList<QGraphicsItem*> selected = m_scene ? m_scene->selectedItems() : QList<QGraphicsItem*>();
    if (selected.size() == 1) {
        const int index = m_drawnItems.indexOf(selected.first());
        if (index >= 0 && index < m_footprint.primitives().size()) {
            populatePropertiesFor(index);
            m_propertyEditor->endUpdate();
            return;
        }
    }

    m_propertyEditor->clear();
    m_propertyEditor->addSectionHeader("Footprint Settings");
    m_propertyEditor->addProperty("Footprint Name", m_nameEdit ? m_nameEdit->text() : QString());
    m_propertyEditor->addProperty("Description", m_descriptionEdit ? m_descriptionEdit->text() : QString());
    m_propertyEditor->addProperty("Category", m_categoryCombo ? m_categoryCombo->currentText() : QString(),
                                  "enum|Through-Hole,SMD,Connectors,Discrete,IC");
    m_propertyEditor->addProperty("Classification", m_classificationCombo ? m_classificationCombo->currentText() : QString(),
                                  "enum|Unspecified,SMD,Through-Hole,Virtual");
    m_propertyEditor->addProperty("Keywords", m_keywordsEdit ? m_keywordsEdit->text() : QString());
    m_propertyEditor->addProperty("Exclude From BOM", m_excludeBOMCheck && m_excludeBOMCheck->isChecked());
    m_propertyEditor->addProperty("Exclude From Position Files", m_excludePosCheck && m_excludePosCheck->isChecked());
    m_propertyEditor->addProperty("DNP", m_dnpCheck && m_dnpCheck->isChecked());
    m_propertyEditor->addProperty("Net Tie", m_netTieCheck && m_netTieCheck->isChecked());
    m_propertyEditor->addSectionHeader("Geometry");
    m_propertyEditor->addProperty("Primitive Count", m_footprint.primitives().size());
    m_propertyEditor->addProperty("Bounds Width", m_footprint.boundingRect().width());
    m_propertyEditor->addProperty("Bounds Height", m_footprint.boundingRect().height());
    m_propertyEditor->endUpdate();
}

void FootprintEditor::updatePreview() {
    if (!m_codePreview) return;

    FootprintDefinition previewDef = m_footprint;
    previewDef.setName(m_nameEdit ? m_nameEdit->text().trimmed() : previewDef.name());
    previewDef.setDescription(m_descriptionEdit ? m_descriptionEdit->text().trimmed() : previewDef.description());
    previewDef.setCategory(m_categoryCombo ? m_categoryCombo->currentText().trimmed() : previewDef.category());
    previewDef.setClassification(m_classificationCombo ? m_classificationCombo->currentText().trimmed() : previewDef.classification());
    previewDef.setExcludeFromBOM(m_excludeBOMCheck && m_excludeBOMCheck->isChecked());
    previewDef.setExcludeFromPosFiles(m_excludePosCheck && m_excludePosCheck->isChecked());
    previewDef.setDnp(m_dnpCheck && m_dnpCheck->isChecked());
    previewDef.setIsNetTie(m_netTieCheck && m_netTieCheck->isChecked());

    QStringList keywords;
    if (m_keywordsEdit) {
        for (const QString& token : m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
            const QString trimmed = token.trimmed();
            if (!trimmed.isEmpty()) keywords.append(trimmed);
        }
    }
    previewDef.setKeywords(keywords);
    previewDef.setModels3D(m_models3D);
    if (!m_models3D.isEmpty()) previewDef.setModel3D(m_models3D.first());

    m_codePreview->setPlainText(QJsonDocument(previewDef.toJson()).toJson(QJsonDocument::Indented));
}

void FootprintEditor::drawGrid() {}

void FootprintEditor::updateSceneFromDefinition() {
    clearResizeHandles();
    m_scene->clear();
    m_previewItem = nullptr; 
    m_drawnItems.clear(); 
    
    for (int i = 0; i < m_footprint.primitives().size(); ++i) {
        if (!isLayerVisible(m_footprint.primitives()[i].layer)) {
            m_drawnItems.append(nullptr);
            continue;
        }
        QGraphicsItem* item = buildVisual(m_footprint.primitives()[i], i);
        if (item) {
            m_scene->addItem(item);
        }
        m_drawnItems.append(item);
    }

    updatePropertiesPanel();
    updatePreview();
    updateResizeHandles();
}

QGraphicsItem* FootprintEditor::buildVisual(const FootprintPrimitive& prim, int index) {
    FootprintPrimitiveItem* visual = nullptr;

    switch (prim.type) {
        case FootprintPrimitive::Line:    visual = new FootprintLineItem(prim); break;
        case FootprintPrimitive::Rect:    visual = new FootprintRectItem(prim); break;
        case FootprintPrimitive::Circle:  visual = new FootprintCircleItem(prim); break;
        case FootprintPrimitive::Arc:     visual = new FootprintArcItem(prim); break;
        case FootprintPrimitive::Pad:     visual = new FootprintPadItem(prim); break;
        case FootprintPrimitive::Text:    visual = new FootprintTextItem(prim); break;
        case FootprintPrimitive::Polygon: visual = new FootprintPolygonItem(prim); break;
        default: return nullptr;
    }

    if (visual) {
        visual->setPrimitiveIndex(index);
    }
    return visual;
}

bool FootprintEditor::isLayerVisible(FootprintPrimitive::Layer layer) const {
    Q_UNUSED(layer);
    return true;
}

void FootprintEditor::setLayerVisibility(FootprintPrimitive::Layer layer, bool visible) {
    Q_UNUSED(layer);
    Q_UNUSED(visible);
    refreshLayerChipStates();
}

void FootprintEditor::isolateLayer(FootprintPrimitive::Layer layer) {
    m_activeLayer = layer;
    if (m_layerCombo) {
        const int idx = m_layerCombo->findData(layer);
        if (idx >= 0) m_layerCombo->setCurrentIndex(idx);
    }
    refreshLayerChipStates();
}

void FootprintEditor::restoreAllLayerVisibility() {
    refreshLayerChipStates();
}

void FootprintEditor::refreshLayerChipStates() {
    for (auto it = m_layerChipButtons.begin(); it != m_layerChipButtons.end(); ++it) {
        if (!it.value()) continue;
        const FootprintPrimitive::Layer layer = static_cast<FootprintPrimitive::Layer>(it.key());
        const QColor color = footprintLayerColor(layer);
        const bool active = (m_activeLayer == layer);
        it.value()->setChecked(active);
        it.value()->setStyleSheet(QString(
            "QToolButton { border: 1px solid %1; border-radius: 10px; padding: 4px 10px; background-color: %2; color: %3; font-weight: %4; }"
            "QToolButton:hover { border-color: %5; }")
            .arg(color.darker(150).name())
            .arg(active ? color.lighter(110).name() : QString("#1f2937"))
            .arg(active ? QString("#0f172a") : QString("#cbd5e1"))
            .arg(active ? "700" : "500")
            .arg(color.name()));
        it.value()->setToolTip(QString("%1\nClick: make active layer").arg(footprintLayerName(layer)));
    }
}

void FootprintEditor::setFootprintDefinition(const FootprintDefinition& def) {
    m_footprint = def;
    const QSignalBlocker nameBlocker(m_nameEdit);
    const QSignalBlocker descriptionBlocker(m_descriptionEdit);
    const QSignalBlocker categoryBlocker(m_categoryCombo);
    const QSignalBlocker classificationBlocker(m_classificationCombo);
    const QSignalBlocker keywordsBlocker(m_keywordsEdit);
    const QSignalBlocker bomBlocker(m_excludeBOMCheck);
    const QSignalBlocker posBlocker(m_excludePosCheck);
    const QSignalBlocker dnpBlocker(m_dnpCheck);
    const QSignalBlocker netTieBlocker(m_netTieCheck);

    // update fields
    m_nameEdit->setText(def.name());
    m_descriptionEdit->setText(def.description());
    m_categoryCombo->setCurrentText(def.category());
    m_classificationCombo->setCurrentText(def.classification());
    m_keywordsEdit->setText(def.keywords().join(", "));
    m_excludeBOMCheck->setChecked(def.excludeFromBOM());
    m_excludePosCheck->setChecked(def.excludeFromPosFiles());
    m_dnpCheck->setChecked(def.dnp());
    m_netTieCheck->setChecked(def.isNetTie());
    
    // 3D Models
    m_models3D = def.models3D();
    if (m_models3D.isEmpty()) {
        m_models3D.append(def.model3D());
    }
    if (m_models3D.isEmpty()) {
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_models3D.append(model);
    }
    if (m_model3DPanel) {
        m_model3DPanel->updatePanelFromModels();
    }
    
    updateSceneFromDefinition();
    updatePreview();
}
