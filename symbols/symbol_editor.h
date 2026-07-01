/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_EDITOR_H
#define SYMBOL_EDITOR_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QToolBar>
#include <QStatusBar>
#include <QToolButton>
#include <QDockWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QPointer>
#include <QTableWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QTextEdit>
#include <functional>
#include "models/symbol_definition.h"
#include "editor/symbol_canvas.h"
#include "../python/cpp/gemini/gemini_panel.h"
#include "ui/symbol_preview_widget.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class QAbstractGraphicsShapeItem;
class QGraphicsRectItem;
class PropertyEditor;

/**
 * @brief Main window for creating and editing schematic symbols
 */
class SymbolEditor : public QMainWindow {
    Q_OBJECT

public:
    explicit SymbolEditor(QWidget* parent = nullptr);
    explicit SymbolEditor(const SymbolDefinition& symbol, QWidget* parent = nullptr);
    ~SymbolEditor();
    
    SymbolDefinition symbolDefinition() const;
    void setSymbolDefinition(const SymbolDefinition& def);
    void applySymbolDefinition(const SymbolDefinition& def);
    void setProjectKey(const QString& key);

    bool importKicadSymbol(const QString& path, const QString& symbolName = QString());
    bool importLtspiceSymbol(const QString& path);
    bool loadLibrary(const QString& path);
    
Q_SIGNALS:
    void symbolSaved(const SymbolDefinition& symbol);
    void placeInSchematicRequested(const SymbolDefinition& symbol);

private Q_SLOTS:
    void onToolSelected();
    void onSave();
    void onSaveToLibrary();
    void onExportVioSym();
    void onRefreshLibraries();
    void onClear();
    void onUndo();
    void onRedo();
    void onDelete();
    void onSelectionChanged();
    void onNewSymbol();
    void onAIDatasheetImport();
    void onImportSpiceSubcircuit();
    void onRotateCW();
    void onRotateCCW();
    void onFlipH();
    void onFlipV();
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterX();
    void onAlignCenterY();
    void onDistributeH();
    void onDistributeV();
    void onMatchSpacing();
    void onMoveExactly();
    void onAddPrimitiveExact();
    void onSnapToGrid();
    void onPinTable();
    void onApplySmartSubcktMapping();
    void onApplyOrderedSubcktMapping();
    void onClearSubcktMapping();
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomSelection();
    void onCopy();
    void onPaste();
    void onDuplicate();
    void onItemErased(QGraphicsItem* item);
    void onGridSizeChanged(const QString& size);
    void onUnitChanged(int index);
    void onCopyToAlternateStyle();
    void updateCoordinates(QPointF pos);
    void onAiSymbolGenerated(const QString& json);
    void onWizardSymbolGenerated(const SymbolDefinition& def);
    void onWizardSaveTemplate();
    void onImportKicadSymbol();
    void onImportLtspiceSymbol();
    void onImportImage();
    void onManageCustomFields();
    void onBrowseFootprint();
    void onPlaceInSchematic();
    void onRunSRC();
    void onCanvasContextMenu(const QPoint& pos);
    void onPropertyChanged(const QString& name, const QVariant& value);

private:
    void applyTheme();
    void setupUI();
    QIcon getThemeIcon(const QString& path, bool tinted = true);
    void updatePropertiesPanel();
    void populatePropertiesFor(int index);
    void createMenuBar();
    void createToolBar();
    void rebuildPanelsMenu();
    void tryAutoDetectModelName();
    void onSpiceSubcircuitCodeChanged();
    void createStatusBar();
    enum class SaveTarget { None, CurrentFlow, Library };
    void setEditingUnlocked(bool unlocked, const QString& message = QString());
    QString promptForTargetLibrary();
    bool saveSymbolToCurrentFlow();
    bool saveSymbolToLibrary();
    bool promptForSaveTarget();
    void connectViewSignals();
    void createSymbolInfoPanel();

    void updateSubcktMappingTable();
    void refreshSubcktMappingStatus();
    QString currentSymbolPinSignature() const;
    void markSubcktMappingSynchronized();
    QStringList currentSubcktPinNames() const;
    QStringList buildSmartSubcktMapping(const QStringList& subcktPins) const;
    void applySubcktMappingList(const QStringList& mappedPins);
    QWidget* createSymbolMetadataWidget();
    void openSubcircuitPicker();
    QStringList currentSymbolPinNames() const;
    bool validateCurrentSymbolForSave(QStringList* errors, QStringList* warnings) const;
    void updateCodePreview();
      


protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

public:
    enum Tool { Select, Line, Rect, Circle, Arc, Text, Pin, Polygon, Erase, ZoomArea, Anchor, Bezier, Image, Pen };
     
    // UI Components
    SymbolCanvas* m_canvas = nullptr;
    PropertyEditor* m_propertyEditor = nullptr;
    QToolBar* m_toolbar = nullptr;
    QToolBar* m_leftToolbar = nullptr;
    class QMenu* m_panelsMenu = nullptr;
    QStatusBar* m_statusBar = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_gridLabel = nullptr;
    QAction* m_selectAction = nullptr;
    
    // Multi-unit & Styles
    QComboBox* m_unitCombo = nullptr;
    QComboBox* m_styleCombo = nullptr;
    QComboBox* m_colorPresetCombo = nullptr;
    int m_currentUnit = 1; // 0 = shared, 1 = Unit A...
    int m_currentStyle = 0; // 0 = shared, 1 = Standard, 2 = Alternate
    int m_colorPreset = 0; // 0 = Theme, 1..N = editor color presets

    // Symbol info
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_descriptionEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QLineEdit* m_prefixEdit = nullptr;
    QLineEdit* m_footprintEdit = nullptr;
    QTextEdit* m_codePreview = nullptr;
    QComboBox* m_modelSourceCombo = nullptr;
    QLineEdit* m_modelPathEdit = nullptr;
    QLineEdit* m_modelNameEdit = nullptr;
    QPlainTextEdit* m_spiceSubcircuitEdit = nullptr;
    QWidget* m_embeddedCodeWidget = nullptr;
    QTableWidget* m_subcktMappingTable = nullptr;
    QLabel* m_subcktMappingSummaryLabel = nullptr;
    QLabel* m_subcktSyncLabel = nullptr;
    QString m_subcktMappingPinSignature;
    
    // Properties panel
    class QTabWidget* m_propsTabWidget = nullptr;
    QDockWidget* m_propsDock = nullptr;
    class SymbolPropertiesPanel* m_propertiesPanel = nullptr;
    
    // Library Browser
    class SymbolLibraryBrowserPanel* m_libraryBrowser = nullptr;
    class SymbolPinTablePanel* m_pinTablePanel = nullptr;
    QListWidget* m_srcList = nullptr;
    class GeminiPanel* m_aiPanel = nullptr;
    SymbolPreviewWidget* m_livePreview = nullptr;
    


    // Wizard
    class SymbolWizardPanel* m_wizardPanel = nullptr;
    
    // Internal state
    SymbolDefinition m_symbol;
    QList<SymbolPrimitive> m_copyBuffer;
    QMap<QString, QAction*> m_toolActions;
    
    // Undo/Redo
    class QUndoStack* m_undoStack = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_rotateCWAct = nullptr;

    bool m_editingUnlocked = false;
    QString m_targetLibraryName;
    QString m_projectKey;
    SaveTarget m_lastSaveTarget = SaveTarget::None;

    friend class AddPrimitiveCommand;
    friend class RemovePrimitiveCommand;
    friend class UpdateSymbolCommand;
    friend class SymbolCanvas;
    friend class SymbolPropertiesPanel;
    friend class SymbolPinTablePanel;
    friend class SymbolLibraryBrowserPanel;
};

#endif // SYMBOL_EDITOR_H
