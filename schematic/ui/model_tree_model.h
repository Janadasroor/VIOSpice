/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODEL_TREE_MODEL_H
#define MODEL_TREE_MODEL_H

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QVector>
#include <QSet>
#include <QHash>
#include "../../simulator/bridge/model_library_manager.h"

// Grouped (tree) model for the SPICE model browser. Top-level rows are broad
// categories (Diodes, BJT, MOSFET, ...); children are the individual models.
// Keeps ~59k models navigable without a flat, unscannable list.
class ModelTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Category {
        CatDiode = 0,
        CatBJT,
        CatMOSFET,
        CatJFET,
        CatSubcircuit,
        CatOther,
        CategoryCount,
        CatAll = CategoryCount // sentinel used by filters ("show everything")
    };

    enum ModelRoles {
        NameRole = Qt::UserRole + 1,
        TypeRole,
        LibraryFileNameRole,
        LibraryPathRole,
        DescriptionRole,
        ParamsRole,
        IsFavoriteRole,
        IsUsedRole,
        CategoryRole,
        CategoryNameRole
    };

    enum ColumnId { ColName = 0, ColType, ColLibrary, ColFavorites };

    explicit ModelTreeModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;

    void setModels(const QVector<SpiceModelInfo>& models);

    bool isGroup(const QModelIndex& index) const;
    Category categoryAt(int row) const;
    Category categoryOf(const QModelIndex& childIndex) const;
    const SpiceModelInfo& modelInfo(const QModelIndex& index) const;
    int groupSize(Category cat) const;
    QVector<Category> visibleCategories() const { return m_visibleCategories; }

    static QString categoryName(Category cat);
    static Category categoryFor(const SpiceModelInfo& info);

    // Favorites
    void setFavorites(const QSet<QString>& favs);
    void toggleFavorite(const QModelIndex& index);
    QSet<QString> favorites() const { return m_favorites; }

    // Models referenced in the current schematic
    void setUsedModels(const QSet<QString>& used);
    QSet<QString> usedModels() const { return m_usedModels; }

private:
    QVector<SpiceModelInfo> m_models;
    QVector<QVector<int>> m_groupRows;   // indexed by Category
    QVector<Category> m_visibleCategories; // only non-empty categories
    QSet<QString> m_favorites;
    QSet<QString> m_usedModels;

    // Reverse index for cheap per-row updates (name -> flat row -> (category, child row))
    QHash<QString, int> m_flatRowByName;
    QVector<Category> m_flatRowCategory;
    QVector<int> m_flatRowToChildPos;
};

// Tree-aware filter proxy: a category row is shown if it passes the category
// filter AND at least one of its child models matches the search/favorites filters.
class ModelTreeFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ModelTreeFilterProxy(QObject* parent = nullptr);

    void setSearchText(const QString& text);
    void setCategoryFilter(ModelTreeModel::Category cat);
    void setFavoritesOnly(bool on);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    bool acceptModel(const ModelTreeModel* model, const QModelIndex& idx) const;

    QString m_search;
    QString m_searchLower; // cached lowercase copy of m_search for fast matching
    ModelTreeModel::Category m_category = ModelTreeModel::CatAll;
    bool m_favoritesOnly = false;
};

#endif // MODEL_TREE_MODEL_H
