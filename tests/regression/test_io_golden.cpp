/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

#include <iostream>

#if __has_include("pcb/factories/pcb_item_registry.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "../../schematic/items/avr_microcontroller_item.h"
#include "../../schematic/items/voltage_source_item.h"
#include "../../schematic/items/generic_component_item.h"

namespace {

QJsonValue sanitizeValue(const QJsonValue& value);

QJsonObject sanitizeObject(QJsonObject object) {
    static const QSet<QString> volatileKeys = {
        "id",
        "createdAt",
        "modifiedAt",
        "exportedAt"
    };

    for (auto it = object.begin(); it != object.end();) {
        if (volatileKeys.contains(it.key())) {
            it = object.erase(it);
            continue;
        }

        it.value() = sanitizeValue(it.value());
        ++it;
    }
    return object;
}

QJsonArray sanitizeArray(const QJsonArray& array) {
    QJsonArray out;
    for (const QJsonValue& v : array) {
        out.append(sanitizeValue(v));
    }
    return out;
}

QJsonValue sanitizeValue(const QJsonValue& value) {
    if (value.isObject()) {
        return sanitizeObject(value.toObject());
    }
    if (value.isArray()) {
        return sanitizeArray(value.toArray());
    }
    return value;
}

QString itemSortKey(const QJsonObject& item) {
    const QString type = item.value("type").toString();
    const QString ref = item.value("reference").toString();
    const QString name = item.value("name").toString();
    const QString value = item.value("value").toString();
    const double x = item.value("x").toDouble();
    const double y = item.value("y").toDouble();
    return QString("%1|%2|%3|%4|%5|%6")
        .arg(type, ref, name, value)
        .arg(x, 0, 'f', 6)
        .arg(y, 0, 'f', 6);
}

void normalizeItems(QJsonObject& root) {
    if (!root.contains("items") || !root.value("items").isArray()) {
        return;
    }

    const QJsonArray arr = root.value("items").toArray();
    std::vector<QJsonObject> items;
    items.reserve(static_cast<size_t>(arr.size()));

    for (const QJsonValue& value : arr) {
        if (value.isObject()) {
            items.push_back(value.toObject());
        }
    }

    std::sort(items.begin(), items.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return itemSortKey(a) < itemSortKey(b);
    });

    QJsonArray normalized;
    for (const QJsonObject& obj : items) {
        normalized.append(obj);
    }
    root["items"] = normalized;
}

bool verifyPcbRoundTrip(const QString& fixturesDir, QString& err) {
#if !VIOSPICE_HAS_PCB
    Q_UNUSED(fixturesDir);
    err.clear();
    return true;
#else
    const QString fixture = QDir(fixturesDir).filePath("test_fix.pcb");

    QGraphicsScene sceneA;
    if (!PCBFileIO::loadPCB(&sceneA, fixture)) {
        err = QString("PCB load failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    if (sceneA.items().isEmpty()) {
        err = "PCB fixture loaded with zero items";
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err = "failed to create temporary directory for PCB round-trip";
        return false;
    }

    const QString outputFile = QDir(tempDir.path()).filePath("roundtrip.pcb");
    if (!PCBFileIO::savePCB(&sceneA, outputFile)) {
        err = QString("PCB save failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    QGraphicsScene sceneB;
    if (!PCBFileIO::loadPCB(&sceneB, outputFile)) {
        err = QString("PCB round-trip reload failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    QJsonObject expected = PCBFileIO::serializeSceneToJson(&sceneA);
    QJsonObject actual = PCBFileIO::serializeSceneToJson(&sceneB);

    expected = sanitizeObject(expected);
    actual = sanitizeObject(actual);
    normalizeItems(expected);
    normalizeItems(actual);

    if (expected != actual) {
        err = "PCB golden round-trip mismatch";
        return false;
    }

    return true;
#endif
}

bool verifySchematicRoundTrip(const QString& fixturesDir, QString& err) {
    const QString fixture = QDir(fixturesDir).filePath("untitled.sch");

    QGraphicsScene sceneA;
    QString pageSizeA;
    TitleBlockData titleBlock;
    QString script;
    QMap<QString, QList<QString>> busAliases;
    QSet<QString> ercExclusions;

    if (!SchematicFileIO::loadSchematic(&sceneA, fixture, pageSizeA, titleBlock, &script, &busAliases, &ercExclusions)) {
        err = QString("Schematic load failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    if (sceneA.items().isEmpty()) {
        err = "Schematic fixture loaded with zero items";
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err = "failed to create temporary directory for schematic round-trip";
        return false;
    }

    const QString outputFile = QDir(tempDir.path()).filePath("roundtrip.sch");
    if (!SchematicFileIO::saveSchematic(&sceneA, outputFile, pageSizeA, script, titleBlock, busAliases, ercExclusions)) {
        err = QString("Schematic save failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    QGraphicsScene sceneB;
    QString pageSizeB;
    TitleBlockData titleBlockB;
    QString scriptB;
    QMap<QString, QList<QString>> busAliasesB;
    QSet<QString> ercExclusionsB;
    if (!SchematicFileIO::loadSchematic(&sceneB, outputFile, pageSizeB, titleBlockB, &scriptB, &busAliasesB, &ercExclusionsB)) {
        err = QString("Schematic round-trip reload failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    QJsonObject expected = SchematicFileIO::serializeSceneToJson(&sceneA, pageSizeA);
    QJsonObject actual = SchematicFileIO::serializeSceneToJson(&sceneB, pageSizeB);

    expected = sanitizeObject(expected);
    actual = sanitizeObject(actual);
    normalizeItems(expected);
    normalizeItems(actual);

    if (expected != actual) {
        err = "Schematic golden round-trip mismatch";
        return false;
    }

    return true;
}

// External file references (AVR firmware, PWL files, symbol model paths, ...)
// must be stored portably: forward slashes, and relative to the schematic
// directory when the target lives under it. On load, Windows-style backslash
// paths must be converted to forward slashes so files round-trip across
// macOS / Windows / Linux.
bool verifyPortablePathSave(QString& err) {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err = "failed to create temporary directory for path portability";
        return false;
    }

    const QString projDir = QDir(tempDir.path()).filePath("proj");
    QDir().mkpath(QDir(projDir).filePath("fw"));

    QFile fw(QDir(projDir).filePath("fw/blink.hex"));
    if (!fw.open(QIODevice::WriteOnly)) {
        err = "failed to create firmware fixture";
        return false;
    }
    fw.write(":00000001FF\n");
    fw.close();

    QGraphicsScene scene;
    auto* avr = new AvrMicrocontrollerItem();
    avr->setPos(10, 10);
    avr->setFirmwarePath(QDir(projDir).filePath("fw/blink.hex"));
    scene.addItem(avr);

    auto* vs = new VoltageSourceItem(QPointF(100, 100), "V1", VoltageSourceItem::DC);
    vs->setPwlFile(QDir(projDir).filePath("data/pwl.txt"));
    scene.addItem(vs);

    const QString schPath = QDir(projDir).filePath("circuit.flxsch");
    if (!SchematicFileIO::saveSchematic(&scene, schPath)) {
        err = QString("schematic save failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    QFile f(schPath);
    if (!f.open(QIODevice::ReadOnly)) {
        err = "failed to reopen saved schematic";
        return false;
    }
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QString fwStored, fwParamStored, pwlStored;
    const QJsonArray items = root["items"].toArray();
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        if (o.contains("firmwarePath")) {
            fwStored = o["firmwarePath"].toString();
            fwParamStored = o["paramExpressions"].toObject()["firmwarePath"].toString();
        }
        if (o.contains("pwlFile")) {
            pwlStored = o["pwlFile"].toString();
        }
    }

    if (fwStored != "fw/blink.hex") {
        err = "firmwarePath was not stored relative to schematic dir: " + fwStored;
        return false;
    }
    if (fwParamStored != "fw/blink.hex") {
        err = "paramExpressions firmwarePath was not stored relative: " + fwParamStored;
        return false;
    }
    if (pwlStored != "data/pwl.txt") {
        err = "pwlFile was not stored relative to schematic dir: " + pwlStored;
        return false;
    }

    QGraphicsScene sceneB;
    QString pageSize;
    TitleBlockData titleBlock;
    if (!SchematicFileIO::loadSchematic(&sceneB, schPath, pageSize, titleBlock)) {
        err = QString("schematic reload failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }
    bool avrReloaded = false;
    for (QGraphicsItem* item : sceneB.items()) {
        if (auto* a = dynamic_cast<AvrMicrocontrollerItem*>(item)) {
            avrReloaded = true;
            if (a->firmwarePath() != "fw/blink.hex") {
                err = "reloaded firmwarePath mismatch: " + a->firmwarePath();
                return false;
            }
        }
    }
    if (!avrReloaded) {
        err = "AVR item missing after reload";
        return false;
    }

    return true;
}

bool verifyLoadSeparatorNormalization(QString& err) {
    QJsonObject root;
    QJsonArray items;

    QJsonObject avr;
    avr["type"] = "AvrMicrocontroller";
    avr["x"] = 0;
    avr["y"] = 0;
    avr["firmwarePath"] = "C:\\Users\\me\\fw\\blink.hex";
    QJsonObject pe;
    pe["firmwarePath"] = "C:\\Users\\me\\fw\\blink.hex";
    avr["paramExpressions"] = pe;
    items.append(avr);

    QJsonObject gci;
    gci["type"] = "GenericComponent";
    gci["x"] = 50;
    gci["y"] = 50;
    QJsonObject sym;
    sym["name"] = "LM317";
    sym["modelPath"] = "sub\\models\\LM317.lib";
    gci["symbolDef"] = sym;
    items.append(gci);

    root["items"] = items;

    QGraphicsScene scene;
    if (!SchematicFileIO::loadSchematicFromJson(&scene, root)) {
        err = QString("loadSchematicFromJson failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    bool avrOk = false, gciOk = false;
    for (QGraphicsItem* it : scene.items()) {
        if (auto* a = dynamic_cast<AvrMicrocontrollerItem*>(it)) {
            avrOk = true;
            if (a->firmwarePath() != "C:/Users/me/fw/blink.hex") {
                err = "AVR firmwarePath not normalized on load: " + a->firmwarePath();
                return false;
            }
        } else if (auto* g = dynamic_cast<GenericComponentItem*>(it)) {
            gciOk = true;
            if (g->symbol().modelPath() != "sub/models/LM317.lib") {
                err = "symbol modelPath not normalized on load: " + g->symbol().modelPath();
                return false;
            }
        }
    }
    if (!avrOk || !gciOk) {
        err = "expected items not found after loadSchematicFromJson";
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString fixturesDir = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                           : QStringLiteral("tests/regression/fixtures");

    #if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
    #endif
    SchematicItemRegistry::registerBuiltInItems();

    QString err;
    if (!verifyPcbRoundTrip(fixturesDir, err)) {
        std::cerr << "[FAIL] PCB regression: " << err.toStdString() << std::endl;
        return 1;
    }

    if (!verifySchematicRoundTrip(fixturesDir, err)) {
        std::cerr << "[FAIL] Schematic regression: " << err.toStdString() << std::endl;
        return 1;
    }

    if (!verifyPortablePathSave(err)) {
        std::cerr << "[FAIL] Path portability (save): " << err.toStdString() << std::endl;
        return 1;
    }

    if (!verifyLoadSeparatorNormalization(err)) {
        std::cerr << "[FAIL] Path portability (load): " << err.toStdString() << std::endl;
        return 1;
    }

    std::cout << "[PASS] IO golden regression checks passed." << std::endl;
    return 0;
}
