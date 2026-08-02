/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "schematic_components_widget.h"
#include "model_browser_widget.h"
#include "theme_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/symbol_editor.h"
#include "../../symbols/models/symbol_definition.h"
#include "../items/avr_microcontroller_item.h"
#include "library_browser_dialog.h"
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QLibrary>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QHeaderView>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QSet>
#include <QCursor>
#include <QEvent>
#include <QTimer>
#include <QSettings>
#include <QMimeData>
#include <QDrag>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
int pinCount(const SymbolDefinition& sym) {
    int count = 0;
    for (const auto& prim : sym.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++count;
    }
    return count;
}

bool hasConcreteModelName(const SymbolDefinition& sym) {
    const QString modelName = sym.modelName().trimmed();
    if (modelName.isEmpty()) return false;
    const QString spiceDevice = sym.spiceModelName().trimmed();
    return spiceDevice.isEmpty() || modelName.compare(spiceDevice, Qt::CaseInsensitive) != 0;
}

bool isResolvableSubckt(const SymbolDefinition& sym) {
    if (!hasConcreteModelName(sym)) return false;
    const QString modelName = sym.modelName().trimmed();
    if (ModelLibraryManager::instance().findSubcircuit(modelName) != nullptr) return true;
    const QString modelPath = sym.modelPath().trimmed();
    return !modelPath.isEmpty() && QFileInfo::exists(modelPath);
}

bool isSimulatableLibrarySymbol(const SymbolDefinition& sym) {
    if (sym.name().trimmed().isEmpty()) return false;
    if (pinCount(sym) <= 0) return false;
    if (sym.isPowerSymbol()) return true;

    const QString spiceDevice = sym.spiceModelName().trimmed().toUpper();
    const QString modelPath = sym.modelPath().trimmed();
    const bool hasPath = !modelPath.isEmpty();
    const bool hasModel = hasConcreteModelName(sym);

    if (spiceDevice == "SUBCKT") {
        return hasPath || isResolvableSubckt(sym);
    }

    if (spiceDevice.isEmpty() && !hasPath && !hasModel) return false;

    // Generic subcircuit fallback: some symbols rely on modelName/modelPath without Sim.Device.
    if (spiceDevice.isEmpty() && (hasPath || hasModel)) return true;

    // Primitive/behavioral device classes that can be netlisted directly.
    static const QSet<QString> kPrimitiveDevices = {
        "R", "C", "L", "V", "I", "D", "Q", "M", "J",
        "E", "F", "G", "H", "B", "SW", "A"
    };
    if (kPrimitiveDevices.contains(spiceDevice)) return true;

    // If a device token is custom/non-standard, require explicit model linkage.
    return hasPath || hasModel;
}

bool isBundledKicadSymLibraryPath(const QString& p) {
    const QString np = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(p).absoluteFilePath()));
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList roots = {
        QDir(appDir).absoluteFilePath("viospicelib"),
        QDir(appDir).absoluteFilePath("../viospicelib")
    };
    for (const QString& rootRaw : roots) {
        QString root = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(rootRaw).absoluteFilePath()));
        if (!root.endsWith('/')) root += '/';
        if (np.startsWith(root) && np.contains("/symbols/kicad/")) return true;
    }
    return false;
}

bool isUserLibraryPath(const QString& p) {
    return !isBundledKicadSymLibraryPath(p);
}

/**
 * @brief Smarter filter for component search that handles synonyms and category matches.
 */
class ComponentFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit ComponentFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setRecursiveFilteringEnabled(true);
    }

    void setCategoryFilter(const QString& category) {
        m_categoryFilter = category;
        invalidateFilter();
    }

    QString categoryFilter() const { return m_categoryFilter; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        const QString query = filterRegularExpression().pattern().trimmed().toLower();
        if (query.isEmpty() && m_categoryFilter.isEmpty()) return true;

        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString name = sourceModel()->data(index, Qt::DisplayRole).toString().toLower();
        const QString category = sourceModel()->data(index, SymbolListModel::CategoryRole).toString().toLower();
        const QString library = sourceModel()->data(index, SymbolListModel::LibraryRole).toString().toLower();
        const bool isCategory = sourceModel()->data(index, SymbolListModel::IsCategoryRole).toBool();

        // Category filter: if active, only accept items in that category (or category headers themselves)
        if (!m_categoryFilter.isEmpty()) {
            if (isCategory) {
                return category == m_categoryFilter.toLower();
            }
            if (category != m_categoryFilter.toLower()) return false;
        }

        // 1. Check if this item matches
        if (matchSmarter(name, category, library, query)) return true;

        // 2. Check if any parent matches (if so, we accept all children)
        QModelIndex p = sourceParent;
        while (p.isValid()) {
            const QString pName = sourceModel()->data(p, Qt::DisplayRole).toString().toLower();
            const QString pLibrary = sourceModel()->data(p, SymbolListModel::LibraryRole).toString().toLower();
            if (pName.contains(query) || pLibrary.contains(query)) return true;
            p = p.parent();
        }

        return false;
    }

private:
    QString m_categoryFilter;

private:
    bool matchSmarter(const QString& name, const QString& category, const QString& library, const QString& query) const {
        if (name.contains(query) || category.contains(query) || library.contains(query)) return true;

        // Synonym: "instruments" matches "simulation", "probe", "meter", "scope", "analyzer"
        if (query == "instruments" || query == "instrument") {
            if (category == "simulation" || category == "instruments" ||
                name.contains("probe") || name.contains("meter") || 
                name.contains("oscilloscope") || name.contains("logic analyzer")) {
                return true;
            }
        }

        // Synonym: "meter" matches voltmeters/ammeters
        if (query == "meter") {
            if (name.contains("voltmeter") || name.contains("ammeter") || name.contains("wattmeter")) return true;
        }
        
        // Synonym: "pwr" matches power symbols
        if (query == "pwr" || query == "power") {
            if (category.contains("power") || name.contains("vcc") || name.contains("gnd")) return true;
        }

        // Synonym: MCU/microcontroller/avr/arduino/cosim match Co-Simulation
        if (query == "mcu" || query == "µc" || query == "microcontroller" ||
            query == "cosim" || query == "cosimulation" || query == "embedded") {
            if (category == "co-simulation") return true;
        }
        if (query == "avr") {
            if (category == "co-simulation" || name.contains("atmega")) return true;
        }
        if (query == "arduino") {
            if (category == "co-simulation" || name.contains("arduino")) return true;
        }

        // Substring common abbreviations
        if (query == "cap") return name.contains("capacitor");
        if (query == "res") return name.contains("resistor");
        if (query == "dio") return name.contains("diode");
        if (query == "tran" || query == "tnr") return name.contains("transistor");
        if (query == "src") return name.contains("source");

        return false;
    }
};

} // namespace

// ─── Section Header Factory ────────────────────────────────────────────────
QWidget* SchematicComponentsWidget::createSectionHeader(const QString& title, bool expanded, std::function<void()> toggleFn) {
    PCBTheme* theme = ThemeManager::theme();
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString fg = theme ? theme->textSecondary().name() : "#555";

    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    header->setStyleSheet(QString(
        "background-color: %1; border-bottom: 1px solid %2;"
    ).arg(headerBg, border));

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* indicator = new QPushButton(header);
    indicator->setIcon(QIcon(expanded ? ":/icons/chevron_down.svg" : ":/icons/chevron_right.svg"));
    indicator->setIconSize(QSize(12, 12));
    indicator->setFixedSize(24, 28);
    indicator->setCursor(Qt::PointingHandCursor);
    indicator->setStyleSheet(
        "QPushButton { border: none; background: transparent; padding: 4px; }"
        "QPushButton:hover { background: rgba(59,130,246,0.15); border-radius: 4px; }"
    );
    connect(indicator, &QPushButton::clicked, toggleFn);
    layout->addWidget(indicator);

    auto* label = new QLabel(title, header);
    label->setStyleSheet(QString(
        "color: %1; font-size: 10px; font-weight: 700; background: transparent;"
    ).arg(fg));
    layout->addWidget(label);
    layout->addStretch();

    // Store indicator for later update
    header->setProperty("indicator", QVariant::fromValue(reinterpret_cast<quintptr>(indicator)));

    return header;
}

void SchematicComponentsWidget::updateSectionHeader(QPushButton* indicator, bool expanded) {
    if (indicator) {
        indicator->setIcon(QIcon(expanded ? ":/icons/chevron_down.svg" : ":/icons/chevron_right.svg"));
    }
}

// ─── Constructor ────────────────────────────────────────────────────────────
SchematicComponentsWidget::SchematicComponentsWidget(QWidget *parent)
    : QWidget(parent)
{
    // 1. Initialize data models first
    m_symbolListModel = new SymbolListModel(this);
    m_proxyModel = new ComponentFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_symbolListModel);
    connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::librariesChanged,
            this, &SchematicComponentsWidget::populate);

    // Search debounce timer (300ms)
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        m_proxyModel->setFilterFixedString(m_pendingSearchText);
        if (!m_pendingSearchText.isEmpty()) {
            m_componentList->expandAll();
        } else {
            for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
                QModelIndex idx = m_proxyModel->index(i, 0);
                if (m_proxyModel->data(idx, SymbolListModel::LibraryRole).toString().isEmpty()) {
                    m_componentList->expand(idx);
                }
            }
        }
    });

    // 2. Setup Base UI
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString inputBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(QString(
        "QTabWidget::pane { border-top: 1px solid %1; background: %2; }"
        "QTabBar::tab { background: %2; color: %3; padding: 10px 15px; font-weight: 600; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QTabBar::tab:selected { background: %2; color: #3b82f6; border-bottom: 2px solid #3b82f6; }"
        "QTabBar::tab:hover:!selected { background: %4; }"
    ).arg(border, bg, fg, (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a"));

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(QString(
        "QProgressBar { background: transparent; border: none; }"
        "QProgressBar::chunk { background-color: #3b82f6; }"
    ));
    m_progressBar->hide();

    connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::progressUpdated,
            this, [this](const QString& status, int progress, int total) {
        m_progressBar->show();
        m_progressBar->setMaximum(total);
        m_progressBar->setValue(progress);
        m_progressBar->setToolTip(status);
    });
    connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::loadingFinished,
            m_progressBar, &QWidget::hide);

    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(m_tabs);

    // --- Tab 1: Symbols ---
    m_symbolTab = new QWidget();
    QVBoxLayout* symbolLayout = new QVBoxLayout(m_symbolTab);
    symbolLayout->setContentsMargins(0, 0, 0, 0);
    symbolLayout->setSpacing(0);

    // ── Search bar with compact toggle ───────────────────────────────────
    QWidget* searchBarContainer = new QWidget(m_symbolTab);
    QHBoxLayout* searchBarLayout = new QHBoxLayout(searchBarContainer);
    searchBarLayout->setContentsMargins(0, 0, 0, 0);
    searchBarLayout->setSpacing(0);

    m_searchBox = new QLineEdit(m_symbolTab);
    m_searchBox->setPlaceholderText("Search components...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    m_searchBox->setStyleSheet(QString(
        "QLineEdit {"
        "   background-color: %1;"
        "   border: none;"
        "   border-bottom: 1px solid %2;"
        "   border-radius: 0px;"
        "   padding: 10px 12px 10px 32px;"
        "   color: %3;"
        "   font-size: 13px;"
        "   background-image: url(:/icons/tool_search.svg);"
        "   background-repeat: no-repeat;"
        "   background-position: left 10px center;"
        "}"
        "QLineEdit:focus {"
        "   background-color: %4;"
        "}"
    ).arg(inputBg, border, fg, bg));
    
    searchBarLayout->addWidget(m_searchBox);

    // Compact mode toggle
    m_compactToggle = new QPushButton(m_symbolTab);
    m_compactToggle->setIcon(QIcon(":/icons/view_rows.svg"));
    m_compactToggle->setIconSize(QSize(18, 18));
    m_compactToggle->setFixedSize(36, 44);
    m_compactToggle->setCursor(Qt::PointingHandCursor);
    m_compactToggle->setToolTip("Toggle compact mode");
    {
        QPalette pal = m_compactToggle->palette();
        pal.setBrush(QPalette::Button, Qt::transparent);
        pal.setBrush(QPalette::ButtonText, QColor(fg));
        m_compactToggle->setPalette(pal);
    }
    m_compactToggle->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(128,128,128,0.15); border-radius: 4px; }"
    );
    connect(m_compactToggle, &QPushButton::clicked, this, &SchematicComponentsWidget::onToggleCompactMode);
    searchBarLayout->addWidget(m_compactToggle);

    // Hide action cards toggle
    m_actionCardsToggle = new QPushButton(m_symbolTab);
    m_actionCardsToggle->setIcon(QIcon(":/icons/eye.svg"));
    m_actionCardsToggle->setIconSize(QSize(18, 18));
    m_actionCardsToggle->setFixedSize(36, 44);
    m_actionCardsToggle->setCursor(Qt::PointingHandCursor);
    m_actionCardsToggle->setToolTip("Toggle action cards");
    {
        QPalette pal = m_actionCardsToggle->palette();
        pal.setBrush(QPalette::Button, Qt::transparent);
        pal.setBrush(QPalette::ButtonText, QColor(fg));
        m_actionCardsToggle->setPalette(pal);
    }
    m_actionCardsToggle->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(128,128,128,0.15); border-radius: 4px; }"
    );
    connect(m_actionCardsToggle, &QPushButton::clicked, this, &SchematicComponentsWidget::onToggleActionCards);
    searchBarLayout->addWidget(m_actionCardsToggle);

    connect(m_searchBox, &QLineEdit::textChanged, this, &SchematicComponentsWidget::onSearchTextChanged);
    symbolLayout->addWidget(searchBarContainer);

    // ── Filter Dropdown ─────────────────────────────────────────────────
    setupFilterChips();
    symbolLayout->addWidget(m_filterCombo);

    // ── Action Cards Container ──────────────────────────────────────────
    m_actionContainer = new QWidget(m_symbolTab);
    QVBoxLayout* actionLayout = new QVBoxLayout(m_actionContainer);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(0); 

    auto createActionCard = [this, actionLayout, theme, bg, fg, border](const QString& title, const QString& subTitle, const char* slot) {
        QPushButton* btn = new QPushButton(m_symbolTab);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(65);
        
        QVBoxLayout* btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(15, 0, 15, 0);
        btnLayout->setSpacing(2);
        
        QLabel* titleLabel = new QLabel(title);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel->setStyleSheet(QString("color: %1; font-weight: 600; font-size: 13px; border: none; background: transparent;").arg(fg));
        
        QLabel* descLabel = new QLabel(subTitle);
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        descLabel->setStyleSheet(QString("color: %1; font-size: 11px; border: none; background: transparent;").arg(theme ? theme->textSecondary().name() : "#888"));
        
        btnLayout->addWidget(titleLabel);
        btnLayout->addWidget(descLabel);
        
        QString hoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d2d";
        btn->setStyleSheet(QString(
            "QPushButton {"
            "   background-color: %1;"
            "   border: none;"
            "   border-bottom: 1px solid %2;"
            "   text-align: left;"
            "}"
            "QPushButton:hover {"
            "   background-color: %3;"
            "}"
        ).arg(bg, border, hoverBg));
        
        connect(btn, SIGNAL(clicked()), this, slot);
        actionLayout->addWidget(btn);
        return btn;
    };

    createActionCard("Create Custom Symbol", "Open symbol editor to draw new parts", SLOT(onCreateSymbol()));
    createActionCard("Browse Libraries", "Search millions of symbols and footprints", SLOT(onOpenLibraryBrowser()));

    symbolLayout->addWidget(m_actionContainer);

    // ── Recently Placed Section ─────────────────────────────────────────
    setupRecentSection();
    symbolLayout->addWidget(m_recentHeader);
    symbolLayout->addWidget(m_recentContainer);

    // ── Standard Components Section (collapsible) ────────────────────────
    m_standardHeader = createSectionHeader("STANDARD COMPONENTS", m_standardExpanded, [this]() {
        onToggleStandardSection();
    });
    symbolLayout->addWidget(m_standardHeader);

    // ── Component Tree ──────────────────────────────────────────────────
    m_componentList = new QTreeView(this);
    m_componentList->setFrameShape(QFrame::NoFrame);
    m_componentList->setModel(m_proxyModel);
    m_componentList->setHeaderHidden(true);
    m_componentList->setRootIsDecorated(true);
    m_componentList->setIndentation(16);
    m_componentList->setAnimated(true);
    m_componentList->setUniformRowHeights(true);
    m_componentList->setIconSize(QSize(16, 16));
    m_componentList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_componentList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_componentList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    QString selBg = theme ? theme->accentColor().name() : "#007acc";
    QString treeHoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a";
    
    m_componentList->setStyleSheet(QString(
        "QTreeView {"
        "   background-color: %1;"
        "   border: none;"
        "   color: %2;"
        "   outline: none;"
        "   padding: 2px;"
        "   font-size: 12px;"
        "}"
        "QTreeView::item {"
        "   padding: 4px 6px;"
        "   border-radius: 4px;"
        "   margin: 1px 4px;"
        "   border: none;"
        "}"
        "QTreeView::item:hover {"
        "   background-color: %3;"
        "}"
        "QTreeView::item:selected {"
        "   background-color: %4;"
        "   color: #ffffff;"
        "}"
        "QTreeView::branch {"
        "   background: transparent;"
        "}"
    ).arg(bg, fg, treeHoverBg, selBg));

    m_componentList->setMouseTracking(true);
    m_componentList->setDragEnabled(true);
    m_componentList->installEventFilter(this);
    connect(m_componentList, &QTreeView::entered, this, &SchematicComponentsWidget::onItemHovered);
    connect(m_componentList, &QTreeView::clicked, this, &SchematicComponentsWidget::onItemClicked);
    
    m_previewPopup = new SymbolPreviewWidget(this, Qt::ToolTip | Qt::FramelessWindowHint);
    m_previewPopup->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_previewPopup->setAttribute(Qt::WA_NoSystemBackground);
    m_previewPopup->setStaticMode(true);
    m_previewPopup->setFixedSize(220, 220);
    
    symbolLayout->addWidget(m_componentList);

    m_tabs->addTab(m_symbolTab, "Symbols");

    // --- Tab 2: Models ---
    m_modelTab = new ModelBrowserWidget(this);
    connect(m_modelTab, &ModelBrowserWidget::applyModelRequested, this, &SchematicComponentsWidget::onApplyModelRequested);
    m_tabs->addTab(m_modelTab, "SPICE Models");

    mainLayout->addWidget(m_tabs);

    // Load saved preferences
    QSettings settings;
    m_compactMode = settings.value("Components/CompactMode", false).toBool();
    m_actionsVisible = settings.value("Components/HideActionCards", true).toBool();
    m_actionContainer->setVisible(m_actionsVisible);
    if (m_compactMode) applyCompactMode(true);

    populate();
}

void SchematicComponentsWidget::onApplyModelRequested(const SpiceModelInfo& info) {
    Q_EMIT modelAssignmentRequested(info.name);
}

bool SchematicComponentsWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_componentList && event->type() == QEvent::Leave) {
        if (m_previewPopup) m_previewPopup->hide();
    }
    // Enter/Return on selected component → place it
    if (watched == m_componentList && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            QModelIndex idx = m_componentList->currentIndex();
            if (idx.isValid()) {
                QModelIndex sourceIndex = m_proxyModel->mapToSource(idx);
                if (!m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
                    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
                    if (!sym.name().isEmpty()) {
                        addRecentComponent(sym.name());
                        Q_EMIT toolSelected(sym.name());
                        m_previewPopup->hide();
                    }
                }
            }
            return true; // consumed
        }
    }
    return QWidget::eventFilter(watched, event);
}

SchematicComponentsWidget::~SchematicComponentsWidget() {}

// ─── Filter Dropdown ─────────────────────────────────────────────────────────
void SchematicComponentsWidget::setupFilterChips() {
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString inputBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";

    m_filterCombo = new QComboBox(this);
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->addItems({"All", "Passives", "Semiconductors", "Logic", "Power", "MCU", "Instruments", "Simulation", "XSPICE"});
    m_filterCombo->setStyleSheet(QString(
        "QComboBox {"
        "   background-color: %1;"
        "   border: none;"
        "   border-bottom: 1px solid %2;"
        "   border-radius: 0px;"
        "   padding: 4px 10px;"
        "   color: %3;"
        "   font-size: 11px;"
        "}"
        "QComboBox::drop-down {"
        "   border: none;"
        "   width: 20px;"
        "}"
        "QComboBox::down-arrow {"
        "   image: none;"
        "   border-left: 4px solid transparent;"
        "   border-right: 4px solid transparent;"
        "   border-top: 5px solid %3;"
        "   margin-right: 8px;"
        "}"
        "QComboBox QAbstractItemView {"
        "   background-color: %1;"
        "   color: %3;"
        "   border: 1px solid %2;"
        "   selection-background-color: #3b82f6;"
        "   selection-color: #ffffff;"
        "   font-size: 11px;"
        "}"
    ).arg(inputBg, border, fg));

    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SchematicComponentsWidget::onFilterChipClicked);
}

void SchematicComponentsWidget::onFilterChipClicked(int chipIndex) {
    if (chipIndex == m_activeChipIndex) return;
    m_activeChipIndex = chipIndex;

    // Map index to category name (empty = show all)
    static const QStringList categories = {
        "", "Passives", "Semiconductors", "Logic", "Power Symbols",
        "Co-Simulation", "Instruments", "Simulation", "XSPICE"
    };

    QString category = (chipIndex < categories.size()) ? categories[chipIndex] : "";
    auto* proxy = static_cast<ComponentFilterProxyModel*>(m_proxyModel);
    if (proxy) proxy->setCategoryFilter(category);

    // Clear search when switching filter
    if (!category.isEmpty()) {
        m_searchBox->clear();
    }

    // Expand all visible categories
    m_componentList->expandAll();
}

// ─── Recently Placed Section ────────────────────────────────────────────────
void SchematicComponentsWidget::setupRecentSection() {
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    QString hoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d2d";

    // Collapsible header with clear button
    m_recentHeader = new QWidget(this);
    m_recentHeader->setFixedHeight(28);
    m_recentHeader->setStyleSheet(QString(
        "background-color: %1; border-bottom: 1px solid %2;"
    ).arg(headerBg, border));

    auto* headerLayout = new QHBoxLayout(m_recentHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    // Triangle indicator
    m_recentIndicator = new QPushButton(m_recentHeader);
    m_recentIndicator->setIcon(QIcon(m_recentExpanded ? ":/icons/chevron_down.svg" : ":/icons/chevron_right.svg"));
    m_recentIndicator->setIconSize(QSize(12, 12));
    m_recentIndicator->setFixedSize(24, 28);
    m_recentIndicator->setCursor(Qt::PointingHandCursor);
    m_recentIndicator->setStyleSheet(
        "QPushButton { border: none; background: transparent; padding: 4px; }"
        "QPushButton:hover { background: rgba(59,130,246,0.15); border-radius: 4px; }"
    );
    connect(m_recentIndicator, &QPushButton::clicked, this, &SchematicComponentsWidget::onToggleRecentSection);
    headerLayout->addWidget(m_recentIndicator);

    auto* headerLabel = new QLabel("RECENT", m_recentHeader);
    headerLabel->setStyleSheet(QString(
        "color: %1; font-size: 10px; font-weight: 700; background: transparent;"
    ).arg(theme ? theme->textSecondary().name() : "#555"));
    headerLayout->addWidget(headerLabel);
    headerLayout->addStretch();

    auto* clearBtn = new QPushButton("Clear", m_recentHeader);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(QString(
        "QPushButton { color: %1; font-size: 10px; border: none; background: transparent; padding: 0 8px; }"
        "QPushButton:hover { color: #3b82f6; }"
    ).arg(theme ? theme->textSecondary().name() : "#555"));
    connect(clearBtn, &QPushButton::clicked, this, &SchematicComponentsWidget::onClearRecent);
    headerLayout->addWidget(clearBtn);

    // Recent items container
    m_recentContainer = new QWidget(this);
    m_recentLayout = new QVBoxLayout(m_recentContainer);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(0);

    // Load from settings
    QSettings settings;
    m_recentList = settings.value("Components/RecentPlacements").toStringList();
    m_recentList = m_recentList.mid(0, 15); // Enforce max 15

    updateRecentSection();
}

void SchematicComponentsWidget::updateRecentSection() {
    // Clear existing items
    QLayoutItem* item;
    while ((item = m_recentLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString hoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d2d";

    bool hasRecent = !m_recentList.isEmpty();
    m_recentHeader->setVisible(hasRecent);
    m_recentContainer->setVisible(hasRecent && m_recentExpanded);

    for (const QString& name : m_recentList) {
        auto* btn = new QPushButton(name, m_recentContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(m_compactMode ? 22 : 28);
        btn->setStyleSheet(QString(
            "QPushButton {"
            "   background-color: %1; border: none; border-bottom: 1px solid %2;"
            "   text-align: left; padding: 0 12px; font-size: %4; color: %3;"
            "}"
            "QPushButton:hover { background-color: %5; }"
        ).arg(bg, name.isEmpty() ? "transparent" : "transparent", fg, m_compactMode ? "10px" : "11px", hoverBg));
        connect(btn, &QPushButton::clicked, this, [this, name]() {
            addRecentComponent(name);
            Q_EMIT toolSelected(name);
        });
        m_recentLayout->addWidget(btn);
    }
}

void SchematicComponentsWidget::addRecentComponent(const QString& name) {
    if (name.isEmpty()) return;
    m_recentList.removeAll(name);
    m_recentList.prepend(name);
    if (m_recentList.size() > 15) m_recentList = m_recentList.mid(0, 15);

    QSettings settings;
    settings.setValue("Components/RecentPlacements", m_recentList);

    updateRecentSection();
}

void SchematicComponentsWidget::onClearRecent() {
    m_recentList.clear();
    QSettings settings;
    settings.remove("Components/RecentPlacements");
    updateRecentSection();
}

// ─── Section Collapse/Expand ─────────────────────────────────────────────────
void SchematicComponentsWidget::onToggleRecentSection() {
    m_recentExpanded = !m_recentExpanded;
    updateSectionHeader(m_recentIndicator, m_recentExpanded);
    m_recentContainer->setVisible(m_recentExpanded && !m_recentList.isEmpty());
}

void SchematicComponentsWidget::onToggleStandardSection() {
    m_standardExpanded = !m_standardExpanded;
    auto* indicator = reinterpret_cast<QPushButton*>(m_standardHeader->property("indicator").value<quintptr>());
    updateSectionHeader(indicator, m_standardExpanded);
    m_componentList->setVisible(m_standardExpanded);
}

// ─── Compact Mode ────────────────────────────────────────────────────────────
void SchematicComponentsWidget::onToggleCompactMode() {
    m_compactMode = !m_compactMode;
    QSettings settings;
    settings.setValue("Components/CompactMode", m_compactMode);
    applyCompactMode(m_compactMode);
}

void SchematicComponentsWidget::applyCompactMode(bool compact) {
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString treeHoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a";
    QString selBg = theme ? theme->accentColor().name() : "#007acc";

    if (compact) {
        m_componentList->setIconSize(QSize(12, 12));
        m_componentList->setStyleSheet(QString(
            "QTreeView { background-color: %1; border: none; color: %2; outline: none; padding: 0px; font-size: 10px; }"
            "QTreeView::item { padding: 1px 4px; border-radius: 2px; margin: 0px 2px; border: none; }"
            "QTreeView::item:hover { background-color: %3; }"
            "QTreeView::item:selected { background-color: %4; color: #ffffff; }"
            "QTreeView::branch { background: transparent; }"
        ).arg(bg, fg, treeHoverBg, selBg));
    } else {
        m_componentList->setIconSize(QSize(16, 16));
        m_componentList->setStyleSheet(QString(
            "QTreeView { background-color: %1; border: none; color: %2; outline: none; padding: 2px; font-size: 12px; }"
            "QTreeView::item { padding: 4px 6px; border-radius: 4px; margin: 1px 4px; border: none; }"
            "QTreeView::item:hover { background-color: %3; }"
            "QTreeView::item:selected { background-color: %4; color: #ffffff; }"
            "QTreeView::branch { background: transparent; }"
        ).arg(bg, fg, treeHoverBg, selBg));
    }
    updateRecentSection();
}

// ─── Hide Action Cards ───────────────────────────────────────────────────────
void SchematicComponentsWidget::onToggleActionCards() {
    m_actionsVisible = !m_actionsVisible;
    QSettings settings;
    settings.setValue("Components/HideActionCards", m_actionsVisible);
    m_actionContainer->setVisible(m_actionsVisible);
    m_actionCardsToggle->setIcon(QIcon(m_actionsVisible ? ":/icons/eye.svg" : ":/icons/eye_off.svg"));
}

// ─── focusSearch ───────────────────────────────────────────────────────────
void SchematicComponentsWidget::focusSearch() {
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

void SchematicComponentsWidget::setUsedModels(const QSet<QString>& used) {
    if (m_modelTab) m_modelTab->setUsedModels(used);
}

// ─── Populate ───────────────────────────────────────────────────────────────
void SchematicComponentsWidget::populate() {
    // Only reload if we have no libraries
    if (SymbolLibraryManager::instance().libraries().isEmpty()) {
        SymbolLibraryManager::instance().reloadUserLibraries(true);
    }

    QVector<SymbolListModel::SymbolMetadata> builtIn;
    struct StdTool { QString name; QString category; };
    QList<StdTool> builtInTools = {
        {"Resistor", "Passives"}, {"Resistor (US)", "Passives"}, {"Resistor (IEC)", "Passives"},
        {"Capacitor", "Passives"}, {"Capacitor (Non-Polar)", "Passives"}, {"Capacitor (Polarized)", "Passives"},
        {"Inductor", "Passives"}, {"Transformer", "Passives"},
        {"Diode", "Semiconductors"}, {"Diode_Zener", "Semiconductors"}, {"Diode_Schottky", "Semiconductors"},
        {"NPN Transistor", "Semiconductors"}, {"PNP Transistor", "Semiconductors"},
        {"NMOS Transistor", "Semiconductors"}, {"PMOS Transistor", "Semiconductors"},
        {"IC", "Integrated Circuits"}, {"RAM", "Integrated Circuits"}, {"OpAmp", "Integrated Circuits"},
        {"Switch", "Interactive"}, {"Voltage Controlled Switch", "Interactive"}, {"Push Button", "Interactive"}, {"LED", "Interactive"},
        {"Blinking LED", "Interactive"},
        {"7-Segment Display", "Displays"},
        {"Dual 7-Segment Display", "Displays"},
        {"14-Segment Display", "Displays"},
        {"16-Segment Display", "Displays"},
        {"AND Gate", "Logic"}, {"OR Gate", "Logic"}, {"XOR Gate", "Logic"},
        {"NAND Gate", "Logic"}, {"NOR Gate", "Logic"}, {"XNOR Gate", "Logic"},
        {"Buffer", "Logic"}, {"Inverter", "Logic"},
        {"D Flip-Flop", "Logic"}, {"JK Flip-Flop", "Logic"}, {"T Flip-Flop", "Logic"},
        {"SR Flip-Flop", "Logic"}, {"D Latch", "Logic"}, {"SR Latch", "Logic"},
        {"Counter", "Logic"}, {"Schmitt Trigger", "Logic"}, {"Tri-State Buffer", "Logic"},
        {"RAM", "Logic"},
        {"Logic Probe", "Logic"},
        {"Logic Toggle", "Logic"},
        {"VoltageRegulator", "Power Symbols"},
        {"GND", "Power Symbols"}, {"VCC", "Power Symbols"}, {"VDD", "Power Symbols"},
        {"VSS", "Power Symbols"}, {"VBAT", "Power Symbols"}, {"3.3V", "Power Symbols"},
        {"5V", "Power Symbols"}, {"12V", "Power Symbols"},
        {"Probe", "Instruments"},
        {"Voltage Probe", "Instruments"},
        {"Current Probe", "Instruments"},
        {"Power Probe", "Instruments"},
        {"Voltmeter (DC)", "Instruments"},
        {"Voltmeter (AC)", "Instruments"},
        {"Ammeter (DC)", "Instruments"},
        {"Ammeter (AC)", "Instruments"},
        {"Wattmeter", "Instruments"},
        {"Power Meter", "Instruments"},
        {"Frequency Counter", "Instruments"},
        {"Logic Analyzer", "Instruments"},
        {"Virtual Terminal", "Instruments"},
        {"Oscilloscope Instrument", "Instruments"},
        {"Flux Measurement Probe", "Simulation"},
        {"Tuning Slider", "Tuning"},
        {"Rotary Knob", "Tuning"},
        {"Joystick", "Tuning"},
        {"Smart Signal Block", "Simulation"},
        {"Voltage Source (DC)", "Simulation"},
        {"Voltage Source (Sine)", "Simulation"},
        {"Voltage Source (Pulse)", "Simulation"},
        {"BV", "Simulation"},
        {"BI", "Simulation"},
        {"VCVS (E)", "Simulation"},
        {"VCCS (G)", "Simulation"},
        {"CCCS (F)", "Simulation"},
        {"CCVS (H)", "Simulation"},
        // XSPICE Behavioral Blocks
        {"XspiceBlock", "XSPICE"},
        {"SystemVerilogBlock", "XSPICE"},
        {"AvrMicrocontroller", "Co-Simulation"},
        {"AdcBridge", "Mixed-Signal"},
        {"DacBridge", "Mixed-Signal"},
        {"AnalogFunction", "Analog Functions"},
        {"MagneticCore", "Magnetics"},
        {"Lcouple", "Magnetics"},
        {"Sheet", "Hierarchy"},
        {"Hierarchical Port", "Hierarchy"}
    };

    builtIn.reserve(builtInTools.size() + 400);
    for (const auto& t : builtInTools) {
        builtIn.append({t.name, t.category, ""});
    }

    // Add all VioAVR device names to Co-Simulation category
    QLibrary vioavrLib("avr_cosim");
    if (!vioavrLib.load()) {
#ifdef Q_OS_WIN
        QString libName = "avr_cosim.dll";
#elif defined(Q_OS_MACOS)
        QString libName = "libavr_cosim.dylib";
#else
        QString libName = "libavr_cosim.so";
#endif
        vioavrLib.setFileName(QCoreApplication::applicationDirPath() + "/" + libName);
        vioavrLib.load();
    }
    if (vioavrLib.isLoaded()) {
        auto countFn = reinterpret_cast<int(*)()>(vioavrLib.resolve("vioavr_device_count"));
        auto nameFn = reinterpret_cast<const char*(*)(int)>(vioavrLib.resolve("vioavr_device_name"));
        if (countFn && nameFn) {
            int count = countFn();
            for (int i = 0; i < count; ++i) {
                const char* name = nameFn(i);
                if (name) builtIn.append({QString::fromLatin1(name), "Co-Simulation", ""});
            }
        }
    }

    QMap<QString, QStringList> libs;
    int totalLibrarySymbols = 0;
    int simulatableLibrarySymbols = 0;
    auto libraries = SymbolLibraryManager::instance().libraries();
    for (auto* lib : libraries) {
        // KiCad merged libraries are loaded as stubs and resolving every symbol here
        // blocks editor startup. Keep launch responsive by skipping stub-only libs
        // in this eager population path.
        if (lib &&
            lib->path().endsWith(".kicad_sym", Qt::CaseInsensitive) &&
            isBundledKicadSymLibraryPath(lib->path())) {
            continue;
        }

        QStringList accepted;
        const QList<SymbolLibrary::SymbolInfo> infos = lib->symbolInfos();
        totalLibrarySymbols += infos.size();

        for (const SymbolLibrary::SymbolInfo& info : infos) {
            if (info.name.trimmed().isEmpty()) continue;
            accepted.append(info.name);
        }

        accepted.sort(Qt::CaseInsensitive);
        if (!accepted.isEmpty()) {
            libs[lib->name()] = accepted;
            simulatableLibrarySymbols += accepted.size();
        }
    }

    m_symbolListModel->setSymbols(builtIn, libs);
    
    // Expand top-level built-in categories by default
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
        QModelIndex idx = m_proxyModel->index(i, 0);
        if (m_proxyModel->data(idx, SymbolListModel::LibraryRole).toString().isEmpty()) {
            m_componentList->expand(idx);
        }
    }
}

// ─── Slots ──────────────────────────────────────────────────────────────────
void SchematicComponentsWidget::onSearchTextChanged(const QString &text) {
    m_pendingSearchText = text;
    m_searchDebounceTimer->start();
}

void SchematicComponentsWidget::onItemClicked(const QModelIndex& index) {
    if (!index.isValid()) return;

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    if (m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
        return;
    }

    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
    m_selectedSymbol = sym;
    addRecentComponent(sym.name());
    Q_EMIT toolSelected(sym.name());
    
    m_previewPopup->hide();
}

void SchematicComponentsWidget::onItemHovered(const QModelIndex& index) {
    if (!index.isValid()) {
        m_previewPopup->hide();
        return;
    }

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    if (m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
        m_previewPopup->hide();
        return;
    }

    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
    if (sym.name().isEmpty()) {
        m_previewPopup->hide();
        return;
    }

    // Check if this is an MCU name — show AVR chip preview
    const auto& mcuDb = AvrMicrocontrollerItem::mcuDatabase();
    if (mcuDb.contains(sym.name())) {
        m_previewPopup->setAvrPreview(sym.name());
    } else {
        m_previewPopup->setSymbol(sym);
    }

    // Position to the right of the cursor, clamped to screen bounds
    QPoint pos = QCursor::pos() + QPoint(20, -10);
    QRect screenGeometry = QApplication::primaryScreen()->availableGeometry();
    pos.setX(qMin(pos.x(), screenGeometry.right() - m_previewPopup->width() - 5));
    pos.setY(qMin(pos.y(), screenGeometry.bottom() - m_previewPopup->height() - 5));
    pos.setX(qMax(pos.x(), screenGeometry.left() + 5));
    pos.setY(qMax(pos.y(), screenGeometry.top() + 5));
    m_previewPopup->move(pos);
    m_previewPopup->show();
}

void SchematicComponentsWidget::onCreateSymbol() {
    auto* editor = new SymbolEditor(); // No parent for top-level window behavior
    editor->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(editor, &SymbolEditor::symbolSaved, this, [this](const SymbolDefinition& symbol) {
        populate();
        Q_EMIT symbolCreated(symbol.name());
    });

    connect(editor, &SymbolEditor::placeInSchematicRequested, this, [this](const SymbolDefinition& symbol) {
        Q_EMIT symbolPlacementRequested(symbol);
    });

    editor->show();
}
void SchematicComponentsWidget::onOpenLibraryBrowser() {
    LibraryBrowserDialog dialog(this);
    connect(&dialog, &LibraryBrowserDialog::symbolPlaced, this, [this](const SymbolDefinition& symbol) {
        Q_EMIT toolSelected(symbol.name());
    });
    dialog.exec();
}
