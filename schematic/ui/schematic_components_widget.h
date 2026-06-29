/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATICCOMPONENTSWIDGET_H
#define SCHEMATICCOMPONENTSWIDGET_H

#include <QWidget>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include "../../ui/symbol_list_model.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTimer>

#include "../../symbols/ui/symbol_preview_widget.h"

class SchematicComponentsWidget : public QWidget {
    Q_OBJECT

public:
    explicit SchematicComponentsWidget(QWidget *parent = nullptr);
    ~SchematicComponentsWidget();

    void populate();
    void focusSearch();

Q_SIGNALS:
    void toolSelected(const QString &toolName);
    void symbolCreated(const QString &symbolName);
    void symbolPlacementRequested(const class SymbolDefinition& symbol);
    void modelAssignmentRequested(const QString& modelName);
    void componentDropped(const QString& componentName, const QPointF& scenePos);
    
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void onSearchTextChanged(const QString &text);
    void onItemClicked(const QModelIndex& index);
    void onCreateSymbol();
    void onOpenLibraryBrowser();
    void onApplyModelRequested(const SpiceModelInfo& info);
    void onFilterChipClicked(int chipIndex);
    void onClearRecent();
    void onToggleRecentSection();
    void onToggleStandardSection();
    void onToggleCompactMode();
    void onToggleActionCards();

private Q_SLOTS:
    void onItemHovered(const QModelIndex& index);

private:
    void setupFilterChips();
    void setupRecentSection();
    QWidget* createSectionHeader(const QString& title, bool expanded, std::function<void()> toggleFn);
    void addRecentComponent(const QString& name);
    void updateRecentSection();
    void applyCompactMode(bool compact);
    void updateSectionHeader(QPushButton* indicator, bool expanded);

    QTabWidget *m_tabs;
    QWidget *m_symbolTab;
    class ModelBrowserWidget *m_modelTab;
    class QProgressBar *m_progressBar;

    QLineEdit *m_searchBox;
    QTreeView *m_componentList;
    SymbolListModel *m_symbolListModel;
    QSortFilterProxyModel *m_proxyModel;

    SymbolDefinition m_selectedSymbol;
    SymbolPreviewWidget* m_previewPopup;

    // Filter dropdown
    QComboBox *m_filterCombo;
    int m_activeChipIndex = 0;

    // Section headers (collapsible)
    QWidget *m_recentHeader;
    QWidget *m_recentContainer;
    QPushButton *m_recentIndicator;
    bool m_recentExpanded = true;

    QWidget *m_standardHeader;
    QPushButton *m_standardIndicator;
    bool m_standardExpanded = true;

    // Recently placed
    QVBoxLayout *m_recentLayout;
    QStringList m_recentList;

    // Search debounce
    QTimer *m_searchDebounceTimer;
    QString m_pendingSearchText;

    // Action cards
    QWidget *m_actionContainer;
    QPushButton *m_actionCardsToggle;
    bool m_actionsVisible = true;

    // Compact mode
    bool m_compactMode = false;
    QPushButton *m_compactToggle;
};

#endif // SCHEMATICCOMPONENTSWIDGET_H
