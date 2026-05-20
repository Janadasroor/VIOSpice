#include "bjt_properties_dialog.h"
#include "bjt_model_picker_dialog.h"
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

const QVector<BjtPropertiesDialog::LevelInfo>& BjtPropertiesDialog::knownLevels() {
    static const QVector<LevelInfo> levels = {
        {"None",     ""},
        {"BJT",      ""}, // Standard LEVEL=1
        {"VBIC",     "vbic.json"},
        {"HICUM2",   "hicum2.json"},
        {"MEXTRAM",  ""},
    };
    return levels;
}

BjtPropertiesDialog::BjtPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("BJT Properties - %1").arg(item ? item->reference() : "Q?"));
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

void BjtPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // === Top form: Model Name, Type, Level ===
    auto* topForm = new QFormLayout();
    topForm->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. 2N2222 / BC547");
    topForm->addRow("Model Name:", m_modelNameEdit);

    {
        QStringList bjtModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            QString t = info.type.toUpper();
            if (t == "NPN" || t == "PNP" || t == "BJT" || t == "VBIC" || t == "HICUM2") {
                bjtModels.append(info.name);
            }
        }
        bjtModels.sort(Qt::CaseInsensitive);
        bjtModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(bjtModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &BjtPropertiesDialog::fillFromModel);
        connect(m_modelNameEdit, &QLineEdit::textEdited, this, [this]() {
            m_pickedModelName.clear();
        });
    }

    auto* typeLevelLayout = new QHBoxLayout();

    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("NPN");
    m_typeCombo->addItem("PNP");
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
    m_pickModelButton = new QPushButton(isPnp() ? "Pick PNP Model" : "Pick NPN Model");
    m_pickModelButton->setFixedHeight(26);
    connect(m_pickModelButton, &QPushButton::clicked, this, [this]() {
        BjtModelPickerDialog dlg(isPnpSelected(), this);
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
    m_rawParamsEdit->setPlaceholderText("e.g.\nIS=1e-14\nBF=100\nVAF=100");
    m_rawParamsEdit->setMaximumHeight(80);
    m_rawParamsEdit->setStyleSheet("font-family: 'Courier New'; font-size: 10pt;");
    mainLayout->addWidget(m_rawParamsEdit);

    // === Footprint ===
    auto* fpRow = new QHBoxLayout();
    m_footprintEdit = new QLineEdit();
    m_footprintEdit->setPlaceholderText("Select a footprint");
    m_footprintEdit->setReadOnly(true);
    auto* fpBtn = new QPushButton("Pick Footprint");
    connect(fpBtn, &QPushButton::clicked, this, &BjtPropertiesDialog::pickFootprint);
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
    connect(buttons, &QDialogButtonBox::accepted, this, &BjtPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // === Connections ===
    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
    connect(m_modelNameEdit, &QLineEdit::editingFinished, this, &BjtPropertiesDialog::autoMatchModel);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        if (m_pickModelButton) {
            m_pickModelButton->setText(isPnpSelected() ? "Pick PNP Model" : "Pick NPN Model");
        }
        updateCommandPreview();
    });
    connect(m_levelCombo, &QComboBox::currentIndexChanged, this, &BjtPropertiesDialog::onLevelChanged);
    connect(m_rawParamsEdit, &QTextEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
}

bool BjtPropertiesDialog::isPnp() const {
    if (!m_item) return false;
    const QString t = m_item->itemTypeName().trimmed().toLower();
    return t == "transistor_pnp" ||
           t == "pnp" ||
           m_item->referencePrefix().compare("QP", Qt::CaseInsensitive) == 0;
}

bool BjtPropertiesDialog::isPnpSelected() const {
    if (m_typeCombo) {
        return m_typeCombo->currentText().compare("PNP", Qt::CaseInsensitive) == 0;
    }
    return isPnp();
}

void BjtPropertiesDialog::addEssentialDefaults(BjtModelDef& def, const QString& levelName, bool pnp) {
    auto addCat = [&](const QString& name, const QVector<BjtParamDef>& params) {
        BjtParamCategory cat;
        cat.name = name;
        cat.params = params;
        def.categories.append(cat);
    };

    if (levelName == "VBIC") {
        addCat("Core", {{"IS", "1e-14", "A", "Transport saturation current"},
                        {"NF", "1.0", "", "Forward ideality factor"},
                        {"NR", "1.0", "", "Reverse ideality factor"},
                        {"RC", "0", "ohm", "Intrinsic collector resistance"}});
    } else if (levelName == "HICUM2") {
        addCat("Core", {{"C10", "1e-14", "A^2s", "Transport saturation current constant"},
                        {"QP0", "1e-14", "As", "Zero-bias hole charge"},
                        {"ICH", "1e-3", "A", "High-current critical current"}});
    } else {
        addCat("Standard DC", {{"IS", "1e-14", "A", "Saturation current"},
                               {"BF", "100", "", "Forward beta"},
                               {"NF", "1.0", "", "Forward emission coefficient"},
                               {"VAF", "100", "V", "Forward Early voltage"},
                               {"IKF", "0", "A", "Forward beta roll-off"}});
        addCat("Capacitance", {{"CJE", "2p", "F", "B-E zero-bias capacitance"},
                               {"VJE", "0.75", "V", "B-E built-in potential"},
                               {"CJC", "1p", "F", "B-C zero-bias capacitance"},
                               {"VJC", "0.75", "V", "B-C built-in potential"}});
        addCat("Time Constants", {{"TF", "10p", "s", "Forward transit time"},
                                  {"TR", "10n", "s", "Reverse transit time"}});
    }
}

void BjtPropertiesDialog::loadModelDef(const QString& levelName) {
    m_currentDef = BjtModelDef();

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

    if (!searchPath.isEmpty()) {
        // Try to find the JSON file
        QString jsonPath;
        for (const auto& base : searchPaths) {
            QString candidate = base + searchPath;
            if (QFile::exists(candidate)) {
                jsonPath = candidate;
                break;
            }
        }

        if (!jsonPath.isEmpty()) {
            QFile file(jsonPath);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                file.close();
                if (doc.isObject()) {
                    QJsonObject root = doc.object();
                    m_currentDef.model = root["model"].toString();
                    m_currentDef.description = root["description"].toString();
                    m_currentDef.level = root["level"].toInt(1);
                    m_currentDef.spiceType = root["spiceType"].toString("NPN");
                    m_currentDef.spiceLevelParam = root["spiceLevelParam"].toString();

                    QJsonArray cats = root["categories"].toArray();
                    for (const auto& catVal : cats) {
                        QJsonObject catObj = catVal.toObject();
                        BjtParamCategory cat;
                        cat.name = catObj["name"].toString();
                        QJsonArray params = catObj["params"].toArray();
                        for (const auto& pVal : params) {
                            QJsonObject pObj = pVal.toObject();
                            BjtParamDef def;
                            def.name = pObj["name"].toString();
                            def.defaultVal = pObj["default"].toString();
                            def.unit = pObj["unit"].toString();
                            def.desc = pObj["desc"].toString();
                            cat.params.append(def);
                        }
                        m_currentDef.categories.append(cat);
                    }
                    return;
                }
            }
        }
    }

    // Fallback to built-in defaults
    m_currentDef.model = levelName;
    addEssentialDefaults(m_currentDef, levelName, isPnpSelected());
}

void BjtPropertiesDialog::rebuildParamForm(const BjtModelDef& def) {
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

                connect(edit, &QLineEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
                m_paramEdits[param.name.toUpper()] = edit;
            }

            m_paramLayout->addWidget(groupBox);
            m_categoryWidgets.append(groupBox);
        }
    }

    // Add stretch at end
    m_paramLayout->addStretch();
}

void BjtPropertiesDialog::onLevelChanged(int index) {
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

    // Fill values
    QMap<QString, QString> itemData;
    if (m_item) {
        const auto pe = m_item->paramExpressions();
        for (auto it = pe.begin(); it != pe.end(); ++it) {
            QString key = it.key().toUpper();
            if (key.startsWith("BJT.")) key = key.mid(4);
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
            m_paramLayout->insertWidget(m_paramLayout->count() - 1, fallbackGroup);
            m_categoryWidgets.append(fallbackGroup);
        }
        auto* edit = new QLineEdit(val);
        fallbackForm->addRow(upperKey + ":", edit);
        connect(edit, &QLineEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
        m_paramEdits[upperKey] = edit;
    };

    for (auto it = currentSessionEdits.begin(); it != currentSessionEdits.end(); ++it) {
        addFallback(it.key(), it.value());
    }
    for (auto it = itemData.begin(); it != itemData.end(); ++it) {
        addFallback(it.key(), it.value());
    }

    // Update default model name
    if (m_modelNameEdit) {
        const QString currentName = m_modelNameEdit->text().trimmed();
        const bool pnp = isPnpSelected();
        const QString defaultNPN = "2N2222";
        const QString defaultPNP = "2N2907";

        QString newDefault;
        if (levelName == "None" || levelName == "BJT") {
            newDefault = pnp ? defaultPNP : defaultNPN;
        } else {
            newDefault = levelName + (pnp ? "_PNP" : "_NPN");
        }

        QStringList allDefaults;
        allDefaults << defaultNPN << defaultPNP;
        for (const auto& lvl : knownLevels()) {
            if (lvl.name != "None") {
                allDefaults << lvl.name + "_NPN";
                allDefaults << lvl.name + "_PNP";
            }
        }

        if (allDefaults.contains(currentName) && currentName != newDefault) {
            m_modelNameEdit->setText(newDefault);
        }
    }

    updateCommandPreview();
}

void BjtPropertiesDialog::loadValues() {
    if (!m_item) return;

    const auto pe = m_item->paramExpressions();
    const QString typeExpr = pe.value("bjt.type").trimmed();
    const bool pnp = typeExpr.isEmpty() ? isPnp() : (typeExpr.compare("PNP", Qt::CaseInsensitive) == 0);

    if (m_typeCombo) {
        m_typeCombo->setCurrentText(pnp ? "PNP" : "NPN");
    }

    QString modelName = m_item->spiceModel().trimmed();
    if (modelName.isEmpty()) {
        modelName = m_item->value().trimmed();
        if (modelName.isEmpty() || modelName.compare("NPN", Qt::CaseInsensitive) == 0 ||
            modelName.compare("PNP", Qt::CaseInsensitive) == 0) {
            modelName = pnp ? "2N2907" : "2N2222";
        }
    }
    m_modelNameEdit->setText(modelName);

    QString level = pe.value("bjt.level").trimmed();
    if (level.isEmpty()) {
        const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
        if (mdl && !mdl->modelLevel.empty()) {
            level = QString::fromStdString(mdl->modelLevel);
        }
    }
    if (level.isEmpty()) level = "BJT";

    for (int i = 0; i < m_levelCombo->count(); ++i) {
        if (m_levelCombo->itemText(i).compare(level, Qt::CaseInsensitive) == 0) {
            m_levelCombo->setCurrentIndex(i);
            break;
        }
    }

    onLevelChanged(m_levelCombo->currentIndex());

    for (auto it = pe.begin(); it != pe.end(); ++it) {
        QString key = it.key();
        if (key == "bjt.type" || key == "bjt.level") continue;
        if (key.startsWith("bjt.", Qt::CaseInsensitive)) key = key.mid(4);
        QString upperKey = key.toUpper();
        if (m_paramEdits.contains(upperKey)) {
            m_paramEdits[upperKey]->setText(it.value());
        }
    }

    QString rawParams = pe.value("bjt.raw").trimmed();
    if (!rawParams.isEmpty()) m_rawParamsEdit->setPlainText(rawParams);

    if (m_footprintEdit) m_footprintEdit->setText(m_item->footprint());
}

void BjtPropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_pickedModelName = modelName;
    m_pickedModelLevel = QString::fromStdString(mdl->modelLevel);
    m_modelNameEdit->setText(modelName);
    if (m_rawParamsEdit) m_rawParamsEdit->clear();
    if (m_typeCombo) {
        if (mdl->type == SimComponentType::BJT_PNP) m_typeCombo->setCurrentText("PNP");
        else if (mdl->type == SimComponentType::BJT_NPN) m_typeCombo->setCurrentText("NPN");
    }

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

    loadModelDef(m_pickedModelLevel.isEmpty() ? "BJT" : m_pickedModelLevel);
    rebuildParamForm(m_currentDef);

    QGroupBox* fallbackGroup = nullptr;
    QFormLayout* fallbackForm = nullptr;

    for (const auto& [k, v] : mdl->params) {
        QString key = QString::fromStdString(k).toUpper();
        if (m_paramEdits.contains(key)) {
            m_paramEdits[key]->setText(QString::number(v, 'g', 12));
        } else {
            if (!fallbackGroup) {
                fallbackGroup = new QGroupBox("Model Parameters");
                fallbackForm = new QFormLayout(fallbackGroup);
                fallbackForm->setLabelAlignment(Qt::AlignRight);
                m_paramLayout->addWidget(fallbackGroup);
                m_categoryWidgets.append(fallbackGroup);
            }
            auto* edit = new QLineEdit(QString::number(v, 'g', 12));
            fallbackForm->addRow(key + ":", edit);
            connect(edit, &QLineEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
            m_paramEdits[key] = edit;
        }
    }

    updateCommandPreview();
}

void BjtPropertiesDialog::autoMatchModel() {
    const QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    const SimModel* mdl = ModelLibraryManager::instance().findModel(name);
    if (!mdl) return;

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
            if (m_paramEdits[key]->text().trimmed().isEmpty()) {
                m_paramEdits[key]->setText(QString::number(v, 'g', 12));
            }
        }
    }
    updateCommandPreview();
}

void BjtPropertiesDialog::updateCommandPreview() {
    QString model = modelName();
    if (model.isEmpty()) model = isPnpSelected() ? "2N2907" : "2N2222";

    QStringList params;
    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        const QString v = it.value()->text().trimmed();
        if (!v.isEmpty()) params << QString("%1=%2").arg(it.key(), v);
    }

    QString rawText = m_rawParamsEdit->toPlainText().trimmed();
    if (!rawText.isEmpty()) {
        for (const QString& line : rawText.split('\n', Qt::SkipEmptyParts)) {
            if (line.contains('=')) params << line.trimmed();
        }
    }

    const QString levelName = m_levelCombo->currentText();
    QString spiceType = isPnpSelected() ? "PNP" : "NPN";
    
    int level = 0;
    if (levelName == "VBIC") level = 4;
    else if (levelName == "HICUM2") level = 8;
    else if (levelName == "MEXTRAM") level = 6;

    if (m_currentDef.level > 1) level = m_currentDef.level;
    if (level > 0 && level != 1) params.prepend(QString("LEVEL=%1").arg(level));

    m_commandPreview->setText(QString(".model %1 %2(%3)").arg(model, spiceType, params.join(" ")));
}

void BjtPropertiesDialog::applyChanges() {
    accept();
}

QString BjtPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

QString BjtPropertiesDialog::modelLevel() const {
    return m_levelCombo ? m_levelCombo->currentText() : QString();
}

QMap<QString, QString> BjtPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    for (auto it = m_paramEdits.begin(); it != m_paramEdits.end(); ++it) {
        pe[QString("bjt.%1").arg(it.key())] = it.value()->text().trimmed();
    }
    QString rawText = m_rawParamsEdit->toPlainText().trimmed();
    if (!rawText.isEmpty()) {
        pe["bjt.raw"] = rawText;
        for (const QString& line : rawText.split('\n', Qt::SkipEmptyParts)) {
            int eqPos = line.indexOf('=');
            if (eqPos > 0) {
                QString key = "bjt." + line.left(eqPos).trimmed().toUpper();
                if (!pe.contains(key)) pe[key] = line.mid(eqPos + 1).trimmed();
            }
        }
    }
    pe["bjt.type"] = isPnpSelected() ? "PNP" : "NPN";
    if (m_levelCombo) {
        const QString lvl = m_levelCombo->currentText();
        if (lvl != "None" && lvl != "BJT") pe["bjt.level"] = lvl;
    }
    return pe;
}

QString BjtPropertiesDialog::newSymbolName() const {
    return isPnpSelected() ? "pnp" : "npn";
}

QString BjtPropertiesDialog::footprint() const {
    return m_footprintEdit ? m_footprintEdit->text().trimmed() : QString();
}

void BjtPropertiesDialog::pickFootprint() {
    FootprintBrowserDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString name = dlg.selectedFootprint().name();
        if (!name.isEmpty()) m_footprintEdit->setText(name);
    }
}
