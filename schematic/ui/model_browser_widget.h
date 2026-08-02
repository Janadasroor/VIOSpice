/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODEL_BROWSER_WIDGET_H
#define MODEL_BROWSER_WIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeView>
#include "model_tree_model.h"

class QTimer;
class QComboBox;
class QCheckBox;

class ModelBrowserWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelBrowserWidget(QWidget* parent = nullptr);
    ~ModelBrowserWidget();

Q_SIGNALS:
    void modelSelected(const SpiceModelInfo& info);
    void applyModelRequested(const SpiceModelInfo& info);

public:
    void setUsedModels(const QSet<QString>& used);

private Q_SLOTS:
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged(const QModelIndex& current);
    void onApplyClicked();
    void onReloadClicked();
    void onLibraryReloaded();
    void applyTheme();

private:
    void setupUI();
    void updateCategoryCounts();
    void expandToFit();

    QLineEdit* m_searchBox;
    QComboBox* m_categoryCombo;
    QCheckBox* m_favOnlyCheck;
    QTreeView* m_treeView;
    ModelTreeModel* m_model;
    ModelTreeFilterProxy* m_proxyModel;

    QTimer* m_searchDebounceTimer;
    QString m_pendingSearchText;

    QLabel* m_detailLabel;
    QPushButton* m_applyBtn;
};

#endif // MODEL_BROWSER_WIDGET_H
