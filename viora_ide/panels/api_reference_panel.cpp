/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api_reference_panel.h"
#include "core/api_reference_data.h"
#include "../core/ide_theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QLineEdit>
#include <QTextEdit>
#include <QStandardItemModel>
#include <QSplitter>
#include <QLabel>

namespace IDE {

ApiReferencePanel::ApiReferencePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    loadApiData();
    ThemeManager::instance().registerThemeCallback(this, [this]() { reapplyTheme(); });
}

void ApiReferencePanel::setupUI() {
    auto tc = currentTheme();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search bar
    auto* searchWidget = new QWidget();
    searchWidget->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;").arg(tc.bgPanel, tc.border)
    );
    auto* searchLayout = new QHBoxLayout(searchWidget);
    searchLayout->setContentsMargins(10, 6, 10, 6);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search API functions...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(
        QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 4px 8px; border-radius: 3px; font-size: 10pt; }"
        ).arg(tc.bgDarkest, tc.textPrimary, tc.border)
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ApiReferencePanel::onSearchChanged);
    searchLayout->addWidget(m_searchEdit);
    layout->addWidget(searchWidget);

    // Splitter for list + detail
    auto* splitter = new QSplitter(Qt::Vertical);

    // Function list
    m_listView = new QListView();
    m_listView->setStyleSheet(
        QString(
            "QListView { background: %1; color: %2; border: none; font-size: 10pt; }"
            "QListView::item { padding: 8px 12px; border-bottom: 1px solid %3; min-height: 28px; }"
            "QListView::item:hover { background: %4; }"
            "QListView::item:selected { background: %5; color: %6; }"
        ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.bgDarkest, tc.accentBlue, tc.textPrimary)
    );
    connect(m_listView, &QListView::clicked, this, &ApiReferencePanel::onItemClicked);
    splitter->addWidget(m_listView);

    // Detail view
    m_detailView = new QTextEdit();
    m_detailView->setReadOnly(true);
    m_detailView->setStyleSheet(
        QString(
            "QTextEdit { background: %1; color: %2; border: none; "
            "  font-family: 'Consolas', monospace; font-size: 10pt; padding: 12px 16px; }"
        ).arg(tc.bgEditor, tc.textPrimary)
    );
    m_detailView->setPlaceholderText("Select a function to see details...");
    splitter->addWidget(m_detailView);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    layout->addWidget(splitter);
}

void ApiReferencePanel::loadApiData() {
    m_model = new QStandardItemModel(this);

    const auto& functions = ApiReferenceData::allFunctions();
    QString currentCategory;

    for (const auto& fn : functions) {
        if (fn.category != currentCategory) {
            currentCategory = fn.category;
            QStandardItem* sep = new QStandardItem("--- " + currentCategory + " ---");
            sep->setEditable(false);
            sep->setEnabled(false);
            QFont f = sep->font();
            f.setBold(true);
            sep->setFont(f);
            sep->setForeground(QColor(currentTheme().accentBlue));
            m_model->appendRow(sep);
        }

        QStandardItem* item = new QStandardItem(fn.name);
        item->setData(QVariant::fromValue(functions.indexOf(fn)), Qt::UserRole);
        m_model->appendRow(item);
    }

    m_listView->setModel(m_model);
}

void ApiReferencePanel::filterByCategory(const QString& category) {
    m_currentCategory = category;
    onSearchChanged(m_searchEdit->text());
}

void ApiReferencePanel::onSearchChanged(const QString& text) {
    if (!m_model) return;

    const auto& functions = ApiReferenceData::allFunctions();

    m_model->clear();

    for (int i = 0; i < functions.size(); ++i) {
        const auto& fn = functions[i];

        bool matchesSearch = text.isEmpty() ||
            fn.name.toLower().contains(text.toLower()) ||
            fn.description.toLower().contains(text.toLower());

        bool matchesCategory = m_currentCategory.isEmpty() || fn.category == m_currentCategory;

        if (matchesSearch && matchesCategory) {
            QStandardItem* item = new QStandardItem(fn.name);
            item->setData(i, Qt::UserRole);
            m_model->appendRow(item);
        }
    }
}

void ApiReferencePanel::onItemClicked(const QModelIndex& index) {
    int funcIdx = index.data(Qt::UserRole).toInt();
    const auto& functions = ApiReferenceData::allFunctions();

    if (funcIdx >= 0 && funcIdx < functions.size()) {
        auto dtc = currentTheme();
        const auto& fn = functions[funcIdx];
        QString html = QString(
            "<h3 style='color: %1; font-family: Consolas;'>%2</h3>"
            "<p style='color: %5; font-family: Consolas; font-size: 11pt;'>%3</p>"
            "<p style='color: %2;'>%4</p>"
            "<p style='color: %6; font-size: 9pt;'>Category: %7</p>"
        ).arg(dtc.accentBlue, dtc.textPrimary, fn.signature.toHtmlEscaped(),
             fn.description.toHtmlEscaped(), dtc.green, dtc.textSecondary, fn.category);

        m_detailView->setHtml(html);

        emit functionSelected(fn.name);
    }
}

void ApiReferencePanel::reapplyTheme() {
    auto tc = currentTheme();
    if (m_searchEdit) {
        m_searchEdit->setStyleSheet(
            QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 4px 8px; border-radius: 3px; font-size: 10pt; }")
            .arg(tc.bgDarkest, tc.textPrimary, tc.border)
        );
    }
    if (m_listView) {
        m_listView->setStyleSheet(
            QString(
                "QListView { background: %1; color: %2; border: none; font-size: 10pt; }"
                "QListView::item { padding: 8px 12px; border-bottom: 1px solid %3; min-height: 28px; }"
                "QListView::item:hover { background: %4; }"
                "QListView::item:selected { background: %5; color: %6; }"
            ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.bgDarkest, tc.accentBlue, tc.textPrimary)
        );
    }
    if (m_detailView) {
        m_detailView->setStyleSheet(
            QString("QTextEdit { background: %1; color: %2; border: none; "
            "  font-family: 'Consolas', monospace; font-size: 10pt; padding: 12px 16px; }")
            .arg(tc.bgEditor, tc.textPrimary)
        );
    }
}

} // namespace IDE
