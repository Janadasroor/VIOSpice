#include "schematic_components_widget.h"
#include "model_browser_widget.h"
#include "theme_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/symbol_editor.h"
#include "../../symbols/models/symbol_definition.h"
#include "library_browser_dialog.h"
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
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

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        const QString query = filterRegularExpression().pattern().trimmed().toLower();
        if (query.isEmpty()) return true;

        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString name = sourceModel()->data(index, Qt::DisplayRole).toString().toLower();
        const QString category = sourceModel()->data(index, SymbolListModel::CategoryRole).toString().toLower();
        const QString library = sourceModel()->data(index, SymbolListModel::LibraryRole).toString().toLower();

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

// Replaced by SymbolPreviewWidget


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

    // ── Search bar ──────────────────────────────────────────────────────
    m_searchBox = new QLineEdit(m_symbolTab);
    m_searchBox->setPlaceholderText("Search components...");
    m_searchBox->setClearButtonEnabled(true);
    
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
    
    connect(m_searchBox, &QLineEdit::textChanged, this, &SchematicComponentsWidget::onSearchTextChanged);
    symbolLayout->addWidget(m_searchBox);

    // ── Action Cards Container ──────────────────────────────────────────
    QWidget* actionContainer = new QWidget(m_symbolTab);
    QVBoxLayout* actionLayout = new QVBoxLayout(actionContainer);
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

    symbolLayout->addWidget(actionContainer);

    // ── Section Header ──────────────────────────────────────────────────
    QLabel* listHeader = new QLabel("   STANDARD COMPONENTS", m_symbolTab);
    listHeader->setFixedHeight(28);
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    listHeader->setStyleSheet(QString(
        "background-color: %1;"
        "color: %2;"
        "font-size: 10px;"
        "font-weight: 700;"
        "border-bottom: 1px solid %3;"
    ).arg(headerBg, theme ? theme->textSecondary().name() : "#555", border));
    symbolLayout->addWidget(listHeader);

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

    populate();
}

void SchematicComponentsWidget::onApplyModelRequested(const SpiceModelInfo& info) {
    Q_EMIT modelAssignmentRequested(info.name);
}

bool SchematicComponentsWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_componentList && event->type() == QEvent::Leave) {
        if (m_previewPopup) m_previewPopup->hide();
    }
    return QWidget::eventFilter(watched, event);
}

SchematicComponentsWidget::~SchematicComponentsWidget() {}

// ─── focusSearch ───────────────────────────────────────────────────────────
void SchematicComponentsWidget::focusSearch() {
    m_searchBox->setFocus();
    m_searchBox->selectAll();
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
        {"AdcBridge", "Mixed-Signal"},
        {"DacBridge", "Mixed-Signal"},
        {"AnalogFunction", "Analog Functions"},
        {"MagneticCore", "Magnetics"},
        {"Lcouple", "Magnetics"},
        {"Sheet", "Hierarchy"},
        {"Hierarchical Port", "Hierarchy"}
    };

    builtIn.reserve(builtInTools.size());
    for (const auto& t : builtInTools) {
        builtIn.append({t.name, t.category, ""});
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

//    qInfo() << "SchematicComponentsWidget: simulatable symbol filter kept"
//            << simulatableLibrarySymbols << "of" << totalLibrarySymbols
//            << "library symbols.";

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
    m_proxyModel->setFilterFixedString(text);
    if (!text.isEmpty()) {
        m_componentList->expandAll();
    }
}

void SchematicComponentsWidget::onItemClicked(const QModelIndex& index) {
    if (!index.isValid()) return;

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    if (m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
        return;
    }

    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
    m_selectedSymbol = sym;
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

    m_previewPopup->setSymbol(sym);
    
    // Position to the right of the cursor
    QPoint pos = QCursor::pos() + QPoint(20, -10);
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
