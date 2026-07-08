/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_tab_widget.h"
#include "ide_editor.h"
#include "../core/ide_theme.h"
#include <QMenu>
#include <QAction>
#include <QTabBar>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QLabel>
#include <QVBoxLayout>

namespace IDE {

IdeTabWidget::IdeTabWidget(QWidget* parent)
    : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested, this, &IdeTabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &IdeTabWidget::onCurrentChanged);

    applyTabTheme();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &IdeTabWidget::applyTabTheme);
    ThemeManager::instance().registerThemeCallback(this, [this]() { applyTabTheme(); });

    // Welcome view
    auto tc = currentTheme();
    m_welcomeWidget = new QWidget();
    m_welcomeWidget->setStyleSheet(
        QString("background: %1;").arg(tc.bgEditor)
    );
    auto* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    welcomeLayout->setSpacing(12);

    auto* welcomeTitle = new QLabel("Extension IDE");
    welcomeTitle->setStyleSheet(
        QString("color: %1; font-size: 22pt; font-weight: bold;").arg(tc.textPrimary)
    );
    welcomeTitle->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(welcomeTitle);

    auto* welcomeSub = new QLabel("Open a .flux file or create a new extension to get started");
    welcomeSub->setStyleSheet(
        QString("color: %1; font-size: 11pt;").arg(tc.textSecondary)
    );
    welcomeSub->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(welcomeSub);

    auto* shortcutLabel = new QLabel("Ctrl+N  New File    Ctrl+O  Open File    Ctrl+Shift+E  New Extension");
    shortcutLabel->setStyleSheet(
        QString("color: %1; font-size: 10pt; padding-top: 20px;").arg(tc.textSecondary)
    );
    shortcutLabel->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(shortcutLabel);

    addTab(m_welcomeWidget, "Welcome");

    setupTabContextMenu();
}

IdeEditor* IdeTabWidget::addEditorTab(const QString& filePath) {
    int welcomeIdx = indexOf(m_welcomeWidget);
    if (welcomeIdx >= 0) setTabVisible(welcomeIdx, false);

    auto* editor = new IdeEditor(this);

    if (!filePath.isEmpty()) {
        editor->openFile(filePath);
    }

    QString title = tabTitleForFile(filePath);
    int idx = addTab(editor, QIcon(":/extension_ide/icons/tab_close.svg"), title);
    setTabToolTip(idx, filePath.isEmpty() ? "New File" : filePath);
    setCurrentIndex(idx);

    connect(editor, &IdeEditor::modificationChanged, this, &IdeTabWidget::onModificationChanged);
    connect(editor, &IdeEditor::filePathChanged, this, [this, editor](const QString& path) {
        int idx = indexOf(editor);
        if (idx >= 0) {
            setTabText(idx, tabTitleForFile(path, editor->isModified()));
            setTabToolTip(idx, path);
        }
    });

    return editor;
}

IdeEditor* IdeTabWidget::openFile(const QString& filePath) {
    int existing = findTabByFilePath(filePath);
    if (existing >= 0) {
        setCurrentIndex(existing);
        return editorAt(existing);
    }

    auto* editor = addEditorTab(filePath);
    return editor;
}

bool IdeTabWidget::saveCurrentFile() {
    auto* editor = currentEditor();
    if (!editor) return false;
    return editor->saveFile();
}

bool IdeTabWidget::saveFileAs(int index) {
    auto* editor = editorAt(index);
    if (!editor) return false;
    return editor->saveFile();
}

void IdeTabWidget::closeTab(int index) {
    auto* editor = editorAt(index);
    if (!editor) return;

    if (editor->isModified()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Unsaved Changes",
            QString("File '%1' has unsaved changes. Save before closing?").arg(
                QFileInfo(editor->filePath()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save && !editor->saveFile()) return;
    }

    if (!editor->filePath().isEmpty()) {
        ClosedTab ct;
        ct.filePath = editor->filePath();
        ct.content = editor->toPlainText();
        m_closedTabs.prepend(ct);
        if (m_closedTabs.size() > 20) m_closedTabs.removeLast();
    }

    removeTab(index);
    editor->deleteLater();

    if (count() <= 1) {
        int welcomeIdx = indexOf(m_welcomeWidget);
        if (welcomeIdx >= 0) setTabVisible(welcomeIdx, true);
    }
}

void IdeTabWidget::closeAllTabs() {
    for (int i = count() - 1; i >= 0; --i) {
        auto* editor = editorAt(i);
        if (editor) closeTab(i);
    }
    int welcomeIdx = indexOf(m_welcomeWidget);
    if (welcomeIdx >= 0) setTabVisible(welcomeIdx, true);
}

void IdeTabWidget::closeOtherTabs(int keepIndex) {
    for (int i = count() - 1; i >= 0; --i) {
        if (i != keepIndex) closeTab(i);
    }
}

void IdeTabWidget::reopenClosedTab() {
    if (m_closedTabs.isEmpty()) return;

    ClosedTab ct = m_closedTabs.takeFirst();
    auto* editor = addEditorTab(ct.filePath);
    if (editor && !ct.content.isEmpty()) {
        editor->setPlainText(ct.content);
    }
}

IdeEditor* IdeTabWidget::currentEditor() const {
    return qobject_cast<IdeEditor*>(currentWidget());
}

IdeEditor* IdeTabWidget::editorAt(int index) const {
    return qobject_cast<IdeEditor*>(widget(index));
}

bool IdeTabWidget::hasUnsavedChanges() const {
    for (int i = 0; i < count(); ++i) {
        auto* editor = editorAt(i);
        if (editor && editor->isModified()) return true;
    }
    return false;
}

QStringList IdeTabWidget::openFilePaths() const {
    QStringList paths;
    for (int i = 0; i < count(); ++i) {
        auto* editor = editorAt(i);
        if (editor && !editor->filePath().isEmpty()) {
            paths.append(editor->filePath());
        }
    }
    return paths;
}

void IdeTabWidget::setupTabContextMenu() {
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int idx = tabBar()->tabAt(pos);
        if (idx < 0) idx = currentIndex();
        if (idx < 0) return;

        QMenu menu;
        auto mtc = currentTheme();
        menu.setStyleSheet(
            QString(
                "QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
                "QMenu::item { padding: 6px 24px 6px 12px; }"
                "QMenu::item:selected { background: %4; color: %5; }"
            ).arg(mtc.bgPanel, mtc.textPrimary, mtc.border, mtc.accentBlue, mtc.textPrimary)
        );
        menu.addAction("Close", [this, idx]() { closeTab(idx); });
        menu.addAction("Close Others", [this, idx]() { closeOtherTabs(idx); });
        menu.addAction("Close All", [this]() { closeAllTabs(); });
        menu.addSeparator();
        menu.addAction("Copy File Path", [this, idx]() {
            auto* editor = editorAt(idx);
            if (editor && !editor->filePath().isEmpty()) {
                QApplication::clipboard()->setText(editor->filePath());
            }
        });
        menu.addAction("Reopen Closed Tab", [this]() { reopenClosedTab(); });

        menu.exec(mapToGlobal(pos));
    });
}

int IdeTabWidget::findTabByFilePath(const QString& path) const {
    for (int i = 0; i < count(); ++i) {
        auto* editor = editorAt(i);
        if (editor && editor->filePath() == path) return i;
    }
    return -1;
}

QString IdeTabWidget::tabTitleForFile(const QString& path, bool modified) const {
    QString name = path.isEmpty() ? "Untitled" : QFileInfo(path).fileName();
    if (modified) name += " *";
    return name;
}

void IdeTabWidget::applyTabTheme() {
    auto tc = currentTheme();
    setStyleSheet(
        QString(
            "QTabWidget::pane { border-top: 2px solid %4; background: %1; }"
            "QTabBar { background: %5; spacing: 4px; padding: 2px 0px; }"
            "QTabBar::tab { background: %5; color: %3; padding: 10px 24px; "
            "  border: 1px solid %2; border-bottom: none; margin-right: 4px; "
            "  border-radius: 5px 5px 0 0; min-width: 80px; }"
            "QTabBar::tab:selected { background: %1; color: %6; "
            "  border-top: 2px solid %4; border-bottom: 2px solid %1; }"
            "QTabBar::tab:hover { background: %2; color: %6; }"
        ).arg(tc.bgEditor, tc.border, tc.textSecondary, tc.accentBlue, tc.bgDarkest, tc.textPrimary)
    );
    if (m_welcomeWidget) {
        m_welcomeWidget->setStyleSheet(QString("background: %1;").arg(tc.bgEditor));
    }
}

void IdeTabWidget::onTabCloseRequested(int index) {
    closeTab(index);
}

void IdeTabWidget::onCurrentChanged(int index) {
    emit currentEditorChanged(editorAt(index));
}

void IdeTabWidget::onModificationChanged(bool modified) {
    auto* editor = qobject_cast<IdeEditor*>(sender());
    if (!editor) return;
    int idx = indexOf(editor);
    if (idx >= 0) {
        setTabText(idx, tabTitleForFile(editor->filePath(), modified));
        emit tabModifiedChanged(idx, modified);
    }
}

} // namespace IDE
