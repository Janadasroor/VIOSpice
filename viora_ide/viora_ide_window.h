/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VIORA_IDE_WINDOW_H
#define VIORA_IDE_WINDOW_H

#include <QMainWindow>
#include <QMap>

class QToolBar;
class QDockWidget;
class QAction;
class QSplitter;
class QLabel;
class QStackedWidget;
class QToolButton;
class QTabBar;

class SourceControlPanel;

namespace IDE {

class IdeTabWidget;
class IdeEditor;
class FileTreePanel;
class ApiReferencePanel;
class OutputPanel;
class ManifestEditorPanel;
class TemplateBrowserPanel;
class ExtensionRunner;
class IdeFindReplace;
class ProblemsPanel;
class LspClient;
class CommandPalette;
class RecentFilesDialog;
class IdeDebugger;

class VioraIdeWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit VioraIdeWindow(QWidget* parent = nullptr);
    ~VioraIdeWindow();

    void openFile(const QString& filePath);
    void openVioraDirectory(const QString& dirPath);

    QString currentExtensionDir() const { return m_extensionDir; }

signals:
    void extensionReloadRequested(const QString& dirPath);
    void editorHoverRequested(const QString& filePath, int line, int character);
    void editorGoToDefRequested(const QString& filePath, int line, int character);
    void editorFindRefsRequested(const QString& filePath, int line, int character);
    void editorFormatRequested(const QString& filePath);
    void editorSignatureHelpRequested(const QString& filePath, int line, int character);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewFile();
    void onOpenFile();
    void onOpenDirectory();
    void onSave();
    void onSaveAs();
    void onSaveAll();
    void onRunExtension();
    void onStopExtension();
    void updateRunButtons(bool running);
    void onDebugStart();
    void onDebugStop();
    void onDebugStepOver();
    void onDebugStepInto();
    void onNewExtension();
    void onEditManifest();
    void onShowFindReplace();
    void onSettings();
    void showCommandPalette();
    void showRecentFiles();

    void onToggleExplorerPanel();
    void onToggleBottomPanel();
    void onToggleRightPanel();

    void onCurrentEditorChanged(IdeEditor* editor);
    void onTabModifiedChanged(int index, bool modified);
    void onExtensionOutput(const QString& message);
    void onExtensionError(const QString& message);
    void onExtensionRunFinished(bool success);

    void onViewToggled(bool visible);

private:
    void setupMenus();
    void setupToolbar();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();
    void setupContextMenu();
    void setupSidebarIcons();

    void updateTitle();
    void updateEditorActions();
    void applyTheme();
    void detectLanguage(const QString& filePath);
    void saveWindowState();
    void restoreWindowState();

    QToolBar* m_mainToolBar = nullptr;
    QDockWidget* m_fileTreeDock = nullptr;
    QDockWidget* m_apiRefDock = nullptr;
    QDockWidget* m_outputDock = nullptr;
    QDockWidget* m_templateDock = nullptr;
    QDockWidget* m_manifestDock = nullptr;

    IdeTabWidget* m_tabWidget = nullptr;
    FileTreePanel* m_fileTreePanel = nullptr;
    ApiReferencePanel* m_apiRefPanel = nullptr;
    OutputPanel* m_outputPanel = nullptr;
    TemplateBrowserPanel* m_templatePanel = nullptr;
    ManifestEditorPanel* m_manifestPanel = nullptr;
    ProblemsPanel* m_problemsPanel = nullptr;
    SourceControlPanel* m_sourceControlPanel = nullptr;
    ExtensionRunner* m_runner = nullptr;
    LspClient* m_lspClient = nullptr;
    IdeDebugger* m_debugger = nullptr;
    CommandPalette* m_commandPalette = nullptr;
    RecentFilesDialog* m_recentFilesDialog = nullptr;
    QStringList m_recentFiles;

    QLabel* m_cursorLabel = nullptr;
    QLabel* m_languageLabel = nullptr;
    QLabel* m_errorCountLabel = nullptr;
    IdeEditor* m_currentEditor = nullptr;
    QString m_extensionDir;

    // Sidebar
    QToolButton* m_hamburgerBtn = nullptr;
    QWidget* m_sidebarStrip = nullptr;
    QToolButton* m_sidebarExplorerBtn = nullptr;
    QToolButton* m_sidebarSearchBtn = nullptr;
    QToolButton* m_sidebarGitBtn = nullptr;
    QToolButton* m_sidebarSettingsBtn = nullptr;

    // Toolbar run/pause/stop button (single button that changes state)
    QToolButton* m_runBtn = nullptr;
    bool m_isRunning = false;

    IdeFindReplace* m_findReplaceBar = nullptr;
    QDockWidget* m_findDock = nullptr;

    // Panel toggle widgets (stored for show/hide)
    QWidget* m_explorerWidget = nullptr;
    QWidget* m_bottomPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QSplitter* m_contentSplitter = nullptr;
    QSplitter* m_centerSplitter = nullptr;

    // Toggle buttons
    QToolButton* m_toggleExplorerBtn = nullptr;
    QToolButton* m_toggleBottomBtn = nullptr;
    QToolButton* m_toggleRightBtn = nullptr;

    QTabBar* m_rightTabs = nullptr;
    QStackedWidget* m_rightStack = nullptr;
    QTabBar* m_bottomTabs = nullptr;
    QStackedWidget* m_bottomStack = nullptr;
};

} // namespace IDE

#endif // VIORA_IDE_WINDOW_H
