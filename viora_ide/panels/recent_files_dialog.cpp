/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "recent_files_dialog.h"
#include "../core/ide_theme.h"
#include "../../core/visuals/theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>

namespace IDE {

RecentFilesDialog::RecentFilesDialog(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Widget);
    setVisible(false);
    setFixedWidth(520);
    setMinimumHeight(350);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(16, 12, 16, 8);

    auto* titleLabel = new QLabel("Recent Files", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    m_countLabel = new QLabel("0 files", this);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_countLabel);
    layout->addLayout(headerLayout);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Filter recent files...");
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &RecentFilesDialog::onFilterChanged);
    connect(m_listWidget, &QListWidget::itemActivated, this, &RecentFilesDialog::onItemActivated);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &RecentFilesDialog::applyTheme);
}

void RecentFilesDialog::setRecentFiles(const QStringList& files) {
    m_files = files;
    rebuildFilteredList(m_searchEdit->text());
}

void RecentFilesDialog::showDialog() {
    rebuildFilteredList("");
    positionOverParent();
    show();
    raise();
    m_searchEdit->clear();
    m_searchEdit->setFocus();
    update();
}

void RecentFilesDialog::hideDialog() {
    hide();
}

bool RecentFilesDialog::isDialogVisible() const {
    return isVisible();
}

void RecentFilesDialog::positionOverParent() {
    QWidget* p = parentWidget();
    if (!p) return;
    int w = qMin(520, p->width() - 80);
    int x = (p->width() - w) / 2;
    int h = qMin(380, p->height() / 2);
    int y = p->height() / 6;
    setGeometry(x, y, w, h);
}

void RecentFilesDialog::onFilterChanged(const QString& text) {
    rebuildFilteredList(text);
}

void RecentFilesDialog::onItemActivated(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        hide();
        emit fileSelected(path);
    }
}

void RecentFilesDialog::rebuildFilteredList(const QString& filter) {
    m_listWidget->clear();
    QString lower = filter.toLower();
    int matchCount = 0;

    for (const QString& filePath : m_files) {
        if (!lower.isEmpty() && !filePath.toLower().contains(lower)) continue;
        QFileInfo fi(filePath);
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, filePath);
        item->setText(fi.fileName());
        item->setToolTip(filePath);
        m_listWidget->addItem(item);
        matchCount++;
    }

    m_countLabel->setText(QString("%1 of %2").arg(matchCount).arg(m_files.size()));
}

void RecentFilesDialog::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_listWidget->currentItem())
            onItemActivated(m_listWidget->currentItem());
        return;
    }
    if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) {
        QWidget::keyPressEvent(e);
        return;
    }
    m_searchEdit->event(e);
}

void RecentFilesDialog::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto tc = currentTheme();
    QPainterPath path;
    path.addRoundedRect(rect(), 8, 8);
    p.setClipPath(path);

    p.fillRect(rect(), QColor(tc.bgDarkest));
    p.setPen(QColor(tc.border));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    QWidget::paintEvent(e);
}

void RecentFilesDialog::applyTheme() {
    auto tc = currentTheme();
    QString fg = tc.textPrimary;
    QString hover = tc.bgTabInactive;
    QString border = tc.border;
    QString accent = tc.accentBlue;
    setStyleSheet(
        "QLineEdit { background: transparent; color: " + fg + "; border: none; border-bottom: 1px solid " + border + "; "
        "  padding: 10px 16px; font-size: 13px; }"
        "QLineEdit:focus { border-bottom: 2px solid " + accent + "; }"
        "QListWidget { background: transparent; color: " + fg + "; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 6px 16px; }"
        "QListWidget::item:selected { background: " + accent + "; color: white; border-radius: 4px; }"
        "QListWidget::item:hover { background: " + hover + "; }"
        "QLabel { color: " + fg + "; }"
        "QScrollBar:vertical { background: transparent; width: 6px; }"
        "QScrollBar::handle:vertical { background: " + border + "; border-radius: 3px; min-height: 30px; }"
    );
}

} // namespace IDE
