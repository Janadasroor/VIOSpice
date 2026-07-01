/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_wizard_panel.h"
#include "symbol_canvas.h"
#include "../models/symbol_primitive.h"
#include "theme_manager.h"

#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QMessageBox>
#include <QInputDialog>
#include <QGraphicsItem>
#include <QPainter>
#include <QDate>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

using Flux::Model::SymbolPrimitive;

namespace {

struct WizardTemplateDef {
    QString id;
    QString name;
    QString description;
    QString kind; // "ic_dual", "ic_quad", "logic", "symbol"
    QString defaultCategory;
    QString defaultPrefix;
    QString defaultSymbolName;
    int pins = 0;
    qreal pitch = 10.0;
    qreal width = 50.0;
    QString gate; // and, nand, or, nor, xor, xnor, not, buf
    QJsonObject symbolJson; // used when kind == "symbol"
};

const QList<WizardTemplateDef>& builtinWizardTemplateDefs() {
    auto addDigitalBlockText = [](SymbolDefinition& def, const QString& textValue, const QPointF& pos,
                                  int size, const QString& hAlign = "center", const QString& vAlign = "center") {
        SymbolPrimitive text = SymbolPrimitive::createText(textValue, pos, size, QColor(Qt::black));
        text.data["hAlign"] = hAlign;
        text.data["vAlign"] = vAlign;
        def.addPrimitive(text);
    };

    auto addDigitalMarker = [](SymbolDefinition& def, const QString& pinName, const QPointF& anchor) {
        const QString upper = pinName.trimmed().toUpper();
        if (upper == "CLK" || upper == "CLOCK" || upper == "CK" || upper == "C") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 0)));
        } else if (upper == "EN" || upper == "G" || upper == "GATE") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, -6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(0, -6), anchor + QPointF(0, 6)));
        }
    };

    auto buildDigitalBlockDef = [&](const QString& symbolName,
                                    const QString& description,
                                    const QString& spiceModel,
                                    const QString& label,
                                    const QList<SymbolPrimitive>& pins,
                                    qreal bodyHeight,
                                    bool invertPrimaryOutput = false,
                                    bool invertSecondaryOutput = false) {
        SymbolDefinition def(symbolName);
        def.setDescription(description);
        def.setCategory("Logic");
        def.setReferencePrefix("U");
        def.setSpiceModelName(spiceModel);

        const qreal halfHeight = bodyHeight / 2.0;
        const qreal bodyLeft = -45.0;
        const qreal bodyWidth = 90.0;
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(bodyLeft, -halfHeight, bodyWidth, bodyHeight), false));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(bodyLeft, -halfHeight + 15.0), QPointF(bodyLeft + bodyWidth, -halfHeight + 15.0)));
        addDigitalBlockText(def, spiceModel, QPointF(0, -halfHeight + 7.5), 8);
        addDigitalBlockText(def, label, QPointF(0, 6.0), 10);

        int outputIndex = 0;
        for (const SymbolPrimitive& pin : pins) {
            def.addPrimitive(pin);
            const QString direction = pin.data.value("signalDirection").toString();
            const QString pinName = pin.data.value("name").toString();
            const QPointF pinPos(pin.data.value("x").toDouble(), pin.data.value("y").toDouble());
            if (direction == "input") {
                addDigitalMarker(def, pinName, QPointF(bodyLeft, pinPos.y()));
            } else if (direction == "output") {
                const bool invertThis = (outputIndex == 0) ? invertPrimaryOutput : invertSecondaryOutput;
                if (invertThis) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(bodyLeft + bodyWidth + 4.0, pinPos.y()), 3.0, false));
                }
                ++outputIndex;
            }
        }

        return def;
    };

    auto makeDigitalPin = [](const QPointF& pos, int number, const QString& name,
                             const QString& orientation, const QString& direction) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, number, name, orientation, 15.0);
        pin.data["signalDomain"] = "digital_event";
        pin.data["signalDirection"] = direction;
        return pin;
    };

    static const QList<WizardTemplateDef> defs = {
        {"ic_8pins", "IC 8 Pins (DIP/SOIC)", "Dual-inline 8-pin IC frame", "ic_dual", "IC", "U", "IC8", 8, 10.0, 50.0, ""},
        {"ic_14pins", "IC 14 Pins (DIP/SOIC)", "Dual-inline 14-pin IC frame", "ic_dual", "IC", "U", "IC14", 14, 10.0, 60.0, ""},
        {"ic_16pins", "IC 16 Pins (DIP/SOIC)", "Dual-inline 16-pin IC frame", "ic_dual", "IC", "U", "IC16", 16, 10.0, 65.0, ""},
        {"ic_20pins", "IC 20 Pins (DIP/SOIC)", "Dual-inline 20-pin IC frame", "ic_dual", "IC", "U", "IC20", 20, 10.0, 70.0, ""},
        {"ic_28pins", "IC 28 Pins (DIP/SOIC)", "Dual-inline 28-pin IC frame", "ic_dual", "IC", "U", "IC28", 28, 10.0, 80.0, ""},
        {"ic_40pins", "IC 40 Pins (DIP/SOIC)", "Dual-inline 40-pin IC frame", "ic_dual", "IC", "U", "IC40", 40, 10.0, 95.0, ""},
        {"ic_qfn_32", "IC 32 Pins (QFP/QFN)", "Quad package 32-pin IC frame", "ic_quad", "IC", "U", "IC32", 32, 10.0, 80.0, ""},
        {"and_2", "AND Gate (2-input)", "Digital 2-input AND gate", "logic", "Digital", "U", "AND2", 3, 10.0, 0.0, "and"},
        {"nand_2", "NAND Gate (2-input)", "Digital 2-input NAND gate", "logic", "Digital", "U", "NAND2", 3, 10.0, 0.0, "nand"},
        {"or_2", "OR Gate (2-input)", "Digital 2-input OR gate", "logic", "Digital", "U", "OR2", 3, 10.0, 0.0, "or"},
        {"nor_2", "NOR Gate (2-input)", "Digital 2-input NOR gate", "logic", "Digital", "U", "NOR2", 3, 10.0, 0.0, "nor"},
        {"xor_2", "XOR Gate (2-input)", "Digital 2-input XOR gate", "logic", "Digital", "U", "XOR2", 3, 10.0, 0.0, "xor"},
        {"xnor_2", "XNOR Gate (2-input)", "Digital 2-input XNOR gate", "logic", "Digital", "U", "XNOR2", 3, 10.0, 0.0, "xnor"},
        {"not_1", "NOT Gate (Inverter)", "Digital inverter", "logic", "Digital", "U", "NOT", 2, 10.0, 0.0, "not"},
        {"buf_1", "Buffer Gate", "Digital non-inverting buffer", "logic", "Digital", "U", "BUF", 2, 10.0, 0.0, "buf"},
        {"d_flipflop", "D Flip-Flop", "Edge-triggered D flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "D_FlipFlop", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "D_FlipFlop",
                "Edge-triggered D flip-flop with asynchronous set/reset and complementary outputs",
                "DFF",
                "D FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "D", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"jk_flipflop", "JK Flip-Flop", "Edge-triggered JK flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "JK_FlipFlop", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "JK_FlipFlop",
                "Edge-triggered JK flip-flop with asynchronous set/reset and complementary outputs",
                "JKFF",
                "JK FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "J", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "K", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"t_flipflop", "T Flip-Flop", "Edge-triggered toggle flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "T_FlipFlop", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "T_FlipFlop",
                "Edge-triggered toggle flip-flop with asynchronous set/reset and complementary outputs",
                "TFF",
                "T FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "T", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"sr_flipflop", "SR Flip-Flop", "Edge-triggered set-reset flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "SR_FlipFlop", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "SR_FlipFlop",
                "Edge-triggered set-reset flip-flop with asynchronous set/reset and complementary outputs",
                "SRFF",
                "SR FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "S", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "R", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"d_latch", "D Latch", "Level-sensitive D latch with enable, set/reset, and Q/QN outputs", "symbol", "Logic", "U", "D_Latch", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "D_Latch",
                "Level-sensitive D latch with asynchronous set/reset and complementary outputs",
                "DLATCH",
                "D LAT",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "D", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "EN", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"sr_latch", "SR Latch", "Level-sensitive SR latch with enable, set/reset, and Q/QN outputs", "symbol", "Logic", "U", "SR_Latch", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "SR_Latch",
                "Level-sensitive set-reset latch with enable, asynchronous set/reset, and complementary outputs",
                "SRLATCH",
                "SR LAT",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "S", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "R", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "EN", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
    };
    return defs;
}

QString projectWizardTemplatesPath(const QString& projectKey) {
    const QString trimmed = projectKey.trimmed();
    if (trimmed.isEmpty()) return QString();

    QFileInfo info(trimmed);
    QString projectDir = trimmed;
    if (!info.isDir()) {
        projectDir = info.absolutePath();
    }
    if (projectDir.isEmpty()) return QString();

    return QDir(projectDir).filePath(".viospice/symbol_wizard_templates.json");
}

QString legacyGlobalWizardTemplatesPath() {
    return QDir::home().filePath(".viospice/symbol_wizard_templates.json");
}

QJsonObject wizardTemplateToJson(const WizardTemplateDef& tpl) {
    QJsonObject obj;
    obj["id"] = tpl.id;
    obj["name"] = tpl.name;
    obj["description"] = tpl.description;
    obj["kind"] = tpl.kind;
    obj["defaultCategory"] = tpl.defaultCategory;
    obj["defaultPrefix"] = tpl.defaultPrefix;
    obj["defaultSymbolName"] = tpl.defaultSymbolName;
    obj["pins"] = tpl.pins;
    obj["pitch"] = tpl.pitch;
    obj["width"] = tpl.width;
    obj["gate"] = tpl.gate;
    if (tpl.kind == "symbol" && !tpl.symbolJson.isEmpty()) {
        obj["symbol"] = tpl.symbolJson;
    }
    return obj;
}

bool wizardTemplateFromJson(const QJsonObject& obj, WizardTemplateDef& out) {
    const QString id = obj.value("id").toString().trimmed();
    const QString name = obj.value("name").toString().trimmed();
    const QString kind = obj.value("kind").toString().trimmed().toLower();
    if (id.isEmpty() || name.isEmpty() || kind.isEmpty()) return false;

    out.id = id;
    out.name = name;
    out.description = obj.value("description").toString();
    out.kind = kind;
    out.defaultCategory = obj.value("defaultCategory").toString("IC");
    out.defaultPrefix = obj.value("defaultPrefix").toString("U");
    out.defaultSymbolName = obj.value("defaultSymbolName").toString(name);
    out.pins = qMax(0, obj.value("pins").toInt(0));
    out.pitch = obj.value("pitch").toDouble(10.0);
    out.width = obj.value("width").toDouble(50.0);
    out.gate = obj.value("gate").toString().toLower();
    out.symbolJson = obj.value("symbol").toObject();
    return true;
}

void ensureProjectWizardTemplatesFile(const QString& projectKey) {
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty() || QFileInfo::exists(path)) return;

    const QFileInfo outInfo(path);
    QDir().mkpath(outInfo.absolutePath());

    QJsonArray arr;
    const QList<WizardTemplateDef>& defaults = builtinWizardTemplateDefs();
    for (const WizardTemplateDef& tpl : defaults) {
        arr.append(wizardTemplateToJson(tpl));
    }

    QJsonObject root;
    root["version"] = 1;
    root["templates"] = arr;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
}

QList<WizardTemplateDef> wizardTemplateDefsForProject(const QString& projectKey) {
    QList<WizardTemplateDef> defs = builtinWizardTemplateDefs();
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty()) return defs;

    ensureProjectWizardTemplatesFile(projectKey);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return defs;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return defs;

    const QJsonArray arr = doc.object().value("templates").toArray();
    if (arr.isEmpty()) return defs;

    QMap<QString, int> byId;
    for (int i = 0; i < defs.size(); ++i) byId[defs[i].id] = i;

    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        WizardTemplateDef parsed;
        if (!wizardTemplateFromJson(v.toObject(), parsed)) continue;
        if (byId.contains(parsed.id)) {
            defs[byId.value(parsed.id)] = parsed;
        } else {
            byId[parsed.id] = defs.size();
            defs.append(parsed);
        }
    }

    return defs;
}

const WizardTemplateDef* findWizardTemplate(const QString& id, const QList<WizardTemplateDef>& defs) {
    if (id.trimmed().isEmpty()) return nullptr;
    for (const WizardTemplateDef& def : defs) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

QString sanitizeWizardTemplateId(const QString& text) {
    QString id = text.trimmed().toLower();
    id.replace(QRegularExpression("[^a-z0-9]+"), "_");
    while (id.contains("__")) id.replace("__", "_");
    if (id.startsWith('_')) id.remove(0, 1);
    if (id.endsWith('_')) id.chop(1);
    if (id.isEmpty()) id = "custom_symbol";
    if (!id.startsWith("custom_")) id = "custom_" + id;
    return id;
}

QString uniqueWizardTemplateId(const QString& projectKey, const QString& preferredId) {
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(projectKey);
    QSet<QString> usedIds;
    for (const WizardTemplateDef& tpl : defs) usedIds.insert(tpl.id);

    if (!usedIds.contains(preferredId)) return preferredId;
    for (int i = 2; i < 100000; ++i) {
        const QString candidate = QString("%1_%2").arg(preferredId).arg(i);
        if (!usedIds.contains(candidate)) return candidate;
    }
    return preferredId + "_" + QString::number(QDateTime::currentSecsSinceEpoch());
}

bool upsertWizardTemplate(const QString& projectKey, const WizardTemplateDef& tpl, QString* errorOut = nullptr) {
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty()) {
        if (errorOut) *errorOut = "Wizard template path is empty.";
        return false;
    }

    ensureProjectWizardTemplatesFile(projectKey);

    QJsonObject root;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) root = doc.object();
    }

    root["version"] = 1;
    QJsonArray arr = root.value("templates").toArray();
    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        if (obj.value("id").toString() == tpl.id) {
            arr[i] = wizardTemplateToJson(tpl);
            replaced = true;
            break;
        }
    }
    if (!replaced) arr.append(wizardTemplateToJson(tpl));
    root["templates"] = arr;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut) *errorOut = QString("Failed to write template file:\n%1").arg(path);
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return true;
}

SymbolDefinition buildLogicTemplateSymbol(const WizardTemplateDef& tpl,
                                          const QString& symbolName,
                                          const QString& prefix,
                                          const QString& category) {
    SymbolDefinition def;
    def.setName(symbolName);
    def.setReferencePrefix(prefix);
    def.setCategory(category);
    def.setDescription(tpl.description);

    const QString gate = tpl.gate.toLower();
    const bool unary = (gate == "not" || gate == "buf");
    const bool inverted = (gate == "nand" || gate == "nor" || gate == "xnor" || gate == "not");
    const QString displayLabel = gate.toUpper();
    const QString modelLabel = (gate == "buf") ? QString("BUF") : (gate == "not" ? QString("NOT") : displayLabel);
    const qreal bodyHeight = unary ? 60.0 : 60.0;
    const qreal halfHeight = bodyHeight / 2.0;
    const qreal bodyLeft = -45.0;
    const qreal bodyWidth = 90.0;

    auto makeLogicPin = [](const QPointF& pos, int number, const QString& name,
                           const QString& orientation, const QString& direction, qreal length = 15.0) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, number, name, orientation, length);
        pin.data["signalDomain"] = "digital_event";
        pin.data["signalDirection"] = direction;
        return pin;
    };

    def.addPrimitive(SymbolPrimitive::createRect(QRectF(bodyLeft, -halfHeight, bodyWidth, bodyHeight), false));
    def.addPrimitive(SymbolPrimitive::createLine(QPointF(bodyLeft, -halfHeight + 15.0), QPointF(bodyLeft + bodyWidth, -halfHeight + 15.0)));

    SymbolPrimitive modelText = SymbolPrimitive::createText(modelLabel, QPointF(0, -halfHeight + 7.5), 8, QColor(Qt::black));
    modelText.data["hAlign"] = "center";
    modelText.data["vAlign"] = "center";
    def.addPrimitive(modelText);

    SymbolPrimitive labelText = SymbolPrimitive::createText(displayLabel, QPointF(0, 6.0), 10, QColor(Qt::black));
    labelText.data["hAlign"] = "center";
    labelText.data["vAlign"] = "center";
    def.addPrimitive(labelText);

    if (unary) {
        def.addPrimitive(makeLogicPin(QPointF(-60, 0.0), 1, "A", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(60, 0.0), 2, "Y", "Left", "output", inverted ? 17.0 : 15.0));
    } else {
        def.addPrimitive(makeLogicPin(QPointF(-60, -15.0), 1, "A", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(-60, 15.0), 2, "B", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(60, 0.0), 3, "Y", "Left", "output", inverted ? 17.0 : 15.0));
    }

    if (inverted) {
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(bodyLeft + bodyWidth + 4.0, 0.0), 3.0, false));
    }

    return def;
}

SymbolDefinition buildIcTemplateSymbol(const WizardTemplateDef& tpl,
                                       const QString& symbolName,
                                       const QString& prefix,
                                       const QString& category) {
    SymbolDefinition def;
    def.setName(symbolName);
    def.setReferencePrefix(prefix);
    def.setCategory(category);
    def.setDescription(tpl.description);

    const bool quad = (tpl.kind == "ic_quad");
    const int pins = qMax(quad ? 4 : 2, tpl.pins);
    const qreal pitch = tpl.pitch > 0.0 ? tpl.pitch : 10.0;
    const qreal width = tpl.width > 0.0 ? tpl.width : 50.0;

    if (!quad) {
        const int half = qMax(1, pins / 2);
        const qreal bodyHeight = qMax(2.0 * pitch, half * pitch + pitch);
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(-width / 2.0, -bodyHeight / 2.0, width, bodyHeight), false));

        for (int i = 0; i < half; ++i) {
            const qreal y = -bodyHeight / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-width / 2.0 - 15.0, y), i + 1, QString::number(i + 1), "Right", 15.0));
        }
        for (int i = 0; i < half; ++i) {
            const qreal y = bodyHeight / 2.0 - pitch - i * pitch;
            const int n = half + i + 1;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(width / 2.0 + 15.0, y), n, QString::number(n), "Left", 15.0));
        }
    } else {
        const int perSide = qMax(1, pins / 4);
        const qreal side = qMax(2.0 * pitch, perSide * pitch + pitch);
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(-side / 2.0, -side / 2.0, side, side), false));

        int pinNum = 1;
        for (int i = 0; i < perSide; ++i) {
            const qreal y = -side / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-side / 2.0 - 15.0, y), pinNum, QString::number(pinNum), "Right", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal x = -side / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(x, side / 2.0 + 15.0), pinNum, QString::number(pinNum), "Up", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal y = side / 2.0 - pitch - i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(side / 2.0 + 15.0, y), pinNum, QString::number(pinNum), "Left", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal x = side / 2.0 - pitch - i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(x, -side / 2.0 - 15.0), pinNum, QString::number(pinNum), "Down", 15.0));
            ++pinNum;
        }
    }

    return def;
}

} // namespace

SymbolWizardPanel::SymbolWizardPanel(SymbolCanvas* canvas, QWidget* parent)
    : QWidget(parent)
    , m_canvas(canvas) {
    setupUI();
    refreshWizardTemplateList();
    if (m_wizardTemplateCombo->count() > 0) {
        m_wizardTemplateCombo->setCurrentIndex(0);
    }
    onWizardApplyTemplate();
    updateWizardTemplatePreview();
}

void SymbolWizardPanel::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // 1. Template Browser Group
    auto* browseGroup = new QGroupBox("Template Browser", this);
    auto* browseLayout = new QVBoxLayout(browseGroup);
    auto* browseForm = new QFormLayout();

    m_wizardTemplateSearchEdit = new QLineEdit(this);
    m_wizardTemplateSearchEdit->setPlaceholderText("Search templates (AND, NAND, XOR, IC, custom...)");
    m_wizardTemplateSearchEdit->setClearButtonEnabled(true);

    m_wizardTemplateCombo = new QComboBox(this);
    m_wizardTemplateCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_wizardTemplateCombo->setMinimumContentsLength(16);

    browseForm->addRow("Search:", m_wizardTemplateSearchEdit);
    browseForm->addRow("Template:", m_wizardTemplateCombo);
    browseLayout->addLayout(browseForm);

    m_wizardTemplateInfoLabel = new QLabel("No template selected", this);
    m_wizardTemplateInfoLabel->setStyleSheet("font-weight: 600;");
    browseLayout->addWidget(m_wizardTemplateInfoLabel);

    m_wizardTemplateDescLabel = new QLabel(this);
    m_wizardTemplateDescLabel->setWordWrap(true);
    m_wizardTemplateDescLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    browseLayout->addWidget(m_wizardTemplateDescLabel);

    m_wizardPreviewScene = new QGraphicsScene(this);
    m_wizardPreviewView = new QGraphicsView(m_wizardPreviewScene, this);
    m_wizardPreviewView->setMinimumHeight(180);
    m_wizardPreviewView->setRenderHint(QPainter::Antialiasing, true);
    m_wizardPreviewView->setFrameShape(QFrame::StyledPanel);
    m_wizardPreviewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wizardPreviewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wizardPreviewView->setDragMode(QGraphicsView::NoDrag);
    browseLayout->addWidget(m_wizardPreviewView);

    mainLayout->addWidget(browseGroup);

    // 2. Manual Generator Settings Group
    auto* genGroup = new QGroupBox("Manual Generator Settings", this);
    auto* genForm = new QFormLayout(genGroup);

    m_wizardStyleCombo = new QComboBox(this);
    m_wizardStyleCombo->addItems({"Dual (DIP/SOIC)", "Quad (QFP/QFN)"});
    genForm->addRow("Style:", m_wizardStyleCombo);

    m_pinCountSpin = new QSpinBox(this);
    m_pinCountSpin->setRange(2, 512);
    m_pinCountSpin->setValue(8);
    genForm->addRow("Pins:", m_pinCountSpin);

    m_pinSpacingSpin = new QDoubleSpinBox(this);
    m_pinSpacingSpin->setRange(1, 50);
    m_pinSpacingSpin->setSingleStep(2.54);
    m_pinSpacingSpin->setValue(10.0);
    m_pinSpacingSpin->setSuffix(" units");
    genForm->addRow("Spacing:", m_pinSpacingSpin);

    m_bodyWidthSpin = new QDoubleSpinBox(this);
    m_bodyWidthSpin->setRange(5, 500);
    m_bodyWidthSpin->setValue(50.0);
    m_bodyWidthSpin->setSuffix(" units");
    genForm->addRow("Width:", m_bodyWidthSpin);

    mainLayout->addWidget(genGroup);

    // Buttons
    auto* applyTplBtn = new QPushButton("Apply Template", this);
    connect(applyTplBtn, &QPushButton::clicked, this, &SymbolWizardPanel::onWizardApplyTemplate);
    mainLayout->addWidget(applyTplBtn);

    auto* importSubcktBtn2 = new QPushButton("Import SPICE Subcircuit", this);
    connect(importSubcktBtn2, &QPushButton::clicked, this, &SymbolWizardPanel::importSpiceSubcircuitRequested);
    mainLayout->addWidget(importSubcktBtn2);

    mainLayout->addWidget(new QLabel("--- or ---", this));

    auto* wizBtn = new QPushButton("Generate Symbol", this);
    connect(wizBtn, &QPushButton::clicked, this, &SymbolWizardPanel::onWizardGenerate);
    mainLayout->addWidget(wizBtn);

    auto* saveTplBtn = new QPushButton("Save Current as Template", this);
    connect(saveTplBtn, &QPushButton::clicked, this, &SymbolWizardPanel::onWizardSaveTemplate);
    mainLayout->addWidget(saveTplBtn);

    auto* importBtn = new QPushButton("Import KiCad Symbol", this);
    connect(importBtn, &QPushButton::clicked, this, &SymbolWizardPanel::importKicadSymbolRequested);
    mainLayout->addWidget(importBtn);

    auto* importLtBtn = new QPushButton("Import LTspice Symbol", this);
    connect(importLtBtn, &QPushButton::clicked, this, &SymbolWizardPanel::importLtspiceSymbolRequested);
    mainLayout->addWidget(importLtBtn);

    mainLayout->addStretch();

    // Connections
    connect(m_wizardTemplateSearchEdit, &QLineEdit::textChanged,
            this, &SymbolWizardPanel::onWizardTemplateSearchChanged);
    connect(m_wizardTemplateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onWizardApplyTemplate(); });
}

void SymbolWizardPanel::setProjectKey(const QString& key) {
    m_projectKey = key.trimmed();
    if (!m_projectKey.isEmpty()) {
        const QString projectTpl = projectWizardTemplatesPath(m_projectKey);
        const QString legacyTpl = legacyGlobalWizardTemplatesPath();
        if (!projectTpl.isEmpty() && !QFileInfo::exists(projectTpl) && QFileInfo::exists(legacyTpl)) {
            QDir().mkpath(QFileInfo(projectTpl).absolutePath());
            QFile::copy(legacyTpl, projectTpl);
        }
        ensureProjectWizardTemplatesFile(m_projectKey);
    }
    refreshWizardTemplateList(m_wizardTemplateSearchEdit ? m_wizardTemplateSearchEdit->text() : QString());
    onWizardApplyTemplate();
}

void SymbolWizardPanel::refreshWizardTemplateList(const QString& query) {
    if (!m_wizardTemplateCombo) return;

    const QString selectedId = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const QString q = query.trimmed().toLower();
    m_wizardTemplateCombo->clear();

    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    for (const WizardTemplateDef& tpl : defs) {
        const QString haystack = (tpl.name + " " + tpl.id + " " + tpl.description + " " + tpl.defaultCategory).toLower();
        if (!q.isEmpty() && !haystack.contains(q)) continue;
        m_wizardTemplateCombo->addItem(QString("%1  [%2]").arg(tpl.name, tpl.defaultCategory), tpl.id);
    }

    if (m_wizardTemplateCombo->count() == 0) {
        if (m_wizardTemplateInfoLabel) m_wizardTemplateInfoLabel->setText("No templates match search.");
        if (m_wizardTemplateDescLabel) m_wizardTemplateDescLabel->clear();
        updateWizardTemplatePreview();
        return;
    }
    const int restoreIdx = m_wizardTemplateCombo->findData(selectedId, Qt::UserRole);
    m_wizardTemplateCombo->setCurrentIndex(restoreIdx >= 0 ? restoreIdx : 0);
    updateWizardTemplatePreview();
}

void SymbolWizardPanel::onWizardTemplateSearchChanged(const QString& text) {
    refreshWizardTemplateList(text);
}

void SymbolWizardPanel::onWizardApplyTemplate() {
    if (!m_wizardTemplateCombo || m_wizardTemplateCombo->count() == 0) return;
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const QString id = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const WizardTemplateDef* tpl = findWizardTemplate(id, defs);
    if (!tpl) return;

    if (tpl->kind == "ic_dual") {
        m_wizardStyleCombo->setCurrentText("Dual (DIP/SOIC)");
        m_pinCountSpin->setValue(qMax(2, tpl->pins));
        m_pinSpacingSpin->setValue(tpl->pitch);
        m_bodyWidthSpin->setValue(tpl->width);
    } else if (tpl->kind == "ic_quad") {
        m_wizardStyleCombo->setCurrentText("Quad (QFP/QFN)");
        m_pinCountSpin->setValue(qMax(4, tpl->pins));
        m_pinSpacingSpin->setValue(tpl->pitch);
        m_bodyWidthSpin->setValue(tpl->width);
    }

    if (m_wizardTemplateInfoLabel) {
        m_wizardTemplateInfoLabel->setText(
            QString("%1  |  %2  |  %3 pins")
                .arg(tpl->name, tpl->defaultCategory, QString::number(qMax(0, tpl->pins))));
    }
    if (m_wizardTemplateDescLabel) {
        m_wizardTemplateDescLabel->setText(tpl->description.trimmed().isEmpty()
            ? QString("Template ID: %1").arg(tpl->id)
            : QString("%1\nID: %2").arg(tpl->description, tpl->id));
    }

    Q_EMIT templateApplied(tpl->defaultSymbolName, tpl->defaultPrefix, tpl->defaultCategory);

    updateWizardTemplatePreview();
}

void SymbolWizardPanel::updateWizardTemplatePreview() {
    if (!m_wizardPreviewScene || !m_wizardPreviewView) return;
    m_wizardPreviewScene->clear();

    if (!m_wizardTemplateCombo || m_wizardTemplateCombo->count() == 0) {
        return;
    }

    const QString id = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const WizardTemplateDef* tpl = findWizardTemplate(id, defs);
    if (!tpl) return;

    SymbolDefinition previewDef;
    if (tpl->kind == "logic") {
        previewDef = buildLogicTemplateSymbol(*tpl,
                                             tpl->defaultSymbolName.isEmpty() ? tpl->name : tpl->defaultSymbolName,
                                             tpl->defaultPrefix.isEmpty() ? "U" : tpl->defaultPrefix,
                                             tpl->defaultCategory.isEmpty() ? "Digital" : tpl->defaultCategory);
    } else if (tpl->kind == "symbol" && !tpl->symbolJson.isEmpty()) {
        previewDef = SymbolDefinition::fromJson(tpl->symbolJson);
    } else {
        previewDef = buildIcTemplateSymbol(*tpl,
                                           tpl->defaultSymbolName.isEmpty() ? tpl->name : tpl->defaultSymbolName,
                                           tpl->defaultPrefix.isEmpty() ? "U" : tpl->defaultPrefix,
                                           tpl->defaultCategory.isEmpty() ? "IC" : tpl->defaultCategory);
    }

    for (const SymbolPrimitive& prim : previewDef.primitives()) {
        if (QGraphicsItem* item = m_canvas->buildVisual(prim, -1)) {
            m_wizardPreviewScene->addItem(item);
        }
    }

    QRectF bounds = m_wizardPreviewScene->itemsBoundingRect();
    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = QRectF(-40, -30, 80, 60);
    }
    bounds = bounds.adjusted(-12, -12, 12, 12);
    m_wizardPreviewScene->setSceneRect(bounds);
    m_wizardPreviewView->fitInView(bounds, Qt::KeepAspectRatio);
}

void SymbolWizardPanel::onWizardGenerate() {
    const QString templateId = m_wizardTemplateCombo
        ? m_wizardTemplateCombo->currentData(Qt::UserRole).toString()
        : QString();
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const WizardTemplateDef* tpl = findWizardTemplate(templateId, defs);

    SymbolDefinition newDef;

    if (tpl && tpl->kind == "symbol" && !tpl->symbolJson.isEmpty()) {
        newDef = SymbolDefinition::fromJson(tpl->symbolJson);
        Q_EMIT symbolGenerated(newDef);
        return;
    }

    if (tpl && tpl->kind == "logic") {
        newDef = buildLogicTemplateSymbol(*tpl, tpl->defaultSymbolName, tpl->defaultPrefix, tpl->defaultCategory);
        Q_EMIT symbolGenerated(newDef);
        return;
    }

    const int    pins  = m_pinCountSpin->value();
    const qreal  pitch = m_pinSpacingSpin->value();
    const qreal  bW    = m_bodyWidthSpin->value();
    const QString style = m_wizardStyleCombo->currentText();

    if (style.startsWith("Dual")) {
        const int   half    = pins / 2;
        const qreal bHeight = qMax(2.0 * pitch, half * pitch + pitch);

        newDef.addPrimitive(SymbolPrimitive::createRect(
            QRectF(-bW/2, -bHeight/2, bW, bHeight), false));

        for (int i = 0; i < half; ++i) {
            qreal y = -bHeight/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-bW/2 - 15, y), i + 1, QString::number(i + 1), "Right", 15));
        }
        for (int i = 0; i < half; ++i) {
            qreal y = bHeight/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(bW/2 + 15, y), half + i + 1,
                QString::number(half + i + 1), "Left", 15));
        }
        if (pins % 2 != 0) {
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-bW/2 - 15, bHeight/2 - pitch - half * pitch),
                pins, QString::number(pins), "Right", 15));
        }
    } else {
        // Quad
        const int   perSide = qMax(1, pins / 4);
        const qreal side    = qMax(2.0 * pitch, perSide * pitch + pitch);

        newDef.addPrimitive(SymbolPrimitive::createRect(
            QRectF(-side/2, -side/2, side, side), false));

        int pinNum = 1;
        // Left
        for (int i = 0; i < perSide; ++i) {
            qreal y = -side/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-side/2 - 15, y), pinNum, QString::number(pinNum), "Right", 15));
            ++pinNum;
        }
        // Bottom
        for (int i = 0; i < perSide; ++i) {
            qreal x = -side/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(x, side/2 + 15), pinNum, QString::number(pinNum), "Up", 15));
            ++pinNum;
        }
        // Right
        for (int i = 0; i < perSide; ++i) {
            qreal y = side/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(side/2 + 15, y), pinNum, QString::number(pinNum), "Left", 15));
            ++pinNum;
        }
        // Top
        for (int i = 0; i < perSide; ++i) {
            qreal x = side/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(x, -side/2 - 15), pinNum, QString::number(pinNum), "Down", 15));
            ++pinNum;
        }
    }

    Q_EMIT symbolGenerated(newDef);
}

void SymbolWizardPanel::onWizardSaveTemplate() {
    Q_EMIT saveCurrentAsTemplateRequested();
}

void SymbolWizardPanel::saveTemplate(const SymbolDefinition& def) {
    if (def.primitives().isEmpty()) {
        QMessageBox::warning(this, "Save Wizard Template", "Current symbol is empty.");
        return;
    }

    const QString defaultName = def.name().trimmed().isEmpty()
        ? QStringLiteral("Custom Symbol")
        : def.name().trimmed();

    bool ok = false;
    const QString templateName = QInputDialog::getText(
        this,
        "Save Wizard Template",
        "Template name:",
        QLineEdit::Normal,
        defaultName,
        &ok).trimmed();
    if (!ok || templateName.isEmpty()) return;

    SymbolDefinition current = def;
    current.setName(templateName);

    int pinCount = 0;
    for (const SymbolPrimitive& prim : current.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++pinCount;
    }

    WizardTemplateDef tpl;
    tpl.id = uniqueWizardTemplateId(m_projectKey, sanitizeWizardTemplateId(templateName));
    tpl.name = templateName;
    tpl.description = current.description().trimmed().isEmpty()
        ? QString("Saved template from symbol \"%1\"").arg(current.name())
        : current.description();
    tpl.kind = "symbol";
    tpl.defaultCategory = current.category().trimmed().isEmpty() ? "Custom" : current.category().trimmed();
    tpl.defaultPrefix = current.referencePrefix().trimmed().isEmpty() ? "U" : current.referencePrefix().trimmed();
    tpl.defaultSymbolName = current.name().trimmed().isEmpty() ? templateName : current.name().trimmed();
    tpl.pins = pinCount;
    tpl.symbolJson = current.toJson();

    QString error;
    if (!upsertWizardTemplate(m_projectKey, tpl, &error)) {
        QMessageBox::critical(this, "Save Wizard Template",
                               error.isEmpty() ? "Failed to save template." : error);
        return;
    }

    refreshWizardTemplateList(m_wizardTemplateSearchEdit ? m_wizardTemplateSearchEdit->text() : QString());
    if (m_wizardTemplateCombo) {
        const int idx = m_wizardTemplateCombo->findData(tpl.id, Qt::UserRole);
        if (idx >= 0) m_wizardTemplateCombo->setCurrentIndex(idx);
    }

    Q_EMIT templateSaved(tpl.name, current);
}
