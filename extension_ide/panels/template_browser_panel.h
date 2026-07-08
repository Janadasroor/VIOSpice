/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEMPLATE_BROWSER_PANEL_H
#define TEMPLATE_BROWSER_PANEL_H

#include <QWidget>

class QListWidget;
class QLineEdit;
class QListWidgetItem;

namespace IDE {

class TemplateBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit TemplateBrowserPanel(QWidget* parent = nullptr);
    void reapplyTheme();

signals:
    void templateSelected(const QString& filePath);

private slots:
    void onSearchChanged(const QString& text);
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void loadTemplates();

    QListWidget* m_templateList = nullptr;
    QLineEdit* m_searchEdit = nullptr;
};

} // namespace IDE

#endif // TEMPLATE_BROWSER_PANEL_H
