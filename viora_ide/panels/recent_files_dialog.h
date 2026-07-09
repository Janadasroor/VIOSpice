/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RECENT_FILES_DIALOG_H
#define RECENT_FILES_DIALOG_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>

namespace IDE {

class RecentFilesDialog : public QWidget {
    Q_OBJECT
public:
    explicit RecentFilesDialog(QWidget* parent = nullptr);

    void setRecentFiles(const QStringList& files);
    void showDialog();
    void hideDialog();
    bool isDialogVisible() const;

signals:
    void fileSelected(const QString& filePath);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private slots:
    void onFilterChanged(const QString& text);
    void onItemActivated(QListWidgetItem* item);

private:
    void applyTheme();
    void rebuildFilteredList(const QString& filter);
    void positionOverParent();

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
    QLabel* m_countLabel = nullptr;
    QStringList m_files;
};

} // namespace IDE

#endif // RECENT_FILES_DIALOG_H
