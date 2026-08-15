/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_commands.h"
#include "common.h"
#include "../command_registry.h"

#include "pcb/io/pcb_file_io.h"
#include "pcb/editor/pcb_eco_resolver.h"
#include "pcb/import/netlist_importer.h"
#include "footprints/footprint_library.h"
#include "core/project/library_index.h"
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
#include "pcb/analysis/pcb_tracks_cleaner.h"
#include "pcb/analysis/pcb_teardrop_generator.h"
#include "pcb/analysis/pcb_array_generator.h"
#include "pcb/analysis/pcb_via_fence_generator.h"
#include "pcb/analysis/pcb_stackup_manager.h"
#include "pcb/analysis/serpentine_generator.h"
#include "pcb/analysis/length_match_manager.h"
#include "pcb/drc/pcb_drc.h"
#include "pcb/models/drc_types.h"
#include "pcb/import/kicad_pcb_importer.h"
#include "pcb/export/kicad_pcb_exporter.h"
#include "pcb/gerber/gerber_exporter.h"
#include "pcb/mcad/mcad_exporter.h"
#include "pcb/manufacturing/manufacturing_exporter.h"
#include "pcb/editor/pcb_export_manager.h"

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

void shrinkBoardOutline(QGraphicsScene* scene, double margin);

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
        parser.addOption(QCommandLineOption("layers", "Comma-separated list of layers to render", "layers"));
        parser.addOption(QCommandLineOption("mode", "Render mode: fab (default), assembly, copper", "mode", "fab"));
        parser.addOption(QCommandLineOption("grid", "Show grid overlay"));
        parser.addOption(QCommandLineOption("labels", "Show component reference designators"));
        parser.addOption(QCommandLineOption("margin", "Margin around board in mm", "margin", "5"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}, {"layers", "string"}, {"mode", "string"}, {"grid", "bool"}, {"labels", "bool"}, {"margin", "number"}}}};
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
        const qreal marginVal = parser.value("margin").toDouble();
        rect.adjust(-marginVal, -marginVal, marginVal, marginVal);

        const qreal scale = qMax(0.1, parser.value("scale").toDouble());
        const bool transparent = parser.isSet("transparent");
        const bool showGrid = parser.isSet("grid");
        const bool showLabels = parser.isSet("labels");

        // Background colors by mode
        QString mode = parser.value("mode").toLower();
        QColor bgColor;
        if (transparent) {
            bgColor = Qt::transparent;
        } else if (mode == "copper") {
            bgColor = QColor(15, 15, 20);
        } else if (mode == "assembly") {
            bgColor = QColor(245, 245, 240);
        } else {
            bgColor = QColor(20, 25, 30); // Dark fab view
        }

        qreal renderScale = parser.isSet("scale") ? parser.value("scale").toDouble() : 10.0;
        int imgW = qRound(rect.width() * renderScale);
        int imgH = qRound(rect.height() * renderScale);

        // Enforce minimum high-definition 1600px width
        if (imgW < 1600) {
            double fitScale = 1600.0 / qMax(1.0, rect.width());
            imgW = qRound(rect.width() * fitScale);
            imgH = qRound(rect.height() * fitScale);
            renderScale = fitScale;
        }

        // Cap maximum image resolution to 4096px for ultra-fast crisp 4K rendering
        if (imgW > 4096 || imgH > 4096) {
            double capScale = 4096.0 / std::max(imgW, imgH);
            imgW = qRound(imgW * capScale);
            imgH = qRound(imgH * capScale);
            renderScale *= capScale;
        }

        QImage image(QSize(imgW, imgH), QImage::Format_ARGB32_Premultiplied);
        image.fill(bgColor);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QRectF targetRect(0.0, 0.0, (qreal)imgW, (qreal)imgH);

        // Draw grid overlay mapped to target coordinates
        if (showGrid && !transparent) {
            painter.save();
            QPen gridPen(QColor(255, 255, 255, 25), 1.0);
            painter.setPen(gridPen);
            qreal gridStep = 2.0; // 2mm grid step
            for (qreal x = rect.left(); x <= rect.right(); x += gridStep) {
                qreal pxX = (x - rect.left()) / rect.width() * imgW;
                painter.drawLine(QPointF(pxX, 0), QPointF(pxX, imgH));
            }
            for (qreal y = rect.top(); y <= rect.bottom(); y += gridStep) {
                qreal pxY = (y - rect.top()) / rect.height() * imgH;
                painter.drawLine(QPointF(0, pxY), QPointF(imgW, pxY));
            }
            painter.restore();
        }

        // Hide raw text primitives to avoid FreeType glyph rendering underflow in headless export
        for (auto* item : scene.items()) {
            if (dynamic_cast<QGraphicsTextItem*>(item)) {
                item->setVisible(false);
            }
        }

        // Fast direct batch renderer for ultra-high speed headless PNG export (<1s for 50k items)
        auto mapPoint = [&](QPointF p) -> QPointF {
            return QPointF(
                (p.x() - rect.left()) / rect.width() * imgW,
                (p.y() - rect.top()) / rect.height() * imgH
            );
        };

        const double scaleX = imgW / rect.width();

        // 1. Draw Traces
        QMap<int, QColor> layerColors;
        layerColors[PCBLayerManager::TopCopper] = QColor(200, 50, 50, 220); // Red
        layerColors[PCBLayerManager::BottomCopper] = QColor(50, 100, 220, 220); // Blue
        layerColors[PCBLayerManager::EdgeCuts] = QColor(240, 200, 80, 255); // Gold outline

        const auto allSceneItems = scene.items(Qt::AscendingOrder);

        // Batch 1: Copper Traces
        for (QGraphicsItem* gItem : allSceneItems) {
            if (!gItem->isVisible()) continue;
            if (auto* trace = dynamic_cast<TraceItem*>(gItem)) {
                QColor c = layerColors.value(trace->layer(), QColor(100, 180, 100, 200));
                double pxW = qMax(1.0, trace->width() * scaleX);
                QPen pen(c, pxW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(pen);
                QPointF s = mapPoint(trace->mapToScene(trace->startPoint()));
                QPointF e = mapPoint(trace->mapToScene(trace->endPoint()));
                painter.drawLine(s, e);
            }
        }

        // Batch 2: Vias
        for (QGraphicsItem* gItem : allSceneItems) {
            if (!gItem->isVisible()) continue;
            if (auto* via = dynamic_cast<ViaItem*>(gItem)) {
                QPointF pos = mapPoint(via->scenePos());
                double d = qMax(2.0, via->diameter() * scaleX);
                double drillD = qMax(1.0, via->drillSize() * scaleX);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(220, 160, 40)); // Copper ring
                painter.drawEllipse(pos, d / 2.0, d / 2.0);
                painter.setBrush(transparent ? Qt::transparent : bgColor);
                painter.drawEllipse(pos, drillD / 2.0, drillD / 2.0);
            }
        }

        // Batch 3: Footprint Pads & Component Bodies
        for (QGraphicsItem* gItem : allSceneItems) {
            if (!gItem->isVisible()) continue;
            if (auto* pad = dynamic_cast<PadItem*>(gItem)) {
                QPointF pos = mapPoint(pad->scenePos());
                QSizeF sz = pad->size() * scaleX;
                double w = qMax(2.0, sz.width());
                double h = qMax(2.0, sz.height());
                QRectF padRect(-w / 2.0, -h / 2.0, w, h);

                painter.save();
                painter.translate(pos);
                painter.rotate(pad->sceneTransform().map(QPointF(1, 0)).y() != 0 ? pad->rotation() : 0);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(220, 50, 50));
                if (pad->padShape().toLower() == "oblong" || pad->padShape().toLower() == "round") {
                    double r = qMin(w, h) / 2.0;
                    painter.drawRoundedRect(padRect, r, r);
                } else {
                    painter.drawRect(padRect);
                }
                if (pad->drillSize() > 0.001) {
                    double drillD = qMax(1.0, pad->drillSize() * scaleX);
                    painter.setBrush(transparent ? Qt::transparent : bgColor);
                    painter.drawEllipse(QPointF(0, 0), drillD / 2.0, drillD / 2.0);
                }
                painter.restore();
            }
        }

        // Draw component reference designator labels
        if (showLabels) {
            int fontPt = qMax(12, qRound(renderScale * 0.45));
            QFont labelFont("DejaVu Sans");
            labelFont.setStyleHint(QFont::SansSerif);
            labelFont.setPixelSize(fontPt);
            labelFont.setBold(true);
            painter.setFont(labelFont);

            for (auto* item : scene.items()) {
                if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                    if (!comp->isVisible()) continue;
                    QPointF pos = comp->scenePos();
                    qreal pxX = (pos.x() - rect.left()) / rect.width() * imgW;
                    qreal pxY = (pos.y() - rect.top()) / rect.height() * imgH;

                    QString label = comp->name();
                    if (label.isEmpty()) label = comp->componentType();

                    QFontMetrics fm(labelFont);
                    int txtW = fm.horizontalAdvance(label);
                    int txtH = fm.height();
                    QPointF textPos(pxX - txtW / 2.0, pxY + txtH / 2.0 + renderScale * 1.2);

                    QRectF bgRect(textPos.x() - 4, textPos.y() - fm.ascent() - 2, txtW + 8, txtH + 4);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(0, 0, 0, 200));
                    painter.drawRoundedRect(bgRect, 4, 4);

                    painter.setPen(QColor(255, 255, 255));
                    painter.drawText(textPos, label);
                }
            }
        }

        painter.end();

        if (!image.save(outPath, "PNG") && !image.save(outPath)) {
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
            out["mode"] = mode;
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

            // Filter components marked for PCB exclusion
            QList<ECOComponent> filteredComps;
            QSet<QString> excludedRefs;
            for (const auto& comp : pkg.components) {
                if (!comp.excludeFromPcb) {
                    filteredComps.append(comp);
                } else {
                    excludedRefs.insert(comp.reference);
                }
            }
            pkg.components = filteredComps;

            // Filter net pins belonging to excluded components
            for (int i = 0; i < pkg.nets.size(); ++i) {
                QList<ECOPin> filteredPins;
                for (const auto& pin : pkg.nets[i].pins) {
                    if (!excludedRefs.contains(pin.componentRef)) {
                        filteredPins.append(pin);
                    }
                }
                pkg.nets[i].pins = filteredPins;
            }
            // Remove empty nets
            pkg.nets.erase(std::remove_if(pkg.nets.begin(), pkg.nets.end(), [](const ECONet& n){
                return n.pins.isEmpty();
            }), pkg.nets.end());

            QStringList footprints;
            for (const auto& r : LibraryIndex::instance().search("", "Footprint")) {
                footprints.append(r.name);
            }
            if (footprints.isEmpty()) {
                auto& fpLibMgr = FootprintLibraryManager::instance();
                for (auto* lib : fpLibMgr.libraries()) {
                    footprints.append(lib->getFootprintNames());
                }
            }
            PCBNetlistImporter::suggestFootprints(pkg, footprints);
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
        parser.addOption(QCommandLineOption("shrink-outline", "Automatically shrink the board outline to fit components: margin=<val_in_mm>", "spec"));
        parser.addOption(QCommandLineOption("auto-route", "Automatically route all connections after composing"));
        parser.addOption(QCommandLineOption("route-layers", "Routing layer selection: top | bottom | both (default: both)", "layers", "both"));
        parser.addOption(QCommandLineOption("add-netclass", "Inject net class rules: name=...,width=...,clearance=...", "spec"));
        parser.addOption(QCommandLineOption("assign-net", "Assign net to a net class: net=...,class=...", "spec"));
        parser.addOption(QCommandLineOption("add-pour", "Inject copper pour: layer=...,net=...,clearance=...", "spec"));
        parser.addOption(QCommandLineOption("allow-diagonals", "Allow the auto-router to use 45-degree diagonal traces"));
    }

    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"add-component", "string (repeatable)"},
                {"add-trace", "string (repeatable)"},
                {"add-via", "string (repeatable)"},
                {"delete-item", "string (repeatable)"},
                {"shrink-outline", "string"},
                {"auto-route", "bool"},
                {"route-layers", "string"},
                {"add-netclass", "string (repeatable)"},
                {"assign-net", "string (repeatable)"},
                {"add-pour", "string (repeatable)"},
                {"allow-diagonals", "bool"},
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

        // 0.5. Handle Net Classes and Assignments
        const QStringList netClasses = parser.values("add-netclass");
        for (const QString& ncStr : netClasses) {
            auto props = parseProperties(ncStr);
            QString name = props.value("name");
            if (name.isEmpty()) continue;
            double width = props.contains("width") ? props.value("width").toDouble() : 0.25;
            double clearance = props.contains("clearance") ? props.value("clearance").toDouble() : 0.2;
            double viaDiameter = props.contains("viadiameter") ? props.value("viadiameter").toDouble() : 0.6;
            double viaDrill = props.contains("viadrill") ? props.value("viadrill").toDouble() : 0.3;

            NetClass nc(name, width, clearance, viaDiameter, viaDrill);
            NetClassManager::instance().addClass(nc);
        }

        const QStringList netAssigns = parser.values("assign-net");
        for (const QString& naStr : netAssigns) {
            auto props = parseProperties(naStr);
            QString netName = props.value("net");
            QString className = props.value("class");
            if (!netName.isEmpty() && !className.isEmpty()) {
                NetClassManager::instance().assignNetToClass(netName, className);
            }
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

        // 4.5. Handle board outline shrinking
        if (parser.isSet("shrink-outline")) {
            double margin = 5.0; // default
            QString spec = parser.value("shrink-outline");
            if (!spec.isEmpty()) {
                auto props = parseProperties(spec);
                if (props.contains("margin")) {
                    margin = props.value("margin").toDouble();
                } else if (spec.toDouble() > 0.001) { // Accept direct number too
                    margin = spec.toDouble();
                }
            }
            shrinkBoardOutline(&scene, margin);
        }

        // 5. Handle Auto-routing
        int routedCount = 0;
        if (parser.isSet("auto-route")) {
            PCBAutoRouter router(&scene);
            PCBAutoRouter::RouterConfig config;

            if (parser.isSet("route-layers")) {
                QString val = parser.value("route-layers").trimmed().toLower();
                if (val == "top") {
                    config.preferTopLayer = true;
                    config.preferBottomLayer = false;
                } else if (val == "bottom") {
                    config.preferTopLayer = false;
                    config.preferBottomLayer = true;
                } else if (val == "both") {
                    config.preferTopLayer = true;
                    config.preferBottomLayer = true;
                }
            }

            if (parser.isSet("allow-diagonals")) {
                config.allowDiagonals = true;
            }

            auto stats = router.routeAll(config);
            routedCount = stats.routedConnections;
        }

        // Add copper pours if requested
        if (parser.isSet("add-pour")) {
            for (const QString& pourStr : parser.values("add-pour")) {
                auto props = parseProperties(pourStr);
                int layer = props.value("layer", "1").toInt();
                QString net = props.value("net", "GND");
                double clearance = props.value("clearance", "0.3").toDouble();

                QRectF bounds = scene.itemsBoundingRect();
                if (bounds.isEmpty() || bounds.width() < 10) {
                    bounds = QRectF(0, 0, 60, 50);
                }
                bounds = bounds.adjusted(-2.0, -2.0, 2.0, 2.0);

                QPolygonF poly;
                poly << bounds.topLeft() << bounds.topRight() << bounds.bottomRight() << bounds.bottomLeft() << bounds.topLeft();

                auto* pour = new CopperPourItem();
                pour->setLayer(layer);
                pour->setNetName(net);
                if (pour->model()) pour->model()->setClearance(clearance);
                pour->setPolygon(poly);
                scene.addItem(pour);
                pour->rebuild();
            }
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
                if (routedCount > 0) {
                    std::cout << "Auto-routed " << routedCount << " net connections." << std::endl;
                }
            }
        }
        return 0;
    }
};

class PcbExportCommand : public CLICommand {
public:
    QString name() const override { return "pcb-export"; }
    QString description() const override { return "Export PCB layout to manufacturing formats (Gerber, Drill, STEP, IGES, IPC-2581, ODB++, Pick-and-Place)."; }

    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption(QStringList() << "f" << "format", "Export format: gerber | pdf | step | iges | ipc2581 | odb | pos (default: gerber)", "format", "gerber"));
        parser.addOption(QCommandLineOption(QStringList() << "o" << "output", "Output directory or file path (default: ./output)", "path", "./output"));
        parser.addOption(QCommandLineOption("pdf-page", "PDF Page size: a4 | a3 | a2 | letter | board (crop to board only)", "size", "a4"));
        parser.addOption(QCommandLineOption("pdf-scale", "PDF Scale mode: 1:1 | fit (default: 1:1)", "scale", "1:1"));
        parser.addOption(QCommandLineOption("pdf-mirror", "Mirror plot (horizontal flip for bottom layer etching)"));
        parser.addOption(QCommandLineOption("pdf-drill-marks", "Drill marks: none | small | full (default: small)", "mode", "small"));
        parser.addOption(QCommandLineOption("pdf-no-title-block", "Omit drawing frame and title block (clean plot)"));
        parser.addOption(QCommandLineOption("pdf-monochrome", "Black and white vector plot"));
    }

    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"format", "string"},
                {"output", "string"},
                {"json", "bool"}
            }}
        };
    }

    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"ok", "bool"},
            {"file", "string"},
            {"format", "string"},
            {"output", "string"},
            {"generatedFiles", "array"}
        };
    }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-export <file.pcb> [--format gerber|pdf|step|iges|ipc2581|odb|pos] [--output <path>] [PDF options]" << std::endl;
            return 1;
        }

        QString pcbPath = args.at(0);
        QString format = parser.value("format").trimmed().toLower();
        QString outputPath = parser.value("output").trimmed();
        if (outputPath.isEmpty()) outputPath = "./output";

        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, pcbPath)) {
            std::cerr << "Error loading PCB file: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QStringList generatedFiles;
        bool ok = true;
        QString errorMsg;

        if (format == "pdf") {
            PCBExportManager::PdfExportOptions opts;
            opts.outputDirectory = outputPath;
            opts.mirrorPlot = parser.isSet("pdf-mirror");
            opts.titleBlock = !parser.isSet("pdf-no-title-block");
            opts.blackAndWhite = parser.isSet("pdf-monochrome");

            QString scaleVal = parser.value("pdf-scale").trimmed().toLower();
            opts.oneToOne = (scaleVal != "fit");

            QString pageVal = parser.value("pdf-page").trimmed().toLower();
            if (pageVal == "board" || pageVal == "crop") opts.pageSizeMode = -1;
            else if (pageVal == "a3") opts.pageSizeMode = 1;
            else if (pageVal == "a2") opts.pageSizeMode = 2;
            else if (pageVal == "letter") opts.pageSizeMode = 3;
            else opts.pageSizeMode = 0; // A4 default

            QString drillVal = parser.value("pdf-drill-marks").trimmed().toLower();
            if (drillVal == "none") opts.drillMarksMode = 0;
            else if (drillVal == "full") opts.drillMarksMode = 2;
            else opts.drillMarksMode = 1;

            ok = PCBExportManager::exportPDFHeadless(&scene, opts, &generatedFiles, &errorMsg);
        } else if (format == "gerber") {
            QDir().mkpath(outputPath);
            QDir outDir(outputPath);
            GerberExportSettings settings;
            settings.outputDirectory = outputPath;

            QList<PCBLayer> exportLayers;
            for (PCBLayer* cl : PCBLayerManager::instance().copperLayers()) {
                if (cl) exportLayers.append(*cl);
            }
            for (const auto& layer : PCBLayerManager::instance().layers()) {
                if (!layer.isCopperLayer()) {
                    exportLayers.append(layer);
                }
            }

            for (const auto& layer : exportLayers) {
                QString safeName = layer.name().toLower().replace(' ', '_');
                QString filePath = outDir.filePath(safeName + ".gbr");
                if (GerberExporter::exportLayer(&scene, layer.id(), filePath, settings)) {
                    generatedFiles.append(filePath);
                }
            }

            QString drillPath = outDir.filePath("drills.drl");
            if (GerberExporter::generateDrillFile(&scene, drillPath)) {
                generatedFiles.append(drillPath);
            }
        } else if (format == "manufacturing" || format == "jlcpcb" || format == "pkg") {
            ManufacturingExporter::ManufacturingPackageOptions opts;
            opts.preset = ManufacturingExporter::JLCPCB;
            opts.zipPackage = true;
            if (outputPath.isEmpty() || outputPath == "./output") {
                outputPath = "./jlcpcb_package.zip";
            }
            ok = ManufacturingExporter::exportManufacturingPackage(&scene, outputPath, opts, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "step") {
            QFileInfo fi(outputPath);
            if (fi.isDir() || !outputPath.endsWith(".step", Qt::CaseInsensitive)) {
                QDir().mkpath(outputPath);
                outputPath = QDir(outputPath).filePath(QFileInfo(pcbPath).baseName() + ".step");
            }
            ok = MCADExporter::exportSTEPWireframe(&scene, outputPath, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "iges") {
            QFileInfo fi(outputPath);
            if (fi.isDir() || !outputPath.endsWith(".igs", Qt::CaseInsensitive)) {
                QDir().mkpath(outputPath);
                outputPath = QDir(outputPath).filePath(QFileInfo(pcbPath).baseName() + ".igs");
            }
            ok = MCADExporter::exportIGESWireframe(&scene, outputPath, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "ipc2581") {
            QFileInfo fi(outputPath);
            if (fi.isDir() || !outputPath.endsWith(".xml", Qt::CaseInsensitive)) {
                QDir().mkpath(outputPath);
                outputPath = QDir(outputPath).filePath(QFileInfo(pcbPath).baseName() + "_ipc2581.xml");
            }
            ok = ManufacturingExporter::exportIPC2581(&scene, outputPath, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "odb") {
            QDir().mkpath(outputPath);
            ok = ManufacturingExporter::exportODBppPackage(&scene, outputPath, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "pos") {
            QFileInfo fi(outputPath);
            if (fi.isDir() || !outputPath.endsWith(".csv", Qt::CaseInsensitive)) {
                QDir().mkpath(outputPath);
                outputPath = QDir(outputPath).filePath(QFileInfo(pcbPath).baseName() + "_pos.csv");
            }
            ManufacturingExporter::PickPlaceOptions opts;
            ok = ManufacturingExporter::exportPickPlace(&scene, outputPath, opts, &errorMsg);
            if (ok) generatedFiles.append(outputPath);
        } else if (format == "kicad" || format == "kicad_pcb") {
            QFileInfo fi(outputPath);
            if (fi.isDir() || !outputPath.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
                QDir().mkpath(outputPath);
                outputPath = QDir(outputPath).filePath(QFileInfo(pcbPath).baseName() + ".kicad_pcb");
            }
            auto stats = KiCadPCBExporter::exportKiCadPCB(outputPath, &scene);
            ok = stats.success;
            if (!ok) errorMsg = stats.error;
            else generatedFiles.append(outputPath);
        } else {
            std::cerr << "Unsupported export format: " << format.toStdString() << std::endl;
            return 1;
        }

        if (!ok) {
            std::cerr << "Export failed: " << errorMsg.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["ok"] = true;
        out["file"] = pcbPath;
        out["format"] = format;
        out["output"] = outputPath;
        QJsonArray genArr;
        for (const auto& f : generatedFiles) genArr.append(f);
        out["generatedFiles"] = genArr;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            if (!g_quiet) {
                std::cout << "Successfully exported " << format.toUpper().toStdString() 
                          << " files to " << outputPath.toStdString() 
                          << " (" << generatedFiles.size() << " files generated)." << std::endl;
            }
        }

        return 0;
    }
};

void shrinkBoardOutline(QGraphicsScene* scene, double margin) {
    if (!scene) return;

    // 1. Gather and delete existing EdgeCuts outline items
    QList<QGraphicsItem*> toDelete;
    for (auto* item : scene->items()) {
        if (auto* pcbItem = dynamic_cast<PCBItem*>(item)) {
            if (pcbItem->layer() == PCBLayerManager::EdgeCuts) {
                toDelete.append(item);
            }
        }
    }
    for (auto* item : toDelete) {
        scene->removeItem(item);
        delete item;
    }

    // 2. Find bounding rect of all other items
    QRectF contentRect;
    bool first = true;
    for (auto* item : scene->items()) {
        if (auto* pcbItem = dynamic_cast<PCBItem*>(item)) {
            if (pcbItem->layer() != PCBLayerManager::EdgeCuts) {
                QRectF r = pcbItem->sceneBoundingRect();
                if (first) {
                    contentRect = r;
                    first = false;
                } else {
                    contentRect = contentRect.united(r);
                }
            }
        }
    }

    if (first || contentRect.isEmpty()) {
        contentRect = QRectF(0, 0, 50, 50);
    }

    double left = contentRect.left() - margin;
    double top = contentRect.top() - margin;
    double right = contentRect.right() + margin;
    double bottom = contentRect.bottom() + margin;

    // 3. Create new border traces on the EdgeCuts layer
    auto createEdgeTrace = [&](QPointF p1, QPointF p2) {
        TraceItem* trace = new TraceItem(p1, p2);
        trace->setLayer(PCBLayerManager::EdgeCuts);
        trace->setWidth(0.2);
        scene->addItem(trace);
    };

    createEdgeTrace(QPointF(left, top), QPointF(right, top));
    createEdgeTrace(QPointF(right, top), QPointF(right, bottom));
    createEdgeTrace(QPointF(right, bottom), QPointF(left, bottom));
    createEdgeTrace(QPointF(left, bottom), QPointF(left, top));
}

class PcbShrinkCommand : public CLICommand {
public:
    QString name() const override { return "pcb-shrink"; }
    QString description() const override { return "Automatically shrink the PCB board outline (EdgeCuts layer) to fit placed components/traces tightly."; }

    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("margin", "Clearance margin around components in mm (default: 5.0)", "value", "5.0"));
    }

    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"margin", "string"},
                {"json", "bool"}
            }}
        };
    }

    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"ok", "bool"},
            {"file", "string"},
            {"margin", "double"}
        };
    }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-shrink <file.pcb> [options]" << std::endl;
            return 1;
        }

        QString pcbPath = args.at(0);
        double margin = 5.0;
        if (parser.isSet("margin")) {
            margin = parser.value("margin").toDouble();
        }

        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, pcbPath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        shrinkBoardOutline(&scene, margin);

        if (!PCBFileIO::savePCB(&scene, pcbPath)) {
            std::cerr << "Failed to save PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["ok"] = true;
        out["file"] = pcbPath;
        out["margin"] = margin;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            if (!g_quiet) {
                std::cout << "Successfully shrunk board outline for " << pcbPath.toStdString() 
                          << " with margin of " << margin << "mm." << std::endl;
            }
        }

        return 0;
    }
};

class PcbSyncCommand : public CLICommand {
public:
    QString name() const override { return "pcb-sync"; }
    QString description() const override { return "Synchronize a PCB layout with a schematic file (importing netlist and footprint changes incrementally)."; }

    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("schematic", "Source schematic file to synchronize with", "file.flxsch"));
    }

    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"schematic", "string"},
                {"json", "bool"}
            }}
        };
    }

    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"ok", "bool"},
            {"file", "string"},
            {"sourceSchematic", "string"},
            {"changesCount", "int"}
        };
    }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty() || !parser.isSet("schematic")) {
            std::cerr << "Usage: viora pcb-sync <file.pcb> --schematic <file.flxsch>" << std::endl;
            return 1;
        }

        QString pcbPath = args.at(0);
        QString schematicPath = parser.value("schematic");

        QGraphicsScene pcbScene;
        if (!PCBFileIO::loadPCB(&pcbScene, pcbPath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QGraphicsScene schematicScene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&schematicScene, schematicPath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        ECOPackage pkg = NetlistGenerator::generateECOPackage(&schematicScene, QFileInfo(schematicPath).absolutePath(), nullptr);

        // Filter components marked for PCB exclusion
        QList<ECOComponent> filteredComps;
        QSet<QString> excludedRefs;
        for (const auto& comp : pkg.components) {
            if (!comp.excludeFromPcb) {
                filteredComps.append(comp);
            } else {
                excludedRefs.insert(comp.reference);
            }
        }
        pkg.components = filteredComps;

        // Filter net pins belonging to excluded components
        for (int i = 0; i < pkg.nets.size(); ++i) {
            QList<ECOPin> filteredPins;
            for (const auto& pin : pkg.nets[i].pins) {
                if (!excludedRefs.contains(pin.componentRef)) {
                    filteredPins.append(pin);
                }
            }
            pkg.nets[i].pins = filteredPins;
        }
        // Remove empty nets
        pkg.nets.erase(std::remove_if(pkg.nets.begin(), pkg.nets.end(), [](const ECONet& n){
            return n.pins.isEmpty();
        }), pkg.nets.end());

        QStringList footprints;
        for (const auto& r : LibraryIndex::instance().search("", "Footprint")) {
            footprints.append(r.name);
        }
        if (footprints.isEmpty()) {
            auto& fpLibMgr = FootprintLibraryManager::instance();
            for (auto* lib : fpLibMgr.libraries()) {
                footprints.append(lib->getFootprintNames());
            }
        }
        PCBNetlistImporter::suggestFootprints(pkg, footprints);
        PCBECOResolver::applyECO(pkg, &pcbScene, nullptr, nullptr);

        if (!PCBFileIO::savePCB(&pcbScene, pcbPath)) {
            std::cerr << "Failed to save PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        int changes = pkg.components.size() + pkg.nets.size();
        QJsonObject out;
        out["ok"] = true;
        out["file"] = pcbPath;
        out["sourceSchematic"] = schematicPath;
        out["changesCount"] = changes;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            if (!g_quiet) {
                std::cout << "Successfully synchronized PCB at " << pcbPath.toStdString() 
                          << " with schematic at " << schematicPath.toStdString() 
                          << " (" << changes << " components/nets imported)." << std::endl;
            }
        }

        return 0;
    }
};

class PcbNetlistCommand : public CLICommand {
public:
    QString name() const override { return "pcb-netlist"; }
    QString description() const override { return "Dump detailed netlist and connectivity report of a PCB file."; }
    void setupParser(QCommandLineParser& parser) override {
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{
            {"args", QJsonArray{"file.pcb"}},
            {"options", QJsonObject{
                {"json", "bool"}
            }}
        };
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-netlist <file.pcb>" << std::endl;
            return 1;
        }
        QString filePath = args.at(0);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        QJsonArray padsArray;
        QJsonArray tracesArray;
        QJsonArray viasArray;

        for (auto* item : scene.items()) {
            if (auto* pad = dynamic_cast<PadItem*>(item)) {
                QJsonObject p;
                QString compName = "Board";
                if (auto* comp = dynamic_cast<ComponentItem*>(pad->parentItem())) {
                    compName = comp->name();
                }
                p["component"] = compName;
                QString pinName = pad->name();
                if (pinName.isEmpty() && pad->model()) {
                    pinName = pad->model()->number();
                }
                p["pin"] = pinName;
                p["net"] = pad->netName();
                p["layer"] = pad->layer();
                p["x"] = pad->scenePos().x();
                p["y"] = pad->scenePos().y();
                p["drill"] = pad->drillSize();
                padsArray.append(p);
            } else if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                QJsonObject t;
                t["net"] = trace->netName();
                t["layer"] = trace->layer();
                t["width"] = trace->width();
                t["x1"] = trace->startPoint().x();
                t["y1"] = trace->startPoint().y();
                t["x2"] = trace->endPoint().x();
                t["y2"] = trace->endPoint().y();
                tracesArray.append(t);
            } else if (auto* via = dynamic_cast<ViaItem*>(item)) {
                QJsonObject v;
                v["net"] = via->netName();
                v["x"] = via->scenePos().x();
                v["y"] = via->scenePos().y();
                v["startLayer"] = via->startLayer();
                v["endLayer"] = via->endLayer();
                v["diameter"] = via->diameter();
                v["drill"] = via->drillSize();
                viasArray.append(v);
            }
        }

        QJsonObject out;
        out["ok"] = true;
        out["file"] = filePath;
        out["pads"] = padsArray;
        out["traces"] = tracesArray;
        out["vias"] = viasArray;

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "PCB Netlist & Connectivity Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n\n";

            std::cout << "Component Pads:\n";
            std::cout << "------------------------------------------------------------------------\n";
            QMap<QString, QList<QJsonObject>> compPads;
            for (const auto& val : padsArray) {
                QJsonObject p = val.toObject();
                compPads[p["component"].toString()].append(p);
            }
            for (auto it = compPads.begin(); it != compPads.end(); ++it) {
                std::cout << "Component " << it.key().toStdString() << ":\n";
                for (const auto& p : it.value()) {
                    std::cout << "  Pad " << p["pin"].toString().toStdString()
                              << ": Net='" << p["net"].toString().toStdString()
                              << "', Layer=" << p["layer"].toInt()
                              << ", Pos=(" << p["x"].toDouble() << ", " << p["y"].toDouble() << ")"
                              << (p["drill"].toDouble() > 0.001 ? " [THT]" : " [SMD]") << "\n";
                }
            }
            std::cout << "\n";

            std::cout << "Trace Segments (by Net):\n";
            std::cout << "------------------------------------------------------------------------\n";
            QMap<QString, QList<QJsonObject>> netTraces;
            for (const auto& val : tracesArray) {
                QJsonObject t = val.toObject();
                netTraces[t["net"].toString()].append(t);
            }
            for (auto it = netTraces.begin(); it != netTraces.end(); ++it) {
                std::cout << "Net '" << (it.key().isEmpty() ? "unassigned" : it.key().toStdString()) << "':\n";
                for (const auto& t : it.value()) {
                    std::cout << "  - Segment: (" << t["x1"].toDouble() << ", " << t["y1"].toDouble() << ") -> ("
                              << t["x2"].toDouble() << ", " << t["y2"].toDouble() << ")"
                              << ", Layer=" << t["layer"].toInt()
                              << ", Width=" << t["width"].toDouble() << "mm\n";
                }
            }
            std::cout << "\n";

            std::cout << "Vias (by Net):\n";
            std::cout << "------------------------------------------------------------------------\n";
            QMap<QString, QList<QJsonObject>> netVias;
            for (const auto& val : viasArray) {
                QJsonObject v = val.toObject();
                netVias[v["net"].toString()].append(v);
            }
            for (auto it = netVias.begin(); it != netVias.end(); ++it) {
                std::cout << "Net '" << (it.key().isEmpty() ? "unassigned" : it.key().toStdString()) << "':\n";
                for (const auto& v : it.value()) {
                    std::cout << "  - Via: Pos=(" << v["x"].toDouble() << ", " << v["y"].toDouble() << ")"
                              << ", Layers=" << v["startLayer"].toInt() << " to " << v["endLayer"].toInt()
                              << ", Drill=" << v["drill"].toDouble() << "mm"
                              << ", Dia=" << v["diameter"].toDouble() << "mm\n";
                }
            }
            std::cout << std::endl;
        }

        return 0;
    }
};

class PcbCleanupCommand : public CLICommand {
public:
    QString name() const override { return "pcb-cleanup"; }
    QString description() const override { return "Automated Board Cleanup: Purge dangling tracks/vias, zero-length tracks, duplicate vias, and merge collinear segments."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb"}}, {"options", QJsonObject{{"out", "string"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"file", "string"},
            {"outputFile", "string"},
            {"cleanedTotal", "int"},
            {"zeroLengthTracesRemoved", "int"},
            {"duplicateViasRemoved", "int"},
            {"danglingTracesRemoved", "int"},
            {"danglingViasRemoved", "int"},
            {"collinearTracesMerged", "int"}
        };
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-cleanup <file.pcb> [--out <output.pcb>]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        PCBTracksCleaner::Options opts;
        PCBTracksCleaner::Report report = PCBTracksCleaner::cleanBoard(&scene, opts);

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save cleaned PCB file to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"cleanedTotal", report.totalItemsCleaned()},
            {"zeroLengthTracesRemoved", report.zeroLengthTracesRemoved},
            {"duplicateViasRemoved", report.duplicateViasRemoved},
            {"danglingTracesRemoved", report.danglingTracesRemoved},
            {"danglingViasRemoved", report.danglingViasRemoved},
            {"collinearTracesMerged", report.collinearTracesMerged}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "PCB Board Cleanup Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Zero-Length Traces Removed : " << report.zeroLengthTracesRemoved << "\n";
            std::cout << "Duplicate Vias Removed    : " << report.duplicateViasRemoved << "\n";
            std::cout << "Dangling Traces Removed   : " << report.danglingTracesRemoved << "\n";
            std::cout << "Dangling Vias Removed     : " << report.danglingViasRemoved << "\n";
            std::cout << "Collinear Traces Merged   : " << report.collinearTracesMerged << "\n";
            std::cout << "------------------------------------------------------------------------\n";
            std::cout << "Total Items Cleaned       : " << report.totalItemsCleaned() << "\n";
            std::cout << "Output Saved To           : " << outPath.toStdString() << "\n";
        }

        return 0;
    }
};

class PcbTeardropsCommand : public CLICommand {
public:
    QString name() const override { return "pcb-teardrops"; }
    QString description() const override { return "Smart Teardrop Generator: Add or remove curved/filleted teardrop transitions on pads, vias, and traces."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
        parser.addOption(QCommandLineOption("remove", "Remove all teardrops from the PCB layout."));
        parser.addOption(QCommandLineOption("shape", "Teardrop shape: curved, fillet, arc (default: curved).", "shape", "curved"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb"}}, {"options", QJsonObject{{"out", "string"}, {"remove", "bool"}, {"shape", "string"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{
            {"file", "string"},
            {"outputFile", "string"},
            {"totalTeardropsAdded", "int"},
            {"padTeardropsAdded", "int"},
            {"viaTeardropsAdded", "int"},
            {"teardropsRemoved", "int"}
        };
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-teardrops <file.pcb> [--out <output.pcb>] [--remove] [--shape <curved|fillet|arc>]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        bool isRemove = parser.isSet("remove");
        int removedCount = 0;
        PCBTeardropGenerator::Report report;

        if (isRemove) {
            removedCount = PCBTeardropGenerator::removeTeardrops(&scene);
        } else {
            PCBTeardropGenerator::Options opts;
            QString shapeStr = parser.value("shape").toLower();
            if (shapeStr == "fillet") opts.shape = PCBTeardropGenerator::TeardropShape::Fillet;
            else if (shapeStr == "arc") opts.shape = PCBTeardropGenerator::TeardropShape::Arc;
            else opts.shape = PCBTeardropGenerator::TeardropShape::Curved;

            report = PCBTeardropGenerator::addTeardrops(&scene, opts);
        }

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save PCB file to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"totalTeardropsAdded", report.totalTeardropsAdded()},
            {"padTeardropsAdded", report.padTeardropsAdded},
            {"viaTeardropsAdded", report.viaTeardropsAdded},
            {"teardropsRemoved", removedCount}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "PCB Teardrop Generator Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            if (isRemove) {
                std::cout << "Teardrops Removed         : " << removedCount << "\n";
            } else {
                std::cout << "Pad Teardrops Added       : " << report.padTeardropsAdded << "\n";
                std::cout << "Via Teardrops Added       : " << report.viaTeardropsAdded << "\n";
                std::cout << "------------------------------------------------------------------------\n";
                std::cout << "Total Teardrops Added     : " << report.totalTeardropsAdded() << "\n";
            }
            std::cout << "Output Saved To           : " << outPath.toStdString() << "\n";
        }

        return 0;
    }
};

class PcbArrayCommand : public CLICommand {
public:
    QString name() const override { return "pcb-array"; }
    QString description() const override { return "Grid & Circular Array Placement Generator for PCB items."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
        parser.addOption(QCommandLineOption("mode", "Array mode: rect or polar (default: rect).", "mode", "rect"));
        parser.addOption(QCommandLineOption("count", "Number of array items.", "count", "8"));
        parser.addOption(QCommandLineOption("radius", "Radius for polar array (mm).", "radius", "15.0"));
        parser.addOption(QCommandLineOption("cols", "Grid columns.", "cols", "3"));
        parser.addOption(QCommandLineOption("rows", "Grid rows.", "rows", "3"));
        parser.addOption(QCommandLineOption("dx", "Delta X spacing (mm).", "dx", "5.0"));
        parser.addOption(QCommandLineOption("dy", "Delta Y spacing (mm).", "dy", "5.0"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-array <file.pcb> [--out <output.pcb>] [--mode <rect|polar>]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        QList<PCBItem*> sourceItems;
        for (auto* item : scene.items()) {
            if (PCBItem* pcb = dynamic_cast<PCBItem*>(item)) {
                sourceItems.append(pcb);
                break; // Use first item as array source if no selection
            }
        }

        if (sourceItems.isEmpty()) {
            std::cerr << "Error: No items found in PCB layout to array." << std::endl;
            return 1;
        }

        PCBArrayGenerator::Options opts;
        QString modeStr = parser.value("mode").toLower();
        if (modeStr == "polar" || modeStr == "circular") {
            opts.mode = PCBArrayGenerator::ArrayMode::Circular;
            opts.count = parser.value("count").toInt();
            opts.radius = parser.value("radius").toDouble();
        } else {
            opts.mode = PCBArrayGenerator::ArrayMode::Rectangular;
            opts.cols = parser.value("cols").toInt();
            opts.rows = parser.value("rows").toInt();
            opts.deltaX = parser.value("dx").toDouble();
            opts.deltaY = parser.value("dy").toDouble();
        }

        PCBArrayGenerator::Report report = PCBArrayGenerator::createArray(&scene, sourceItems, opts);

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save PCB file to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"itemsCreated", report.itemsCreated}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "PCB Array Generator Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Items Created             : " << report.itemsCreated << "\n";
            std::cout << "Output Saved To           : " << outPath.toStdString() << "\n";
        }

        return 0;
    }
};

class PcbViaFenceCommand : public CLICommand {
public:
    QString name() const override { return "pcb-via-fence"; }
    QString description() const override { return "RF & High-Speed Via Fencing Generator."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
        parser.addOption(QCommandLineOption("net", "Via net name.", "net", "GND"));
        parser.addOption(QCommandLineOption("pitch", "Via pitch spacing (mm).", "pitch", "1.5"));
        parser.addOption(QCommandLineOption("offset", "Fence offset distance (mm).", "offset", "1.0"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-via-fence <file.pcb> [--out <output.pcb>] [--net GND] [--pitch 1.5]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        QList<TraceItem*> traces;
        for (auto* item : scene.items()) {
            if (auto* t = dynamic_cast<TraceItem*>(item)) traces.append(t);
        }

        PCBViaFenceGenerator::Options opts;
        opts.netName = parser.value("net");
        opts.viaPitch = parser.value("pitch").toDouble();
        opts.offsetDistance = parser.value("offset").toDouble();

        PCBViaFenceGenerator::Report report = PCBViaFenceGenerator::generateViaFence(&scene, traces, opts);

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save PCB file to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"viasPlaced", report.viasPlaced}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "PCB Via Fence Generator Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Fence Vias Placed         : " << report.viasPlaced << "\n";
            std::cout << "Output Saved To           : " << outPath.toStdString() << "\n";
        }

        return 0;
    }
};

class PcbStackupCommand : public CLICommand {
public:
    QString name() const override { return "pcb-stackup"; }
    QString description() const override { return "Layer Stackup & Dielectric Material Impedance Calculator."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("layers", "Number of copper layers.", "layers", "2"));
        parser.addOption(QCommandLineOption("width", "Trace width for Z0 calculation (mm).", "width", "0.25"));
        parser.addOption(QCommandLineOption("space", "Trace spacing for Zdiff calculation (mm).", "space", "0.20"));
        parser.addOption(QCommandLineOption("thick", "Dielectric thickness (mm).", "thick", "0.20"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        int layersCount = parser.value("layers").toInt();
        double w = parser.value("width").toDouble();
        double s = parser.value("space").toDouble();
        double h = parser.value("thick").toDouble();

        auto stackup = PCBStackupManager::createStandardStackup(layersCount);
        double z0 = PCBStackupManager::calculateMicrostripZ0(w, h);
        double zDiff = PCBStackupManager::calculateDiffImpedance(w, s, h);

        QJsonObject jsonOut = PCBStackupManager::toJson(stackup);
        jsonOut["z0_Ohms"] = z0;
        jsonOut["zDiff_Ohms"] = zDiff;

        if (parser.isSet("json")) {
            printJsonValue(jsonOut);
        } else {
            std::cout << "PCB Board Substrate Stackup & Impedance Report:\n";
            std::cout << "========================================================================\n";
            std::cout << "Copper Layers Count        : " << stackup.copperLayers << "\n";
            std::cout << "Total Substrate Thickness  : " << stackup.totalThicknessMm << " mm\n";
            std::cout << "------------------------------------------------------------------------\n";
            std::cout << "Microstrip Z0 Impedance   : " << z0 << " Ohms (Width=" << w << "mm, H=" << h << "mm)\n";
            std::cout << "Differential Zdiff        : " << zDiff << " Ohms (Width=" << w << "mm, Space=" << s << "mm)\n";
        }

        return 0;
    }
};

class Pcb3DExportCommand : public CLICommand {
public:
    QString name() const override { return "pcb-3d-export"; }
    QString description() const override { return "3D STEP, STL, OBJ, VRML, GLTF MCAD Board Exporter."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output 3D MCAD file path.", "output"));
        parser.addOption(QCommandLineOption("format", "3D Format: stl, obj, step, vrml, gltf (default: stl).", "format", "stl"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-3d-export <file.pcb> --out <output.stl|obj|step|vrml|gltf> [--format stl|obj|step|vrml|gltf]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) {
            outPath = QFileInfo(filePath).completeBaseName() + ".stl";
        }

        QString format = parser.value("format").toLower();
        if (outPath.endsWith(".obj", Qt::CaseInsensitive)) format = "obj";
        else if (outPath.endsWith(".step", Qt::CaseInsensitive) || outPath.endsWith(".stp", Qt::CaseInsensitive)) format = "step";
        else if (outPath.endsWith(".vrml", Qt::CaseInsensitive) || outPath.endsWith(".wrl", Qt::CaseInsensitive)) format = "vrml";
        else if (outPath.endsWith(".gltf", Qt::CaseInsensitive) || outPath.endsWith(".glb", Qt::CaseInsensitive)) format = "gltf";

        QString error;
        bool ok = false;
        if (format == "obj") ok = MCADExporter::exportOBJ3D(&scene, outPath, &error);
        else if (format == "step" || format == "stp") ok = MCADExporter::exportSTEPWireframe(&scene, outPath, &error);
        else if (format == "vrml" || format == "wrl") ok = MCADExporter::exportVRMLAssembly(&scene, outPath, &error);
        else if (format == "gltf" || format == "glb") ok = MCADExporter::exportGLTF3D(&scene, outPath, &error);
        else ok = MCADExporter::exportSTL3D(&scene, outPath, &error);

        if (!ok) {
            std::cerr << "Error: 3D MCAD Export failed: " << error.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"format", format},
            {"status", "success"}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "3D MCAD Board Export Report:\n";
            std::cout << "========================================================================\n";
            std::cout << "Input Layout              : " << filePath.toStdString() << "\n";
            std::cout << "3D Export Format          : " << format.toUpper().toStdString() << "\n";
            std::cout << "Output File               : " << outPath.toStdString() << "\n";
            std::cout << "Status                    : SUCCESS\n";
        }

        return 0;
    }
};

class PcbExportIpcCommand : public CLICommand {
public:
    QString name() const override { return "pcb-export-ipc"; }
    QString description() const override { return "IPC-2581 / ODB++ Manufacturing Package Exporter."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output file/directory path.", "output"));
        parser.addOption(QCommandLineOption("format", "Format: ipc2581, odbpp, pickplace (default: ipc2581).", "format", "ipc2581"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-export-ipc <file.pcb> --out <output.xml|dir> [--format ipc2581|odbpp|pickplace]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        QString format = parser.value("format").toLower();
        QString outPath = parser.value("out");
        if (outPath.isEmpty()) {
            if (format == "odbpp") outPath = QFileInfo(filePath).completeBaseName() + "_odbpp";
            else if (format == "pickplace") outPath = QFileInfo(filePath).completeBaseName() + "_pos.csv";
            else outPath = QFileInfo(filePath).completeBaseName() + "_ipc2581.xml";
        }

        QString error;
        bool ok = false;
        if (format == "odbpp") {
            ok = ManufacturingExporter::exportODBppPackage(&scene, outPath, &error);
        } else if (format == "pickplace" || format == "pos" || format == "centroid") {
            ManufacturingExporter::PickPlaceOptions opts;
            ok = ManufacturingExporter::exportPickPlace(&scene, outPath, opts, &error);
        } else {
            ok = ManufacturingExporter::exportIPC2581(&scene, outPath, &error);
        }

        if (!ok) {
            std::cerr << "Error: Manufacturing export failed: " << error.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"output", outPath},
            {"format", format},
            {"status", "success"}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "Manufacturing Export Report:\n";
            std::cout << "========================================================================\n";
            std::cout << "Input PCB Layout          : " << filePath.toStdString() << "\n";
            std::cout << "Export Format             : " << format.toUpper().toStdString() << "\n";
            std::cout << "Output File / Directory   : " << outPath.toStdString() << "\n";
            std::cout << "Status                    : SUCCESS\n";
        }

        return 0;
    }
};

class PcbTuneLengthCommand : public CLICommand {
public:
    QString name() const override { return "pcb-tune-length"; }
    QString description() const override { return "High-Speed Serpentine Meander & Differential Pair Length Tuning Engine."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
        parser.addOption(QCommandLineOption("net", "Target net name to tune.", "net"));
        parser.addOption(QCommandLineOption("add", "Extra length to add (mm).", "add", "5.0"));
        parser.addOption(QCommandLineOption("amplitude", "Serpentine amplitude (mm).", "amplitude", "1.5"));
        parser.addOption(QCommandLineOption("spacing", "Serpentine spacing (mm).", "spacing", "0.3"));
        parser.addOption(QCommandLineOption("pnet", "Differential pair P net name.", "pnet"));
        parser.addOption(QCommandLineOption("nnet", "Differential pair N net name.", "nnet"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-tune-length <file.pcb> --net <netName> --add 5.0 [--out <output.pcb>]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;

        QString netName = parser.value("net");
        QString pNet = parser.value("pnet");
        QString nNet = parser.value("nnet");

        int tunedCount = 0;
        double addedLength = 0.0;

        if (!pNet.isEmpty() && !nNet.isEmpty()) {
            tunedCount = LengthMatchManager::instance().autoTuneDiffPair(pNet, nNet, &scene);
        } else if (!netName.isEmpty()) {
            SerpentineGenerator gen(&scene);
            SerpentineGenerator::SerpentineConfig cfg;
            cfg.netName = netName;
            cfg.extraLength = parser.value("add").toDouble();
            cfg.amplitude = parser.value("amplitude").toDouble();
            cfg.spacing = parser.value("spacing").toDouble();
            auto res = gen.generateSerpentine(cfg);
            if (res.success) {
                tunedCount = res.segmentsCreated;
                addedLength = res.actualAddedLength;
            } else {
                std::cerr << "Error: Serpentine generation failed: " << res.error.toStdString() << std::endl;
                return 1;
            }
        } else {
            // Auto-tune all un-matched nets in scene
            tunedCount = LengthMatchManager::instance().autoTuneAll(&scene);
        }

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save tuned PCB to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"segmentsCreated", tunedCount},
            {"addedLengthMm", addedLength},
            {"status", "success"}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "High-Speed Length Tuning Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Serpentine Segments Added : " << tunedCount << "\n";
            std::cout << "Actual Added Length       : " << addedLength << " mm\n";
            std::cout << "Output Saved To           : " << outPath.toStdString() << "\n";
            std::cout << "Status                    : SUCCESS\n";
        }

        return 0;
    }
};

class PcbDrcCommand : public CLICommand {
public:
    QString name() const override { return "pcb-drc"; }
    QString description() const override { return "Run Automated Design Rule Check (DRC) on a PCB layout."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("clearance", "Minimum copper clearance (mm).", "clearance", "0.2"));
        parser.addOption(QCommandLineOption("min-width", "Minimum trace width (mm).", "min-width", "0.2"));
        parser.addOption(QCommandLineOption("min-drill", "Minimum via drill size (mm).", "min-drill", "0.3"));
        parser.addOption(QCommandLineOption("out", "Output PCB file path.", "out"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-drc <file.pcb> [options]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error: Could not load PCB file: " << filePath.toStdString() << std::endl;
            return 1;
        }

        PCBDRC drc;
        if (parser.isSet("clearance")) {
            drc.rules().setMinClearance(parser.value("clearance").toDouble());
        }
        if (parser.isSet("min-width")) {
            drc.rules().setMinTraceWidth(parser.value("min-width").toDouble());
        }
        if (parser.isSet("min-drill")) {
            drc.rules().setMinDrillSize(parser.value("min-drill").toDouble());
        }

        drc.runFullCheck(&scene);

        const auto& violations = drc.violations();
        int errors = drc.errorCount();
        int warnings = drc.warningCount();

        QJsonArray violationArr;
        for (const auto& v : violations) {
            QJsonObject obj;
            obj["type"] = v.typeString();
            obj["severity"] = v.severityString();
            obj["message"] = v.message();
            obj["posX"] = v.location().x();
            obj["posY"] = v.location().y();
            violationArr.append(obj);
        }

        QJsonObject out{
            {"file", filePath},
            {"errorCount", errors},
            {"warningCount", warnings},
            {"totalViolations", violations.size()},
            {"violations", violationArr},
            {"status", (errors == 0) ? "PASS" : "FAIL"}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "Design Rule Check (DRC) Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Errors Detected   : " << errors << "\n";
            std::cout << "Warnings Detected : " << warnings << "\n";
            std::cout << "DRC Status        : " << (errors == 0 ? "PASSED (0 Errors)" : "FAILED (Violations Found)") << "\n";
            std::cout << "------------------------------------------------------------------------\n";
            for (int i = 0; i < violations.size(); ++i) {
                const auto& v = violations[i];
                std::cout << QString("[%1] %2 at (%3, %4) mm: %5\n")
                             .arg(v.severityString().toUpper())
                             .arg(i + 1)
                             .arg(v.location().x(), 0, 'f', 2)
                             .arg(v.location().y(), 0, 'f', 2)
                             .arg(v.message()).toStdString();
            }
        }

        if (parser.isSet("out")) {
            QString outPath = parser.value("out");
            PCBFileIO::savePCB(&scene, outPath);
            if (!parser.isSet("json")) {
                std::cout << "PCB layout saved to: " << outPath.toStdString() << "\n";
            }
        }

        return (errors == 0) ? 0 : 2;
    }
};

class PcbImportKicadCommand : public CLICommand {
public:
    QString name() const override { return "pcb-import-kicad"; }
    QString description() const override { return "Import a KiCad PCB layout (.kicad_pcb) into VioraEDA PCB format."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output VioraEDA PCB file path.", "output"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-import-kicad <file.kicad_pcb> -o <out.pcb>" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        auto stats = KiCadPCBImporter::importKiCadPCB(filePath, &scene);

        if (!stats.success) {
            std::cerr << "Error: Import failed: " << stats.error.toStdString() << std::endl;
            return 1;
        }

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) {
            outPath = filePath;
            outPath.replace(QRegularExpression("\\.kicad_pcb$", QRegularExpression::CaseInsensitiveOption), ".pcb");
        }

        if (!PCBFileIO::savePCB(&scene, outPath)) {
            std::cerr << "Error: Failed to save imported PCB to: " << outPath.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out{
            {"file", filePath},
            {"outputFile", outPath},
            {"netsCount", stats.netsCount},
            {"tracesCount", stats.tracesCount},
            {"viasCount", stats.viasCount},
            {"footprintsCount", stats.footprintsCount},
            {"edgeCutsCount", stats.edgeCutsCount},
            {"status", "success"}
        };

        if (parser.isSet("json")) {
            printJsonValue(out);
        } else {
            std::cout << "KiCad PCB Import Report for: " << filePath.toStdString() << "\n";
            std::cout << "========================================================================\n";
            std::cout << "Nets Imported       : " << stats.netsCount << "\n";
            std::cout << "Traces Imported     : " << stats.tracesCount << "\n";
            std::cout << "Vias Imported       : " << stats.viasCount << "\n";
            std::cout << "Footprints Imported : " << stats.footprintsCount << "\n";
            std::cout << "Edge Cuts Lines     : " << stats.edgeCutsCount << "\n";
            std::cout << "Output Saved To     : " << outPath.toStdString() << "\n";
            std::cout << "Status              : SUCCESS\n";
        }

        return 0;
    }
};

class PcbAutoRouteCommand : public CLICommand {
public:
    QString name() const override { return "pcb-autoroute"; }
    QString description() const override { return "Run Multi-Layer Auto-Router on a PCB layout file."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption({"out", "o"}, "Output PCB file path.", "output"));
        parser.addOption(QCommandLineOption("ripup", "Rip up all existing traces and vias before routing"));
        parser.addOption(QCommandLineOption("grid", "Grid spacing in mm (default 0.5)", "grid", "0.5"));
    }
    QJsonObject inputSchema() const override { return QJsonObject(); }
    QJsonObject outputSchema() const override { return QJsonObject(); }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora pcb-autoroute <file.pcb> [-o out.pcb] [--ripup] [--grid 0.5]" << std::endl;
            return 1;
        }

        QString filePath = args.first();
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (parser.isSet("ripup")) {
            QList<QGraphicsItem*> toDelete;
            for (auto* item : scene.items()) {
                if (dynamic_cast<TraceItem*>(item) || dynamic_cast<ViaItem*>(item)) {
                    toDelete.append(item);
                }
            }
            for (auto* item : toDelete) {
                scene.removeItem(item);
                delete item;
            }
        }

        PCBAutoRouter router(&scene);
        PCBAutoRouter::RouterConfig config;
        if (parser.isSet("grid")) {
            config.gridSpacing = parser.value("grid").toDouble();
        }
        config.enableDirectionalBias = true;

        auto stats = router.routeAll(config);

        QString outPath = parser.value("out");
        if (outPath.isEmpty()) outPath = filePath;
        PCBFileIO::savePCB(&scene, outPath);

        if (parser.isSet("json")) {
            QJsonObject json;
            json["file"] = filePath;
            json["output"] = outPath;
            json["totalConnections"] = stats.totalConnections;
            json["routedConnections"] = stats.routedConnections;
            json["failedConnections"] = stats.failedConnections;
            json["totalTraceLength"] = stats.totalTraceLength;
            json["iterations"] = stats.iterations;
            QJsonDocument doc(json);
            std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << "\n";
        } else {
            std::cout << "--- AUTO-ROUTER RESULTS ---\n";
            std::cout << "Connections Routed : " << stats.routedConnections << " / " << stats.totalConnections << "\n";
            std::cout << "Iterations         : " << stats.iterations << "\n";
            std::cout << "Output File        : " << outPath.toStdString() << "\n";
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
    reg.registerCommand(std::make_unique<PcbSyncCommand>());
    reg.registerCommand(std::make_unique<PcbShrinkCommand>());
    reg.registerCommand(std::make_unique<PcbNetlistCommand>());
    reg.registerCommand(std::make_unique<PcbExportCommand>());
    reg.registerCommand(std::make_unique<PcbCleanupCommand>());
    reg.registerCommand(std::make_unique<PcbTeardropsCommand>());
    reg.registerCommand(std::make_unique<PcbArrayCommand>());
    reg.registerCommand(std::make_unique<PcbViaFenceCommand>());
    reg.registerCommand(std::make_unique<PcbStackupCommand>());
    reg.registerCommand(std::make_unique<Pcb3DExportCommand>());
    reg.registerCommand(std::make_unique<PcbExportIpcCommand>());
    reg.registerCommand(std::make_unique<PcbTuneLengthCommand>());
    reg.registerCommand(std::make_unique<PcbDrcCommand>());
    reg.registerCommand(std::make_unique<PcbImportKicadCommand>());
    reg.registerCommand(std::make_unique<PcbAutoRouteCommand>());
}
