/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_commands.h"
#include "common.h"
#include "../command_registry.h"

#include "pcb/io/pcb_file_io.h"
#include "pcb/editor/pcb_eco_resolver.h"
#include "pcb/drc/pcb_drc.h"
#include "pcb/items/pcb_item.h"
#include "pcb/items/component_item.h"
#include "pcb/items/trace_item.h"
#include "pcb/items/via_item.h"
#include "pcb/items/pad_item.h"
#include "pcb/items/copper_pour_item.h"
#include "pcb/items/shape_item.h"
#include "pcb/items/image_item.h"
#include "pcb/models/board_model.h"
#include "pcb/layers/pcb_layer.h"
#include "pcb/analysis/pcb_auto_router.h"

#include "flux/schematic/io/schematic_file_io.h"
#include "schematic/io/netlist_generator.h"

#include <QGraphicsScene>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <limits>
#include <algorithm>

namespace {

void getBoardSize(const Flux::Model::BoardModel* board, double& outWidth, double& outHeight) {
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    bool hasEdgeCuts = false;

    for (auto* trace : board->traces()) {
        if (trace->layer() == PCBLayerManager::EdgeCuts) {
            hasEdgeCuts = true;
            minX = std::min({minX, trace->start().x(), trace->end().x()});
            minY = std::min({minY, trace->start().y(), trace->end().y()});
            maxX = std::max({maxX, trace->start().x(), trace->end().x()});
            maxY = std::max({maxY, trace->start().y(), trace->end().y()});
        }
    }

    if (hasEdgeCuts) {
        outWidth = maxX - minX;
        outHeight = maxY - minY;
    } else {
        outWidth = 100.0;
        outHeight = 80.0;
    }
}

class PcbQueryCommand : public CLICommand {
public:
    QString name() const override { return "pcb-query"; }
    QString description() const override { return "Query PCB layout file for details."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"file", "string"},
            {"width", "number"},
            {"height", "number"},
            {"layersCount", "int"},
            {"componentsCount", "int"},
            {"tracesCount", "int"},
            {"viasCount", "int"},
            {"copperPoursCount", "int"},
            {"components", "array"},
            {"nets", "array"}
        };
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-query <file.pcb>" << std::endl;
            return 1;
        }
        QString filePath = args.at(0);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        Flux::Model::BoardModel* board = PCBFileIO::sceneToModel(&scene);
        if (!board) {
            std::cerr << "Error parsing PCB model." << std::endl;
            return 1;
        }

        double width = 0.0;
        double height = 0.0;
        getBoardSize(board, width, height);

        QJsonObject out;
        out["file"] = filePath;
        out["width"] = width;
        out["height"] = height;
        out["layersCount"] = PCBLayerManager::instance().copperLayerCount();

        QJsonArray components;
        for (auto* comp : board->components()) {
            QJsonObject c;
            c["id"] = comp->id().toString();
            c["name"] = comp->name();
            c["footprint"] = comp->componentType();
            c["x"] = comp->pos().x();
            c["y"] = comp->pos().y();
            c["rotation"] = comp->rotation();
            c["layer"] = comp->layer();
            components.append(c);
        }
        out["components"] = components;

        int tracesCount = board->traces().size();
        int viasCount = board->vias().size();
        int poursCount = board->copperPours().size();

        out["componentsCount"] = components.size();
        out["tracesCount"] = tracesCount;
        out["viasCount"] = viasCount;
        out["copperPoursCount"] = poursCount;

        QJsonArray nets;
        QSet<QString> netNames;
        for (auto* trace : board->traces()) {
            if (!trace->netName().isEmpty()) netNames.insert(trace->netName());
        }
        for (auto* via : board->vias()) {
            if (!via->netName().isEmpty()) netNames.insert(via->netName());
        }
        for (const QString& netName : netNames) {
            nets.append(netName);
        }
        out["nets"] = nets;

        delete board;

        printJsonValue(out);
        return 0;
    }
};

class PcbValidateCommand : public CLICommand {
public:
    QString name() const override { return "pcb-validate"; }
    QString description() const override { return "Validate PCB layout and run DRC checks."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"file", "string"},
            {"drc", "array"},
            {"summary", "object"}
        };
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-validate <file.pcb>" << std::endl;
            return 1;
        }
        QString filePath = args.at(0);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["file"] = filePath;

        PCBDRC drc;
        drc.runFullCheck(&scene);

        QJsonArray drcIssues;
        for (const auto& v : drc.violations()) {
            QJsonObject issue;
            issue["severity"] = v.severityString();
            issue["type"] = v.typeString();
            issue["message"] = v.message();
            issue["x"] = v.location().x();
            issue["y"] = v.location().y();
            drcIssues.append(issue);
        }
        out["drc"] = drcIssues;

        QJsonObject summary;
        summary["drcCount"] = drcIssues.size();
        summary["errorCount"] = drc.errorCount();
        summary["warningCount"] = drc.warningCount();
        out["summary"] = summary;

        printJsonValue(out);
        return drc.errorCount() > 0 ? 1 : 0;
    }
};

class PcbRenderCommand : public CLICommand {
public:
    QString name() const override { return "pcb-render"; }
    QString description() const override { return "Render a PCB layout file to an image."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("scale", "Render scale (default 4.0)", "scale", "4"));
        parser.addOption(QCommandLineOption("transparent", "Render PNG with transparent background"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
        parser.addOption(QCommandLineOption("layers", "Comma-separated list of layers to render", "layers"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}, {"layers", "string"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"output", "string"}, {"width", "int"}, {"height", "int"}, {"scale", "number"}, {"transparent", "bool"}, {"bounds", "rect"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora pcb-render <file.pcb> <out.png> [options]" << std::endl;
            return 1;
        }
        QString filePath = args.at(0);
        QString outPath = args.at(1);

        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (parser.isSet("layers")) {
            QStringList filterLayers = parser.value("layers").split(',', Qt::SkipEmptyParts);
            for (auto* item : scene.items()) {
                if (auto* pi = dynamic_cast<PCBItem*>(item)) {
                    bool match = false;
                    for (const QString& lay : filterLayers) {
                        bool ok;
                        int layerId = lay.toInt(&ok);
                        if (ok && pi->layer() == layerId) {
                            match = true;
                            break;
                        }
                    }
                    pi->setVisible(match);
                }
            }
        }

        QRectF rect = scene.itemsBoundingRect();
        if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
        rect.adjust(-10, -10, 10, 10);

        const qreal scale = qMax(0.1, parser.value("scale").toDouble());
        QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
        const bool transparent = parser.isSet("transparent");
        image.fill(transparent ? Qt::transparent : QColor(20, 20, 25));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), rect);
        painter.end();

        if (!image.save(outPath)) {
            std::cerr << "Failed to save image to " << outPath.toStdString() << std::endl;
            return 1;
        }

        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["output"] = outPath;
            out["width"] = image.width();
            out["height"] = image.height();
            out["scale"] = scale;
            out["transparent"] = transparent;
            QJsonObject bounds;
            bounds["x"] = rect.x();
            bounds["y"] = rect.y();
            bounds["w"] = rect.width();
            bounds["h"] = rect.height();
            out["bounds"] = bounds;
            printJsonValue(out);
        } else {
            if (!g_quiet) std::cout << "Successfully rendered PCB layout to " << outPath.toStdString() << std::endl;
        }
        return 0;
    }
};

class PcbInitCommand : public CLICommand {
public:
    QString name() const override { return "pcb-init"; }
    QString description() const override { return "Initialize a new PCB layout (standalone or from a schematic)."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("schematic", "Source schematic file to import netlist and components from", "file.flxsch"));
        parser.addOption(QCommandLineOption("width", "Board width in mm (default: 100)", "width", "100"));
        parser.addOption(QCommandLineOption("height", "Board height in mm (default: 80)", "height", "80"));
        parser.addOption(QCommandLineOption("layers", "Board copper layer count (default: 2)", "layers", "2"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"output.pcb"}},
            {"options", QJsonObject{
                {"schematic", "string"},
                {"width", "number"},
                {"height", "number"},
                {"layers", "int"},
                {"json", "bool"}
            }}
        };
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"ok", "bool"},
            {"file", "string"},
            {"sourceSchematic", "string"},
            {"width", "number"},
            {"height", "number"},
            {"layersCount", "int"}
        };
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-init <output.pcb> [options]" << std::endl;
            return 1;
        }
        QString outPath = args.at(0);

        double width = parser.value("width").toDouble();
        double height = parser.value("height").toDouble();
        int layers = parser.value("layers").toInt();
        if (width <= 0.0) width = 100.0;
        if (height <= 0.0) height = 80.0;
        if (layers <= 0) layers = 2;

        QGraphicsScene pcbScene;
        auto* board = new Flux::Model::BoardModel();
        
        // Setup board boundary lines on layer 50 (EdgeCuts)
        const double edgeWidth = 0.2;
        
        auto* top = new Flux::Model::TraceModel();
        top->setStart(QPointF(0, 0));
        top->setEnd(QPointF(width, 0));
        top->setWidth(edgeWidth);
        top->setLayer(PCBLayerManager::EdgeCuts);
        board->addTrace(top);

        auto* right = new Flux::Model::TraceModel();
        right->setStart(QPointF(width, 0));
        right->setEnd(QPointF(width, height));
        right->setWidth(edgeWidth);
        right->setLayer(PCBLayerManager::EdgeCuts);
        board->addTrace(right);

        auto* bottom = new Flux::Model::TraceModel();
        bottom->setStart(QPointF(width, height));
        bottom->setEnd(QPointF(0, height));
        bottom->setWidth(edgeWidth);
        bottom->setLayer(PCBLayerManager::EdgeCuts);
        board->addTrace(bottom);

        auto* left = new Flux::Model::TraceModel();
        left->setStart(QPointF(0, height));
        left->setEnd(QPointF(0, 0));
        left->setWidth(edgeWidth);
        left->setLayer(PCBLayerManager::EdgeCuts);
        board->addTrace(left);

        // Configure copper layers count globally for this session
        PCBLayerManager::instance().setCopperLayerCount(layers);

        PCBFileIO::modelToScene(board, &pcbScene);
        delete board;

        QString sourceSchematic;
        if (parser.isSet("schematic")) {
            sourceSchematic = parser.value("schematic");
            QGraphicsScene schematicScene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (!SchematicFileIO::loadSchematic(&schematicScene, sourceSchematic, pageSize, dummyTB)) {
                std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
                return 1;
            }

            ECOPackage pkg = NetlistGenerator::generateECOPackage(&schematicScene, QFileInfo(sourceSchematic).absolutePath(), nullptr);
            PCBECOResolver::applyECO(pkg, &pcbScene, nullptr, nullptr);
            
            if (!g_quiet) {
                std::cout << "Imported schematic netlist from " << sourceSchematic.toStdString() << std::endl;
            }
        }

        if (!PCBFileIO::savePCB(&pcbScene, outPath)) {
            std::cerr << "Failed to save PCB file: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["ok"] = true;
        out["file"] = outPath;
        out["sourceSchematic"] = sourceSchematic;
        out["width"] = width;
        out["height"] = height;
        out["layersCount"] = layers;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            if (!g_quiet) {
                std::cout << "Successfully initialized PCB at " << outPath.toStdString() << " (" << width << "x" << height << "mm, " << layers << " layers)." << std::endl;
            }
        }

        return 0;
    }
};

#include <QRegularExpression>

class PcbComposeCommand : public CLICommand {
public:
    QString name() const override { return "pcb-compose"; }
    QString description() const override { return "Programmatically add, update, or remove components, traces, and vias on a PCB layout."; }
    
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("add-component", "Inject or update component: footprint=...,x=...,y=...,rotation=...,layer=...,name=...,value=...", "spec"));
        parser.addOption(QCommandLineOption("add-trace", "Inject trace: x1=...,y1=...,x2=...,y2=...,width=...,layer=...,net=...", "spec"));
        parser.addOption(QCommandLineOption("add-via", "Inject via: x=...,y=...,diameter=...,drill=...,startlayer=...,endlayer=...,net=...", "spec"));
        parser.addOption(QCommandLineOption("delete-item", "Delete item: id=... OR name=...", "spec"));
        parser.addOption(QCommandLineOption("auto-route", "Automatically route all connections after composing"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
    }

    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"add-component", "string (repeatable)"},
                {"add-trace", "string (repeatable)"},
                {"add-via", "string (repeatable)"},
                {"delete-item", "string (repeatable)"},
                {"auto-route", "bool"},
                {"json", "bool"}
            }}
        };
    }

    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"ok", "bool"},
            {"file", "string"},
            {"addedComponents", "int"},
            {"addedTraces", "int"},
            {"addedVias", "int"},
            {"deletedItems", "int"},
            {"routedConnections", "int"}
        };
    }

    static QMap<QString, QString> parseProperties(const QString& str) {
        QMap<QString, QString> props;
        QStringList parts = str.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            int eq = part.indexOf('=');
            if (eq > 0) {
                QString key = part.left(eq).trimmed().toLower();
                QString val = part.mid(eq + 1).trimmed();
                props[key] = val;
            }
        }
        return props;
    }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-compose <file.pcb> [options]" << std::endl;
            return 1;
        }
        QString filePath = args.at(0);

        QGraphicsScene scene;
        if (QFile::exists(filePath)) {
            if (!PCBFileIO::loadPCB(&scene, filePath)) {
                std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
                return 1;
            }
        } else {
            // Initialize default board outline
            double width = 100.0;
            double height = 80.0;
            auto* board = new Flux::Model::BoardModel();
            const double edgeWidth = 0.2;
            
            auto* top = new Flux::Model::TraceModel();
            top->setStart(QPointF(0, 0));
            top->setEnd(QPointF(width, 0));
            top->setWidth(edgeWidth);
            top->setLayer(PCBLayerManager::EdgeCuts);
            board->addTrace(top);

            auto* right = new Flux::Model::TraceModel();
            right->setStart(QPointF(width, 0));
            right->setEnd(QPointF(width, height));
            right->setWidth(edgeWidth);
            right->setLayer(PCBLayerManager::EdgeCuts);
            board->addTrace(right);

            auto* bottom = new Flux::Model::TraceModel();
            bottom->setStart(QPointF(width, height));
            bottom->setEnd(QPointF(0, height));
            bottom->setWidth(edgeWidth);
            bottom->setLayer(PCBLayerManager::EdgeCuts);
            board->addTrace(bottom);

            auto* left = new Flux::Model::TraceModel();
            left->setStart(QPointF(0, height));
            left->setEnd(QPointF(0, 0));
            left->setWidth(edgeWidth);
            left->setLayer(PCBLayerManager::EdgeCuts);
            board->addTrace(left);

            PCBLayerManager::instance().setCopperLayerCount(2);
            PCBFileIO::modelToScene(board, &scene);
            delete board;
        }

        // 1. Handle Deletions
        const QStringList deletes = parser.values("delete-item");
        int deletedCount = 0;
        for (const QString& delStr : deletes) {
            auto props = parseProperties(delStr);
            QString idStr = props.value("id");
            QString nameStr = props.value("name");

            QList<QGraphicsItem*> itemsToDelete;
            for (auto* qItem : scene.items()) {
                if (auto* pi = dynamic_cast<PCBItem*>(qItem)) {
                    if (!idStr.isEmpty() && pi->id().toString() == idStr) {
                        itemsToDelete.append(qItem);
                    } else if (auto* comp = dynamic_cast<ComponentItem*>(pi)) {
                        if (!nameStr.isEmpty() && comp->name().compare(nameStr, Qt::CaseInsensitive) == 0) {
                            itemsToDelete.append(qItem);
                        }
                    }
                }
            }
            for (auto* item : itemsToDelete) {
                scene.removeItem(item);
                delete item;
                deletedCount++;
            }
        }

        // 2. Handle Components Insertion
        const QStringList addComps = parser.values("add-component");
        int addedComps = 0;
        for (const QString& compStr : addComps) {
            auto props = parseProperties(compStr);
            QString footprint = props.value("footprint");
            if (footprint.isEmpty()) {
                std::cerr << "Error: --add-component requires 'footprint' property." << std::endl;
                return 1;
            }
            double x = props.value("x").toDouble();
            double y = props.value("y").toDouble();
            double rotation = props.value("rotation").toDouble();
            int layer = props.value("layer").toInt();
            QString name = props.value("name");
            QString value = props.value("value");

            ComponentItem* existing = nullptr;
            if (!name.isEmpty()) {
                for (auto* item : scene.items()) {
                    if (auto* c = dynamic_cast<ComponentItem*>(item)) {
                        if (c->name().compare(name, Qt::CaseInsensitive) == 0) {
                            existing = c;
                            break;
                        }
                    }
                }
            }

            ComponentItem* targetComp = nullptr;
            if (existing) {
                existing->setComponentType(footprint);
                existing->setPos(QPointF(x, y));
                existing->setRotation(rotation);
                existing->setLayer(layer);
                if (!value.isEmpty()) existing->setValue(value);
                targetComp = existing;
            } else {
                auto* compItem = new ComponentItem(QPointF(x, y), footprint);
                if (!name.isEmpty()) compItem->setName(name);
                if (!value.isEmpty()) compItem->setValue(value);
                compItem->setRotation(rotation);
                compItem->setLayer(layer);
                scene.addItem(compItem);
                targetComp = compItem;
            }

            // Map nets to component pads if specified
            QString netsSpec = props.value("nets");
            if (!netsSpec.isEmpty() && targetComp && targetComp->model()) {
                QStringList mappings = netsSpec.split(QRegularExpression("[|/]"), Qt::SkipEmptyParts);
                for (const QString& mapping : mappings) {
                    int colon = mapping.indexOf(':');
                    if (colon > 0) {
                        QString pinNum = mapping.left(colon).trimmed();
                        QString netName = mapping.mid(colon + 1).trimmed();
                        for (auto* pm : targetComp->model()->pads()) {
                            if (pm->number() == pinNum) {
                                pm->setNetName(netName);
                                for (auto* child : targetComp->childItems()) {
                                    if (auto* pad = dynamic_cast<PadItem*>(child)) {
                                        if (pad->model() == pm) {
                                            pad->setNetName(netName);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            addedComps++;
        }

        // 3. Handle Traces Insertion
        const QStringList addTraces = parser.values("add-trace");
        int addedTraces = 0;
        for (const QString& traceStr : addTraces) {
            auto props = parseProperties(traceStr);
            double x1 = props.value("x1").toDouble();
            double y1 = props.value("y1").toDouble();
            double x2 = props.value("x2").toDouble();
            double y2 = props.value("y2").toDouble();
            double width = props.contains("width") ? props.value("width").toDouble() : 0.25;
            int layer = props.value("layer").toInt();
            QString net = props.value("net");

            auto* traceItem = new TraceItem(QPointF(x1, y1), QPointF(x2, y2), width);
            traceItem->setLayer(layer);
            if (traceItem->model()) {
                traceItem->model()->setNetName(net);
            }
            scene.addItem(traceItem);
            addedTraces++;
        }

        // 4. Handle Vias Insertion
        const QStringList addVias = parser.values("add-via");
        int addedVias = 0;
        for (const QString& viaStr : addVias) {
            auto props = parseProperties(viaStr);
            double x = props.value("x").toDouble();
            double y = props.value("y").toDouble();
            double diameter = props.contains("diameter") ? props.value("diameter").toDouble() : 0.8;
            double drill = props.contains("drill") ? props.value("drill").toDouble() : 0.3;
            int startLayer = props.contains("startlayer") ? props.value("startlayer").toInt() : PCBLayerManager::TopCopper;
            int endLayer = props.contains("endlayer") ? props.value("endlayer").toInt() : PCBLayerManager::BottomCopper;
            QString net = props.value("net");

            auto* viaItem = new ViaItem(QPointF(x, y), diameter);
            if (viaItem->model()) {
                viaItem->model()->setDrillSize(drill);
                viaItem->model()->setStartLayer(startLayer);
                viaItem->model()->setEndLayer(endLayer);
                viaItem->model()->setNetName(net);
            }
            scene.addItem(viaItem);
            addedVias++;
        }

        // 5. Handle Auto-routing
        int routedCount = 0;
        if (parser.isSet("auto-route")) {
            PCBAutoRouter router(&scene);
            PCBAutoRouter::RouterConfig config;
            auto stats = router.routeAll(config);
            routedCount = stats.routedConnections;
        }

        if (!PCBFileIO::savePCB(&scene, filePath)) {
            std::cerr << "Failed to save PCB file: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["ok"] = true;
        out["file"] = filePath;
        out["addedComponents"] = addedComps;
        out["addedTraces"] = addedTraces;
        out["addedVias"] = addedVias;
        out["deletedItems"] = deletedCount;
        out["routedConnections"] = routedCount;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            if (!g_quiet) {
                std::cout << "Successfully composed PCB: added " << addedComps << " components, "
                          << addedTraces << " traces, " << addedVias << " vias; deleted "
                          << deletedCount << " items." << std::endl;
                if (parser.isSet("auto-route")) {
                    std::cout << "Auto-routed " << routedCount << " net connections." << std::endl;
                }
            }
        }
        return 0;
    }
};

} // namespace

void registerPCBCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<PcbQueryCommand>());
    reg.registerCommand(std::make_unique<PcbValidateCommand>());
    reg.registerCommand(std::make_unique<PcbRenderCommand>());
    reg.registerCommand(std::make_unique<PcbInitCommand>());
    reg.registerCommand(std::make_unique<PcbComposeCommand>());
}
