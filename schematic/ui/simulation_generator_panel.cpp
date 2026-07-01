/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulation_generator_panel.h"
#include "../items/voltage_source_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../dialogs/spice_step_dialog.h"
#include "theme_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QGraphicsView>
#include <QDebug>

SimulationGeneratorPanel::SimulationGeneratorPanel(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir, QWidget* parent)
    : QWidget(parent), m_scene(scene), m_netManager(netManager), m_projectDir(projectDir) {
    setupUI();
    loadGeneratorLibrary();
    if (m_generatorType) {
        onGeneratorTypeChanged(m_generatorType->currentIndex());
    }
}

void SimulationGeneratorPanel::setTargetScene(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir) {
    m_scene = scene;
    m_netManager = netManager;
    m_projectDir = projectDir;
}

void SimulationGeneratorPanel::setupUI() {
    PCBTheme* theme = ThemeManager::theme();
    QString textColor = theme ? theme->textColor().name() : "#cccccc";
    QString borderColor = theme ? theme->panelBorder().name() : "#3c3c3c";
    const bool isLight = theme && theme->type() == PCBTheme::Light;
    const QString inputBg = isLight ? "#ffffff" : "#121214";
    const QString inputStyle = QString("QLineEdit, QComboBox { background: %1; color: %2; border: 1px solid %3; padding: 2px; }")
        .arg(inputBg, textColor, borderColor);

    QVBoxLayout* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);

    QGroupBox* group = new QGroupBox("SOURCE GENERATOR CONFIGURATION");
    group->setStyleSheet("QGroupBox { font-weight: bold; }");
    QFormLayout* layout = new QFormLayout(group);

    m_generatorType = new QComboBox();
    m_generatorType->addItems({"DC", "SIN", "PULSE", "EXP", "SFFM", "PWL", "AM", "FM"});
    m_generatorType->setStyleSheet(inputStyle);
    layout->addRow("Type:", m_generatorType);

    QHBoxLayout* presetLay = new QHBoxLayout();
    m_generatorPresetCombo = new QComboBox();
    m_generatorPresetCombo->setStyleSheet(inputStyle);
    presetLay->addWidget(m_generatorPresetCombo, 1);

    m_savePresetBtn = new QPushButton("Save");
    m_savePresetBtn->setStyleSheet("background: #0f766e; color: white; padding: 2px 6px; font-weight: bold;");
    presetLay->addWidget(m_savePresetBtn);

    m_deletePresetBtn = new QPushButton("Delete");
    m_deletePresetBtn->setStyleSheet("background: #991b1b; color: white; padding: 2px 6px; font-weight: bold;");
    presetLay->addWidget(m_deletePresetBtn);

    layout->addRow("Template:", presetLay);

    m_genLabel1 = new QLabel("P1:"); m_genParam1 = new QLineEdit("5");
    m_genLabel2 = new QLabel("P2:"); m_genParam2 = new QLineEdit("1");
    m_genLabel3 = new QLabel("P3:"); m_genParam3 = new QLineEdit("1k");
    m_genLabel4 = new QLabel("P4:"); m_genParam4 = new QLineEdit("0");
    m_genLabel5 = new QLabel("P5:"); m_genParam5 = new QLineEdit("0");
    m_genLabel6 = new QLabel("P6:"); m_genParam6 = new QLineEdit("0");

    for (auto* l : {m_genParam1, m_genParam2, m_genParam3, m_genParam4, m_genParam5, m_genParam6}) l->setStyleSheet(inputStyle);

    layout->addRow(m_genLabel1, m_genParam1);
    layout->addRow(m_genLabel2, m_genParam2);
    layout->addRow(m_genLabel3, m_genParam3);
    layout->addRow(m_genLabel4, m_genParam4);
    layout->addRow(m_genLabel5, m_genParam5);
    layout->addRow(m_genLabel6, m_genParam6);

    // PWL Editor Buttons Layout
    QHBoxLayout* pwlButtonsLay = new QHBoxLayout();
    m_pwlEditBtn = new QPushButton("Edit PWL...");
    m_pwlImportBtn = new QPushButton("Import CSV...");
    m_pwlExportBtn = new QPushButton("Export CSV...");
    for (auto* btn : {m_pwlEditBtn, m_pwlImportBtn, m_pwlExportBtn}) {
        btn->setStyleSheet("background: #374151; color: white; font-size: 10px; padding: 4px;");
        pwlButtonsLay->addWidget(btn);
    }
    layout->addRow("", pwlButtonsLay);

    // Step Builder Button
    m_stepBuilderBtn = new QPushButton("Open Step Builder...");
    m_stepBuilderBtn->setStyleSheet("background: #4b5563; color: white; font-size: 10px; padding: 4px;");
    layout->addRow("", m_stepBuilderBtn);

    QPushButton* applyBtn = new QPushButton("Apply to Selected Source");
    applyBtn->setStyleSheet("background: #0f766e; color: white; font-weight: bold; padding: 8px;");
    layout->addRow("", applyBtn);

    mainLay->addWidget(group);
    mainLay->addStretch();

    // Connections
    connect(m_generatorType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationGeneratorPanel::onGeneratorTypeChanged);
    connect(m_generatorPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationGeneratorPanel::onGeneratorPresetActivated);
    connect(applyBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onApplyGeneratorToSelection);
    
    connect(m_pwlEditBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onOpenPwlEditor);
    connect(m_pwlImportBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onImportPwlCsv);
    connect(m_pwlExportBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onExportPwlCsv);
    connect(m_stepBuilderBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onOpenStepBuilder);
    connect(m_savePresetBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onSaveGeneratorPreset);
    connect(m_deletePresetBtn, &QPushButton::clicked, this, &SimulationGeneratorPanel::onDeleteGeneratorPreset);
}

QString SimulationGeneratorPanel::buildGeneratorExpression() const {
    const QString type = m_generatorType ? m_generatorType->currentText() : "DC";
    if (type == "DC") return QString("DC %1").arg(m_genParam1->text().trimmed());
    if (type == "SIN") return QString("SINE(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "PULSE") return QString("PULSE(%1 %2 %3 %4 %5 %6 %7)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed(), m_genParam6->text().trimmed(), "1m");
    if (type == "EXP") return QString("EXP(%1 %2 %3 %4 %5 %6)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed(), m_genParam6->text().trimmed());
    if (type == "SFFM") return QString("SFFM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "PWL") {
        QStringList pairs;
        if (!m_pwlPoints.isEmpty()) { for (const auto& p : m_pwlPoints) pairs << p.first.trimmed() << p.second.trimmed(); }
        else pairs << m_genParam1->text().trimmed() << m_genParam2->text().trimmed() << m_genParam3->text().trimmed() << m_genParam4->text().trimmed() << m_genParam5->text().trimmed() << m_genParam6->text().trimmed();
        return QString("PWL(%1)").arg(pairs.join(' '));
    }
    if (type == "AM") return QString("AM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "FM") return QString("FM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    return QString("DC %1").arg(m_genParam1->text().trimmed());
}

QVariantMap SimulationGeneratorPanel::collectGeneratorConfig() const {
    QVariantMap cfg;
    cfg["type"] = m_generatorType ? m_generatorType->currentText() : "DC";
    cfg["p1"] = m_genParam1->text().trimmed(); cfg["p2"] = m_genParam2->text().trimmed();
    cfg["p3"] = m_genParam3->text().trimmed(); cfg["p4"] = m_genParam4->text().trimmed();
    cfg["p5"] = m_genParam5->text().trimmed(); cfg["p6"] = m_genParam6->text().trimmed();
    cfg["expression"] = buildGeneratorExpression();
    QVariantList points;
    for (const auto& p : m_pwlPoints) { QVariantMap pt; pt["t"] = p.first; pt["v"] = p.second; points.push_back(pt); }
    cfg["pwl_points"] = points;
    return cfg;
}

void SimulationGeneratorPanel::applyGeneratorConfig(const QVariantMap& cfg) {
    if (m_generatorType) { int idx = m_generatorType->findText(cfg.value("type", "DC").toString()); if (idx >= 0) m_generatorType->setCurrentIndex(idx); }
    m_genParam1->setText(cfg.value("p1").toString()); m_genParam2->setText(cfg.value("p2").toString());
    m_genParam3->setText(cfg.value("p3").toString()); m_genParam4->setText(cfg.value("p4").toString());
    m_genParam5->setText(cfg.value("p5").toString()); m_genParam6->setText(cfg.value("p6").toString());
    m_pwlPoints.clear();
    for (const QVariant& v : cfg.value("pwl_points").toList()) { QVariantMap m = v.toMap(); m_pwlPoints.push_back({m.value("t").toString(), m.value("v").toString()}); }
}

QString SimulationGeneratorPanel::generatorPresetsPath() const {
    QString b = m_projectDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : m_projectDir;
    QDir(b).mkpath("."); return QDir(b).filePath("generator_presets.json");
}

void SimulationGeneratorPanel::loadGeneratorLibrary() {
    m_generatorTemplates["Template: DC 5V"] = QVariantMap{{"type", "DC"}, {"p1", "5"}};
    m_generatorTemplates["Template: SIN 1kHz"] = QVariantMap{{"type", "SIN"}, {"p1", "0"}, {"p2", "5"}, {"p3", "1k"}, {"p4", "0"}, {"p5", "0"}};
    m_generatorTemplates["Template: Pulse 0-5V"] = QVariantMap{{"type", "PULSE"}, {"p1", "0"}, {"p2", "5"}, {"p3", "0"}, {"p4", "1u"}, {"p5", "1u"}, {"p6", "500u"}};
    QFile f(generatorPresetsPath());
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : root.value("presets").toArray()) {
            QJsonObject po = v.toObject(); m_userGeneratorPresets[po.value("name").toString()] = po.value("config").toObject().toVariantMap();
        }
    }
    refreshGeneratorPresetCombo();
}

void SimulationGeneratorPanel::saveUserGeneratorPresets() const {
    QJsonArray arr;
    for (auto it = m_userGeneratorPresets.constBegin(); it != m_userGeneratorPresets.constEnd(); ++it) {
        QJsonObject p; p["name"] = it.key(); p["config"] = QJsonObject::fromVariantMap(it.value()); arr.append(p);
    }
    QJsonObject root; root["presets"] = arr;
    QFile f(generatorPresetsPath()); if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson());
}

void SimulationGeneratorPanel::refreshGeneratorPresetCombo() {
    if (!m_generatorPresetCombo) return;
    m_generatorPresetCombo->blockSignals(true); m_generatorPresetCombo->clear();
    m_generatorPresetCombo->addItem("Select Template/Preset", "__none__");
    for (auto it = m_generatorTemplates.constBegin(); it != m_generatorTemplates.constEnd(); ++it) m_generatorPresetCombo->addItem(it.key(), "T:" + it.key());
    for (auto it = m_userGeneratorPresets.constBegin(); it != m_userGeneratorPresets.constEnd(); ++it) m_generatorPresetCombo->addItem("Preset: " + it.key(), "U:" + it.key());
    m_generatorPresetCombo->blockSignals(false);
}

void SimulationGeneratorPanel::seedDefaultPwlPointsIfNeeded() {
    if (!m_pwlPoints.isEmpty()) return;
    m_pwlPoints.push_back({"0", "0"}); m_pwlPoints.push_back({"1m", "5"}); m_pwlPoints.push_back({"2m", "0"});
}

bool SimulationGeneratorPanel::importPwlCsvFile(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    m_pwlPoints.clear(); QTextStream in(&f);
    while(!in.atEnd()) {
        QStringList p = in.readLine().split(QRegularExpression("\\s*,\\s*|\\s+"), Qt::SkipEmptyParts);
        if (p.size() >= 2) m_pwlPoints.push_back({p[0], p[1]});
    }
    return m_pwlPoints.size() >= 2;
}

bool SimulationGeneratorPanel::exportPwlCsvFile(const QString& path) const {
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    QTextStream out(&f); out << "time,value\n";
    for (const auto& p : m_pwlPoints) out << p.first << "," << p.second << "\n";
    return true;
}

void SimulationGeneratorPanel::onGeneratorPresetActivated(int index) {
    if (index < 0 || !m_generatorPresetCombo) return;
    QString tag = m_generatorPresetCombo->itemData(index).toString();
    QVariantMap cfg = tag.startsWith("T:") ? m_generatorTemplates.value(tag.mid(2)) : m_userGeneratorPresets.value(tag.mid(2));
    if (!cfg.isEmpty()) applyGeneratorConfig(cfg);
}

void SimulationGeneratorPanel::onApplyGeneratorToSelection() {
    if (!m_scene) return;
    QString expr = buildGeneratorExpression();
    int applied = 0;
    for (QGraphicsItem* gi : m_scene->selectedItems()) {
        if (auto* v = dynamic_cast<VoltageSourceItem*>(gi)) {
            v->setValue(expr); v->update(); applied++;
        }
    }
    Q_EMIT logMessage(QString("Applied to %1 sources.").arg(applied));
}

void SimulationGeneratorPanel::onGeneratorTypeChanged(int index) {
    Q_UNUSED(index)
    if (!m_generatorType) return;
    const QString type = m_generatorType->currentText();

    auto showParam = [](QLabel* lbl, QLineEdit* edit, const QString& title, const QString& value) {
        lbl->setVisible(true);
        edit->setVisible(true);
        lbl->setText(title);
        if (edit->text().isEmpty()) {
            edit->setText(value);
        }
    };
    auto hideParam = [](QLabel* lbl, QLineEdit* edit) {
        lbl->setVisible(false);
        edit->setVisible(false);
    };

    if (type == "DC") {
        showParam(m_genLabel1, m_genParam1, "Value:", "5");
        hideParam(m_genLabel2, m_genParam2); hideParam(m_genLabel3, m_genParam3);
        hideParam(m_genLabel4, m_genParam4); hideParam(m_genLabel5, m_genParam5);
        hideParam(m_genLabel6, m_genParam6);
    } else if (type == "SIN") {
        showParam(m_genLabel1, m_genParam1, "Offset:", "0");
        showParam(m_genLabel2, m_genParam2, "Amplitude (Peak):", "5");
        showParam(m_genLabel3, m_genParam3, "Freq:", "1k");
        showParam(m_genLabel4, m_genParam4, "Delay:", "0");
        showParam(m_genLabel5, m_genParam5, "Phase:", "0");
        hideParam(m_genLabel6, m_genParam6);
    } else if (type == "PULSE") {
        showParam(m_genLabel1, m_genParam1, "V1:", "0");
        showParam(m_genLabel2, m_genParam2, "V2:", "5");
        showParam(m_genLabel3, m_genParam3, "Delay:", "0");
        showParam(m_genLabel4, m_genParam4, "Rise:", "1u");
        showParam(m_genLabel5, m_genParam5, "Fall:", "1u");
        showParam(m_genLabel6, m_genParam6, "Width:", "500u");
    } else if (type == "PWL") {
        showParam(m_genLabel1, m_genParam1, "T1:", "0");
        showParam(m_genLabel2, m_genParam2, "V1:", "0");
        showParam(m_genLabel3, m_genParam3, "T2:", "1m");
        showParam(m_genLabel4, m_genParam4, "V2:", "5");
        showParam(m_genLabel5, m_genParam5, "T3:", "2m");
        showParam(m_genLabel6, m_genParam6, "V3:", "0");
    }

    bool isPwl = (type == "PWL");
    if (m_pwlEditBtn) m_pwlEditBtn->setVisible(isPwl);
    if (m_pwlImportBtn) m_pwlImportBtn->setVisible(isPwl);
    if (m_pwlExportBtn) m_pwlExportBtn->setVisible(isPwl);
}

void SimulationGeneratorPanel::onOpenPwlEditor() {
    seedDefaultPwlPointsIfNeeded();
    QDialog dlg(this);
    dlg.setWindowTitle("PWL Editor");
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QTableWidget* table = new QTableWidget(static_cast<int>(m_pwlPoints.size()), 2, &dlg);
    table->setHorizontalHeaderLabels({"Time", "Value"});
    for (int i = 0; i < static_cast<int>(m_pwlPoints.size()); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(m_pwlPoints[i].first));
        table->setItem(i, 1, new QTableWidgetItem(m_pwlPoints[i].second));
    }
    layout->addWidget(table);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted) {
        m_pwlPoints.clear();
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* item0 = table->item(r, 0);
            auto* item1 = table->item(r, 1);
            if (item0 && item1) {
                m_pwlPoints.push_back({item0->text(), item1->text()});
            }
        }
    }
}

void SimulationGeneratorPanel::onOpenStepBuilder() {
    QString currentStep;
    if (m_scene) {
        for (auto* gi : m_scene->items()) {
            auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(gi);
            if (!directive) continue;
            if (directive->text().trimmed().startsWith(".step", Qt::CaseInsensitive)) {
                currentStep = directive->text().trimmed();
                break;
            }
        }
    }
    if (currentStep.isEmpty()) {
        Q_EMIT commandLineTextRequested(currentStep);
    }

    SpiceStepDialog dlg(currentStep, m_scene, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString stepCommand = dlg.commandText();
    Q_EMIT commandLineTextChanged(stepCommand);

    if (!m_scene) return;
    SchematicSpiceDirectiveItem* found = nullptr;
    for (auto* gi : m_scene->items()) {
        auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(gi);
        if (!directive) continue;
        if (directive->text().trimmed().startsWith(".step", Qt::CaseInsensitive)) {
            found = directive;
            break;
        }
    }

    if (found) {
        found->setText(stepCommand);
        found->update();
        return;
    }

    QPointF cmdPos(100, 200);
    if (!m_scene->views().isEmpty()) {
        if (auto* view = m_scene->views().first()) {
            cmdPos = view->mapToScene(view->viewport()->rect().center() + QPoint(120, -60));
        }
    }
    auto* cmdItem = new SchematicSpiceDirectiveItem(stepCommand, cmdPos);
    m_scene->addItem(cmdItem);
}

void SimulationGeneratorPanel::onImportPwlCsv() {
    QString path = QFileDialog::getOpenFileName(this, "Import PWL CSV", m_projectDir, "CSV Files (*.csv)");
    if (!path.isEmpty()) importPwlCsvFile(path);
}

void SimulationGeneratorPanel::onExportPwlCsv() {
    QString path = QFileDialog::getSaveFileName(this, "Export PWL CSV", m_projectDir, "CSV Files (*.csv)");
    if (!path.isEmpty()) exportPwlCsvFile(path);
}

void SimulationGeneratorPanel::onSaveGeneratorPreset() {
    QString name = QInputDialog::getText(this, "Save Preset", "Preset Name:");
    if (!name.isEmpty()) {
        m_userGeneratorPresets[name] = collectGeneratorConfig();
        saveUserGeneratorPresets();
        refreshGeneratorPresetCombo();
    }
}

void SimulationGeneratorPanel::onDeleteGeneratorPreset() {
    QString tag = m_generatorPresetCombo->currentData().toString();
    if (tag.startsWith("U:")) {
        m_userGeneratorPresets.remove(tag.mid(2));
        saveUserGeneratorPresets();
        refreshGeneratorPresetCombo();
    }
}
