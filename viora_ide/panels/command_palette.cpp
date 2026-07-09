/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "command_palette.h"
#include "../core/ide_theme.h"
#include "../../core/visuals/theme_manager.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

namespace IDE {

CommandPalette::CommandPalette(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Widget);
    setVisible(false);
    setFixedWidth(520);
    setMinimumHeight(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Type a command...");
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &CommandPalette::onFilterChanged);
    connect(m_listWidget, &QListWidget::itemActivated, this, &CommandPalette::onItemActivated);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &CommandPalette::applyTheme);
}

void CommandPalette::addCommand(const QString& name, const QString& shortcut, std::function<void()> action) {
    m_commands.append({name, shortcut, action});
}

void CommandPalette::addSeparator(const QString& label) {
    m_commands.append({ "--- " + label + " ---", "", nullptr });
}

void CommandPalette::showPalette() {
    rebuildFilteredList("");
    positionOverParent();
    show();
    raise();
    m_searchEdit->clear();
    m_searchEdit->setFocus();
    update();
}

void CommandPalette::hidePalette() {
    hide();
}

bool CommandPalette::isPaletteVisible() const {
    return isVisible();
}

void CommandPalette::positionOverParent() {
    QWidget* p = parentWidget();
    if (!p) return;
    int w = qMin(520, p->width() - 80);
    int x = (p->width() - w) / 2;
    int h = qMin(380, p->height() / 2);
    int y = p->height() / 6;
    setGeometry(x, y, w, h);
}

void CommandPalette::onFilterChanged(const QString& text) {
    rebuildFilteredList(text);
}

void CommandPalette::onItemActivated(QListWidgetItem* item) {
    int row = m_listWidget->row(item);
    if (row < 0 || row >= m_commands.size()) return;
    const PaletteCommand& cmd = m_commands[row];
    if (cmd.action) {
        hide();
        emit commandExecuted();
        cmd.action();
    }
}

void CommandPalette::rebuildFilteredList(const QString& filter) {
    m_listWidget->clear();
    QString lower = filter.toLower();

    for (const PaletteCommand& cmd : m_commands) {
        if (cmd.name.startsWith("--- ")) {
            if (filter.isEmpty()) {
                auto* item = new QListWidgetItem(cmd.name);
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
                item->setForeground(QColor("#569cd6"));
                m_listWidget->addItem(item);
            }
            continue;
        }
        if (lower.isEmpty() || cmd.name.toLower().contains(lower) || cmd.shortcut.toLower().contains(lower)) {
            auto* item = new QListWidgetItem(cmd.name);
            if (!cmd.shortcut.isEmpty()) {
                item->setText(cmd.name + "    " + cmd.shortcut);
            }
            m_listWidget->addItem(item);
        }
    }
    if (m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
}

void CommandPalette::keyPressEvent(QKeyEvent* e) {
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

void CommandPalette::paintEvent(QPaintEvent* e) {
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

void CommandPalette::applyTheme() {
    auto tc = currentTheme();
    QString fg = tc.textPrimary;
    QString hover = tc.bgTabInactive;
    QString border = tc.border;
    QString accent = tc.accentBlue;
    setStyleSheet(
        "QLineEdit { background: transparent; color: " + fg + "; border: none; border-bottom: 1px solid " + border + "; "
        "  padding: 12px 16px; font-size: 14px; }"
        "QLineEdit:focus { border-bottom: 2px solid " + accent + "; }"
        "QListWidget { background: transparent; color: " + fg + "; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 8px 16px; }"
        "QListWidget::item:selected { background: " + accent + "; color: white; border-radius: 4px; }"
        "QListWidget::item:hover { background: " + hover + "; }"
        "QScrollBar:vertical { background: transparent; width: 6px; }"
        "QScrollBar::handle:vertical { background: " + border + "; border-radius: 3px; min-height: 30px; }"
    );
}

} // namespace IDE
