/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_LIBRARY_BROWSER_PANEL_H
#define SYMBOL_LIBRARY_BROWSER_PANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QGraphicsView>
#include <QGraphicsScene>

class SymbolEditor;

class SymbolLibraryBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit SymbolLibraryBrowserPanel(SymbolEditor* editor, QWidget* parent = nullptr);
    ~SymbolLibraryBrowserPanel() = default;

    void populateLibraryTree();
    void onRefreshLibraries();
    void applyTheme();

    QLineEdit* libSearchEdit() const { return m_libSearchEdit; }
    QTreeWidget* libraryTree() const { return m_libraryTree; }
    QGraphicsView* libPreviewView() const { return m_libPreviewView; }
    QGraphicsScene* libPreviewScene() const { return m_libPreviewScene; }

private Q_SLOTS:
    void onLibSearchChanged(const QString& text);
    void onLibraryContextMenu(const QPoint& pos);
    void onCloneSymbol(class QTreeWidgetItem* item, int column);
    void onLibraryItemClicked(class QTreeWidgetItem* item, int column);

private:
    void setupUI();
    QIcon getThemeIcon(const QString& path, bool tinted = true);

    SymbolEditor* m_editor = nullptr;
    QLineEdit* m_libSearchEdit = nullptr;
    QTreeWidget* m_libraryTree = nullptr;
    QGraphicsView* m_libPreviewView = nullptr;
    QGraphicsScene* m_libPreviewScene = nullptr;
};

#endif // SYMBOL_LIBRARY_BROWSER_PANEL_H
