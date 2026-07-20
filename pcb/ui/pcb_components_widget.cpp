/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_components_widget.h"
#include "footprint_preview_view.h"
#include "../dialogs/footprint_browser_dialog.h"
#include "theme_manager.h"
#include "../../footprints/footprint_library.h"
#include "../../footprints/footprint_editor.h"
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QDebug>
#include <QSettings>

PCBComponentsWidget::PCBComponentsWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#1a1a1a";
    QString fg = theme ? theme->textColor().name() : "#dcdcdc";
    QString border = theme ? theme->panelBorder().name() : "#2d2d2d";

    // ── Search bar + Buttons ─────────────────────────────────────────────
    QWidget* searchBarContainer = new QWidget(this);
    QHBoxLayout* searchBarLayout = new QHBoxLayout(searchBarContainer);
    searchBarLayout->setContentsMargins(5, 5, 5, 5);
    searchBarLayout->setSpacing(2);

    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("🔍  Search footprints...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "   background-color: #1a1a1a;"
        "   border: 1px solid #333333;"
        "   border-radius: 4px;"
        "   padding: 7px 10px;"
        "   color: #e0e0e0;"
        "   font-size: 12px;"
        "   margin: 0px;"
        "}"
        "QLineEdit:focus {"
        "   border-color: #007acc;"
        "   background-color: #202020;"
        "}"
    );
    searchBarLayout->addWidget(m_searchBox);

    // Compact mode toggle
    m_compactToggle = new QPushButton(this);
    m_compactToggle->setIcon(QIcon(":/icons/view_rows.svg"));
    m_compactToggle->setIconSize(QSize(18, 18));
    m_compactToggle->setFixedSize(32, 32);
    m_compactToggle->setCursor(Qt::PointingHandCursor);
    m_compactToggle->setToolTip("Toggle compact mode");
    m_compactToggle->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(128,128,128,0.15); border-radius: 4px; }"
    );
    connect(m_compactToggle, &QPushButton::clicked, this, &PCBComponentsWidget::onToggleCompactMode);
    searchBarLayout->addWidget(m_compactToggle);

    // Hide action cards toggle
    m_actionCardsToggle = new QPushButton(this);
    m_actionCardsToggle->setIcon(QIcon(":/icons/eye.svg"));
    m_actionCardsToggle->setIconSize(QSize(18, 18));
    m_actionCardsToggle->setFixedSize(32, 32);
    m_actionCardsToggle->setCursor(Qt::PointingHandCursor);
    m_actionCardsToggle->setToolTip("Toggle action cards");
    m_actionCardsToggle->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(128,128,128,0.15); border-radius: 4px; }"
    );
    connect(m_actionCardsToggle, &QPushButton::clicked, this, &PCBComponentsWidget::onToggleActionCards);
    searchBarLayout->addWidget(m_actionCardsToggle);

    layout->addWidget(searchBarContainer);

    // Search Debounce timer setup
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        onSearchTextChanged(m_pendingSearchText);
    });
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_pendingSearchText = text;
        m_searchDebounceTimer->start(150); // 150ms debounce
    });

    // ── Filter Dropdown ──────────────────────────────────────────────────
    m_filterCombo = new QComboBox(this);
    m_filterCombo->setStyleSheet(
        "QComboBox { background: #1a1a1a; border: 1px solid #333; border-radius: 4px; padding: 4px 8px; font-size: 11px; color: #dcdcdc; margin: 0 5px 5px 5px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1a1a1a; color: #dcdcdc; border: 1px solid #333; }"
    );
    setupFilterChips();
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PCBComponentsWidget::onFilterChanged);
    layout->addWidget(m_filterCombo);

    // ── Action Cards Container ───────────────────────────────────────────
    m_actionContainer = new QWidget(this);
    QVBoxLayout* actionLayout = new QVBoxLayout(m_actionContainer);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(1);

    auto createActionCard = [this, actionLayout](const QString& title, const QString& subTitle, const char* slot) {
        QPushButton* btn = new QPushButton(this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(60);
        
        QVBoxLayout* btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(15, 0, 15, 0);
        btnLayout->setSpacing(2);
        
        QLabel* titleLabel = new QLabel(title);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel->setStyleSheet("color: #ffffff; font-weight: 600; font-size: 12px; border: none; background: transparent;");
        
        QLabel* descLabel = new QLabel(subTitle);
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        descLabel->setStyleSheet("color: #888888; font-size: 10px; border: none; background: transparent;");
        
        btnLayout->addWidget(titleLabel);
        btnLayout->addWidget(descLabel);
        
        btn->setStyleSheet(
            "QPushButton {"
            "   background-color: #222222;"
            "   border: none;"
            "   border-bottom: 1px solid #2d2d2d;"
            "   text-align: left;"
            "}"
            "QPushButton:hover {"
            "   background-color: #2d2d2d;"
            "}"
        );
        
        connect(btn, SIGNAL(clicked()), this, slot);
        actionLayout->addWidget(btn);
        return btn;
    };

    createActionCard("New Footprint", "Open wizard to generate footprints", SLOT(onCreateFootprint()));
    createActionCard("Browse Libraries", "Search global footprint database", SLOT(onOpenLibraryBrowser()));

    layout->addWidget(m_actionContainer);

    // ── Collapsible Recent Section ──────────────────────────────────────
    setupRecentSection();
    layout->addWidget(m_recentHeader);
    layout->addWidget(m_recentContainer);

    // ── Collapsible Standard Libraries Section ───────────────────────────
    m_standardHeader = createSectionHeader("FOOTPRINT LIBRARIES", m_standardExpanded, [this]() {
        onToggleStandardSection();
    });
    layout->addWidget(m_standardHeader);

    // ── Footprint Tree ──────────────────────────────────────────────────
    m_componentList = new QTreeWidget(this);
    m_componentList->setFrameShape(QFrame::NoFrame);
    m_componentList->setHeaderHidden(true);
    m_componentList->setIndentation(16);
    m_componentList->setStyleSheet(
        "QTreeWidget {"
        "   background-color: #1a1a1a;"
        "   border: none;"
        "   color: #dcdcdc;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item:hover { background-color: #2a2a2a; }"
        "QTreeWidget::item:selected { background-color: #094771; color: white; }"
    );

    connect(m_componentList, &QTreeWidget::itemClicked, this, &PCBComponentsWidget::onItemClicked);
    layout->addWidget(m_componentList, 1);

    // ── Preview Panel ───────────────────────────────────────────────────
    QWidget* previewPanel = new QWidget(this);
    previewPanel->setFixedHeight(150);
    previewPanel->setStyleSheet("background-color: #1a1a1a; border-top: 1px solid #2d2d2d;");
    
    QVBoxLayout* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(10, 10, 10, 10);
    
    m_previewView = new FootprintPreviewView(this);
    m_previewView->setStyleSheet("background-color: #0c0c0c; border: 1px solid #333;");
    
    previewLayout->addWidget(m_previewView);
    layout->addWidget(previewPanel);

    populate();
}

PCBComponentsWidget::~PCBComponentsWidget() {}

void PCBComponentsWidget::setupFilterChips() {
    m_filterCombo->clear();
    m_filterCombo->addItem("All Categories");

    QSet<QString> categories;
    auto libraries = FootprintLibraryManager::instance().libraries();
    for (auto* lib : libraries) {
        for (const auto& fpName : lib->getFootprintNames()) {
            FootprintDefinition def = lib->getFootprint(fpName);
            if (!def.category().isEmpty()) {
                categories.insert(def.category());
            }
        }
    }

    QStringList sortedCats = categories.values();
    std::sort(sortedCats.begin(), sortedCats.end());
    for (const auto& cat : sortedCats) {
        m_filterCombo->addItem(cat);
    }
}

void PCBComponentsWidget::onFilterChanged(int index) {
    if (index <= 0) m_activeCategory = "";
    else m_activeCategory = m_filterCombo->itemText(index);
    populate();
}

QWidget* PCBComponentsWidget::createSectionHeader(const QString& title, bool expanded, std::function<void()> toggleFn) {
    QWidget* header = new QWidget(this);
    header->setFixedHeight(28);
    header->setStyleSheet("background-color: #1a1a1a; border-bottom: 1px solid #2d2d2d;");

    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QPushButton* indicator = new QPushButton(header);
    indicator->setIcon(QIcon(expanded ? ":/icons/chevron_down.svg" : ":/icons/chevron_right.svg"));
    indicator->setIconSize(QSize(12, 12));
    indicator->setFixedSize(24, 28);
    indicator->setCursor(Qt::PointingHandCursor);
    indicator->setStyleSheet("QPushButton { border: none; background: transparent; }");
    connect(indicator, &QPushButton::clicked, this, toggleFn);
    layout->addWidget(indicator);

    QLabel* label = new QLabel("   " + title, header);
    label->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; background: transparent;");
    layout->addWidget(label);
    layout->addStretch();

    if (title.contains("RECENT")) {
        m_recentIndicator = indicator;
    } else {
        m_standardIndicator = indicator;
    }

    return header;
}

void PCBComponentsWidget::setupRecentSection() {
    m_recentHeader = createSectionHeader("RECENTLY USED", m_recentExpanded, [this]() {
        onToggleRecentSection();
    });

    // Clear button for Recent section
    QHBoxLayout* recentHeaderLayout = dynamic_cast<QHBoxLayout*>(m_recentHeader->layout());
    if (recentHeaderLayout) {
        recentHeaderLayout->takeAt(recentHeaderLayout->count() - 1); // remove stretch
        recentHeaderLayout->addStretch();
        
        QPushButton* clearBtn = new QPushButton("Clear", m_recentHeader);
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setStyleSheet("QPushButton { color: #71717a; font-size: 10px; border: none; background: transparent; padding: 0 8px; }"
                                "QPushButton:hover { color: #3b82f6; }");
        connect(clearBtn, &QPushButton::clicked, this, &PCBComponentsWidget::onClearRecent);
        recentHeaderLayout->addWidget(clearBtn);
    }

    m_recentContainer = new QWidget(this);
    m_recentLayout = new QVBoxLayout(m_recentContainer);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(0);

    QSettings settings;
    m_recentList = settings.value("Footprints/RecentPlacements").toStringList();
    updateRecentSection();
}

void PCBComponentsWidget::onClearRecent() {
    m_recentList.clear();
    QSettings settings;
    settings.setValue("Footprints/RecentPlacements", m_recentList);
    updateRecentSection();
}

void PCBComponentsWidget::onToggleRecentSection() {
    m_recentExpanded = !m_recentExpanded;
    m_recentContainer->setVisible(m_recentExpanded);
    updateSectionHeader(m_recentIndicator, m_recentExpanded);
}

void PCBComponentsWidget::onToggleStandardSection() {
    m_standardExpanded = !m_standardExpanded;
    m_componentList->setVisible(m_standardExpanded);
    updateSectionHeader(m_standardIndicator, m_standardExpanded);
}

void PCBComponentsWidget::updateSectionHeader(QPushButton* indicator, bool expanded) {
    if (indicator) {
        indicator->setIcon(QIcon(expanded ? ":/icons/chevron_down.svg" : ":/icons/chevron_right.svg"));
    }
}

void PCBComponentsWidget::addRecentFootprint(const QString& name) {
    if (name.isEmpty()) return;
    m_recentList.removeAll(name);
    m_recentList.prepend(name);
    m_recentList = m_recentList.mid(0, 10); // cap at 10 items

    QSettings settings;
    settings.setValue("Footprints/RecentPlacements", m_recentList);
    updateRecentSection();
}

void PCBComponentsWidget::updateRecentSection() {
    // Clear old layout items safely
    QLayoutItem* child;
    while ((child = m_recentLayout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }

    if (m_recentList.isEmpty()) {
        QLabel* emptyLabel = new QLabel("No footprints used recently.", m_recentContainer);
        emptyLabel->setStyleSheet("color: #555558; font-size: 11px; padding: 8px 15px; background: #1a1a1a;");
        m_recentLayout->addWidget(emptyLabel);
    } else {
        for (const QString& name : m_recentList) {
            QPushButton* btn = new QPushButton(m_recentContainer);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(26);
            btn->setStyleSheet(
                "QPushButton { text-align: left; background: #1a1a1a; border: none; color: #dcdcdc; font-size: 11px; padding-left: 24px; }"
                "QPushButton:hover { background-color: #2a2a2a; }"
            );

            // Icon positioning overlay
            QLabel* iconLabel = new QLabel(btn);
            iconLabel->setPixmap(QIcon(":/icons/component_file.svg").pixmap(12, 12));
            iconLabel->setGeometry(6, 7, 12, 12);
            iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

            btn->setText(name);
            connect(btn, &QPushButton::clicked, this, [this, name]() {
                updatePreview(name);
                emit footprintSelected(name);
            });
            m_recentLayout->addWidget(btn);
        }
    }
}

void PCBComponentsWidget::onToggleCompactMode() {
    m_compactMode = !m_compactMode;
    applyCompactMode(m_compactMode);
}

void PCBComponentsWidget::applyCompactMode(bool compact) {
    if (compact) {
        m_actionContainer->hide();
        m_previewView->parentWidget()->hide();
        m_componentList->setStyleSheet(
            "QTreeWidget {"
            "   background-color: #1a1a1a;"
            "   border: none;"
            "   color: #dcdcdc;"
            "   font-size: 11px;"
            "}"
            "QTreeWidget::item:hover { background-color: #2a2a2a; }"
            "QTreeWidget::item:selected { background-color: #094771; color: white; }"
        );
        m_compactToggle->setIcon(QIcon(":/icons/view_cards.svg"));
    } else {
        if (m_actionsVisible) m_actionContainer->show();
        m_previewView->parentWidget()->show();
        m_componentList->setStyleSheet(
            "QTreeWidget {"
            "   background-color: #1a1a1a;"
            "   border: none;"
            "   color: #dcdcdc;"
            "   font-size: 12px;"
            "}"
            "QTreeWidget::item:hover { background-color: #2a2a2a; }"
            "QTreeWidget::item:selected { background-color: #094771; color: white; }"
        );
        m_compactToggle->setIcon(QIcon(":/icons/view_rows.svg"));
    }
}

void PCBComponentsWidget::onToggleActionCards() {
    m_actionsVisible = !m_actionsVisible;
    m_actionContainer->setVisible(m_actionsVisible && !m_compactMode);
}

void PCBComponentsWidget::populate() {
    m_componentList->clear();
    
    QIcon libIcon(":/icons/folder_open.svg");
    QIcon catIcon(":/icons/folder_closed.svg");
    QIcon fpIcon(":/icons/component_file.svg");

    auto libraries = FootprintLibraryManager::instance().libraries();
    
    auto insertLib = [&](FootprintLibrary* lib, QColor color) {
        QTreeWidgetItem* libItem = new QTreeWidgetItem(m_componentList);
        libItem->setText(0, lib->name().toUpper() + (lib->isBuiltIn() ? " [BUILT-IN]" : ""));
        libItem->setIcon(0, libIcon);
        libItem->setData(0, Qt::UserRole + 2, "Library");
        libItem->setForeground(0, QBrush(color));
        
        QFont libFont = libItem->font(0);
        libFont.setBold(true);
        libFont.setPointSize(10);
        libItem->setFont(0, libFont);
        
        QMap<QString, QTreeWidgetItem*> categories;
        int insertCount = 0;

        for (const auto& fpName : lib->getFootprintNames()) {
            FootprintDefinition def = lib->getFootprint(fpName);
            QString catName = def.category();
            if (catName.isEmpty()) catName = "Uncategorized";

            // Apply category filter chip
            if (!m_activeCategory.isEmpty() && m_activeCategory != catName) {
                continue;
            }
            
            QTreeWidgetItem* catItem = nullptr;
            if (!categories.contains(catName)) {
                catItem = new QTreeWidgetItem(libItem);
                catItem->setText(0, catName);
                catItem->setIcon(0, catIcon);
                catItem->setData(0, Qt::UserRole + 2, "Category");
                categories[catName] = catItem;
            } else {
                catItem = categories[catName];
            }
            
            QTreeWidgetItem* item = new QTreeWidgetItem(catItem);
            item->setText(0, fpName);
            item->setData(0, Qt::UserRole, fpName);
            item->setData(0, Qt::UserRole + 1, lib->name());
            item->setData(0, Qt::UserRole + 2, "Footprint");
            item->setIcon(0, fpIcon);
            insertCount++;
        }

        if (insertCount > 0) {
            libItem->setExpanded(true);
            for (auto* cat : categories.values()) {
                cat->setExpanded(true);
            }
        } else {
            // Delete empty library item if it doesn't match category filter
            delete libItem;
        }
    };

    // Pass 1: User / Project Libraries
    for (auto* lib : libraries) {
        if (!lib->isBuiltIn()) {
            insertLib(lib, QColor("#fbbf24")); // Amber
        }
    }

    // Pass 2: Built-in Libraries
    for (auto* lib : libraries) {
        if (lib->isBuiltIn()) {
            insertLib(lib, QColor("#94a3b8")); // Grey
        }
    }
}

void PCBComponentsWidget::onSearchTextChanged(const QString &text) {
    if (text.isEmpty()) {
        for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
            QTreeWidgetItem* lib = m_componentList->topLevelItem(i);
            lib->setHidden(false);
            for (int j = 0; j < lib->childCount(); ++j) {
                QTreeWidgetItem* cat = lib->child(j);
                cat->setHidden(false);
                for (int k = 0; k < cat->childCount(); ++k) cat->child(k)->setHidden(false);
            }
        }
        return;
    }

    for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* lib = m_componentList->topLevelItem(i);
        bool libMatches = false;
        for (int j = 0; j < lib->childCount(); ++j) {
            QTreeWidgetItem* cat = lib->child(j);
            bool catMatches = false;
            for (int k = 0; k < cat->childCount(); ++k) {
                QTreeWidgetItem* item = cat->child(k);
                bool matches = item->text(0).contains(text, Qt::CaseInsensitive);
                item->setHidden(!matches);
                if (matches) catMatches = true;
            }
            cat->setHidden(!catMatches);
            if (catMatches) {
                cat->setExpanded(true);
                libMatches = true;
            }
        }
        lib->setHidden(!libMatches);
        if (libMatches) lib->setExpanded(true);
    }
}

void PCBComponentsWidget::onItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    if (item->data(0, Qt::UserRole + 2).toString() == "Footprint") {
        QString fpName = item->data(0, Qt::UserRole).toString();
        QString libName = item->data(0, Qt::UserRole + 1).toString();
        
        if (!fpName.isEmpty()) {
            updatePreview(fpName, libName);
            addRecentFootprint(fpName);
            emit footprintSelected(fpName);
        }
    }
}

void PCBComponentsWidget::updatePreview(const QString& fpName, const QString& libName) {
    FootprintDefinition def;
    if (!libName.isEmpty()) {
        auto* lib = FootprintLibraryManager::instance().findLibrary(libName);
        if (lib) def = lib->getFootprint(fpName);
    }
    
    if (!def.isValid()) {
        def = FootprintLibraryManager::instance().findFootprint(fpName);
    }
    
    if (def.isValid()) {
        m_previewView->setFootprint(def);
    } else {
        m_previewView->clear();
    }
}

void PCBComponentsWidget::onCreateFootprint() {
     emit footprintCreated("");
}

void PCBComponentsWidget::onOpenLibraryBrowser() {
    FootprintBrowserDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dialog.selectedFootprint();
        if (!fp.name().isEmpty()) {
            addRecentFootprint(fp.name());
            emit footprintSelected(fp.name());
        }
    }
}
