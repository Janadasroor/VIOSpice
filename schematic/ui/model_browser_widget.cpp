/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_browser_widget.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QHeaderView>
#include <QFileInfo>
#include <QGroupBox>
#include <QShortcut>
#include <QApplication>
#include <QTimer>
#include <QComboBox>
#include <QCheckBox>
#include <QMenu>
#include <QClipboard>

ModelBrowserWidget::ModelBrowserWidget(QWidget* parent)
    : QWidget(parent) {
    m_model = new ModelTreeModel(this);
    m_proxyModel = new ModelTreeFilterProxy(this);
    m_proxyModel->setSourceModel(m_model);

    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ModelBrowserWidget::applyTheme);
    applyTheme();

    connect(ModelLibraryManager::instance().ptr(), &ModelLibraryManager::libraryReloaded, this, &ModelBrowserWidget::onLibraryReloaded);
    onLibraryReloaded();
}

ModelBrowserWidget::~ModelBrowserWidget() {}

void ModelBrowserWidget::setUsedModels(const QSet<QString>& used) {
    m_model->setUsedModels(used);
}

void ModelBrowserWidget::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // --- Search row ---
    auto* topLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search models...");
    m_searchBox->setClearButtonEnabled(true);

    auto* reloadBtn = new QPushButton();
    reloadBtn->setIcon(QIcon(":/icons/toolbar_refresh.png"));
    reloadBtn->setToolTip("Reload Libraries");
    reloadBtn->setFixedWidth(30);

    topLayout->addWidget(m_searchBox);
    topLayout->addWidget(reloadBtn);
    layout->addLayout(topLayout);

    // --- Filter row: category + favorites ---
    auto* filterLayout = new QHBoxLayout();
    m_categoryCombo = new QComboBox();
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(14);

    m_favOnlyCheck = new QCheckBox("★ Favorites");
    m_favOnlyCheck->setToolTip("Show favorites only");

    filterLayout->addWidget(m_categoryCombo, 1);
    filterLayout->addWidget(m_favOnlyCheck);
    layout->addLayout(filterLayout);

    // --- Tree View (grouped by category) ---
    m_treeView = new QTreeView();
    m_treeView->setModel(m_proxyModel);
    m_treeView->setUniformRowHeights(true);
    // NOTE: sorting intentionally left disabled. With ~59k models, view-driven
    // sorting makes QTreeView::layout() trigger a full stable_sort of every
    // group on each expandToDepth()/hasChildren() call (freeze). Groups are
    // pre-sorted alphabetically in ModelTreeModel::setModels instead.
    m_treeView->setSortingEnabled(false);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setIndentation(14);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeView->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_treeView->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_treeView->header()->resizeSection(0, 200);
    m_treeView->header()->resizeSection(3, 28);
    m_treeView->header()->setStretchLastSection(true);
    layout->addWidget(m_treeView, 1);

    // --- Detail Panel ---
    auto* detailGrp = new QGroupBox("Selection Details");
    auto* detailLayout = new QVBoxLayout(detailGrp);

    m_detailLabel = new QLabel("Select a model to see details.");
    m_detailLabel->setWordWrap(true);
    detailLayout->addWidget(m_detailLabel);

    m_applyBtn = new QPushButton("Apply to Selected Component");
    m_applyBtn->setEnabled(false);
    detailLayout->addWidget(m_applyBtn);

    layout->addWidget(detailGrp);

    // --- Search debounce ---
    // --- Search debounce ---
    // Restart the timer on every keystroke so a filter only runs after the user
    // pauses typing. This keeps rapid typing responsive.
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        m_proxyModel->setSearchText(m_pendingSearchText);
        expandToFit();
    });

    // --- Connections ---
    connect(m_searchBox, &QLineEdit::textChanged, this, &ModelBrowserWidget::onSearchChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_proxyModel->setCategoryFilter(
            static_cast<ModelTreeModel::Category>(m_categoryCombo->currentData().toInt()));
        expandToFit();
    });
    connect(m_favOnlyCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_proxyModel->setFavoritesOnly(on);
        expandToFit();
    });
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &ModelBrowserWidget::onItemSelectionChanged);
    connect(m_applyBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onApplyClicked);
    connect(reloadBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onReloadClicked);

    // Click the ★ column to toggle favorite
    connect(m_treeView, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (index.column() != ModelTreeModel::ColFavorites) return;
        QModelIndex src = m_proxyModel->mapToSource(index);
        if (!src.isValid()) return;
        m_model->toggleFavorite(src);
    });

    // Context menu: toggle favorite / copy name / apply
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = m_treeView->indexAt(pos);
        if (!index.isValid()) return;
        QModelIndex src = m_proxyModel->mapToSource(index);
        if (!src.isValid() || m_model->isGroup(src)) return;

        QMenu menu(this);
        QAction* favAct = menu.addAction("Toggle Favorite");
        QAction* copyAct = menu.addAction("Copy Model Name");
        QAction* applyAct = menu.addAction("Apply to Selected Component");
        QAction* chosen = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (chosen == favAct) {
            m_model->toggleFavorite(src);
        } else if (chosen == copyAct) {
            QApplication::clipboard()->setText(m_model->modelInfo(src).name);
        } else if (chosen == applyAct) {
            m_treeView->setCurrentIndex(index);
            onApplyClicked();
        }
    });

    // Enter → apply selected model
    auto* enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), m_treeView);
    connect(enterShortcut, &QShortcut::activated, this, &ModelBrowserWidget::onApplyClicked);
}

void ModelBrowserWidget::updateCategoryCounts() {
    const QVector<ModelTreeModel::Category> cats = m_model->visibleCategories();
    int total = 0;
    for (auto c : cats) total += m_model->groupSize(c);

    QSignalBlocker block(m_categoryCombo);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(QString("All Types (%1)").arg(total), int(ModelTreeModel::CatAll));
    for (auto c : cats) {
        m_categoryCombo->addItem(
            QString("%1 (%2)").arg(ModelTreeModel::categoryName(c)).arg(m_model->groupSize(c)),
            int(c));
    }
    block.unblock();

    // Keep the proxy in sync with the combo's current selection after the rebuild.
    m_proxyModel->setCategoryFilter(
        static_cast<ModelTreeModel::Category>(m_categoryCombo->currentData().toInt()));
    expandToFit();
}

void ModelBrowserWidget::expandToFit() {
    // Expanding a group forces QTreeView to lay out every visible child row.
    // With tens of thousands of models this is very slow, so only auto-expand
    // reasonably-sized groups (e.g. after a search narrows the result set).
    const int groups = m_proxyModel->rowCount();
    if (groups <= 0) return;

    for (int g = 0; g < groups; ++g) {
        const QModelIndex gi = m_proxyModel->index(g, 0);
        if (!gi.isValid()) continue;
        const int n = m_proxyModel->rowCount(gi);
        if (n > 0 && n <= 2000)
            m_treeView->setExpanded(gi, true);
    }
}

void ModelBrowserWidget::onSearchChanged(const QString& text) {
    m_pendingSearchText = text.trimmed();
    m_searchDebounceTimer->start();
}

void ModelBrowserWidget::onItemSelectionChanged(const QModelIndex& current) {
    if (!current.isValid()) {
        m_detailLabel->setText("Select a model to see details.");
        m_applyBtn->setEnabled(false);
        return;
    }

    QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    if (!sourceIndex.isValid() || m_model->isGroup(sourceIndex)) {
        m_detailLabel->setText("Select a model to see details.");
        m_applyBtn->setEnabled(false);
        return;
    }

    const auto& found = m_model->modelInfo(sourceIndex);

    QString details = QString("<b>Name:</b> %1<br>"
                              "<b>Type:</b> %2<br>"
                              "<b>Source:</b> %3<br>"
                              "<b>Params:</b> %4")
        .arg(found.name)
        .arg(found.type)
        .arg(QFileInfo(found.libraryPath).fileName())
        .arg(found.params.join(", "));

    if (!found.description.isEmpty()) {
        details += "<br><b>Note:</b> " + found.description;
    }

    m_detailLabel->setText(details);
    m_applyBtn->setEnabled(true);
    Q_EMIT modelSelected(found);
}

void ModelBrowserWidget::onApplyClicked() {
    QModelIndex current = m_treeView->currentIndex();
    if (!current.isValid()) return;

    QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    if (!sourceIndex.isValid() || m_model->isGroup(sourceIndex)) return;

    const auto& info = m_model->modelInfo(sourceIndex);
    Q_EMIT applyModelRequested(info);
}

void ModelBrowserWidget::onReloadClicked() {
    ModelLibraryManager::instance().reload();
}

void ModelBrowserWidget::onLibraryReloaded() {
    m_model->setModels(ModelLibraryManager::instance().allModels());
    expandToFit();
    updateCategoryCounts();
}

void ModelBrowserWidget::applyTheme() {
    auto* theme = ThemeManager::theme();
    if (!theme) return;

    bool isLight = (theme->type() == PCBTheme::Light);

    // Tree view: theme base + alternate row colors so cards don't end up
    // black/white mixed (QPalette::AlternateBase is not set by the global theme).
    QColor base = isLight ? Qt::white : QColor(24, 24, 27);
    QColor alternate = isLight ? QColor(241, 245, 249) : QColor(33, 33, 36);
    QColor text = theme->textColor();

    QPalette pal = m_treeView->palette();
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, alternate);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Highlight, theme->accentColor());
    pal.setColor(QPalette::HighlightedText, Qt::white);
    m_treeView->setPalette(pal);
    m_treeView->setStyleSheet(QString(
        "QTreeView::item { padding: 4px 6px; }"
        "QTreeView::item:selected { background: %1; color: white; border-radius: 4px; }"
    ).arg(theme->accentColor().name()));

    // Search box
    QString inputBg = isLight ? "#ffffff" : "#1a1a1a";
    QString inputText = isLight ? "#1e293b" : theme->textColor().name();
    m_searchBox->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 6px 12px; }"
        "QLineEdit:focus { border-color: %4; background-color: %1; }"
    ).arg(inputBg, inputText, theme->panelBorder().name(), theme->accentColor().name()));

    // Detail label + group box
    m_detailLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(theme->textSecondary().name()));
    auto* detailGrp = qobject_cast<QGroupBox*>(m_detailLabel->parentWidget());
    if (detailGrp) {
        detailGrp->setStyleSheet(QString("QGroupBox { color: %1; font-weight: 600; }")
                                     .arg(theme->textColor().name()));
    }

    // Apply button
    QString disabledBg = isLight ? "#cbd5e1" : "#2d2d32";
    QString disabledFg = isLight ? "#64748b" : "#555555";
    m_applyBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border-radius: 4px; padding: 6px; font-weight: bold; }"
        "QPushButton:disabled { background-color: %2; color: %3; }"
    ).arg(theme->accentColor().name(), disabledBg, disabledFg));

    // Combo + favorites checkbox: pull background/text from the theme palette.
    QString comboBg = isLight ? "#ffffff" : "#1a1a1a";
    m_categoryCombo->setStyleSheet(QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px 8px; }"
        "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; selection-color: white; }"
    ).arg(comboBg, theme->textColor().name(), theme->panelBorder().name(), theme->accentColor().name()));
    m_favOnlyCheck->setStyleSheet(QString("QCheckBox { color: %1; }")
                                      .arg(theme->textColor().name()));
}
