/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "template_browser_panel.h"
#include "../core/ide_theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QPixmap>

namespace IDE {

class TemplateCardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        auto tc = currentTheme();

        bool hovered = option.state & QStyle::State_MouseOver;
        bool selected = option.state & QStyle::State_Selected;

        // Card background
        QRect cardRect = option.rect.adjusted(8, 4, -8, -4);
        if (selected) {
            painter->fillRect(cardRect, QColor(tc.accentBlue));
        } else if (hovered) {
            bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
            painter->fillRect(cardRect, isLight ? QColor("#e8edf4") : QColor("#253248"));
        } else {
            painter->fillRect(cardRect, QColor(tc.bgPanel));
        }

        // Get category
        QString category = index.data(Qt::UserRole + 1).toString();

        // Icon area (left side of card)
        QRect iconRect = cardRect.adjusted(16, 12, -cardRect.width() + 50, -12);
        QString iconPath = QString(":/extension_ide/icons/template_%1.svg").arg(category.toLower());
        QPixmap iconPixmap(iconPath);
        if (!iconPixmap.isNull()) {
            // Tint icon for theme
            QPainter iconPainter(&iconPixmap);
            iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            iconPainter.fillRect(iconPixmap.rect(), QColor(tc.textPrimary));
            iconPainter.end();
            painter->drawPixmap(iconRect.x(), iconRect.y() + (iconRect.height() - 22) / 2, 22, 22, iconPixmap);
        } else {
            painter->setPen(QColor(tc.textSecondary));
            QFont iconFont = painter->font();
            iconFont.setPixelSize(18);
            painter->setFont(iconFont);
            QString iconChar = QString::fromUtf8("\u2699");
            if (category == "SIMULATION") iconChar = QString::fromUtf8("\u25B6");
            else if (category == "AUTOMATION") iconChar = QString::fromUtf8("\u2731");
            else iconChar = QString::fromUtf8("\u2261");
            painter->drawText(iconRect, Qt::AlignLeft | Qt::AlignVCenter, iconChar);
        }

        // Text
        QRect textRect = cardRect.adjusted(58, 4, -16, -4);
        painter->setPen(selected ? QColor("#ffffff") : QColor(tc.textPrimary));
        QFont textFont = painter->font();
        textFont.setPixelSize(13);
        textFont.setWeight(QFont::DemiBold);
        painter->setFont(textFont);
        QString displayText = index.data(Qt::DisplayRole).toString();
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, displayText);

        // Category subtitle
        QRect catRect = cardRect.adjusted(58, 4, -16, -2);
        painter->setPen(QColor(currentTheme().textSecondary));
        QFont catFont = painter->font();
        catFont.setPixelSize(10);
        catFont.setWeight(QFont::Normal);
        painter->setFont(catFont);
        painter->drawText(catRect, Qt::AlignLeft | Qt::AlignBottom, category);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option); Q_UNUSED(index);
        return QSize(0, 84);
    }
};

TemplateBrowserPanel::TemplateBrowserPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    loadTemplates();
    ThemeManager::instance().registerThemeCallback(this, [this]() { reapplyTheme(); });
}

void TemplateBrowserPanel::setupUI() {
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
    m_searchEdit->setPlaceholderText("Search templates...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(
        QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 4px 8px; border-radius: 3px; font-size: 10pt; }"
        ).arg(tc.bgDarkest, tc.textPrimary, tc.border)
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TemplateBrowserPanel::onSearchChanged);
    searchLayout->addWidget(m_searchEdit);
    layout->addWidget(searchWidget);

    // Template list with card-style delegate
    m_templateList = new QListWidget();
    m_templateList->setItemDelegate(new TemplateCardDelegate(m_templateList));
    m_templateList->setSpacing(2);
    m_templateList->setStyleSheet(
        QString(
            "QListWidget { background: %1; color: %2; border: none; font-size: 10pt; outline: none; }"
            "QScrollBar:vertical { background: %1; width: 8px; }"
            "QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background: %4; }"
        ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.textSecondary)
    );
    connect(m_templateList, &QListWidget::itemDoubleClicked, this, &TemplateBrowserPanel::onItemDoubleClicked);
    layout->addWidget(m_templateList);
}

void TemplateBrowserPanel::loadTemplates() {
    m_templateList->clear();

    QString appPath = QCoreApplication::applicationDirPath();
    struct Source { QString path; QString category; };
    QList<Source> sources = {
        {QDir(appPath).absoluteFilePath("../python/templates"), "SIMULATION"},
        {QDir(appPath).absoluteFilePath("../python/automation"), "AUTOMATION"},
        {QDir(appPath).absoluteFilePath("../examples"), "EXAMPLES"}
    };

    for (const auto& src : sources) {
        QDir dir(src.path);
        if (!dir.exists()) continue;

        QStringList files = dir.entryList({"*.flux"}, QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& file : files) {
            auto* item = new QListWidgetItem();
            item->setText(file.section('.', 0, 0).replace('_', ' ').toUpper());
            item->setData(Qt::UserRole, dir.absoluteFilePath(file));
            item->setData(Qt::UserRole + 1, src.category);
            item->setToolTip(QString("[%1] %2").arg(src.category, dir.absoluteFilePath(file)));

            m_templateList->addItem(item);
        }
    }
}

void TemplateBrowserPanel::onSearchChanged(const QString& text) {
    for (int i = 0; i < m_templateList->count(); ++i) {
        auto* item = m_templateList->item(i);
        bool matches = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

void TemplateBrowserPanel::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    emit templateSelected(path);
}

void TemplateBrowserPanel::reapplyTheme() {
    auto tc = currentTheme();
    if (m_searchEdit) {
        m_searchEdit->setStyleSheet(
            QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "padding: 4px 8px; border-radius: 3px; font-size: 10pt; }")
            .arg(tc.bgDarkest, tc.textPrimary, tc.border)
        );
    }
    if (m_templateList) {
        m_templateList->setStyleSheet(
            QString(
                "QListWidget { background: %1; color: %2; border: none; font-size: 10pt; outline: none; }"
                "QScrollBar:vertical { background: %1; width: 8px; }"
                "QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 20px; }"
                "QScrollBar::handle:vertical:hover { background: %4; }"
            ).arg(tc.bgPanel, tc.textPrimary, tc.border, tc.textSecondary)
        );
        m_templateList->viewport()->update();
    }
}

} // namespace IDE
