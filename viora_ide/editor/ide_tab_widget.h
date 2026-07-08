/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_TAB_WIDGET_H
#define IDE_TAB_WIDGET_H

#include <QTabWidget>
#include <QMap>

class QWidget;

namespace IDE {

class IdeEditor;

class IdeTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit IdeTabWidget(QWidget* parent = nullptr);

    IdeEditor* addEditorTab(const QString& filePath = QString());
    IdeEditor* openFile(const QString& filePath);
    bool saveCurrentFile();
    bool saveFileAs(int index);
    void closeTab(int index);
    void closeAllTabs();
    void closeOtherTabs(int keepIndex);
    void reopenClosedTab();

    IdeEditor* currentEditor() const;
    IdeEditor* editorAt(int index) const;

    bool hasUnsavedChanges() const;
    QStringList openFilePaths() const;

signals:
    void tabModifiedChanged(int index, bool modified);
    void currentEditorChanged(IdeEditor* editor);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);
    void onModificationChanged(bool modified);

private:
    void setupTabContextMenu();
    void applyTabTheme();
    int findTabByFilePath(const QString& path) const;
    QString tabTitleForFile(const QString& path, bool modified = false) const;

    struct ClosedTab {
        QString filePath;
        QString content;
    };
    QList<ClosedTab> m_closedTabs;
    QMap<int, bool> m_modifiedState;
    QWidget* m_welcomeWidget = nullptr;
};

} // namespace IDE

#endif // IDE_TAB_WIDGET_H
