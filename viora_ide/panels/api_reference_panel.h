/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef API_REFERENCE_PANEL_H
#define API_REFERENCE_PANEL_H

#include <QWidget>

class QListView;
class QLineEdit;
class QTextEdit;
class QStandardItemModel;

namespace IDE {

class ApiReferencePanel : public QWidget {
    Q_OBJECT
public:
    explicit ApiReferencePanel(QWidget* parent = nullptr);
    void reapplyTheme();

    void filterByCategory(const QString& category);

signals:
    void functionSelected(const QString& functionName);
    void insertRequested(const QString& text);

private slots:
    void onSearchChanged(const QString& text);
    void onItemClicked(const QModelIndex& index);

private:
    void setupUI();
    void loadApiData();

    QListView* m_listView = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTextEdit* m_detailView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QString m_currentCategory;
};

} // namespace IDE

#endif // API_REFERENCE_PANEL_H
