#include "mos_properties_dialog.h"
#include "mos_model_picker_dialog.h"
#include "../../pcb/dialogs/footprint_browser_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QScrollArea>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QCoreApplication>
#include <QDir>

const QVector<MosPropertiesDialog::LevelInfo>& MosPropertiesDialog::knownLevels() {
    static const QVector<LevelInfo> levels = {
        {"None",     ""},
        {"MOS1",     "mos1.json"},
        {"MOS2",     "mos2.json"},
        {"MOS3",     "mos3.json"},
        {"MOS6",     ""},
        {"MOS9",     ""},
        {"BSIM1",    ""},
        {"BSIM2",    ""},
        {"BSIM3",    "bsim3.json"},
        {"BSIM4",    "bsim4.json"},
        {"BSIMSOI",  "bsimsoi.json"},
        {"BSIM3SOI", ""},
        {"HISIM2",   "hisim2.json"},
        {"HISIM_HV", "hisim_hv.json"},
        {"VDMOS",    ""},
        {"SOI3",     ""},
    };
    return levels;
}

MosPropertiesDialog::MosPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("MOSFET Properties - %1").arg(item ? item->reference() : "M?"));
    setModal(true);
    setMinimumWidth(580);
    resize(620, 700);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void MosPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // === Top form: Model Name, Type, Level ===
    auto* topForm = new QFormLayout();
    topForm->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. 2N7000 / BS250");
    topForm->addRow("Model Name:", m_modelNameEdit);

    {
        QStringList mosModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            QString t = info.type.toUpper();
            if (t == "NMOS" || t == "PMOS" || t == "VDMOS" || t == "NMF" || t == "PMF" ||
                t == "BSIM4" || t == "BSIM3" || t == "BSIMSOI" || t == "BSIM3SOI" ||
                t == "HISIM2" || t == "HISIM_HV" || t == "MOS1" || t == "MOS2" ||
                t == "MOS3" || t == "MOS6" || t == "MOS9" || t == "BSIM1" ||
                t == "BSIM2" || t == "SOI3") {
                mosModels.append(info.name);
            }
        }
        mosModels.sort(Qt::CaseInsensitive);
        mosModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(mosModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &MosPropertiesDialog::fillFromModel);
        connect(m_modelNameEdit, &QLineEdit::textEdited, this, [this]() {
            m_pickedModelName.clear();
        });
    }

    auto* typeLevelLayout = new QHBoxLayout();

    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("NMOS");
    m_typeCombo->addItem("PMOS");
    typeLevelLayout->addWidget(new QLabel("Type:"));
    typeLevelLayout->addWidget(m_typeCombo);

    m_levelCombo = new QComboBox();
    for (const auto& lvl : knownLevels()) {
        m_levelCombo->addItem(lvl.name);
    }
    typeLevelLayout->addWidget(new QLabel("Level:"));
    typeLevelLayout->addWidget(m_levelCombo);
    typeLevelLayout->addStretch();
    topForm->addRow("", typeLevelLayout);

    auto* pickLayout = new QHBoxLayout();
    m_pickModelButton = new QPushButton(isPmos() ? "Pick PMOS Model" : "Pick NMOS Model");
    m_pickModelButton->setFixedHeight(26);
    connect(m_pickModelButton, &QPushButton::clicked, this, [this]() {
        MosModelPickerDialog dlg(isPmosSelected(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
            fillFromModel(dlg.selectedModel());
        }
    });
    pickLayout->addWidget(m_pickModelButton);
    topForm->addRow("", pickLayout);

    mainLayout->addLayout(topForm);

    // === Scrollable parameter area ===
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    auto* scrollContent = new QWidget();
    m_paramLayout = new QVBoxLayout(scrollContent);
    m_paramLayout->setContentsMargins(0, 0, 0, 0);
    m_scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(m_scrollArea, 1);

    // === Raw params text area ===
    mainLayout->addWidget(new QLabel("Raw Parameters (key=val, one per line):"));
    m_rawParamsEdit = new QTextEdit();
    m_rawParamsEdit->setPlaceholderText("e.g.\nTOX=5e-9\nNCH=2.5e17\nXJ=1.5e-7");
    m_rawParamsEdit->setMaximumHeight(80);
    m_rawParamsEdit->setStyleSheet("font-family: 'Courier New'; font-size: 10pt;");
    mainLayout->addWidget(m_rawParamsEdit);

    // === Footprint ===
    auto* fpRow = new QHBoxLayout();
    m_footprintEdit = new QLineEdit();
    m_footprintEdit->setPlaceholderText("Select a footprint");
    m_footprintEdit->setReadOnly(true);
    auto* fpBtn = new QPushButton("Pick Footprint");
    connect(fpBtn, &QPushButton::clicked, this, &MosPropertiesDialog::pickFootprint);
    fpRow->addWidget(m_footprintEdit, 1);
    fpRow->addWidget(fpBtn);
    mainLayout->addLayout(fpRow);

    // === SPICE Preview ===
    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    // === Buttons ===
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &MosPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // === Connections ===
    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
    connect(m_modelNameEdit, &QLineEdit::editingFinished, this, &MosPropertiesDialog::autoMatchModel);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        if (m_pickModelButton) {
            m_pickModelButton->setText(isPmosSelected() ? "Pick PMOS Model" : "Pick NMOS Model");
        }
        updateCommandPreview();
    });
    connect(m_levelCombo, &QComboBox::currentIndexChanged, this, &MosPropertiesDialog::onLevelChanged);
    connect(m_rawParamsEdit, &QTextEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
}

bool MosPropertiesDialog::isPmos() const {
    if (!m_item) return false;
    const QString t = m_item->itemTypeName().trimmed().toLower();
    return t == "transistor_pmos" ||
           t == "pmos" ||
           t == "pmos4" ||
           m_item->referencePrefix().compare("MP", Qt::CaseInsensitive) == 0;
}

bool MosPropertiesDialog::isPmosSelected() const {
    if (m_typeCombo) {
        return m_typeCombo->currentText().compare("PMOS", Qt::CaseInsensitive) == 0;
    }
    return isPmos();
}

void MosPropertiesDialog::addEssentialDefaults(MosModelDef& def, const QString& levelName, bool pmos) {
    if (levelName.startsWith("HISIM")) {
        MosParamCategory core;
        core.name = "Core Parameters";
        core.params = {{"Vth0", pmos ? "-0.4" : "0.4", "V", "Threshold voltage"},
                       {"Vmax", "2e6", "m/s", "Saturation velocity"},
                       {"Tox", "5n", "m", "Oxide thickness"},
                       {"Xld", "0", "m", "Length offset (XLD=0 fixes negative length error)"}};
        def.categories.append(core);

        MosParamCategory geo;
        geo.name = "Geometry";
        geo.params = {{"L", "2u", "m", "Channel length"},
                      {"W", "10u", "m", "Channel width"}};
        def.categories.append(geo);
        return;
    }

    MosParamCategory dc;
    dc.name = "DC Parameters";
    dc.params = {{"Vth0", pmos ? "-0.4" : "0.4", "V", "Threshold voltage"},
                 {"U0", "0.05", "m^2/V-s", "Low-field mobility"},
                 {"Tox", levelName.contains("3") ? "10n" : "5n", "m", "Oxide thickness"}};
    def.categories.append(dc);

    MosParamCategory hf;
    hf.name = "RF / Stability";
    hf.params = {{"Cgso", "200p", "F/m", "Gate-source overlap"},
                 {"Cgdo", "200p", "F/m", "Gate-drain overlap"},
                 {"Rg", "2.0", "ohm", "Gate resistance"},
                 {"Rb", "1.0", "ohm", "Substrate resistance"}};
    def.categories.append(hf);

    MosParamCategory geo;
    geo.name = "Geometry";
    geo.params = {{"L", "0.18u", "m", "Channel length"},
                  {"W", "2u", "m", "Channel width"}};
    def.categories.append(geo);
}

void MosPropertiesDialog::loadModelDef(const QString& levelName) {
    m_currentDef = MosModelDef();

    // First try to load from the model_params directory
    QString searchPath;
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/../core/simulation/model_params/",
        QCoreApplication::applicationDirPath() + "/../../core/simulation/model_params/",
        QDir::currentPath() + "/core/simulation/model_params/",
    };

    for (const auto& lvl : knownLevels()) {
        if (lvl.name == levelName) {
            searchPath = lvl.jsonFile;
            break;
        }
    }

    if (searchPath.isEmpty()) return;

    // Try to find the JSON file
    QString jsonPath;
    for (const auto& base : searchPaths) {
        QString candidate = base + searchPath;
        if (QFile::exists(candidate)) {
            jsonPath = candidate;
            break;
        }
    }

    // Also check alongside the executable
    if (jsonPath.isEmpty()) {
        QString candidate = QCoreApplication::applicationDirPath() + "/" + searchPath;
        if (QFile::exists(candidate)) {
            jsonPath = candidate;
        }
    }

    if (jsonPath.isEmpty()) {
        m_currentDef.model = levelName;
        const bool pmos = isPmosSelected();

        auto addCat = [&](const QString& name, const QVector<MosParamDef>& params) {
            MosParamCategory cat;
            cat.name = name;
            cat.params = params;
            m_currentDef.categories.append(cat);
        };

        if (levelName.startsWith("MOS") || levelName == "None") {
            addCat("DC", {{"Vto", pmos ? "-2" : "2", "V", "Threshold voltage"},
                          {"Kp", "100u", "A/V^2", "Transconductance"},
                          {"Lambda", "0.02", "V^-1", "Channel modulation"},
                          {"Rd", "0", "ohm", "Drain resistance"},
                          {"Rs", "0", "ohm", "Source resistance"}});
            addCat("Capacitance", {{"Cgso", "50p", "F/m", "Gate-source overlap"},
                                   {"Cgdo", "50p", "F/m", "Gate-drain overlap"},
                                   {"Cj", "0", "F/m^2", "Junction capacitance"}});
        } else if (levelName.startsWith("BSIM")) {
            addEssentialDefaults(m_currentDef, levelName, pmos);
        } else if (levelName == "VDMOS") {
            addCat("DC", {{"Vto", pmos ? "-2" : "2", "V", "Threshold voltage"},
                          {"Kp", "1.0", "A/V^2", "Transconductance"},
                          {"Rd", "0.1", "ohm", "Drain resistance"},
                          {"Rs", "0.1", "ohm", "Source resistance"},
                          {"Rb", "0.01", "ohm", "Base resistance"}});
            addCat("Capacitance", {{"Cgdmax", "500p", "F", "Max gate-drain cap"},
                                   {"Cgdmin", "50p", "F", "Min gate-drain cap"},
                                   {"Cgs", "1n", "F", "Gate-source cap"}});
        } else {
            // Generic fallback for SOI3, HISIM, etc.
            addCat("Basic Parameters", {{"Vth0", pmos ? "-0.4" : "0.4", "V", "Threshold voltage"},
                                        {"U0", "0.05", "m^2/V-s", "Low-field mobility"},
                                        {"Tox", "5n", "m", "Oxide thickness"},
                                        {"L", "1u", "m", "Channel length"},
                                        {"W", "1u", "m", "Channel width"}});
        }
        return;
    }

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_currentDef.model = root["model"].toString();
    m_currentDef.description = root["description"].toString();
    m_currentDef.level = root["level"].toInt(1);
    m_currentDef.spiceType = root["spiceType"].toString("NMOS");
    m_currentDef.spiceLevelParam = root["spiceLevelParam"].toString();

    QJsonArray cats = root["categories"].toArray();
    for (const auto& catVal : cats) {
        QJsonObject catObj = catVal.toObject();
        MosParamCategory cat;
        cat.name = catObj["name"].toString();
        QJsonArray params = catObj["params"].toArray();
        for (const auto& pVal : params) {
            QJsonObject pObj = pVal.toObject();
            MosParamDef def;
            def.name = pObj["name"].toString();
            def.defaultVal = pObj["default"].toString();
            def.unit = pObj["unit"].toString();
            def.desc = pObj["desc"].toString();
            cat.params.append(def);
        }
        m_currentDef.categories.append(cat);
    }
}

void MosPropertiesDialog::rebuildParamForm(const MosModelDef& def) {
    // Clear existing param widgets and layout items
    QLayoutItem* item;
    while ((item = m_paramLayout->takeAt(0))) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_categoryWidgets.clear();
    m_paramEdits.clear();

    if (!def.categories.isEmpty()) {
        for (const auto& cat : def.categories) {
            auto* groupBox = new QGroupBox(cat.name);
            groupBox->setCheckable(false);
            auto* form = new QFormLayout(groupBox);
            form->setLabelAlignment(Qt::AlignRight);

            for (const auto& param : cat.params) {
                auto* edit = new QLineEdit();
                edit->setText(param.defaultVal);
                QString unitStr = param.unit.isEmpty() ? "" : QString(" [%1]").arg(param.unit);
                QString tooltip = param.desc;
                if (!param.unit.isEmpty()) tooltip += QString("\nUnit: %1").arg(param.unit);
                edit->setToolTip(tooltip);

                QString label = param.name;
                if (!unitStr.isEmpty()) label += unitStr;
                form->addRow(label + ":", edit);

                connect(edit, &QLineEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
                m_paramEdits[param.name.toUpper()] = edit;
            }

            m_paramLayout->addWidget(groupBox);
            m_categoryWidgets.append(groupBox);
        }
    }

    // Add stretch at end
    m_paramLayout->addStretch();
}

void MosPropertiesDialog::onLevelChanged(int index) {
    if (index < 0 || index >= knownLevels().size()) return;
    const QString levelName = knownLevels()[index].name;

    if (levelName == "None") {
        if (!m_pickedModelName.isEmpty()) {
            m_modelNameEdit->setText(m_pickedModelName);
        }
        updateCommandPreview();
        return;
    }

    // Preserve current session edits before rebuilding the form
    QMap<QString, QString> currentSessionEdits;
    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        currentSessionEdits[it.key()] = it.value()->text().trimmed();
    }

    loadModelDef(levelName);
    rebuildParamForm(m_currentDef);

    // Fill values: Priority 1: Current session edits (if parameter exists in new level)
    // Priority 2: Existing item data
    QMap<QString, QString> itemData;
    if (m_item) {
        const auto pe = m_item->paramExpressions();
        for (auto it = pe.begin(); it != pe.end(); ++it) {
            QString key = it.key().toUpper();
            if (key.startsWith("MOS.")) key = key.mid(4);
            itemData[key] = it.value();
        }
    }

    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        const QString key = it.key();
        if (currentSessionEdits.contains(key) && !currentSessionEdits[key].isEmpty()) {
            it.value()->setText(currentSessionEdits[key]);
        } else if (itemData.contains(key)) {
            it.value()->setText(itemData[key]);
        }
    }

    // Handle parameters that didn't find a field in the built form (Fallback Group)
    QGroupBox* fallbackGroup = nullptr;
    QFormLayout* fallbackForm = nullptr;

    auto addFallback = [&](const QString& key, const QString& val) {
        if (val.isEmpty()) return;
        const QString upperKey = key.toUpper();
        if (upperKey == "LEVEL" || upperKey == "TYPE") return;
        if (m_paramEdits.contains(upperKey)) return;

        if (!fallbackGroup) {
            fallbackGroup = new QGroupBox("Model Parameters");
            fallbackForm = new QFormLayout(fallbackGroup);
            fallbackForm->setLabelAlignment(Qt::AlignRight);
            // Insert before the final stretch
            m_paramLayout->insertWidget(m_paramLayout->count() - 1, fallbackGroup);
            m_categoryWidgets.append(fallbackGroup);
        }
        auto* edit = new QLineEdit(val);
        fallbackForm->addRow(upperKey + ":", edit);
        connect(edit, &QLineEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
        m_paramEdits[upperKey] = edit;
    };

    // Check session edits first (to preserve manually added ones)
    for (auto it = currentSessionEdits.begin(); it != currentSessionEdits.end(); ++it) {
        addFallback(it.key(), it.value());
    }
    // Then item data
    for (auto it = itemData.begin(); it != itemData.end(); ++it) {
        addFallback(it.key(), it.value());
    }

    // Update default model name when level changes
    if (m_modelNameEdit) {
        const QString currentName = m_modelNameEdit->text().trimmed();
        const bool pmos = isPmosSelected();
        const QString oldDefaultNMOS = "2N7000";
        const QString oldDefaultPMOS = "BS250";

        // Compute the expected default for the new level
        QString newDefault;
        if (levelName == "None") {
            newDefault = pmos ? oldDefaultPMOS : oldDefaultNMOS;
        } else if (!m_currentDef.model.isEmpty()) {
            newDefault = m_currentDef.model + (pmos ? "_PMOS" : "_NMOS");
        } else {
            newDefault = levelName + (pmos ? "_PMOS" : "_NMOS");
        }

        // If level changed, we are no longer using the "picked" model precisely as it was
        if (levelName != "None" && !m_pickedModelName.isEmpty()) {
            m_pickedModelName.clear();
            m_pickedModelLevel.clear();
        }

        // Build a list of all possible defaults to detect stale names from other levels
        QStringList allDefaults;
        allDefaults << oldDefaultNMOS << oldDefaultPMOS;
        for (const auto& lvl : knownLevels()) {
            if (lvl.name != "None") {
                allDefaults << lvl.name + "_NMOS";
                allDefaults << lvl.name + "_PMOS";
            }
        }

        if (allDefaults.contains(currentName) && currentName != newDefault) {
            m_modelNameEdit->setText(newDefault);
        }
    }

    updateCommandPreview();
}

void MosPropertiesDialog::loadValues() {
    if (!m_item) return;

    const auto pe = m_item->paramExpressions();
    const QString typeExpr = pe.value("mos.type").trimmed();
    const bool pmos = typeExpr.isEmpty() ? isPmos() : (typeExpr.compare("PMOS", Qt::CaseInsensitive) == 0);
    const QString defaultModel = pmos ? "BS250" : "2N7000";

    if (m_typeCombo) {
        m_typeCombo->setCurrentText(pmos ? "PMOS" : "NMOS");
    }

    QString modelName = m_item->spiceModel().trimmed();
    if (modelName.isEmpty()) {
        modelName = m_item->value().trimmed();
        if (modelName.isEmpty() || modelName.compare("NMOS", Qt::CaseInsensitive) == 0 ||
            modelName.compare("PMOS", Qt::CaseInsensitive) == 0) {
            modelName = defaultModel;
        }
    }
    m_modelNameEdit->setText(modelName);

    // Determine model level
    QString level = pe.value("mos.level").trimmed();
    if (level.isEmpty()) {
        const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
        if (mdl && !mdl->modelLevel.empty()) {
            level = QString::fromStdString(mdl->modelLevel);
        }
    }
    if (level.isEmpty()) {
        level = "MOS1";
    }

    // Set level combo
    for (int i = 0; i < m_levelCombo->count(); ++i) {
        if (m_levelCombo->itemText(i).compare(level, Qt::CaseInsensitive) == 0) {
            m_levelCombo->setCurrentIndex(i);
            break;
        }
    }

    // Load model definition and rebuild form
    onLevelChanged(m_levelCombo->currentIndex());

    // Fill values from stored params
    for (auto it = pe.begin(); it != pe.end(); ++it) {
        QString key = it.key();
        if (key == "mos.type" || key == "mos.level") continue;

        // Strip "mos." prefix
        QString cleanKey = key;
        if (cleanKey.startsWith("mos.", Qt::CaseInsensitive))
            cleanKey = cleanKey.mid(4);

        QString upperKey = cleanKey.toUpper();
        if (m_paramEdits.contains(upperKey)) {
            m_paramEdits[upperKey]->setText(it.value());
        }
    }

    // Fill raw params from stored "mos.raw" key
    QString rawParams = pe.value("mos.raw").trimmed();
    if (!rawParams.isEmpty()) {
        m_rawParamsEdit->setPlainText(rawParams);
    }

    if (m_footprintEdit) {
        m_footprintEdit->setText(m_item->footprint());
    }

    // Load params from model library if available
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (mdl && m_typeCombo) {
        if (mdl->type == SimComponentType::MOSFET_PMOS) m_typeCombo->setCurrentText("PMOS");
        else if (mdl->type == SimComponentType::MOSFET_NMOS) m_typeCombo->setCurrentText("NMOS");

        // Fill params from model
        for (const auto& [k, v] : mdl->params) {
            QString key = QString::fromStdString(k).toUpper();
            if (m_paramEdits.contains(key) && m_paramEdits[key]->text().trimmed().isEmpty()) {
                m_paramEdits[key]->setText(QString::number(v, 'g', 12));
            }
        }
    }
}

void MosPropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_pickedModelName = modelName;
    m_pickedModelLevel = QString::fromStdString(mdl->modelLevel);
    m_modelNameEdit->setText(modelName);
    if (m_rawParamsEdit) m_rawParamsEdit->clear();
    if (m_typeCombo) {
        if (mdl->type == SimComponentType::MOSFET_PMOS) m_typeCombo->setCurrentText("PMOS");
        else if (mdl->type == SimComponentType::MOSFET_NMOS) m_typeCombo->setCurrentText("NMOS");
    }

    // Set level to the model's actual level (VDMOS, BSIM4, etc.) so the param
    // form is built correctly. Block signals to prevent onLevelChanged from
    // overwriting the model name and clearing m_pickedModelName prematurely.
    if (!m_pickedModelLevel.isEmpty()) {
        for (int i = 0; i < m_levelCombo->count(); ++i) {
            if (m_levelCombo->itemText(i).compare(m_pickedModelLevel, Qt::CaseInsensitive) == 0) {
                m_levelCombo->blockSignals(true);
                m_levelCombo->setCurrentIndex(i);
                m_levelCombo->blockSignals(false);
                break;
            }
        }
    }

    // Must manually build the param form since onLevelChanged was suppressed
    loadModelDef(m_pickedModelLevel);
    rebuildParamForm(m_currentDef);

    // Fill param fields after the form is built.
    // For params that don't have a JSON-defined field, create dynamic ones.
    const bool formEmpty = m_paramEdits.isEmpty();
    QGroupBox* fallbackGroup = nullptr;
    QFormLayout* fallbackForm = nullptr;

    for (const auto& [k, v] : mdl->params) {
        QString key = QString::fromStdString(k).toUpper();
        if (m_paramEdits.contains(key)) {
            m_paramEdits[key]->setText(QString::number(v, 'g', 12));
        } else {
            // Create a fallback group box lazily
            if (!fallbackGroup) {
                fallbackGroup = new QGroupBox("Model Parameters");
                fallbackForm = new QFormLayout(fallbackGroup);
                fallbackForm->setLabelAlignment(Qt::AlignRight);
                m_paramLayout->addWidget(fallbackGroup);
                m_categoryWidgets.append(fallbackGroup);
            }
            auto* edit = new QLineEdit(QString::number(v, 'g', 12));
            edit->setToolTip(QString("From model: %1").arg(m_pickedModelName));
            fallbackForm->addRow(key + ":", edit);
            connect(edit, &QLineEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
            m_paramEdits[key] = edit;
        }
    }

    updateCommandPreview();
}

void MosPropertiesDialog::autoMatchModel() {
    const QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    const SimModel* mdl = ModelLibraryManager::instance().findModel(name);
    if (!mdl) return;

    bool typeMatch = false;
    if (mdl->type == SimComponentType::MOSFET_NMOS && m_typeCombo->currentText() == "NMOS") typeMatch = true;
    if (mdl->type == SimComponentType::MOSFET_PMOS && m_typeCombo->currentText() == "PMOS") typeMatch = true;
    if (!typeMatch) return;

    // Set level from matched model
    if (!mdl->modelLevel.empty()) {
        QString lvl = QString::fromStdString(mdl->modelLevel);
        for (int i = 0; i < m_levelCombo->count(); ++i) {
            if (m_levelCombo->itemText(i).compare(lvl, Qt::CaseInsensitive) == 0) {
                m_levelCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    for (const auto& [k, v] : mdl->params) {
        QString key = QString::fromStdString(k).toUpper();
        if (m_paramEdits.contains(key)) {
            const QString current = m_paramEdits[key]->text().trimmed();
            if (current.isEmpty()) {
                m_paramEdits[key]->setText(QString::number(v, 'g', 12));
            }
        }
    }

    updateCommandPreview();
}

void MosPropertiesDialog::updateCommandPreview() {
    QString model = modelName();
    if (model.isEmpty()) {
        model = isPmosSelected() ? "BS250" : "2N7000";
    }

    QStringList params;

    // Add params from form fields
    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        const QString v = it.value()->text().trimmed();
        if (!v.isEmpty()) {
            params << QString("%1=%2").arg(it.key(), v);
        }
    }

    // Add raw params
    QString rawText = m_rawParamsEdit->toPlainText().trimmed();
    if (!rawText.isEmpty()) {
        QStringList rawLines = rawText.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : rawLines) {
            QString trimmed = line.trimmed();
            if (trimmed.contains('=')) {
                params << trimmed;
            }
        }
    }

    // Determine SPICE model type — all use NMOS/PMOS + LEVEL=N
    const QString levelName = m_levelCombo->currentText();
    const bool pmos = isPmosSelected();
    QString spiceType = pmos ? "PMOS" : "NMOS";
    
    int level = 0;
    if (levelName == "BSIM4") level = 14;
    else if (levelName == "BSIM3") level = 8;
    else if (levelName == "BSIMSOI") level = 10;
    else if (levelName == "BSIM3SOI") level = 55;
    else if (levelName == "HISIM2") level = 68;
    else if (levelName == "HISIM_HV") level = 73;
    else if (levelName == "MOS2") level = 2;
    else if (levelName == "MOS3") level = 3;
    else if (levelName == "MOS6") level = 6;
    else if (levelName == "MOS9") level = 9;
    else if (levelName == "BSIM1") level = 4;
    else if (levelName == "BSIM2") level = 5;
    else if (levelName == "SOI3") level = 60;

    // Use m_currentDef.level if available (from JSON)
    if (m_currentDef.level > 0 && m_currentDef.level != 1) {
        level = m_currentDef.level;
    }

    if (level > 0) {
        params.prepend(QString("LEVEL=%1").arg(level));
    }

    m_commandPreview->setText(QString(".model %1 %2(%3)").arg(model, spiceType, params.join(" ")));
}

void MosPropertiesDialog::applyChanges() {
    accept();
}

QString MosPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

QString MosPropertiesDialog::modelLevel() const {
    return m_levelCombo ? m_levelCombo->currentText() : QString();
}

QMap<QString, QString> MosPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;

    auto add = [&](const QString& key, const QString& val) {
        if (!val.isEmpty()) pe[key] = val;
    };

    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        add(QString("mos.%1").arg(it.key()), it.value()->text().trimmed());
    }

    // Raw params
    QString rawText = m_rawParamsEdit->toPlainText().trimmed();
    if (!rawText.isEmpty()) {
        add("mos.raw", rawText);

        // Also add individual raw params for netlist generation
        QStringList rawLines = rawText.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : rawLines) {
            QString trimmed = line.trimmed();
            int eqPos = trimmed.indexOf('=');
            if (eqPos > 0) {
                QString key = "mos." + trimmed.left(eqPos).trimmed().toUpper();
                QString val = trimmed.mid(eqPos + 1).trimmed();
                if (!pe.contains(key)) {
                    add(key, val);
                }
            }
        }
    }

    add("mos.type", isPmosSelected() ? "PMOS" : "NMOS");
    if (m_levelCombo) {
        const QString lvl = m_levelCombo->currentText();
        if (lvl.compare("None", Qt::CaseInsensitive) == 0) {
            // When level is "None", store the picked model's actual level so
            // the netlist generator emits the correct SPICE type
            if (!m_pickedModelLevel.isEmpty()) {
                add("mos.pickedLevel", m_pickedModelLevel);
            }
        } else {
            add("mos.level", lvl);
        }
    }

    return pe;
}

QString MosPropertiesDialog::newSymbolName() const {
    return isPmosSelected() ? "pmos" : "nmos";
}

QString MosPropertiesDialog::footprint() const {
    return m_footprintEdit ? m_footprintEdit->text().trimmed() : QString();
}

void MosPropertiesDialog::pickFootprint() {
    FootprintBrowserDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dlg.selectedFootprint();
        if (!fp.name().isEmpty()) {
            m_footprintEdit->setText(fp.name());
        }
    }
}