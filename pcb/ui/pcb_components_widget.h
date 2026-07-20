/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCBCOMPONENTSWIDGET_H
#define PCBCOMPONENTSWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QSettings>

class PCBComponentsWidget : public QWidget {
    Q_OBJECT

public:
    explicit PCBComponentsWidget(QWidget *parent = nullptr);
    ~PCBComponentsWidget();

    void populate();

signals:
    void footprintSelected(const QString &fpName);
    void footprintCreated(const QString &fpName);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onCreateFootprint();
    void onOpenLibraryBrowser();

    // Extra features slots
    void onToggleRecentSection();
    void onToggleStandardSection();
    void onToggleCompactMode();
    void onToggleActionCards();
    void onClearRecent();
    void onFilterChanged(int index);

private:
    void updatePreview(const QString& fpName, const QString& libName = QString());
    void setupRecentSection();
    void setupFilterChips();
    QWidget* createSectionHeader(const QString& title, bool expanded, std::function<void()> toggleFn);
    void addRecentFootprint(const QString& name);
    void updateRecentSection();
    void applyCompactMode(bool compact);
    void updateSectionHeader(QPushButton* indicator, bool expanded);

    QLineEdit *m_searchBox;
    QTreeWidget *m_componentList;
    class FootprintPreviewView *m_previewView;

    // Search debounce
    QTimer *m_searchDebounceTimer;
    QString m_pendingSearchText;

    // Filter dropdown
    QComboBox *m_filterCombo;
    QString m_activeCategory;

    // Collapsible sections
    QWidget *m_recentHeader;
    QWidget *m_recentContainer;
    QPushButton *m_recentIndicator;
    bool m_recentExpanded = true;

    QWidget *m_standardHeader;
    QPushButton *m_standardIndicator;
    bool m_standardExpanded = true;

    // Recently placed list
    QVBoxLayout *m_recentLayout;
    QStringList m_recentList;

    // Compact mode
    bool m_compactMode = false;
    QPushButton *m_compactToggle;

    // Action cards container
    QWidget *m_actionContainer;
    QPushButton *m_actionCardsToggle;
    bool m_actionsVisible = true;
};

#endif // PCBCOMPONENTSWIDGET_H
