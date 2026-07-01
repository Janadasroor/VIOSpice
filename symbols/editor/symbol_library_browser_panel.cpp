/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_library_browser_panel.h"
#include "../symbol_editor.h"
#include "theme_manager.h"
#include "../symbol_library.h"

#include <QVBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QGraphicsSimpleTextItem>
#include <QUndoStack>
#include <QDir>
#include <QPainter>
#include <QIcon>
#include <QPixmap>

SymbolLibraryBrowserPanel::SymbolLibraryBrowserPanel(SymbolEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setupUI();
}

void SymbolLibraryBrowserPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_libSearchEdit = new QLineEdit();
    m_libSearchEdit->setPlaceholderText("Search symbols…");
    m_libSearchEdit->setClearButtonEnabled(true);
    connect(m_libSearchEdit, &QLineEdit::textChanged,
            this, &SymbolLibraryBrowserPanel::onLibSearchChanged);

    m_libraryTree = new QTreeWidget();
    m_libraryTree->setHeaderHidden(true);
    m_libraryTree->setMinimumHeight(200);
    m_libraryTree->setAnimated(true);
    m_libraryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_libraryTree, &QTreeWidget::customContextMenuRequested, this, &SymbolLibraryBrowserPanel::onLibraryContextMenu);
    
    connect(m_libraryTree, &QTreeWidget::itemDoubleClicked,
            this, &SymbolLibraryBrowserPanel::onCloneSymbol);
    connect(m_libraryTree, &QTreeWidget::itemClicked,
            this, &SymbolLibraryBrowserPanel::onLibraryItemClicked);

    m_libPreviewScene = new QGraphicsScene(this);
    m_libPreviewView = new QGraphicsView(m_libPreviewScene);
    m_libPreviewView->setFixedHeight(180);
    m_libPreviewView->setRenderHint(QPainter::Antialiasing);
    m_libPreviewView->setFrameShape(QFrame::NoFrame);

    layout->addWidget(m_libSearchEdit);
    layout->addWidget(m_libraryTree);
    layout->addWidget(m_libPreviewView);

    connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::librariesChanged,
            this, &SymbolLibraryBrowserPanel::populateLibraryTree);
    if (SymbolLibraryManager::instance().libraries().isEmpty()) {
        SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym", true);
    }

    populateLibraryTree();
    applyTheme();
}

void SymbolLibraryBrowserPanel::populateLibraryTree() {
    if (!m_libraryTree) return;
    m_libraryTree->clear();
    
    QList<SymbolLibrary*> allLibs = SymbolLibraryManager::instance().libraries();
    
    // Pass 1: User / Project Libraries (Non-Built-in)
    for (SymbolLibrary* lib : allLibs) {
        if (lib->isBuiltIn()) continue;
        
        auto* libItem = new QTreeWidgetItem(m_libraryTree, {lib->name()});
        libItem->setIcon(0, getThemeIcon(":/icons/folder_closed.svg"));
        libItem->setForeground(0, QBrush(QColor("#fbbf24"))); // Amber for user libs

        for (const QString& cat : lib->categories()) {
            QList<SymbolDefinition*> syms = lib->symbolsInCategory(cat);
            if (syms.isEmpty()) continue;

            auto* catItem = new QTreeWidgetItem(libItem, {cat});
            catItem->setIcon(0, getThemeIcon(":/icons/folder_open.svg"));

            for (SymbolDefinition* sym : syms) {
                auto* symItem = new QTreeWidgetItem(catItem, {sym->name()});
                symItem->setIcon(0, getThemeIcon(":/icons/component_file.svg"));
                symItem->setData(0, Qt::UserRole, lib->name());
            }
        }
    }

    // Pass 2: Built-in Libraries
    for (SymbolLibrary* lib : allLibs) {
        if (!lib->isBuiltIn()) continue;

        auto* libItem = new QTreeWidgetItem(m_libraryTree, {lib->name() + " [Built-in]"});
        libItem->setIcon(0, getThemeIcon(":/icons/folder_closed.svg"));
        libItem->setForeground(0, QBrush(QColor("#94a3b8"))); // Grey for built-ins

        for (const QString& cat : lib->categories()) {
            QList<SymbolDefinition*> syms = lib->symbolsInCategory(cat);
            if (syms.isEmpty()) continue;

            auto* catItem = new QTreeWidgetItem(libItem, {cat});
            catItem->setIcon(0, getThemeIcon(":/icons/folder_open.svg"));

            for (SymbolDefinition* sym : syms) {
                auto* symItem = new QTreeWidgetItem(catItem, {sym->name()});
                symItem->setIcon(0, getThemeIcon(":/icons/component_file.svg"));
                symItem->setData(0, Qt::UserRole, lib->name());
            }
        }
    }
    
    m_libraryTree->expandAll();
}

void SymbolLibraryBrowserPanel::onLibSearchChanged(const QString& text) {
    const QString query = text.trimmed().toLower();

    for (int i = 0; i < m_libraryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* libItem = m_libraryTree->topLevelItem(i);
        bool libVisible = false;

        for (int j = 0; j < libItem->childCount(); ++j) {
            QTreeWidgetItem* catItem = libItem->child(j);
            bool catVisible = false;

            for (int k = 0; k < catItem->childCount(); ++k) {
                QTreeWidgetItem* symItem = catItem->child(k);
                bool matches = query.isEmpty()
                            || symItem->text(0).toLower().contains(query);
                symItem->setHidden(!matches);
                if (matches) catVisible = true;
            }

            catItem->setHidden(!catVisible && !query.isEmpty());
            if (catVisible) catItem->setExpanded(true);
            if (catVisible) libVisible = true;
        }

        libItem->setHidden(!libVisible && !query.isEmpty());
        if (libVisible) libItem->setExpanded(true);
    }
}

void SymbolLibraryBrowserPanel::onCloneSymbol(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    if (!item || item->childCount() > 0) return; // leaf nodes only

    const QString symName = item->text(0);
    const QString libName = item->data(0, Qt::UserRole).toString();

    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    SymbolDefinition* source = lib->findSymbol(symName);
    if (!source) return;

    if (m_editor->m_canvas && !m_editor->m_canvas->m_drawnItems.isEmpty() && m_editor->m_undoStack && !m_editor->m_undoStack->isClean()) {
        if (QMessageBox::question(this, "Load Symbol",
                "Current symbol has unsaved changes. Discard and load selected symbol?")
                != QMessageBox::Yes)
            return;
    }

    m_editor->setSymbolDefinition(*source);
}

void SymbolLibraryBrowserPanel::onLibraryItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString libName = item->data(0, Qt::UserRole).toString();
    if (libName.isEmpty()) {
        m_libPreviewScene->clear();
        return;
    }

    QString symbolName = item->text(0);
    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    SymbolDefinition* def = lib->findSymbol(symbolName);
    if (!def) return;

    m_libPreviewScene->clear();
    const QList<SymbolPrimitive>& prims = def->primitives();
    for (int i = 0; i < prims.size(); ++i) {
        if (m_editor->m_canvas) {
            QGraphicsItem* visual = m_editor->m_canvas->buildVisual(prims[i], -1);
            if (visual) {
                visual->setFlag(QGraphicsItem::ItemIsSelectable, false);
                visual->setFlag(QGraphicsItem::ItemIsMovable, false);
                m_libPreviewScene->addItem(visual);
            }
        }
    }

    QRectF bounds = def->boundingRect();
    auto* refLabel = new QGraphicsSimpleTextItem(def->referencePrefix() + "?");
    refLabel->setBrush(QColor("#4db6ac"));
    refLabel->setFont(QFont("SansSerif", 8, QFont::Bold));
    refLabel->setPos(bounds.left(), bounds.top() - 20);
    m_libPreviewScene->addItem(refLabel);

    auto* nameLabel = new QGraphicsSimpleTextItem(def->name());
    nameLabel->setBrush(QColor("#64b5f6"));
    nameLabel->setFont(QFont("SansSerif", 8, QFont::Bold));
    nameLabel->setPos(bounds.left(), bounds.bottom() + 5);
    m_libPreviewScene->addItem(nameLabel);

    m_libPreviewView->fitInView(m_libPreviewScene->itemsBoundingRect().adjusted(-15,-15,15,15), Qt::KeepAspectRatio);
}

void SymbolLibraryBrowserPanel::onLibraryContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_libraryTree->itemAt(pos);
    if (!item) return;

    QString libName = item->data(0, Qt::UserRole).toString();
    if (libName.isEmpty()) return;

    QString symbolName = item->text(0);
    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");

    QAction* editAct = menu.addAction(getThemeIcon(":/icons/tool_line.svg"), "Edit Symbol");
    QAction* dupAct  = menu.addAction(getThemeIcon(":/icons/tool_duplicate.svg"), "Duplicate / Copy");
    QAction* deriveAct = menu.addAction(getThemeIcon(":/icons/tool_bezier.svg"), "Create Derived Symbol");
    menu.addSeparator();
    QAction* placeAct = menu.addAction(getThemeIcon(":/icons/nav_pcb.svg"), "Place in Schematic");
    menu.addSeparator();
    QAction* delAct = menu.addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete from Library");

    if (lib->isBuiltIn()) {
        editAct->setText("View Symbol (Read-Only)");
        delAct->setEnabled(false);
    }

    QAction* selected = menu.exec(m_libraryTree->mapToGlobal(pos));
    if (!selected) return;

    SymbolDefinition* def = lib->findSymbol(symbolName);
    if (!def) return;

    if (selected == editAct) {
        m_editor->setSymbolDefinition(*def);
    } else if (selected == deriveAct) {
        SymbolDefinition derived;
        derived.setName(def->name() + "_Derived");
        derived.setParentName(def->name());
        derived.setParentLibrary(libName);
        derived.setCategory(def->category());
        derived.setReferencePrefix(def->referencePrefix());
        m_editor->setSymbolDefinition(derived);
        if (m_editor->statusBar()) {
            m_editor->statusBar()->showMessage("Created derived symbol inheriting from " + def->name(), 3000);
        }
    } else if (selected == dupAct) {
        SymbolDefinition copy = def->clone();
        copy.setName(copy.name() + "_Copy");
        m_editor->setSymbolDefinition(copy);
        if (m_editor->statusBar()) {
            m_editor->statusBar()->showMessage("Symbol copied. Edit and save to library.", 3000);
        }
    } else if (selected == placeAct) {
        Q_EMIT m_editor->placeInSchematicRequested(*def);
    } else if (selected == delAct) {
        if (QMessageBox::question(this, "Delete Symbol", 
            QString("Are you sure you want to delete '%1' from library '%2'?").arg(symbolName, libName)) == QMessageBox::Yes) {
            lib->removeSymbol(symbolName);
            lib->save();
            populateLibraryTree();
        }
    }
}

void SymbolLibraryBrowserPanel::onRefreshLibraries() {
    SymbolLibraryManager::instance().reloadUserLibraries();
    populateLibraryTree();
    if (m_editor && m_editor->statusBar()) {
        m_editor->statusBar()->showMessage("Libraries refreshed.", 3000);
    }
}

QIcon SymbolLibraryBrowserPanel::getThemeIcon(const QString& path, bool tinted) {
    QIcon icon(path);
    if (!tinted || !ThemeManager::theme()) {
        return icon;
    }

    QPixmap pixmap = icon.pixmap(QSize(64, 64));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), ThemeManager::theme()->textColor());
    painter.end();
    return QIcon(pixmap);
}

void SymbolLibraryBrowserPanel::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString border = theme->panelBorder().name();
    QString fg = theme->textColor().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#ffffff" : "#121212";
    QString focusColor = "#52525b";

    if (m_libraryTree) {
        m_libraryTree->setStyleSheet(QString(
            "QTreeWidget { background-color: %1; border: 1px solid %2; border-radius: 4px; color: %3; }"
            "QTreeWidget::item { padding: 4px; }"
            "QTreeWidget::item:selected { background-color: %4; color: white; }"
        ).arg(inputBg, border, fg, focusColor));
    }
    
    if (m_libSearchEdit) {
        m_libSearchEdit->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px; }")
            .arg(inputBg, fg, border));
    }

    if (m_libPreviewView) {
        m_libPreviewView->setBackgroundBrush(theme->type() == PCBTheme::Light ? QBrush(QColor("#f8fafc")) : QBrush(QColor("#121212")));
        m_libPreviewView->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px; margin-top: 5px;")
            .arg((theme->type() == PCBTheme::Light) ? "#f8fafc" : "#121212", border));
    }
}
