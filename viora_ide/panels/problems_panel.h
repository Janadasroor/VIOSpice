/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PROBLEMS_PANEL_H
#define PROBLEMS_PANEL_H

#include <QWidget>
#include <QTreeWidget>

namespace IDE {

struct LspDiagnostic;

class ProblemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(QWidget* parent = nullptr);

    void setDiagnostics(const QString& filePath, const QList<LspDiagnostic>& diagnostics);
    void clearAll();
    void clearFile(const QString& filePath);

    int errorCount() const;
    int warningCount() const;

signals:
    void problemClicked(const QString& filePath, int line, int column);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    QTreeWidget* m_tree = nullptr;
};

} // namespace IDE

#endif // PROBLEMS_PANEL_H
