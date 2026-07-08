/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_ide_window.h"
#include "editor/ide_editor.h"
#include "editor/ide_tab_widget.h"
#include "editor/ide_find_replace.h"
#include "panels/file_tree_panel.h"
#include "panels/api_reference_panel.h"
#include "panels/output_panel.h"
#include "panels/manifest_editor_panel.h"
#include "panels/template_browser_panel.h"
#include "panels/extension_scaffold_dialog.h"
#include "core/extension_runner.h"
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

ExtensionIdeWindow::ExtensionIdeWindow(QWidget* parent)
    : QMainWindow(parent) {
    loadThemeColors();

    setWindowTitle("Extension IDE - VioraEDA");
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
            openExtensionDirectory(demoExt);
        }
    }
}

ExtensionIdeWindow::~ExtensionIdeWindow() {
    saveWindowState();
}

// ============================================================================
// Setup
// ============================================================================

void ExtensionIdeWindow::setupMenus() {
    // File menu
    auto* fileMenu = new QMenu("File", this);
    fileMenu->addAction("&New File", this, &ExtensionIdeWindow::onNewFile, QKeySequence::New);
    fileMenu->addAction("&Open File...", this, &ExtensionIdeWindow::onOpenFile, QKeySequence::Open);
    fileMenu->addAction("Open &Directory...", this, &ExtensionIdeWindow::onOpenDirectory);
    fileMenu->addSeparator();
    fileMenu->addAction("&Save", this, &ExtensionIdeWindow::onSave, QKeySequence::Save);
    fileMenu->addAction("Save &As...", this, &ExtensionIdeWindow::onSaveAs, QKeySequence("Ctrl+Shift+S"));
    fileMenu->addAction("Save A&ll", this, &ExtensionIdeWindow::onSaveAll, QKeySequence("Ctrl+Shift+A"));
    fileMenu->addSeparator();
    fileMenu->addAction("&Close Tab", this, [this]() {
        if (m_tabWidget) m_tabWidget->closeTab(m_tabWidget->currentIndex());
    }, QKeySequence::Close);
    fileMenu->addAction("Close &All", this, [this]() {
        if (m_tabWidget) m_tabWidget->closeAllTabs();
    });
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
    editMenu->addSeparator();
    editMenu->addAction("&Find && Replace...", this, &ExtensionIdeWindow::onShowFindReplace, QKeySequence::Find);

    // Run menu
    auto* runMenu = new QMenu("Run", this);
    runMenu->addAction("&Run Extension", this, &ExtensionIdeWindow::onRunExtension, QKeySequence("F5"));
    runMenu->addAction("&Stop", this, &ExtensionIdeWindow::onStopExtension, QKeySequence("Shift+F5"));

    // Extensions menu
    auto* extMenu = new QMenu("Extensions", this);
    extMenu->addAction("&New Extension...", this, &ExtensionIdeWindow::onNewExtension);
    extMenu->addAction("&Edit Manifest...", this, &ExtensionIdeWindow::onEditManifest);

    // Add menus to the native menu bar (prevents crashes in Qt 6.10+)
    menuBar()->addMenu(fileMenu);
    menuBar()->addMenu(editMenu);
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

void ExtensionIdeWindow::setupToolbar() {
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
            "padding: 6px 16px; border-radius: 16px; font-size: 10pt; font-weight: 600; }"
            "QToolButton:hover { background: %3; }"
        ).arg(bgColor, textColor, hoverColor));
        btn->setMinimumHeight(32);
        btn->setMinimumWidth(60);
        btn->setIconSize(QSize(16, 16));
        btn->setCursor(Qt::PointingHandCursor);
        m_mainToolBar->addWidget(btn);
        return btn;
    };

    auto* newBtn = makePillBtn("+ New", "New File (Ctrl+N)", kAccentBlue, "#2563eb");
    newBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_new.svg"));
    connect(newBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onNewFile);

    auto* openBtn = makePillBtn("Open", "Open File (Ctrl+O)", kBorder, "#475569", false);
    openBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_open.svg"));
    connect(openBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onOpenFile);

    auto* saveBtn = makePillBtn("Save", "Save (Ctrl+S)", kBorder, "#475569", false);
    saveBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_save.svg"));
    connect(saveBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onSave);

    m_mainToolBar->addSeparator();

    auto* runBtn = makePillBtn("Run", "Run Extension (F5)", kGreen, "#059669");
    runBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_run.svg"));
    connect(runBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onRunExtension);

    auto* stopBtn = makePillBtn("Stop", "Stop Extension (Shift+F5)", kRed, "#dc2626");
    stopBtn->setIcon(themeIcon(":/extension_ide/icons/toolbar_stop.svg"));
    connect(stopBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onStopExtension);

    m_mainToolBar->addSeparator();

    auto* newExtBtn = makePillBtn("New Extension", "Create New Extension", kAccentBlue, "#2563eb");

    auto* manifestBtn = makePillBtn("Manifest", "Edit manifest.json", kBorder, "#475569", false);
    connect(manifestBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onEditManifest);

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
    connect(m_toggleExplorerBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onToggleExplorerPanel);

    m_toggleBottomBtn = makeToggleBtn(":/extension_ide/icons/panel_bottom.svg", "Toggle Bottom Panel");
    connect(m_toggleBottomBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onToggleBottomPanel);

    m_toggleRightBtn = makeToggleBtn(":/extension_ide/icons/panel_right.svg", "Toggle Right Panel");
    connect(m_toggleRightBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onToggleRightPanel);

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

void ExtensionIdeWindow::setupSidebarIcons() {
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

    connect(m_sidebarSettingsBtn, &QToolButton::clicked, this, &ExtensionIdeWindow::onSettings);
}

void ExtensionIdeWindow::setupDockWidgets() {
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

    // ── Bottom: Output + Manifest (tabbed) ──────────────────
    auto* bottomStack = new QStackedWidget();
    bottomStack->addWidget(m_outputPanel);
    bottomStack->addWidget(m_manifestPanel);
    auto* bottomTabs = new QTabBar();
    bottomTabs->addTab("OUTPUT");
    bottomTabs->addTab("MANIFEST");
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

void ExtensionIdeWindow::setupStatusBar() {
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

void ExtensionIdeWindow::setupConnections() {
    connect(m_tabWidget, &IdeTabWidget::currentEditorChanged, this, [this](IdeEditor* editor) {
        onCurrentEditorChanged(editor);
        if (m_currentEditor) {
            disconnect(m_currentEditor, &IdeEditor::cursorPositionChanged, this, nullptr);
        }
        m_currentEditor = editor;
        if (editor) {
            connect(editor, &IdeEditor::cursorPositionChanged, this, [this](int line, int col) {
                if (m_cursorLabel) m_cursorLabel->setText(QString("Ln %1, Col %2").arg(line).arg(col));
            });
        }
    });
    connect(m_tabWidget, &IdeTabWidget::tabModifiedChanged, this, &ExtensionIdeWindow::onTabModifiedChanged);

    connect(m_runner, &ExtensionRunner::outputReceived, this, &ExtensionIdeWindow::onExtensionOutput);
    connect(m_runner, &ExtensionRunner::errorReceived, this, &ExtensionIdeWindow::onExtensionError);
    connect(m_runner, &ExtensionRunner::runFinished, this, &ExtensionIdeWindow::onExtensionRunFinished);

    connect(m_fileTreePanel, &FileTreePanel::fileDoubleClicked, this, &ExtensionIdeWindow::openFile);
    connect(m_templatePanel, &TemplateBrowserPanel::templateSelected, this, &ExtensionIdeWindow::openFile);

    connect(m_outputPanel, &OutputPanel::errorClicked, this, [this](int line) {
        if (auto* editor = m_tabWidget->currentEditor()) {
            editor->goToLine(line);
        }
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        loadThemeColors();
        applyTheme();
    });
}

void ExtensionIdeWindow::setupContextMenu() {
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

        menu.addAction("New File", this, &ExtensionIdeWindow::onNewFile);
        menu.addAction("Open File", this, &ExtensionIdeWindow::onOpenFile);
        menu.addSeparator();
        menu.addAction("Run Extension", this, &ExtensionIdeWindow::onRunExtension);
        menu.addAction("Stop Extension", this, &ExtensionIdeWindow::onStopExtension);
        menu.addSeparator();
        menu.addAction("New Extension", this, &ExtensionIdeWindow::onNewExtension);
        menu.addAction("Edit Manifest", this, &ExtensionIdeWindow::onEditManifest);

        menu.exec(QCursor::pos());
    });
}

void ExtensionIdeWindow::applyTheme() {
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

void ExtensionIdeWindow::openFile(const QString& filePath) {
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

void ExtensionIdeWindow::openExtensionDirectory(const QString& dirPath) {
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

void ExtensionIdeWindow::onNewFile() {
    m_tabWidget->addEditorTab();
    updateTitle();
}

void ExtensionIdeWindow::onOpenFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open File", "",
        "FluxScript (*.flux);;JSON (*.json);;All Files (*)");
    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void ExtensionIdeWindow::onOpenDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Extension Directory",
        QDir::homePath() + "/.config/VioraEDA/extensions");
    if (!dir.isEmpty()) {
        openExtensionDirectory(dir);
    }
}

void ExtensionIdeWindow::onSave() {
    if (m_tabWidget->saveCurrentFile()) {
        statusBar()->showMessage("Saved.", 3000);
    }
}

void ExtensionIdeWindow::onSaveAs() {
    if (m_tabWidget->saveFileAs(m_tabWidget->currentIndex())) {
        statusBar()->showMessage("Saved.", 3000);
    }
}

void ExtensionIdeWindow::onSaveAll() {
    int count = 0;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* editor = m_tabWidget->editorAt(i);
        if (editor && editor->isModified()) {
            if (editor->saveFile()) count++;
        }
    }
    statusBar()->showMessage(QString("Saved %1 file(s).").arg(count), 3000);
}

void ExtensionIdeWindow::onRunExtension() {
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

    m_runner->runSource(editor->toPlainText());
}

void ExtensionIdeWindow::onStopExtension() {
    m_runner->stop();
}

void ExtensionIdeWindow::onNewExtension() {
    ExtensionScaffoldDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString extDir = dlg.extensionPath();
        if (!extDir.isEmpty()) {
            openExtensionDirectory(extDir);
            m_outputPanel->appendInfo("Extension created: " + extDir);
        }
    }
}

void ExtensionIdeWindow::onEditManifest() {
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

void ExtensionIdeWindow::onShowFindReplace() {
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

void ExtensionIdeWindow::onSettings() {
    // TODO: Settings dialog
}

// ============================================================================
// Panel Toggles
// ============================================================================

void ExtensionIdeWindow::onToggleExplorerPanel() {
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

void ExtensionIdeWindow::onToggleBottomPanel() {
    if (!m_bottomPanel || !m_centerSplitter) return;
    bool visible = m_bottomPanel->isVisible();
    m_bottomPanel->setVisible(!visible);
    saveWindowState();
}

void ExtensionIdeWindow::onToggleRightPanel() {
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

void ExtensionIdeWindow::onCurrentEditorChanged(IdeEditor* editor) {
    updateEditorActions();
    if (editor) {
        m_cursorLabel->setText(QString("Ln %1, Col %2").arg(editor->currentLine()).arg(editor->currentColumn()));
        m_languageLabel->setText(editor->language() == "json" ? "JSON" : "FluxScript");
    }
}

void ExtensionIdeWindow::onTabModifiedChanged(int index, bool modified) {
    Q_UNUSED(index);
    Q_UNUSED(modified);
    updateTitle();
}

void ExtensionIdeWindow::onExtensionOutput(const QString& message) {
    m_outputPanel->appendOutput(message);
}

void ExtensionIdeWindow::onExtensionError(const QString& message) {
    m_outputPanel->appendError(message);
}

void ExtensionIdeWindow::onExtensionRunFinished(bool success) {
    if (success) {
        m_outputPanel->appendInfo("Extension finished successfully.");
    } else {
        m_outputPanel->appendError("Extension failed.");
    }
}

void ExtensionIdeWindow::onViewToggled(bool visible) {
    Q_UNUSED(visible);
}

// ============================================================================
// Helpers
// ============================================================================

void ExtensionIdeWindow::updateTitle() {
    QString base = "Extension IDE";
    if (!m_extensionDir.isEmpty()) {
        base += " - " + QFileInfo(m_extensionDir).fileName();
    }
    if (m_tabWidget && m_tabWidget->hasUnsavedChanges()) {
        base += " *";
    }
    setWindowTitle(base);
}

void ExtensionIdeWindow::updateEditorActions() {
    if (auto* editor = m_tabWidget->currentEditor()) {
        statusBar()->showMessage(
            QString("Line %1, Col %2").arg(editor->currentLine()).arg(editor->currentColumn()));
    }
}

void ExtensionIdeWindow::detectLanguage(const QString& filePath) {
    if (auto* editor = m_tabWidget->currentEditor()) {
        if (filePath.endsWith(".json", Qt::CaseInsensitive)) {
            editor->setLanguage("json");
        } else {
            editor->setLanguage("flux");
        }
    }
}

void ExtensionIdeWindow::saveWindowState() {
    auto& cfg = ConfigManager::instance();
    cfg.setToolProperty("ExtensionIDE", "geometry", saveGeometry());
    cfg.setToolProperty("ExtensionIDE", "openFiles", m_tabWidget->openFilePaths());
    cfg.setToolProperty("ExtensionIDE", "extensionDir", m_extensionDir);
}

void ExtensionIdeWindow::restoreWindowState() {
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

void ExtensionIdeWindow::closeEvent(QCloseEvent* event) {
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
