/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "viora_ide_window.h"
#include "editor/ide_editor.h"
#include "editor/ide_tab_widget.h"
#include "editor/ide_find_replace.h"
#include "panels/file_tree_panel.h"
#include "panels/api_reference_panel.h"
#include "panels/output_panel.h"
#include "panels/manifest_editor_panel.h"
#include "panels/template_browser_panel.h"
#include "panels/extension_scaffold_dialog.h"
#include "panels/problems_panel.h"
#include "panels/command_palette.h"
#include "panels/recent_files_dialog.h"
#include "core/extension_runner.h"
#include "core/lsp_client.h"
#include "../ui/source_control_panel.h"
#include "../ui/source_control_manager.h"
#include "../core/project/config_manager.h"
#include <QToolBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QTabBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QToolTip>
#include <QInputDialog>
#include <QShortcut>
#include "../core/visuals/theme_manager.h"
#include "../core/visuals/theme.h"
#include "core/ide_theme.h"

namespace IDE {

// ── Theme colors ─────────────────────────────────────────────
static IdeTheme tc;

static void loadThemeColors() { tc = currentTheme(); }

static const char* C(const QString& s) { return s.toLocal8Bit().constData(); }

#define kBgDarkest    C(tc.bgDarkest)
#define kBgPanel      C(tc.bgPanel)
#define kBgEditor     C(tc.bgEditor)
#define kTextPrimary  C(tc.textPrimary)
#define kTextSecondary C(tc.textSecondary)
#define kAccentBlue   C(tc.accentBlue)
#define kGreen        C(tc.green)
#define kRed          C(tc.red)
#define kBorder       C(tc.border)
#define kBgTabActive  C(tc.bgTabActive)
#define kBgTabInactive C(tc.bgTabInactive)

VioraIdeWindow::VioraIdeWindow(QWidget* parent)
    : QMainWindow(parent) {
    loadThemeColors();

    setWindowTitle("VioraIDE - VioraEDA");
    setMinimumSize(900, 600);
    resize(1280, 800);

    applyTheme();

    m_runner = new ExtensionRunner(this);

    setupSidebarIcons();
    setupDockWidgets();
    setupMenus();
    setupToolbar();
    setupStatusBar();
    setupConnections();
    setupContextMenu();

    // Save state on application quit
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, [this]() { saveWindowState(); });

    restoreWindowState();

    // Open demo_extension by default if no directory was restored
    if (m_extensionDir.isEmpty()) {
        QString appDir = QCoreApplication::applicationDirPath();
        QString demoExt = appDir + "/../demo_extension";
        if (QDir(demoExt).exists()) {
            openVioraDirectory(demoExt);
        }
    }
}

VioraIdeWindow::~VioraIdeWindow() {
    saveWindowState();
}

// ============================================================================
// Setup
// ============================================================================

void VioraIdeWindow::setupMenus() {
    // File menu
    auto* fileMenu = new QMenu("File", this);
    fileMenu->addAction("&New File", this, &VioraIdeWindow::onNewFile, QKeySequence::New);
    fileMenu->addAction("&Open File...", this, &VioraIdeWindow::onOpenFile, QKeySequence::Open);
    fileMenu->addAction("Open &Directory...", this, &VioraIdeWindow::onOpenDirectory, QKeySequence("Ctrl+Shift+O"));
    fileMenu->addSeparator();
    fileMenu->addAction("&Save", this, &VioraIdeWindow::onSave, QKeySequence::Save);
    fileMenu->addAction("Save &As...", this, &VioraIdeWindow::onSaveAs, QKeySequence("Ctrl+Shift+S"));
    fileMenu->addAction("Save A&ll", this, &VioraIdeWindow::onSaveAll, QKeySequence("Ctrl+Shift+A"));
    fileMenu->addSeparator();
    fileMenu->addAction("&Close Tab", this, [this]() {
        if (m_tabWidget) m_tabWidget->closeTab(m_tabWidget->currentIndex());
    }, QKeySequence::Close);
    fileMenu->addAction("Close &All", this, [this]() {
        if (m_tabWidget) m_tabWidget->closeAllTabs();
    });
    fileMenu->addAction("&Reopen Closed Tab", this, [this]() {
        if (m_tabWidget) m_tabWidget->reopenClosedTab();
    }, QKeySequence("Ctrl+Shift+T"));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit, QKeySequence::Quit);

    // Edit menu
    auto* editMenu = new QMenu("Edit", this);
    editMenu->addAction("&Undo", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->undo();
    }, QKeySequence::Undo);
    editMenu->addAction("&Redo", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->redo();
    }, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction("Cu&t", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->cut();
    }, QKeySequence::Cut);
    editMenu->addAction("&Copy", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->copy();
    }, QKeySequence::Copy);
    editMenu->addAction("&Paste", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->paste();
    }, QKeySequence::Paste);
    editMenu->addAction("&Select All", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            e->selectAll();
    }, QKeySequence::SelectAll);
    editMenu->addSeparator();
    editMenu->addAction("&Find && Replace...", this, &VioraIdeWindow::onShowFindReplace, QKeySequence::Find);
    editMenu->addAction("&Go to Line...", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr) {
            // Simple go-to-line: prompt via status bar or use a basic input
            bool ok;
            int line = QInputDialog::getInt(this, "Go to Line", "Line number:", 1, 1, e->document()->blockCount(), 1, &ok);
            if (ok) e->goToLine(line);
        }
    }, QKeySequence("Ctrl+G"));
    editMenu->addAction("For&mat Document", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr) {
            emit editorFormatRequested(e->filePath());
        }
    }, QKeySequence("Ctrl+Shift+F"));

    // View menu
    auto* viewMenu = new QMenu("View", this);
    viewMenu->addAction("Toggle &Explorer", this, &VioraIdeWindow::onToggleExplorerPanel, QKeySequence("Ctrl+E"));
    viewMenu->addAction("Toggle &Bottom Panel", this, &VioraIdeWindow::onToggleBottomPanel, QKeySequence("Ctrl+`"));
    viewMenu->addAction("Toggle &Right Panel", this, &VioraIdeWindow::onToggleRightPanel, QKeySequence("Ctrl+Shift+E"));
    viewMenu->addSeparator();
    viewMenu->addAction("&Command Palette", this, &VioraIdeWindow::showCommandPalette);
    viewMenu->addAction("&Recent Files", this, &VioraIdeWindow::showRecentFiles);

    // Navigation menu
    auto* navMenu = new QMenu("Navigation", this);
    navMenu->addAction("&Go to Definition", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr) {
            emit editorGoToDefRequested(e->filePath(), e->cursorLine() - 1, e->cursorColumn() - 1);
        }
    }, QKeySequence("F12"));
    navMenu->addAction("Find All &References", this, [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr) {
            emit editorFindRefsRequested(e->filePath(), e->cursorLine() - 1, e->cursorColumn() - 1);
        }
    }, QKeySequence("Shift+F12"));
    navMenu->addSeparator();
    navMenu->addAction("Next &Tab", this, [this]() {
        if (m_tabWidget) {
            int next = m_tabWidget->currentIndex() + 1;
            if (next >= m_tabWidget->count()) next = 0;
            m_tabWidget->setCurrentIndex(next);
        }
    }, QKeySequence("Ctrl+Tab"));
    navMenu->addAction("&Previous Tab", this, [this]() {
        if (m_tabWidget) {
            int prev = m_tabWidget->currentIndex() - 1;
            if (prev < 0) prev = m_tabWidget->count() - 1;
            m_tabWidget->setCurrentIndex(prev);
        }
    }, QKeySequence("Ctrl+Shift+Tab"));

    // Run menu
    auto* runMenu = new QMenu("Run", this);
    runMenu->addAction("&Run Extension", this, &VioraIdeWindow::onRunExtension, QKeySequence("F5"));
    runMenu->addAction("&Stop", this, &VioraIdeWindow::onStopExtension, QKeySequence("Shift+F5"));

    // Extensions menu
    auto* extMenu = new QMenu("Extensions", this);
    extMenu->addAction("&New Extension...", this, &VioraIdeWindow::onNewExtension);
    extMenu->addAction("&Edit Manifest...", this, &VioraIdeWindow::onEditManifest);

    // Add menus to the native menu bar (prevents crashes in Qt 6.10+)
    menuBar()->addMenu(fileMenu);
    menuBar()->addMenu(editMenu);
    menuBar()->addMenu(viewMenu);
    menuBar()->addMenu(navMenu);
    menuBar()->addMenu(runMenu);
    menuBar()->addMenu(extMenu);

    // Hide the menu bar visually (setVisible doesn't work in Qt 6.10+)
    menuBar()->setStyleSheet(
        "QMenuBar { max-height: 0px; padding: 0px; }"
        "QMenuBar::item { max-height: 0px; padding: 0px; }"
    );

    // Hamburger menu assembles all sub-menus via a standalone QMenu
    auto hamburgerStyle = QString(
        "QToolButton { background: transparent; color: %1; border: 1px solid transparent; "
        "padding: 6px 10px; border-radius: 4px; font-size: 16pt; font-weight: bold; }"
        "QToolButton:hover { background: %2; border-color: %3; }"
    ).arg(kTextPrimary, kBorder, kAccentBlue);

    m_hamburgerBtn = new QToolButton();
    m_hamburgerBtn->setText(QString::fromUtf8("\u2261")); // ≡
    m_hamburgerBtn->setToolTip("Menu");
    m_hamburgerBtn->setStyleSheet(hamburgerStyle);
    m_hamburgerBtn->setFixedSize(36, 36);
    m_hamburgerBtn->setCursor(Qt::PointingHandCursor);
    m_hamburgerBtn->setPopupMode(QToolButton::InstantPopup);

    auto* hamburgerMenu = new QMenu(m_hamburgerBtn);
    hamburgerMenu->addMenu(fileMenu);
    hamburgerMenu->addMenu(editMenu);
    hamburgerMenu->addMenu(runMenu);
    hamburgerMenu->addMenu(extMenu);
    m_hamburgerBtn->setMenu(hamburgerMenu);
}

void VioraIdeWindow::setupToolbar() {
    m_mainToolBar = addToolBar("Main");
    m_mainToolBar->setObjectName("MainToolBar");
    m_mainToolBar->setMovable(false);
    m_mainToolBar->setIconSize(QSize(16, 16));
    m_mainToolBar->setStyleSheet(
        QString(
            "QToolBar { background: %1; border-bottom: 1px solid %2; spacing: 8px; padding: 6px 12px; }"
        ).arg(kBgDarkest, kBorder)
    );

    // Hamburger goes in menu bar, not toolbar
    menuBar()->setFixedWidth(48);
    menuBar()->setCornerWidget(m_hamburgerBtn);

    auto makePillBtn = [this](const QString& text, const QString& tip, const QString& bgColor, const QString& hoverColor, bool whiteText = true) {
        auto* btn = new QToolButton();
        btn->setText(text);
        btn->setToolTip(tip);
        QString textColor = whiteText ? "white" : kTextPrimary;
        btn->setStyleSheet(QString(
            "QToolButton { background: %1; color: %2; border: none; "
            "padding: 4px 12px; border-radius: 14px; font-size: 9pt; font-weight: 600; }"
            "QToolButton:hover { background: %3; }"
        ).arg(bgColor, textColor, hoverColor));
        btn->setMinimumHeight(28);
        btn->setMinimumWidth(50);
        btn->setIconSize(QSize(14, 14));
        btn->setCursor(Qt::PointingHandCursor);
        m_mainToolBar->addWidget(btn);
        return btn;
    };

    auto* newBtn = makePillBtn("+ New", "New File (Ctrl+N)", kAccentBlue, "#2563eb");
    newBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_new.svg"));
    connect(newBtn, &QToolButton::clicked, this, &VioraIdeWindow::onNewFile);

    auto* openBtn = makePillBtn("Open", "Open File (Ctrl+O)", kBorder, "#475569", false);
    openBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_open.svg"));
    connect(openBtn, &QToolButton::clicked, this, &VioraIdeWindow::onOpenFile);

    auto* saveBtn = makePillBtn("Save", "Save (Ctrl+S)", kBorder, "#475569", false);
    saveBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_save.svg"));
    connect(saveBtn, &QToolButton::clicked, this, &VioraIdeWindow::onSave);

    m_mainToolBar->addSeparator();

    // Single run/pause/stop button — changes appearance based on state
    m_runBtn = new QToolButton();
    m_runBtn->setText("Run");
    m_runBtn->setToolTip("Run Extension (F5)");
    m_runBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_run.svg"));
    m_runBtn->setIconSize(QSize(14, 14));
    m_runBtn->setCursor(Qt::PointingHandCursor);
    m_runBtn->setMinimumHeight(28);
    m_runBtn->setMinimumWidth(50);
    m_runBtn->setStyleSheet(
        "QToolButton { background: #16a34a; color: white; border: none; "
        "padding: 4px 12px; border-radius: 14px; font-size: 9pt; font-weight: 600; }"
        "QToolButton:hover { background: #15803d; }"
    );
    connect(m_runBtn, &QToolButton::clicked, this, [this]() {
        if (m_isRunning) {
            onStopExtension();
        } else {
            onRunExtension();
        }
    });
    m_mainToolBar->addWidget(m_runBtn);

    m_mainToolBar->addSeparator();

    auto* newExtBtn = makePillBtn("New Extension", "Create New Extension", kAccentBlue, "#2563eb");

    auto* manifestBtn = makePillBtn("Manifest", "Edit manifest.json", kBorder, "#475569", false);
    connect(manifestBtn, &QToolButton::clicked, this, &VioraIdeWindow::onEditManifest);

    // ── Panel toggle buttons (right-aligned, like schematic editor) ──
    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mainToolBar->addWidget(spacer);

    auto makeToggleBtn = [this](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QToolButton();
        btn->setIcon(themeIcon(iconPath));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setChecked(true);
        auto tc = currentTheme();
        btn->setStyleSheet(
            QString(
                "QToolButton { background: %1; color: %2; border: 2px solid transparent; "
                "padding: 5px; border-radius: 6px; }"
                "QToolButton:hover { background: %3; border-color: %4; }"
                "QToolButton:checked { background: %5; border-color: %4; }"
            ).arg(tc.bgPanel, tc.textSecondary, tc.border, tc.accentBlue, tc.bgDarkest)
        );
        btn->setFixedSize(38, 38);
        btn->setIconSize(QSize(20, 20));
        btn->setCursor(Qt::PointingHandCursor);
        m_mainToolBar->addWidget(btn);
        return btn;
    };

    m_toggleExplorerBtn = makeToggleBtn(":/extension_ide/icons/panel_left.svg", "Toggle Explorer Panel");
    connect(m_toggleExplorerBtn, &QToolButton::clicked, this, &VioraIdeWindow::onToggleExplorerPanel);

    m_toggleBottomBtn = makeToggleBtn(":/extension_ide/icons/panel_bottom.svg", "Toggle Bottom Panel");
    connect(m_toggleBottomBtn, &QToolButton::clicked, this, &VioraIdeWindow::onToggleBottomPanel);

    m_toggleRightBtn = makeToggleBtn(":/extension_ide/icons/panel_right.svg", "Toggle Right Panel");
    connect(m_toggleRightBtn, &QToolButton::clicked, this, &VioraIdeWindow::onToggleRightPanel);

    // Theme toggle
    auto* themeBtn = makeToggleBtn(":/extension_ide/icons/theme_toggle.svg", "Toggle Light/Dark Theme");
    connect(themeBtn, &QToolButton::clicked, this, [this]() {
        auto* tm = &ThemeManager::instance();
        auto* t = tm->theme();
        if (t && t->type() == PCBTheme::Dark) {
            tm->setTheme(PCBTheme::Light);
        } else {
            tm->setTheme(PCBTheme::Dark);
        }
    });
}

void VioraIdeWindow::setupSidebarIcons() {
    m_sidebarStrip = new QWidget();
    m_sidebarStrip->setFixedWidth(48);
    m_sidebarStrip->setStyleSheet(
        QString("background: %1; border-right: 1px solid %2;").arg(kBgDarkest, kBorder)
    );

    auto* stripLayout = new QVBoxLayout(m_sidebarStrip);
    stripLayout->setContentsMargins(0, 12, 0, 12);
    stripLayout->setSpacing(6);

    auto makeIconBtn = [](const QString& iconChar, const QString& tip) {
        auto* btn = new QToolButton();
        btn->setText(iconChar);
        btn->setToolTip(tip);
        btn->setFixedSize(44, 44);
        btn->setIconSize(QSize(22, 22));
        btn->setStyleSheet(QString(
            "QToolButton { background: transparent; color: %1; border: none; "
            "border-radius: 6px; padding: 0px; }"
            "QToolButton:hover { background: %2; color: %3; }"
            "QToolButton:checked { background: %2; color: %3; border-left: 3px solid %3; }"
        ).arg(kTextSecondary, kBgPanel, kTextPrimary));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_sidebarExplorerBtn = makeIconBtn(QString::fromUtf8("\U0001F4C1"), "Explorer");
    m_sidebarSearchBtn = makeIconBtn(QString::fromUtf8("\U0001F50D"), "Search");
    m_sidebarGitBtn = makeIconBtn(QString::fromUtf8("\u2442"), "Git");
    m_sidebarSettingsBtn = makeIconBtn(QString::fromUtf8("\u2699"), "Settings");

    // Set SVG icons (theme-tinted)
    m_sidebarExplorerBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_explorer.svg"));
    m_sidebarSearchBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_search.svg"));
    m_sidebarGitBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_git.svg"));
    m_sidebarSettingsBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_settings.svg"));
    m_sidebarExplorerBtn->setText("");

    m_sidebarExplorerBtn->setChecked(true);

    stripLayout->addWidget(m_sidebarExplorerBtn);
    stripLayout->addWidget(m_sidebarSearchBtn);
    stripLayout->addStretch(1);
    stripLayout->addWidget(m_sidebarGitBtn);
    stripLayout->addWidget(m_sidebarSettingsBtn);

    connect(m_sidebarSettingsBtn, &QToolButton::clicked, this, &VioraIdeWindow::onSettings);
}

void VioraIdeWindow::setupDockWidgets() {
    auto titleStyle = QString(
        "background: %1; color: %3; padding: 8px 12px; "
        "font-size: 9pt; font-weight: bold; letter-spacing: 1px; "
        "border-bottom: 1px solid %2;"
    ).arg(kBgPanel, kBorder, kTextSecondary);

    auto tabStyle = QString(
        "QTabBar { background: %1; border-bottom: 1px solid %2; }"
        "QTabBar::tab { background: %4; color: %3; padding: 6px 14px; "
        "  border: none; border-bottom: 2px solid transparent; font-size: 9pt; }"
        "QTabBar::tab:selected { color: %5; border-bottom-color: %6; }"
        "QTabBar::tab:hover { color: %5; }"
    ).arg(kBgDarkest, kBorder, kTextSecondary, kBgTabInactive, kTextPrimary, kAccentBlue);

    // ── Create panels ───────────────────────────────────────
    m_fileTreePanel = new FileTreePanel();
    m_apiRefPanel = new ApiReferencePanel();
    m_templatePanel = new TemplateBrowserPanel();
    m_outputPanel = new OutputPanel();
    m_manifestPanel = new ManifestEditorPanel();
    m_problemsPanel = new ProblemsPanel();
    m_lspClient = new LspClient(this);
    m_commandPalette = new CommandPalette(this);
    m_recentFilesDialog = new RecentFilesDialog(this);

    // Keyboard shortcuts for overlays — these fire via QShortcut regardless of focus
    auto* paletteShortcut = new QShortcut(QKeySequence("Ctrl+Shift+P"), this);
    connect(paletteShortcut, &QShortcut::activated, this, &VioraIdeWindow::showCommandPalette);

    auto* recentShortcut = new QShortcut(QKeySequence("Ctrl+B"), this);
    connect(recentShortcut, &QShortcut::activated, this, &VioraIdeWindow::showRecentFiles);

    m_sourceControlPanel = new ::SourceControlPanel();

    // ── Left: Explorer with title ───────────────────────────
    auto* explorerTitle = new QLabel("Explorer");
    explorerTitle->setStyleSheet(titleStyle);
    explorerTitle->setFixedHeight(36);
    m_explorerWidget = new QWidget();
    auto* explorerLayout = new QVBoxLayout(m_explorerWidget);
    explorerLayout->setContentsMargins(0, 0, 0, 0);
    explorerLayout->setSpacing(0);
    explorerLayout->addWidget(explorerTitle);
    explorerLayout->addWidget(m_fileTreePanel);

    // ── Right: Templates + API Reference + Source Control (tabbed) ──
    auto* rightStack = new QStackedWidget();
    rightStack->addWidget(m_templatePanel);
    rightStack->addWidget(m_apiRefPanel);
    rightStack->addWidget(m_sourceControlPanel);
    auto* rightTabs = new QTabBar();
    rightTabs->addTab("Templates");
    rightTabs->addTab("API");
    rightTabs->addTab("Git");
    rightTabs->setStyleSheet(tabStyle);
    connect(rightTabs, &QTabBar::currentChanged, rightStack, &QStackedWidget::setCurrentIndex);
    m_rightTabs = rightTabs;
    m_rightStack = rightStack;
    m_rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(rightTabs);
    rightLayout->addWidget(rightStack, 1);

    // ── Bottom: Output + Manifest + Problems (tabbed) ───────
    auto* bottomStack = new QStackedWidget();
    bottomStack->addWidget(m_outputPanel);
    bottomStack->addWidget(m_manifestPanel);
    if (m_problemsPanel) bottomStack->addWidget(m_problemsPanel);
    auto* bottomTabs = new QTabBar();
    bottomTabs->addTab("OUTPUT");
    bottomTabs->addTab("MANIFEST");
    if (m_problemsPanel) bottomTabs->addTab("PROBLEMS");
    bottomTabs->setStyleSheet(tabStyle);
    connect(bottomTabs, &QTabBar::currentChanged, bottomStack, &QStackedWidget::setCurrentIndex);
    m_bottomTabs = bottomTabs;
    m_bottomStack = bottomStack;
    m_bottomPanel = new QWidget();
    m_bottomPanel->setMaximumHeight(180);
    m_bottomPanel->setMinimumHeight(80);
    auto* bottomLayout = new QVBoxLayout(m_bottomPanel);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);
    bottomLayout->addWidget(bottomTabs);
    bottomLayout->addWidget(bottomStack);

    // ── Center: Editor tabs on top, bottom panel below ──────
    m_tabWidget = new IdeTabWidget(this);
    m_tabWidget->setMinimumHeight(100);

    auto* centerSplitter = new QSplitter(Qt::Vertical);
    centerSplitter->addWidget(m_tabWidget);
    centerSplitter->addWidget(m_bottomPanel);
    centerSplitter->setStretchFactor(0, 1);
    centerSplitter->setStretchFactor(1, 0);
    centerSplitter->setSizes({600, 100});
    centerSplitter->setHandleWidth(8);
    centerSplitter->setCollapsible(1, true);
    m_centerSplitter = centerSplitter;

    // ── Main: sidebar icons | file tree | center | right ────
    auto* contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->addWidget(m_explorerWidget);
    contentSplitter->addWidget(centerSplitter);
    contentSplitter->addWidget(m_rightPanel);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setStretchFactor(2, 0);
    contentSplitter->setSizes({240, 750, 280});
    contentSplitter->setHandleWidth(3);
    m_contentSplitter = contentSplitter;

    auto* mainSplitter = new QSplitter(Qt::Horizontal);
    mainSplitter->addWidget(m_sidebarStrip);
    mainSplitter->addWidget(contentSplitter);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({52, 1200});
    mainSplitter->setHandleWidth(1);

    setCentralWidget(mainSplitter);

    m_fileTreeDock = nullptr;
    m_apiRefDock = nullptr;
    m_templateDock = nullptr;
    m_outputDock = nullptr;
    m_manifestDock = nullptr;
}

void VioraIdeWindow::setupStatusBar() {
    statusBar()->setStyleSheet(
        QString(
            "QStatusBar { background: %1; color: %2; font-size: 9pt; padding: 4px 12px; }"
            "QStatusBar::item { border: none; padding: 0px 8px; }"
        ).arg(kBgDarkest, kTextSecondary)
    );
    statusBar()->showMessage("Ready");

    // Left side: error/warning counts
    m_errorCountLabel = new QLabel(QString::fromUtf8("\u24D8 0  \u26A0 0"));
    m_errorCountLabel->setStyleSheet(
        QString("color: %1; padding: 0 12px; font-size: 9pt;").arg(kTextSecondary)
    );
    statusBar()->addPermanentWidget(m_errorCountLabel);

    // Right side: cursor position
    m_cursorLabel = new QLabel("Ln 1, Col 1");
    m_cursorLabel->setStyleSheet(
        QString("color: %1; padding: 0 12px; font-size: 9pt;").arg(kTextSecondary)
    );
    statusBar()->addPermanentWidget(m_cursorLabel);

    // Language
    m_languageLabel = new QLabel("FluxScript");
    m_languageLabel->setStyleSheet(
        QString("color: %1; padding: 0 12px; font-size: 9pt;").arg(kTextSecondary)
    );
    statusBar()->addPermanentWidget(m_languageLabel);
}

void VioraIdeWindow::setupConnections() {
    connect(m_tabWidget, &IdeTabWidget::currentEditorChanged, this, [this](IdeEditor* editor) {
        onCurrentEditorChanged(editor);
        if (m_currentEditor) {
            m_currentEditor->disconnect(this);
            if (m_lspClient) {
                m_currentEditor->disconnect(m_lspClient);
            }
        }
        m_currentEditor = editor;
        if (editor) {
            connect(editor, &IdeEditor::cursorPositionChanged, this, [this](int line, int col) {
                if (m_cursorLabel) m_cursorLabel->setText(QString("Ln %1, Col %2").arg(line).arg(col));
            });

            // LSP document sync
            if (m_lspClient) {
                connect(editor, &IdeEditor::contentsChangedForLsp, m_lspClient,
                    [this](const QString& path, const QString& text, int version) {
                        m_lspClient->changeDocument(path, text, version);
                    });

                connect(editor, &IdeEditor::fileOpenedForLsp, m_lspClient,
                    [this](const QString& path, const QString& text) {
                        m_lspClient->openDocument(path, text);
                    });

                connect(editor, &IdeEditor::fileSavedForLsp, m_lspClient,
                    [this](const QString& path) {
                        m_lspClient->saveDocument(path);
                    });

                // Immediately register currently opened file to handle initialization race conditions
                if (!editor->filePath().isEmpty()) {
                    m_lspClient->openDocument(editor->filePath(), editor->toPlainText());
                }

                // Forward editor LSP signals to window-level signals
        connect(editor, &IdeEditor::hoverRequested, this,
            [this](const QString& path, int line, int col) {
                emit editorHoverRequested(path, line, col);
            });

                connect(editor, &IdeEditor::goToDefinitionRequested, this,
                    [this](const QString& path, int line, int col) {
                        emit editorGoToDefRequested(path, line, col);
                    });

                connect(editor, &IdeEditor::findReferencesRequested, this,
                    [this](const QString& path, int line, int col) {
                        emit editorFindRefsRequested(path, line, col);
                    });

                connect(editor, &IdeEditor::formatDocumentRequested, this,
                    [this](const QString& path) {
                        emit editorFormatRequested(path);
                    });

                connect(editor, &IdeEditor::signatureHelpRequested, this,
                    [this](const QString& path, int line, int col) {
                        emit editorSignatureHelpRequested(path, line, col);
                    });

                // Start LSP on first .flux file open
                if (!m_lspClient->isRunning() && editor->filePath().endsWith(".flux")) {
                    m_lspClient->startServer();
                }
            }
        }
    });
    connect(m_tabWidget, &IdeTabWidget::tabModifiedChanged, this, &VioraIdeWindow::onTabModifiedChanged);

    connect(m_runner, &ExtensionRunner::outputReceived, this, &VioraIdeWindow::onExtensionOutput);
    connect(m_runner, &ExtensionRunner::errorReceived, this, &VioraIdeWindow::onExtensionError);
    connect(m_runner, &ExtensionRunner::runFinished, this, &VioraIdeWindow::onExtensionRunFinished);

    connect(m_fileTreePanel, &FileTreePanel::fileDoubleClicked, this, &VioraIdeWindow::openFile);
    connect(m_templatePanel, &TemplateBrowserPanel::templateSelected, this, &VioraIdeWindow::openFile);

    connect(m_outputPanel, &OutputPanel::errorClicked, this, [this](int line) {
        if (auto* editor = m_tabWidget->currentEditor()) {
            editor->goToLine(line);
        }
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        loadThemeColors();
        applyTheme();
    });

    // ── LSP wiring ──────────────────────────────────────────
    if (m_lspClient) {
        connect(m_lspClient, &LspClient::serverStarted, this, []() {
            qInfo() << "LSP server started";
        });

        connect(m_lspClient, &LspClient::diagnosticsReceived, this,
            [this](const QString& filePath, const QList<LspDiagnostic>& diagnostics) {
                if (m_problemsPanel) m_problemsPanel->setDiagnostics(filePath, diagnostics);

                // Update status bar error count
                int errors = m_problemsPanel ? m_problemsPanel->errorCount() : 0;
                int warnings = m_problemsPanel ? m_problemsPanel->warningCount() : 0;
                if (m_errorCountLabel) {
                    QString text;
                    if (errors > 0) text += QString("%1 error%2").arg(errors).arg(errors > 1 ? "s" : "");
                    if (warnings > 0) {
                        if (!text.isEmpty()) text += ", ";
                        text += QString("%1 warning%2").arg(warnings).arg(warnings > 1 ? "s" : "");
                    }
                    m_errorCountLabel->setText(text);
                }

                // Apply squiggly underlines to editor
                if (m_currentEditor && m_currentEditor->filePath() == filePath) {
                    m_currentEditor->applyDiagnostics(diagnostics);
                }
            });

        connect(m_lspClient, &LspClient::hoverReady, this,
            [this](const QString& contents, const QString& filePath, int line, int col) {
                if (m_currentEditor && m_currentEditor->filePath() == filePath) {
                    m_currentEditor->showHoverTooltip(contents, line, col);
                }
            });

        connect(m_lspClient, &LspClient::definitionReady, this,
            [this](const QString& filePath, int line, int character) {
                openFile(filePath);
                if (m_currentEditor) {
                    m_currentEditor->goToLine(line + 1);
                }
            });

        connect(m_lspClient, &LspClient::errorOccurred, this,
            [this](const QString& message) {
                if (m_outputPanel) {
                    m_outputPanel->appendOutput("[LSP] " + message);
                }
            });

        connect(m_lspClient, &LspClient::referencesReady, this,
            [this](const QList<LspLocation>& locations) {
                if (m_outputPanel) {
                    m_outputPanel->appendOutput(QString("[LSP] Found %1 reference(s):").arg(locations.size()));
                    for (const LspLocation& loc : locations) {
                        QFileInfo fi(loc.uri);
                        m_outputPanel->appendOutput(
                            QString("  %1:%2:%3")
                                .arg(fi.fileName())
                                .arg(loc.range.start.line + 1)
                                .arg(loc.range.start.character + 1));
                    }
                }
            });

        connect(m_lspClient, &LspClient::formattingReady, this,
            [this](const QString& newText) {
                if (m_currentEditor && !newText.isEmpty()) {
                    m_currentEditor->setPlainText(newText);
                }
            });

        connect(m_lspClient, &LspClient::signatureHelpReady, this,
            [this](const QList<LspSignature>& signatures) {
                if (signatures.isEmpty() || !m_currentEditor) return;
                // Show first signature as tooltip at cursor
                const LspSignature& sig = signatures.first();
                QString tooltip = sig.label;
                if (!sig.documentation.isEmpty()) {
                    tooltip += "\n" + sig.documentation;
                }
                // Show near cursor position
                QTextCursor tc = m_currentEditor->textCursor();
                QRect r = m_currentEditor->cursorRect(tc);
                QToolTip::showText(m_currentEditor->mapToGlobal(r.topLeft()), tooltip, m_currentEditor);
            });
    }

    // ── Editor LSP signal wiring ────────────────────────────
    // These are connected per-editor in the currentEditorChanged handler above
    // but we also need to handle the window-level signals
    if (m_lspClient) {
        // Hover request from editor
        connect(this, &VioraIdeWindow::editorHoverRequested, m_lspClient,
            [this](const QString& path, int line, int col) {
                m_lspClient->requestHover(path, line, col);
            });

        // Go to definition from editor
        connect(this, &VioraIdeWindow::editorGoToDefRequested, m_lspClient,
            [this](const QString& path, int line, int col) {
                m_lspClient->requestDefinition(path, line, col);
            });

        // Find references from editor
        connect(this, &VioraIdeWindow::editorFindRefsRequested, m_lspClient,
            [this](const QString& path, int line, int col) {
                m_lspClient->requestReferences(path, line, col);
            });

        // Format document from editor
        connect(this, &VioraIdeWindow::editorFormatRequested, m_lspClient,
            [this](const QString& path) {
                m_lspClient->requestFormatting(path);
            });

        // Signature help from editor
        connect(this, &VioraIdeWindow::editorSignatureHelpRequested, m_lspClient,
            [this](const QString& path, int line, int col) {
                m_lspClient->requestSignatureHelp(path, line, col);
            });
    }

    if (m_problemsPanel) {
        connect(m_problemsPanel, &ProblemsPanel::problemClicked, this,
            [this](const QString& filePath, int line, int column) {
                openFile(filePath);
                if (m_currentEditor) {
                    m_currentEditor->goToLine(line + 1);
                }
            });
    }
}

void VioraIdeWindow::setupContextMenu() {
    auto* cw = centralWidget();
    if (!cw) return;
    cw->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(cw, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        Q_UNUSED(pos);
        QMenu menu;
        menu.setStyleSheet(
            QString(
                "QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
                "QMenu::item { padding: 6px 24px 6px 12px; }"
                "QMenu::item:selected { background: %4; color: %5; }"
            ).arg(kBgPanel, kTextPrimary, kBorder, kAccentBlue, kTextPrimary)
        );

        menu.addAction("New File", this, &VioraIdeWindow::onNewFile);
        menu.addAction("Open File", this, &VioraIdeWindow::onOpenFile);
        menu.addSeparator();
        menu.addAction("Run Extension", this, &VioraIdeWindow::onRunExtension);
        menu.addAction("Stop Extension", this, &VioraIdeWindow::onStopExtension);
        menu.addSeparator();
        menu.addAction("New Extension", this, &VioraIdeWindow::onNewExtension);
        menu.addAction("Edit Manifest", this, &VioraIdeWindow::onEditManifest);

        menu.exec(QCursor::pos());
    });
}

void VioraIdeWindow::applyTheme() {
    loadThemeColors();
    ThemeManager::theme()->applyToWidget(this);

    setStyleSheet(
        QString(
            "QMainWindow { background: %1; }"
            "QMenuBar { background: %1; color: %2; border: none; }"
            "QMenu { background: %3; color: %2; border: 1px solid %4; }"
            "QMenu::item:selected { background: %5; }"
            "QToolTip { background: %3; color: %2; border: 1px solid %4; padding: 4px; }"
            "QSplitter::handle { background: %4; }"
            "QSplitter::handle:hover { background: %5; }"
        ).arg(kBgDarkest, kTextPrimary, kBgPanel, kBorder, kAccentBlue)
    );

    if (m_mainToolBar) {
        m_mainToolBar->setStyleSheet(
            QString(
                "QToolBar { background: %1; border-bottom: 1px solid %2; spacing: 8px; padding: 6px 12px; }"
            ).arg(kBgDarkest, kBorder)
        );
    }

    if (statusBar()) {
        statusBar()->setStyleSheet(
            QString(
                "QStatusBar { background: %1; color: %2; font-size: 9pt; padding: 4px 12px; }"
                "QStatusBar::item { border: none; padding: 0px 8px; }"
            ).arg(kBgDarkest, kTextSecondary)
        );
    }

    if (m_sidebarStrip) {
        m_sidebarStrip->setStyleSheet(
            QString("background: %1; border-right: 1px solid %2;").arg(kBgDarkest, kBorder)
        );
    }

    // Re-theme tab bars
    auto tabStyle = QString(
        "QTabBar { background: %1; border-bottom: 1px solid %2; }"
        "QTabBar::tab { background: %4; color: %3; padding: 6px 14px; "
        "  border: none; border-bottom: 2px solid transparent; font-size: 9pt; }"
        "QTabBar::tab:selected { color: %5; border-bottom-color: %6; }"
        "QTabBar::tab:hover { color: %5; }"
    ).arg(kBgDarkest, kBorder, kTextSecondary, kBgTabInactive, kTextPrimary, kAccentBlue);

    if (m_rightTabs) m_rightTabs->setStyleSheet(tabStyle);
    if (m_bottomTabs) m_bottomTabs->setStyleSheet(tabStyle);

    // Re-theme sidebar icons
    auto sidebarBtnStyle = QString(
        "QToolButton { background: transparent; color: %1; border: none; "
        "border-radius: 6px; padding: 0px; }"
        "QToolButton:hover { background: %2; color: %3; }"
        "QToolButton:checked { background: %2; color: %3; border-left: 3px solid %3; }"
    ).arg(kTextSecondary, kBgPanel, kTextPrimary);

    if (m_sidebarExplorerBtn) m_sidebarExplorerBtn->setStyleSheet(sidebarBtnStyle);
    if (m_sidebarSearchBtn) m_sidebarSearchBtn->setStyleSheet(sidebarBtnStyle);
    if (m_sidebarGitBtn) m_sidebarGitBtn->setStyleSheet(sidebarBtnStyle);
    if (m_sidebarSettingsBtn) m_sidebarSettingsBtn->setStyleSheet(sidebarBtnStyle);

    // Re-tint icons for new theme
    if (m_sidebarExplorerBtn) m_sidebarExplorerBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_explorer.svg"));
    if (m_sidebarSearchBtn) m_sidebarSearchBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_search.svg"));
    if (m_sidebarGitBtn) m_sidebarGitBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_git.svg"));
    if (m_sidebarSettingsBtn) m_sidebarSettingsBtn->setIcon(themeIcon(":/extension_ide/icons/sidebar_settings.svg"));
    if (m_toggleExplorerBtn) m_toggleExplorerBtn->setIcon(themeIcon(":/extension_ide/icons/panel_left.svg"));
    if (m_toggleBottomBtn) m_toggleBottomBtn->setIcon(themeIcon(":/extension_ide/icons/panel_bottom.svg"));
    if (m_toggleRightBtn) m_toggleRightBtn->setIcon(themeIcon(":/extension_ide/icons/panel_right.svg"));

    // Re-style toggle buttons for new theme
    auto restyleToggle = [](QToolButton* btn) {
        if (!btn) return;
        auto tc = currentTheme();
        btn->setStyleSheet(
            QString(
                "QToolButton { background: %1; color: %2; border: 2px solid transparent; "
                "padding: 5px; border-radius: 6px; }"
                "QToolButton:hover { background: %3; border-color: %4; }"
                "QToolButton:checked { background: %5; border-color: %4; }"
            ).arg(tc.bgPanel, tc.textSecondary, tc.border, tc.accentBlue, tc.bgDarkest)
        );
    };
    restyleToggle(m_toggleExplorerBtn);
    restyleToggle(m_toggleBottomBtn);
    restyleToggle(m_toggleRightBtn);

    // Cascade theme to all child panels
    if (m_fileTreePanel) m_fileTreePanel->reapplyTheme();
    if (m_templatePanel) m_templatePanel->reapplyTheme();
    if (m_apiRefPanel) m_apiRefPanel->reapplyTheme();
    if (m_outputPanel) m_outputPanel->reapplyTheme();
    if (m_manifestPanel) m_manifestPanel->reapplyTheme();
    if (m_sourceControlPanel) m_sourceControlPanel->refresh();
}

// ============================================================================
// Actions
// ============================================================================

void VioraIdeWindow::openFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    detectLanguage(filePath);
    m_tabWidget->openFile(filePath);

    QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();
    for (int i = 0; i < 5; ++i) {
        if (QFile::exists(dir.filePath("manifest.json"))) {
            m_extensionDir = dir.absolutePath();
            m_fileTreePanel->setRootPath(m_extensionDir);
            m_manifestPanel->loadManifest(dir.filePath("manifest.json"));
            break;
        }
        if (!dir.cdUp()) break;
    }

    updateTitle();
    statusBar()->showMessage("Opened: " + filePath);
}

void VioraIdeWindow::openVioraDirectory(const QString& dirPath) {
    m_extensionDir = dirPath;
    m_fileTreePanel->setRootPath(dirPath);
    SourceControlManager::instance().setProjectDir(dirPath);

    QString mainFile = dirPath + "/main.flux";
    if (QFile::exists(mainFile)) {
        openFile(mainFile);
    }

    QString manifestFile = dirPath + "/manifest.json";
    if (QFile::exists(manifestFile)) {
        m_manifestPanel->loadManifest(manifestFile);
    }

    updateTitle();
}

void VioraIdeWindow::onNewFile() {
    m_tabWidget->addEditorTab();
    updateTitle();
}

void VioraIdeWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open File", "",
        "FluxScript (*.flux);;JSON (*.json);;All Files (*)");
    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void VioraIdeWindow::onOpenDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Extension Directory",
        QDir::homePath() + "/.config/VioraEDA/extensions");
    if (!dir.isEmpty()) {
        openVioraDirectory(dir);
    }
}

void VioraIdeWindow::onSave() {
    if (m_tabWidget->saveCurrentFile()) {
        statusBar()->showMessage("Saved.", 3000);
    }
}

void VioraIdeWindow::onSaveAs() {
    if (m_tabWidget->saveFileAs(m_tabWidget->currentIndex())) {
        statusBar()->showMessage("Saved.", 3000);
    }
}

void VioraIdeWindow::onSaveAll() {
    int count = 0;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* editor = m_tabWidget->editorAt(i);
        if (editor && editor->isModified()) {
            if (editor->saveFile()) count++;
        }
    }
    statusBar()->showMessage(QString("Saved %1 file(s).").arg(count), 3000);
}

void VioraIdeWindow::onRunExtension() {
    auto* editor = m_tabWidget->currentEditor();
    if (!editor) {
        QMessageBox::information(this, "No File", "Open a .flux file to run.");
        return;
    }

    QString filePath = editor->filePath();
    if (!filePath.isEmpty() && !filePath.endsWith(".flux", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "Not a FluxScript File",
            QString("'%1' is not a .flux file. Only FluxScript files can be run.")
            .arg(QFileInfo(filePath).fileName()));
        return;
    }

    m_outputPanel->clear();
    m_outputPanel->appendInfo("Running extension...");
    updateRunButtons(true);

    // Run synchronously but defer button update so UI repaints the running state first
    QApplication::processEvents();
    m_runner->runSource(editor->toPlainText(), m_extensionDir);
}

void VioraIdeWindow::onStopExtension() {
    m_runner->stop();
    updateRunButtons(false);
}

void VioraIdeWindow::onNewExtension() {
    ExtensionScaffoldDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString extDir = dlg.extensionPath();
        if (!extDir.isEmpty()) {
            openVioraDirectory(extDir);
            m_outputPanel->appendInfo("Extension created: " + extDir);
        }
    }
}

void VioraIdeWindow::onEditManifest() {
    if (m_extensionDir.isEmpty()) {
        QMessageBox::information(this, "No Extension",
            "Open an extension directory first (containing manifest.json).");
        return;
    }

    QString manifestPath = m_extensionDir + "/manifest.json";
    if (!QFile::exists(manifestPath)) {
        QMessageBox::information(this, "No Manifest",
            "No manifest.json found in the extension directory.");
        return;
    }

    m_manifestPanel->loadManifest(manifestPath);
}

void VioraIdeWindow::onShowFindReplace() {
    if (auto* editor = m_tabWidget->currentEditor()) {
        if (!m_findDock) {
            m_findDock = new QDockWidget("Find & Replace", this);
            m_findDock->setObjectName("FindReplaceDock");
            m_findReplaceBar = new IdeFindReplace(editor);
            m_findDock->setWidget(m_findReplaceBar);
            m_findDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
            addDockWidget(Qt::BottomDockWidgetArea, m_findDock);
        }
        m_findReplaceBar->setEditor(editor);
        m_findDock->show();
        m_findReplaceBar->activate();
    }
}

void VioraIdeWindow::onSettings() {
    // TODO: Settings dialog
}

// ============================================================================
// Panel Toggles
// ============================================================================

void VioraIdeWindow::onToggleExplorerPanel() {
    if (!m_explorerWidget || !m_contentSplitter) return;
    bool visible = m_explorerWidget->isVisible();
    m_explorerWidget->setVisible(!visible);
    if (!visible) {
        // Restoring: set a reasonable width
        QList<int> sizes = m_contentSplitter->sizes();
        if (sizes.size() >= 2 && sizes[0] < 20) {
            sizes[0] = 240;
            m_contentSplitter->setSizes(sizes);
        }
    }
    saveWindowState();
}

void VioraIdeWindow::onToggleBottomPanel() {
    if (!m_bottomPanel || !m_centerSplitter) return;
    bool visible = m_bottomPanel->isVisible();
    m_bottomPanel->setVisible(!visible);
    saveWindowState();
}

void VioraIdeWindow::onToggleRightPanel() {
    if (!m_rightPanel || !m_contentSplitter) return;
    bool visible = m_rightPanel->isVisible();
    m_rightPanel->setVisible(!visible);
    if (!visible) {
        // Restoring: set a reasonable width
        QList<int> sizes = m_contentSplitter->sizes();
        if (sizes.size() >= 3 && sizes[2] < 20) {
            sizes[2] = 280;
            m_contentSplitter->setSizes(sizes);
        }
    }
    saveWindowState();
}

// ============================================================================
// Callbacks
// ============================================================================

void VioraIdeWindow::onCurrentEditorChanged(IdeEditor* editor) {
    updateEditorActions();
    if (editor) {
        m_cursorLabel->setText(QString("Ln %1, Col %2").arg(editor->currentLine()).arg(editor->currentColumn()));
        m_languageLabel->setText(editor->language() == "json" ? "JSON" : "FluxScript");
    }
}

void VioraIdeWindow::onTabModifiedChanged(int index, bool modified) {
    Q_UNUSED(index);
    Q_UNUSED(modified);
    updateTitle();
}

void VioraIdeWindow::onExtensionOutput(const QString& message) {
    m_outputPanel->appendOutput(message);
}

void VioraIdeWindow::onExtensionError(const QString& message) {
    m_outputPanel->appendError(message);
}

void VioraIdeWindow::onExtensionRunFinished(bool success) {
    // Defer button update so UI can repaint the running state first
    QTimer::singleShot(50, this, [this, success]() {
        updateRunButtons(false);
        if (success) {
            m_outputPanel->appendInfo("Extension finished successfully.");
        } else {
            m_outputPanel->appendError("Extension failed.");
        }
    });
}

void VioraIdeWindow::updateRunButtons(bool running) {
    m_isRunning = running;
    if (running) {
        m_runBtn->setText("Stop");
        m_runBtn->setToolTip("Stop Extension (Shift+F5)");
        m_runBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_stop.svg"));
        m_runBtn->setStyleSheet(
            "QToolButton { background: #dc2626; color: white; border: none; "
            "padding: 4px 12px; border-radius: 14px; font-size: 9pt; font-weight: 600; }"
            "QToolButton:hover { background: #b91c1c; }"
        );
    } else {
        m_runBtn->setText("Run");
        m_runBtn->setToolTip("Run Extension (F5)");
        m_runBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_run.svg"));
        m_runBtn->setStyleSheet(
            "QToolButton { background: #16a34a; color: white; border: none; "
            "padding: 4px 12px; border-radius: 14px; font-size: 9pt; font-weight: 600; }"
            "QToolButton:hover { background: #15803d; }"
        );
    }
}

void VioraIdeWindow::onViewToggled(bool visible) {
    Q_UNUSED(visible);
}

// ============================================================================
// Command Palette & Recent Files
// ============================================================================

void VioraIdeWindow::showCommandPalette() {
    if (!m_commandPalette) return;

    // Toggle if already visible
    if (m_commandPalette->isPaletteVisible()) {
        m_commandPalette->hidePalette();
        return;
    }

    // Hide recent files if open
    if (m_recentFilesDialog && m_recentFilesDialog->isDialogVisible())
        m_recentFilesDialog->hideDialog();

    // Populate commands fresh each time
    m_commandPalette->addSeparator("File");
    m_commandPalette->addCommand("New File", "Ctrl+N", [this]() { onNewFile(); });
    m_commandPalette->addCommand("Open File", "Ctrl+O", [this]() { onOpenFile(); });
    m_commandPalette->addCommand("Open Directory", "Ctrl+Shift+O", [this]() { onOpenDirectory(); });
    m_commandPalette->addCommand("Save", "Ctrl+S", [this]() { onSave(); });
    m_commandPalette->addCommand("Save As", "Ctrl+Shift+S", [this]() { onSaveAs(); });
    m_commandPalette->addCommand("Save All", "Ctrl+Shift+A", [this]() { onSaveAll(); });
    m_commandPalette->addCommand("Close Tab", "Ctrl+W", [this]() {
        if (m_tabWidget) m_tabWidget->closeTab(m_tabWidget->currentIndex());
    });
    m_commandPalette->addCommand("Reopen Closed Tab", "Ctrl+Shift+T", [this]() {
        if (m_tabWidget) m_tabWidget->reopenClosedTab();
    });
    m_commandPalette->addCommand("Recent Files", "Ctrl+B", [this]() { showRecentFiles(); });

    m_commandPalette->addSeparator("Edit");
    m_commandPalette->addCommand("Find & Replace", "Ctrl+F", [this]() { onShowFindReplace(); });
    m_commandPalette->addCommand("Go to Line", "Ctrl+G", [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr) {
            bool ok;
            int line = QInputDialog::getInt(this, "Go to Line", "Line:", 1, 1, e->document()->blockCount(), 1, &ok);
            if (ok) e->goToLine(line);
        }
    });
    m_commandPalette->addCommand("Format Document", "Ctrl+Shift+F", [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            emit editorFormatRequested(e->filePath());
    });

    m_commandPalette->addSeparator("Navigation");
    m_commandPalette->addCommand("Go to Definition", "F12", [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            emit editorGoToDefRequested(e->filePath(), e->cursorLine() - 1, e->cursorColumn() - 1);
    });
    m_commandPalette->addCommand("Find All References", "Shift+F12", [this]() {
        if (auto* e = m_tabWidget ? m_tabWidget->currentEditor() : nullptr)
            emit editorFindRefsRequested(e->filePath(), e->cursorLine() - 1, e->cursorColumn() - 1);
    });
    m_commandPalette->addCommand("Next Tab", "Ctrl+Tab", [this]() {
        if (m_tabWidget) {
            int next = m_tabWidget->currentIndex() + 1;
            if (next >= m_tabWidget->count()) next = 0;
            m_tabWidget->setCurrentIndex(next);
        }
    });
    m_commandPalette->addCommand("Previous Tab", "Ctrl+Shift+Tab", [this]() {
        if (m_tabWidget) {
            int prev = m_tabWidget->currentIndex() - 1;
            if (prev < 0) prev = m_tabWidget->count() - 1;
            m_tabWidget->setCurrentIndex(prev);
        }
    });

    m_commandPalette->addSeparator("View");
    m_commandPalette->addCommand("Toggle Explorer", "Ctrl+E", [this]() { onToggleExplorerPanel(); });
    m_commandPalette->addCommand("Toggle Bottom Panel", "Ctrl+`", [this]() { onToggleBottomPanel(); });
    m_commandPalette->addCommand("Toggle Right Panel", "Ctrl+Shift+E", [this]() { onToggleRightPanel(); });
    m_commandPalette->addCommand("Toggle Theme", "Ctrl+Shift+T", [this]() {
        auto* theme = ThemeManager::theme();
        if (theme) {
            ThemeManager::instance().setTheme(
                theme->type() == PCBTheme::Dark ? PCBTheme::Light : PCBTheme::Dark);
        }
    });

    m_commandPalette->addSeparator("Run");
    m_commandPalette->addCommand("Run Extension", "F5", [this]() { onRunExtension(); });
    m_commandPalette->addCommand("Stop Extension", "Shift+F5", [this]() { onStopExtension(); });

    m_commandPalette->addSeparator("Extensions");
    m_commandPalette->addCommand("New Extension", "", [this]() { onNewExtension(); });
    m_commandPalette->addCommand("Edit Manifest", "", [this]() { onEditManifest(); });

    m_commandPalette->showPalette();
}

void VioraIdeWindow::showRecentFiles() {
    if (!m_recentFilesDialog) return;

    // Toggle if already visible
    if (m_recentFilesDialog->isDialogVisible()) {
        m_recentFilesDialog->hideDialog();
        return;
    }

    // Hide command palette if open
    if (m_commandPalette && m_commandPalette->isPaletteVisible())
        m_commandPalette->hidePalette();

    // Collect open files
    m_recentFiles.clear();
    if (m_tabWidget) {
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            auto* editor = qobject_cast<IdeEditor*>(m_tabWidget->widget(i));
            if (editor && !editor->filePath().isEmpty()) {
                m_recentFiles.append(editor->filePath());
            }
        }
    }

    // Add extension dir files
    if (!m_extensionDir.isEmpty()) {
        QDir dir(m_extensionDir);
        for (const QFileInfo& fi : dir.entryInfoList({"*.flux", "*.json"}, QDir::Files)) {
            if (!m_recentFiles.contains(fi.absoluteFilePath())) {
                m_recentFiles.append(fi.absoluteFilePath());
            }
        }
    }

    m_recentFilesDialog->setRecentFiles(m_recentFiles);
    m_recentFilesDialog->showDialog();
}

// ============================================================================
// Helpers
// ============================================================================

void VioraIdeWindow::updateTitle() {
    QString base = "VioraIDE";
    if (!m_extensionDir.isEmpty()) {
        base += " - " + QFileInfo(m_extensionDir).fileName();
    }
    if (m_tabWidget && m_tabWidget->hasUnsavedChanges()) {
        base += " *";
    }
    setWindowTitle(base);
}

void VioraIdeWindow::updateEditorActions() {
    if (auto* editor = m_tabWidget->currentEditor()) {
        statusBar()->showMessage(
            QString("Line %1, Col %2").arg(editor->currentLine()).arg(editor->currentColumn()));
    }
}

void VioraIdeWindow::detectLanguage(const QString& filePath) {
    if (auto* editor = m_tabWidget->currentEditor()) {
        if (filePath.endsWith(".json", Qt::CaseInsensitive)) {
            editor->setLanguage("json");
        } else {
            editor->setLanguage("flux");
        }
    }
}

void VioraIdeWindow::saveWindowState() {
    auto& cfg = ConfigManager::instance();
    cfg.setToolProperty("ExtensionIDE", "geometry", saveGeometry());
    cfg.setToolProperty("ExtensionIDE", "openFiles", m_tabWidget->openFilePaths());
    cfg.setToolProperty("ExtensionIDE", "extensionDir", m_extensionDir);
}

void VioraIdeWindow::restoreWindowState() {
    auto& cfg = ConfigManager::instance();
    QByteArray geometry = cfg.toolProperty("ExtensionIDE", "geometry").toByteArray();
    QStringList openFiles = cfg.toolProperty("ExtensionIDE", "openFiles").toStringList();
    QString extDir = cfg.toolProperty("ExtensionIDE", "extensionDir").toString();

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    if (!extDir.isEmpty()) {
        m_extensionDir = extDir;
        m_fileTreePanel->setRootPath(extDir);
    }

    for (const QString& file : openFiles) {
        if (QFile::exists(file)) {
            openFile(file);
        }
    }
}

void VioraIdeWindow::closeEvent(QCloseEvent* event) {
    if (m_runner && m_runner->isRunning()) {
        m_runner->stop();
    }

    if (m_tabWidget && m_tabWidget->hasUnsavedChanges()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Unsaved Changes",
            "There are unsaved changes. Save before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (reply == QMessageBox::Save) {
            onSaveAll();
        }
    }

    saveWindowState();
    event->accept();
}

} // namespace IDE
