/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "problems_panel.h"
#include "../core/lsp_client.h"
#include "../core/ide_theme.h"
#include "../../core/visuals/theme_manager.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileInfo>

namespace IDE {

ProblemsPanel::ProblemsPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"", "Message", "File", "Line"});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto* header = m_tree->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->resizeSection(0, 24);

    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &ProblemsPanel::onItemClicked);

    // Apply theme
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        auto tc = currentTheme();
        m_tree->setStyleSheet(QString(
            "QTreeWidget { background: %1; color: %2; border: none; }"
            "QTreeWidget::item:selected { background: %3; }"
            "QHeaderView::section { background: %4; color: %2; border: 1px solid %5; padding: 4px; }"
        ).arg(tc.bgPanel, tc.textPrimary, tc.accentBlue, tc.bgDarkest, tc.border));
    });

    // Initial theme apply
    auto tc = currentTheme();
    m_tree->setStyleSheet(QString(
        "QTreeWidget { background: %1; color: %2; border: none; }"
        "QTreeWidget::item:selected { background: %3; }"
        "QHeaderView::section { background: %4; color: %2; border: 1px solid %5; padding: 4px; }"
    ).arg(tc.bgPanel, tc.textPrimary, tc.accentBlue, tc.bgDarkest, tc.border));
}

void ProblemsPanel::setDiagnostics(const QString& filePath, const QList<LspDiagnostic>& diagnostics) {
    // Remove existing items for this file
    clearFile(filePath);

    for (const LspDiagnostic& diag : diagnostics) {
        auto* item = new QTreeWidgetItem(m_tree);

        // Severity icon (text-based since SVG is heavy)
        QString icon;
        if (diag.isError()) {
            icon = "E";
            item->setForeground(0, QColor("#f44336"));
        } else if (diag.isWarning()) {
            icon = "W";
            item->setForeground(0, QColor("#ff9800"));
        } else {
            icon = "I";
            item->setForeground(0, QColor("#2196f3"));
        }

        item->setText(0, icon);
        item->setText(1, diag.message);

        QFileInfo fi(filePath);
        item->setText(2, fi.fileName());
        item->setText(3, QString::number(diag.range.start.line + 1));

        // Store metadata for navigation
        item->setData(0, Qt::UserRole, filePath);
        item->setData(1, Qt::UserRole, diag.range.start.line);
        item->setData(2, Qt::UserRole, diag.range.start.character);

        // Color the row
        QColor bgColor = diag.isError() ? QColor("#3a1515") : QColor("#3a2a10");
        for (int col = 0; col < 4; ++col) {
            item->setBackground(col, bgColor);
        }
    }

    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(3);
}

void ProblemsPanel::clearAll() {
    m_tree->clear();
}

void ProblemsPanel::clearFile(const QString& filePath) {
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == filePath) {
            delete m_tree->takeTopLevelItem(i);
        }
    }
}

int ProblemsPanel::errorCount() const {
    int count = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(0) == "E") ++count;
    }
    return count;
}

int ProblemsPanel::warningCount() const {
    int count = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(0) == "W") ++count;
    }
    return count;
}

void ProblemsPanel::onItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    int line = item->data(1, Qt::UserRole).toInt();
    int col = item->data(2, Qt::UserRole).toInt();

    emit problemClicked(filePath, line, col);
}

} // namespace IDE
