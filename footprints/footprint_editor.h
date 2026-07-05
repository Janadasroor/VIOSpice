/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOOTPRINT_EDITOR_H
#define FOOTPRINT_EDITOR_H

#include <QDialog>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QToolBar>
#include <QDockWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTreeWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QPointer>
#include <QCheckBox>
#include <QList>
#include <QTabWidget>
#include <QTextEdit>
#include <QListWidget>
#include <QGraphicsItem>
#include <QToolButton>
#include <QSet>
#include "../ui/property_editor.h"
#include "models/footprint_definition.h"

#include "ui/footprint_editor_view.h"
#include "ui/footprint_library_browser_panel.h"
#include "ui/footprint_wizard_panel.h"

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;
using Flux::Model::Footprint3DModel;

class FootprintEditorView;
class QDragEnterEvent;
class QDropEvent;
class PCB3DWindow;

/**
 * @brief Dialog for creating and editing PCB footprints
 */
class FootprintModel3DPanel;

class FootprintEditor : public QDialog {
    Q_OBJECT
    friend class FootprintModel3DPanel;

public:
    explicit FootprintEditor(QWidget* parent = nullptr);
    explicit FootprintEditor(const FootprintDefinition& footprint, QWidget* parent = nullptr);
    ~FootprintEditor();
    
    FootprintDefinition footprintDefinition() const;
    void setFootprintDefinition(const FootprintDefinition& def);
    
    enum Tool { 
        Select, 
        Pad, 
        Line, 
        Arc, 
        Rect, 
        Circle, 
        Text,
        ZoomArea,
        Measure,
        Anchor
    };
    
signals:
    void footprintSaved(const FootprintDefinition& footprint);

private slots:
    void onToolSelected();
    void onSave();
    void onSaveToLibrary(); // Save to database/file
    void onClear();
    void onUndo();
    void onRedo();
    void onDelete();
    void onSelectionChanged();
    
    // Zoom tools
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onOpenWizard();
    
    // Alignment tools
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterH();
    void onAlignCenterV();
    void onDistributeH();
    void onDistributeV();
    void onMatchSpacing();
    void onMoveExactly();
    void onAddPrimitiveExact();
    
    // Flip tools
    void onFlipHorizontal();
    void onFlipVertical();
    void onCreateMirroredPair();
    
    // Advanced tools
    void onRunDRC();
    void onArrayTool();
    void onPolarGridTool();
    void onConvertToPad();
    void onImportKicadFootprint();
    void onOpen3DPreview();
    void onSetAnchor(QPointF pos);
    
    // Grid / Measure
    void onGridSizeChanged(const QString& size);
    void onMeasure(QPointF p1, QPointF p2);
    void onWizardGenerate(const FootprintDefinition& def);
    void onContextMenu(QPoint pos);
    void onRectResizeStarted(const QString& corner, QPointF scenePos);
    void onRectResizeUpdated(QPointF scenePos);
    void onRectResizeFinished(QPointF scenePos);
    
private:
    void setupUI();
    void createToolBar();
    void createPropertiesPanel();
    void createInfoPanel();
    void updatePropertiesPanel();
    void updatePreview();
    void drawGrid();
    void updateSceneFromDefinition();
    QGraphicsItem* buildVisual(const FootprintPrimitive& prim, int index);
    enum class SaveTarget { None, CurrentFlow, Library };
    bool ensureFootprintName();
    bool prepareFootprint();
    bool saveFootprintToCurrentFlow(bool closeAfterSave);
    bool saveFootprintToLibrary();
    bool promptForSaveTarget();
    bool importKicadFootprintFromFile(const QString& path);
    QString resolveModelPathForPreview(const QString& rawPath) const;
    bool hasUnsavedChanges() const;
    void applyPadToolbarDefaults(FootprintPrimitive& prim) const;
    void applyPadPresetFromDrill();
    void applyPadToolbarToSelection();
    void openPadSettingsDialog();
    void syncPadToolbarFromSelection();
    bool isLayerVisible(FootprintPrimitive::Layer layer) const;
    void setLayerVisibility(FootprintPrimitive::Layer layer, bool visible);
    void isolateLayer(FootprintPrimitive::Layer layer);
    void restoreAllLayerVisibility();
    void refreshLayerChipStates();
    void generateOutlineFromSelection(FootprintPrimitive::Layer layer, qreal margin, const QString& commandText);
    void renumberPads(const QString& pattern);
    
    // Current editing state
    // Current editing state
    Tool m_currentTool;
    
    // UI Components
    QGraphicsScene* m_scene;
    FootprintEditorView* m_view;
    QToolBar* m_toolbar;
    QToolBar* m_leftToolbar;
    QWidget* m_bottomPanel = nullptr;
    QWidget* m_leftNavigatorPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QDockWidget* m_propertiesDock;
    PropertyEditor* m_propertyEditor;
    QTabWidget* m_leftTabWidget = nullptr;
    QTabWidget* m_rightTabWidget = nullptr;
    QTabWidget* m_bottomTabWidget = nullptr;
    QTextEdit* m_codePreview = nullptr;
    QListWidget* m_ruleList = nullptr;
    
    // Footprint info
    QLineEdit* m_nameEdit;
    QLineEdit* m_descriptionEdit;
    QComboBox* m_categoryCombo;
    QComboBox* m_classificationCombo;
    QCheckBox* m_excludeBOMCheck;
    QCheckBox* m_excludePosCheck;
    QCheckBox* m_dnpCheck;
    QCheckBox* m_netTieCheck;
    QLineEdit* m_keywordsEdit;
    
    // 3D Model Settings
    FootprintModel3DPanel* m_model3DPanel = nullptr;
    
    // Internal state
    FootprintDefinition m_footprint;
    QList<QGraphicsItem*> m_drawnItems;
    QMap<QString, QAction*> m_toolActions;
    SaveTarget m_lastSaveTarget = SaveTarget::None;
    
    // Undo/Redo
    class QUndoStack* m_undoStack = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    
    // Drawing state
    bool m_isDrawing;
    QPointF m_startPoint;
    QGraphicsItem* m_previewItem;
    QList<QPointF> m_polyPoints;
    QList<QGraphicsItem*> m_resizeHandles;
    bool m_rectResizeSessionActive = false;
    int m_rectResizePrimIdx = -1;
    QString m_rectResizeCorner;
    FootprintDefinition m_rectResizeOldDef;
    QPointF m_rectResizeAnchor;
    QPointF m_resizeLineOtherEnd;
    QPointF m_resizeCircleCenter;
    
    // Pad settings
    QString m_currentPadShape;
    void setPadShape(const QString& shape);
    QComboBox* m_padShapeCombo = nullptr;
    QDoubleSpinBox* m_padWidthSpin = nullptr;
    QDoubleSpinBox* m_padHeightSpin = nullptr;
    QDoubleSpinBox* m_padDrillSpin = nullptr;
    QSpinBox* m_padNumberStepSpin = nullptr;
    QToolButton* m_padSettingsButton = nullptr;
    double m_padRotationDefault = 0.0;
    double m_padTrapezoidDeltaX = 0.0;

    // Layer settings
    FootprintPrimitive::Layer m_activeLayer;
    QComboBox* m_layerCombo;
    QWidget* m_layerChipsBar = nullptr;
    QMap<int, QToolButton*> m_layerChipButtons;
    QSet<int> m_visibleLayers;

    // Panels
    FootprintLibraryBrowserPanel* m_libraryBrowserPanel;
    FootprintWizardPanel* m_wizardPanel;

    QLabel* m_statusLabel; // Status information

    QPointer<PCB3DWindow> m_footprint3DWindow;
    QGraphicsScene* m_footprint3DScene = nullptr;
    QString m_lastImportBaseDir;
    QList<Footprint3DModel> m_models3D;
    QPointF m_lastMouseScenePos;
    
    QString getNextPadNumber() const;
    void clearResizeHandles();
    void updateResizeHandles();
    void populatePropertiesFor(int index);
    
private slots:
    void onLoadFootprint(const FootprintDefinition& def);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

#endif // FOOTPRINT_EDITOR_H
