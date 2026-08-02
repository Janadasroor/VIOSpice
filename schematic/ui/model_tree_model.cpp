/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_tree_model.h"
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <algorithm>

ModelTreeModel::ModelTreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
    m_groupRows = QVector<QVector<int>>(CategoryCount);
}

int ModelTreeModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid()) return m_visibleCategories.size();
    if (parent.column() != 0) return 0;
    Category cat = m_visibleCategories.value(parent.row(), CatOther);
    return m_groupRows.value(cat).size();
}

int ModelTreeModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 4;
}

QModelIndex ModelTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column < 0 || column >= columnCount()) return QModelIndex();
    if (!parent.isValid()) {
        if (row >= m_visibleCategories.size()) return QModelIndex();
        return createIndex(row, column, quintptr(-1));
    }
    Category cat = m_visibleCategories.value(parent.row(), CatOther);
    if (row >= m_groupRows.value(cat).size()) return QModelIndex();
    return createIndex(row, column, quintptr(parent.row()));
}

QModelIndex ModelTreeModel::parent(const QModelIndex& index) const {
    if (!index.isValid()) return QModelIndex();
    quintptr id = index.internalId();
    if (id == quintptr(-1)) return QModelIndex(); // top-level group row
    return createIndex(int(id), 0, quintptr(-1));
}

QVariant ModelTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.column() >= columnCount()) return QVariant();

    // ── Top-level category row ────────────────────────────────────────────
    if (!index.parent().isValid()) {
        if (index.row() >= m_visibleCategories.size()) return QVariant();
        Category cat = m_visibleCategories.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            if (index.column() == ColName) {
                return QString("%1 (%2)").arg(categoryName(cat)).arg(m_groupRows.at(cat).size());
            }
            return QVariant();
        case Qt::FontRole:
            if (index.column() == ColName) {
                QFont f;
                f.setBold(true);
                return f;
            }
            return QVariant();
        case Qt::ForegroundRole:
            if (index.column() == ColName) {
                QColor c(150, 150, 150);
                return c;
            }
            return QVariant();
        case CategoryRole:
            return int(cat);
        case CategoryNameRole:
            return categoryName(cat);
        default:
            return QVariant();
        }
    }

    // ── Model (leaf) row ──────────────────────────────────────────────────
    const SpiceModelInfo& info = modelInfo(index);
    if (info.name.isEmpty()) return QVariant();
    const bool fav = m_favorites.contains(info.name);
    const bool used = m_usedModels.contains(info.name);

    if (role == Qt::ToolTipRole && used) {
        return QString("In current schematic");
    }
    if (role == Qt::ForegroundRole && index.column() != ColFavorites) {
        if (used) {
            // Highlight models referenced in the schematic with a green tint.
            QColor c(34, 197, 94);
            return c;
        }
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName: return info.name;
        case ColType: return info.type;
        case ColLibrary: return QFileInfo(info.libraryPath).fileName();
        case ColFavorites: return fav ? QStringLiteral("★") : QString();
        default: return QVariant();
        }
    }
    if (role == Qt::DecorationRole && index.column() == ColName) {
        if (info.type == "Subcircuit") return QIcon(":/icons/comp_ic.svg");
        if (info.type == "NMOS" || info.type == "PMOS" ||
            info.type == "NPN" || info.type == "PNP") {
            return QIcon(":/icons/comp_transistor.svg");
        }
        return QIcon(":/icons/comp_diode.svg");
    }
    if (role == Qt::ForegroundRole && index.column() == ColFavorites) {
        if (fav) {
            QColor c(202, 138, 4); // amber star
            return c;
        }
        return QColor(100, 100, 100);
    }

    switch (role) {
    case NameRole: return info.name;
    case TypeRole: return info.type;
    case LibraryFileNameRole: return QFileInfo(info.libraryPath).fileName();
    case LibraryPathRole: return info.libraryPath;
    case DescriptionRole: return info.description;
    case ParamsRole: return info.params;
    case IsFavoriteRole: return fav;
    case IsUsedRole: return used;
    case CategoryRole: return int(categoryOf(index));
    case CategoryNameRole: return categoryName(categoryOf(index));
    default: return QVariant();
    }
}

QVariant ModelTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case ColName: return "Model Name";
    case ColType: return "Type";
    case ColLibrary: return "Library";
    case ColFavorites: return "★";
    default: return QVariant();
    }
}

void ModelTreeModel::setModels(const QVector<SpiceModelInfo>& models) {
    beginResetModel();
    m_models = models;
    m_groupRows = QVector<QVector<int>>(CategoryCount);
    m_flatRowCategory.resize(models.size());
    m_flatRowToChildPos.resize(models.size());
    m_flatRowByName.clear();
    m_flatRowByName.reserve(models.size());

    for (int i = 0; i < m_models.size(); ++i) {
        Category cat = categoryFor(m_models.at(i));
        m_groupRows[cat].append(i);
        m_flatRowCategory[i] = cat;
        m_flatRowByName[m_models.at(i).name] = i;
    }

    // Sort each category alphabetically (case-insensitive) so the tree shows
    // ordered model names without relying on QSortFilterProxyModel sorting,
    // which is far too expensive for tens of thousands of rows.
    for (int c = 0; c < CategoryCount; ++c) {
        QVector<int>& rows = m_groupRows[c];
        std::stable_sort(rows.begin(), rows.end(), [this](int a, int b) {
            return m_models.at(a).name.compare(m_models.at(b).name, Qt::CaseInsensitive) < 0;
        });
        for (int p = 0; p < rows.size(); ++p)
            m_flatRowToChildPos[rows[p]] = p;
    }

    m_visibleCategories.clear();
    for (int c = 0; c < CategoryCount; ++c) {
        if (!m_groupRows[c].isEmpty()) m_visibleCategories.append(Category(c));
    }
    endResetModel();
}

bool ModelTreeModel::isGroup(const QModelIndex& index) const {
    return index.isValid() && !index.parent().isValid();
}

ModelTreeModel::Category ModelTreeModel::categoryAt(int row) const {
    return m_visibleCategories.value(row, CatOther);
}

ModelTreeModel::Category ModelTreeModel::categoryOf(const QModelIndex& childIndex) const {
    if (!childIndex.isValid() || !childIndex.parent().isValid()) return CatOther;
    quintptr id = childIndex.internalId();
    return m_visibleCategories.value(int(id), CatOther);
}

const SpiceModelInfo& ModelTreeModel::modelInfo(const QModelIndex& index) const {
    static SpiceModelInfo s_invalid;
    if (!index.isValid() || !index.parent().isValid()) return s_invalid;
    quintptr id = index.internalId();
    Category cat = m_visibleCategories.value(int(id), CatOther);
    const QVector<int>& rows = m_groupRows.value(cat);
    if (index.row() < 0 || index.row() >= rows.size()) return s_invalid;
    return m_models.at(rows.at(index.row()));
}

int ModelTreeModel::groupSize(Category cat) const {
    return m_groupRows.value(cat).size();
}

QString ModelTreeModel::categoryName(Category cat) {
    switch (cat) {
    case CatDiode: return "Diodes";
    case CatBJT: return "BJT";
    case CatMOSFET: return "MOSFET";
    case CatJFET: return "JFET";
    case CatSubcircuit: return "Subcircuits";
    case CatOther: return "Other";
    default: return "All";
    }
}

ModelTreeModel::Category ModelTreeModel::categoryFor(const SpiceModelInfo& info) {
    const QString t = info.type.toUpper();
    if (info.type.compare("Subcircuit", Qt::CaseInsensitive) == 0) return CatSubcircuit;
    if (t == "NPN" || t == "PNP") return CatBJT;
    if (t == "NMOS" || t == "PMOS" || t == "VDMOS" || t == "NMF" || t == "PMF" ||
        t.startsWith("MOS") || t.startsWith("BSIM") || t.startsWith("HISIM") ||
        t == "SOI3") {
        return CatMOSFET;
    }
    if (t == "NJF" || t == "PJF" || t == "JFET") return CatJFET;
    if (t == "D" || t.startsWith("D") || t == "LED") return CatDiode;
    return CatOther;
}

void ModelTreeModel::setFavorites(const QSet<QString>& favs) {
    beginResetModel();
    m_favorites = favs;
    endResetModel();
}

void ModelTreeModel::toggleFavorite(const QModelIndex& index) {
    const SpiceModelInfo& info = modelInfo(index);
    if (info.name.isEmpty()) return;
    if (m_favorites.contains(info.name)) {
        m_favorites.remove(info.name);
    } else {
        m_favorites.insert(info.name);
    }
    if (index.isValid()) {
        Q_EMIT dataChanged(this->index(index.row(), 0, index.parent()),
                           this->index(index.row(), columnCount() - 1, index.parent()),
                           {IsFavoriteRole});
    }
}

void ModelTreeModel::setUsedModels(const QSet<QString>& used) {
    if (m_usedModels == used) return;

    const QSet<QString> removed = m_usedModels - used;
    const QSet<QString> added = used - m_usedModels;
    m_usedModels = used;

    // Refresh only the rows whose usage changed, preserving expansion state.
    const QSet<QString> changed = removed + added;
    for (const QString& name : changed) {
        const int fr = m_flatRowByName.value(name, -1);
        if (fr < 0 || fr >= m_flatRowCategory.size()) continue;
        const Category cat = m_flatRowCategory.at(fr);
        const int groupRow = m_visibleCategories.indexOf(cat);
        if (groupRow < 0) continue;
        QModelIndex parent = index(groupRow, 0);
        const int childRow = m_flatRowToChildPos.at(fr);
        Q_EMIT dataChanged(index(childRow, 0, parent),
                           this->index(childRow, columnCount() - 1, parent),
                           {IsUsedRole});
    }
}

// ─── ModelTreeFilterProxy ────────────────────────────────────────────────────

ModelTreeFilterProxy::ModelTreeFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(false);
}

void ModelTreeFilterProxy::setSearchText(const QString& text) {
    m_search = text.trimmed();
    m_searchLower = m_search.toLower();
    invalidate();
}

void ModelTreeFilterProxy::setCategoryFilter(ModelTreeModel::Category cat) {
    m_category = cat;
    invalidate();
}

void ModelTreeFilterProxy::setFavoritesOnly(bool on) {
    m_favoritesOnly = on;
    invalidate();
}

bool ModelTreeFilterProxy::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    const auto* model = qobject_cast<const ModelTreeModel*>(sourceModel());
    if (!model) return false;

    if (source_parent.isValid()) {
        QModelIndex idx = model->index(source_row, 0, source_parent);
        return acceptModel(model, idx);
    }

    // Top-level category row: keep if the category filter passes AND at least
    // one child model matches the active filters.
    if (m_category != ModelTreeModel::CatAll &&
        model->categoryAt(source_row) != m_category) {
        return false;
    }
    const QModelIndex groupIdx = model->index(source_row, 0);
    const int n = model->rowCount(groupIdx);
    for (int i = 0; i < n; ++i) {
        if (acceptModel(model, model->index(i, 0, groupIdx))) return true;
    }
    return false;
}

bool ModelTreeFilterProxy::acceptModel(const ModelTreeModel* model, const QModelIndex& idx) const {
    if (!idx.isValid()) return false;
    // Fast path for the common case (no active filters): accept immediately.
    if (m_favoritesOnly || m_category != ModelTreeModel::CatAll || !m_searchLower.isEmpty()) {
        if (m_favoritesOnly && !idx.data(ModelTreeModel::IsFavoriteRole).toBool()) return false;
        if (m_category != ModelTreeModel::CatAll &&
            model->categoryOf(idx) != m_category) {
            return false;
        }
        if (!m_searchLower.isEmpty()) {
            const SpiceModelInfo& info = model->modelInfo(idx);
            if (!info.name.contains(m_searchLower, Qt::CaseInsensitive) &&
                !info.type.contains(m_searchLower, Qt::CaseInsensitive) &&
                !info.libraryPath.contains(m_searchLower, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }
    return true;
}
