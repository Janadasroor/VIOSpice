/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FILE_TREE_PANEL_H
#define FILE_TREE_PANEL_H

#include <QWidget>

class QTreeView;
class QFileSystemModel;
class QSortFilterProxyModel;
class QLineEdit;
class QPushButton;

namespace IDE {

class FileTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit FileTreePanel(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    void reapplyTheme();
    QString rootPath() const { return m_rootPath; }

signals:
    void fileDoubleClicked(const QString& filePath);
    void fileRenamed(const QString& oldPath, const QString& newPath);
    void fileDeleted(const QString& filePath);
    void newFileRequested(const QString& parentDir);
    void newFolderRequested(const QString& parentDir);

private slots:
    void onDoubleClicked(const QModelIndex& index);
    void onCustomContextMenu(const QPoint& pos);
    void onFilterChanged(const QString& text);

private:
    void setupUI();
    void refreshTree();
    QString relativePath(const QString& absolutePath) const;

    QTreeView* m_tree = nullptr;
    QFileSystemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QString m_rootPath;
};

} // namespace IDE

#endif // FILE_TREE_PANEL_H
