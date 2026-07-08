/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "file_tree_panel.h"
#include "../core/ide_theme.h"
#include <QTreeView>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QAbstractItemView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QPainter>
#include <QApplication>
#include <QClipboard>

namespace IDE {

// ── File type icon colors ──────────────────────────────────────
static constexpr const char* kFluxColor   = "#8b5cf6"; // purple for .flux
static constexpr const char* kJsonColor   = "#f59e0b"; // amber for .json
static constexpr const char* kPngColor    = "#10b981"; // green for .png/image
static constexpr const char* kFolderColor = "#3b82f6"; // blue for folders
static constexpr const char* kDefaultColor = "#94a3b8"; // gray for others

// ── File type icon helper ──────────────────────────────────────
static QIcon fileTypeIcon(const QFileInfo& fi) {
    if (fi.isDir()) {
        return themeIcon(":/extension_ide/icons/folder.svg");
    }

    QString ext = fi.suffix().toLower();
    QString iconPath;

    if (ext == "flux") iconPath = ":/extension_ide/icons/file_flux.svg";
    else if (ext == "json") iconPath = ":/extension_ide/icons/file_json.svg";
    else if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "svg")
        iconPath = ":/extension_ide/icons/file_image.svg";
    else if (ext == "txt" || ext == "md" || ext == "readme")
        iconPath = ":/extension_ide/icons/file_text.svg";
    else if (ext == "py") iconPath = ":/extension_ide/icons/file_python.svg";
    else iconPath = ":/extension_ide/icons/file_generic.svg";

    return themeIcon(iconPath);
}

// ── Custom icon delegate via proxy model ───────────────────────
class FileIconProxy : public QSortFilterProxyModel {
public:
    explicit FileIconProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (role == Qt::DecorationRole && index.column() == 0) {
            auto* fsm = qobject_cast<QFileSystemModel*>(sourceModel());
            if (fsm) {
                QFileInfo fi(fsm->filePath(mapToSource(index)));
                return fileTypeIcon(fi);
            }
        }
        return QSortFilterProxyModel::data(index, role);
    }
};

FileTreePanel::FileTreePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    ThemeManager::instance().registerThemeCallback(this, [this]() { reapplyTheme(); });
}

void FileTreePanel::setupUI() {
    auto tc = currentTheme();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Filter bar
    auto* filterBar = new QWidget();
    filterBar->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;").arg(tc.bgPanel, tc.border)
    );
    auto* filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(10, 6, 10, 6);

    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Filter files...");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setStyleSheet(
        QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 7px 12px; border-radius: 5px; font-size: 10pt; }"
        ).arg(tc.bgDarkest, tc.textPrimary, tc.border)
    );
    connect(m_filterEdit, &QLineEdit::textChanged, this, &FileTreePanel::onFilterChanged);
    filterLayout->addWidget(m_filterEdit);
    layout->addWidget(filterBar);

    // Tree view
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(false);

    m_proxyModel = new FileIconProxy(this);
    m_proxyModel->setSourceModel(m_model);

    m_tree = new QTreeView();
    m_tree->setModel(m_proxyModel);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(16);
    m_tree->setRootIsDecorated(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(
        QString(
            "QTreeView { background: %1; color: %2; border: none; font-size: 10pt; }"
            "QTreeView::item { padding: 6px 12px; border: none; min-height: 30px; }"
            "QTreeView::item:hover { background: %3; }"
            "QTreeView::item:selected { background: %4; color: %5; }"
            "QTreeView::branch { background: %1; }"
            "QTreeView QLineEdit { padding: 4px 8px; border: 1px solid %4; "
            "  background: %1; color: %5; border-radius: 3px; }"
        ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.accentBlue, tc.textPrimary)
    );

    for (int i = 1; i < m_model->columnCount(); ++i) {
        m_tree->hideColumn(i);
    }

    connect(m_tree, &QTreeView::doubleClicked, this, &FileTreePanel::onDoubleClicked);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &FileTreePanel::onCustomContextMenu);

    layout->addWidget(m_tree);
}

void FileTreePanel::setRootPath(const QString& path) {
    m_rootPath = path;
    QModelIndex idx = m_model->setRootPath(path);
    m_tree->setRootIndex(m_proxyModel->mapFromSource(idx));
}

void FileTreePanel::onDoubleClicked(const QModelIndex& index) {
    QString path = m_model->filePath(m_proxyModel->mapToSource(index));
    if (QFileInfo(path).isFile()) {
        emit fileDoubleClicked(path);
    }
}

void FileTreePanel::onCustomContextMenu(const QPoint& pos) {
    QModelIndex proxyIndex = m_tree->indexAt(pos);
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    QString targetPath = m_model->filePath(sourceIndex);
    QString parentDir = QFileInfo(targetPath).isDir() ? targetPath : QFileInfo(targetPath).absolutePath();
    bool hasTarget = !targetPath.isEmpty() && QFileInfo(targetPath).exists();
    bool isFile = hasTarget && QFileInfo(targetPath).isFile();

    auto tc = currentTheme();
    QMenu menu;
    menu.setStyleSheet(
        QString(
            "QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
            "QMenu::item { padding: 6px 24px 6px 12px; }"
            "QMenu::item:selected { background: %4; color: %5; }"
        ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.accentBlue, tc.textPrimary)
    );

    // Open in Editor (only for files)
    if (isFile) {
        menu.addAction("Open in Editor", [this, targetPath]() {
            emit fileDoubleClicked(targetPath);
        });
        menu.addSeparator();
    }

    // Copy Path (only for files)
    if (isFile) {
        menu.addAction("Copy Path", [targetPath]() {
            QApplication::clipboard()->setText(targetPath);
        });
        menu.addAction("Copy Relative Path", [this, targetPath]() {
            QApplication::clipboard()->setText(relativePath(targetPath));
        });
        menu.addSeparator();
    }

    // New File / New Folder
    menu.addAction("New File...", [this, parentDir]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New File", "File name:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            QFile file(QDir(parentDir).filePath(name));
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                emit newFileRequested(parentDir);
            }
        }
    });

    menu.addAction("New Folder...", [this, parentDir]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            QDir(parentDir).mkdir(name);
            emit newFolderRequested(parentDir);
        }
    });

    // Rename / Delete (only for existing items)
    if (hasTarget) {
        menu.addSeparator();

        menu.addAction("Rename...", [this, targetPath]() {
            bool ok;
            QString name = QInputDialog::getText(this, "Rename", "New name:",
                QLineEdit::Normal, QFileInfo(targetPath).fileName(), &ok);
            if (ok && !name.isEmpty()) {
                QString newPath = QFileInfo(targetPath).absoluteDir().filePath(name);
                if (QFile::rename(targetPath, newPath)) {
                    emit fileRenamed(targetPath, newPath);
                }
            }
        });

        menu.addAction("Delete", [this, targetPath]() {
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "Delete", QString("Delete '%1'?").arg(QFileInfo(targetPath).fileName()),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                if (QFileInfo(targetPath).isDir()) {
                    QDir().rmdir(targetPath);
                } else {
                    QFile::remove(targetPath);
                }
                emit fileDeleted(targetPath);
            }
        });
    }

    // Refresh at the bottom
    menu.addSeparator();
    menu.addAction("Refresh", [this]() {
        refreshTree();
    });

    menu.exec(m_tree->mapToGlobal(pos));
}

void FileTreePanel::onFilterChanged(const QString& text) {
    QModelIndex rootIdx = m_tree->rootIndex();
    int rowCount = m_model->rowCount(m_proxyModel->mapToSource(rootIdx));
    for (int r = 0; r < rowCount; ++r) {
        QModelIndex sourceIdx = m_model->index(r, 0, m_proxyModel->mapToSource(rootIdx));
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(sourceIdx);
        QString name = m_model->data(sourceIdx).toString();
        bool hidden = !text.isEmpty() && !name.contains(text, Qt::CaseInsensitive);
        m_tree->setRowHidden(proxyIdx.row(), rootIdx, hidden);
    }
}

void FileTreePanel::refreshTree() {
    if (!m_rootPath.isEmpty()) {
        setRootPath(m_rootPath);
    }
}

QString FileTreePanel::relativePath(const QString& absolutePath) const {
    if (m_rootPath.isEmpty()) return absolutePath;
    QDir rootDir(m_rootPath);
    return rootDir.relativeFilePath(absolutePath);
}

void FileTreePanel::reapplyTheme() {
    auto tc = currentTheme();
    if (m_filterEdit) {
        m_filterEdit->setStyleSheet(
            QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 7px 12px; border-radius: 5px; font-size: 10pt; }")
            .arg(tc.bgDarkest, tc.textPrimary, tc.border)
        );
    }
    if (m_tree) {
        m_tree->setStyleSheet(
            QString(
                "QTreeView { background: %1; color: %2; border: none; font-size: 10pt; }"
                "QTreeView::item { padding: 6px 12px; border: none; min-height: 30px; }"
                "QTreeView::item:hover { background: %3; }"
                "QTreeView::item:selected { background: %4; color: %5; }"
                "QTreeView::branch { background: %1; }"
                "QTreeView QLineEdit { padding: 4px 8px; border: 1px solid %4; "
                "  background: %1; color: %5; border-radius: 3px; }"
            ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.accentBlue, tc.textPrimary)
        );
    }
}

} // namespace IDE
