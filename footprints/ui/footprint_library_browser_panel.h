/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOOTPRINT_LIBRARY_BROWSER_PANEL_H
#define FOOTPRINT_LIBRARY_BROWSER_PANEL_H

#include <QWidget>
#include "../models/footprint_definition.h"

class QTreeWidget;
class QLineEdit;
class QTreeWidgetItem;

using Flux::Model::FootprintDefinition;

class FootprintLibraryBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit FootprintLibraryBrowserPanel(QWidget* parent = nullptr);
    ~FootprintLibraryBrowserPanel() override = default;

    void populateLibraryTree();

signals:
    void footprintSelected(const FootprintDefinition& def);

private slots:
    void onLibSearchChanged(const QString& text);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void setupUI();

    QLineEdit* m_libSearchEdit;
    QTreeWidget* m_libraryTree;
};

#endif // FOOTPRINT_LIBRARY_BROWSER_PANEL_H
