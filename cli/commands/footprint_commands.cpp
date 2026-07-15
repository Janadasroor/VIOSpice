/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_commands.h"
#include "common.h"
#include "../command_registry.h"
#include "footprints/models/footprint_definition.h"
#include "footprints/models/footprint_primitive.h"
#include "footprints/models/footprint_schema.h"
#include "footprints/kicad_footprint_importer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QGuiApplication>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <qmath.h>

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;

namespace {

QColor getFootprintLayerColor(Flux::Model::FootprintPrimitive::Layer layer) {
    switch (layer) {
    case Flux::Model::FootprintPrimitive::Top_Copper:
        return QColor(203, 75, 75); // red/copper
    case Flux::Model::FootprintPrimitive::Bottom_Copper:
        return QColor(31, 153, 93); // green/copper
    case Flux::Model::FootprintPrimitive::Top_Silkscreen:
        return QColor(240, 240, 200); // warm white/yellow
    case Flux::Model::FootprintPrimitive::Bottom_Silkscreen:
        return QColor(200, 150, 100);
    case Flux::Model::FootprintPrimitive::Top_SolderMask:
        return QColor(153, 51, 153, 120); // purple
    case Flux::Model::FootprintPrimitive::Bottom_SolderMask:
        return QColor(102, 51, 153, 120);
    case Flux::Model::FootprintPrimitive::Top_Courtyard:
        return QColor(255, 105, 180, 150); // pink
    case Flux::Model::FootprintPrimitive::Bottom_Courtyard:
        return QColor(255, 182, 193, 150);
    case Flux::Model::FootprintPrimitive::Top_Fabrication:
        return QColor(100, 149, 237); // cornflower blue
    case Flux::Model::FootprintPrimitive::Bottom_Fabrication:
        return QColor(70, 130, 180);
    default:
        return QColor(200, 200, 200); // light gray
    }
}

bool renderFootprintToPng(const FootprintDefinition& footprint, const QString& outPath, bool transparent = false, qreal scale = 20.0) {
    const bool isHeadless = (QGuiApplication::platformName() == "offscreen");
    QRectF rect = footprint.boundingRect();
    if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
        rect = QRectF(-10, -10, 20, 20);
    }

    const qreal margin = 2.0; // 2mm margin
    QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                            qCeil((rect.height() + margin * 2.0) * scale));

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // Scale and translate to center the footprint in the image
    painter.scale(scale, scale);
    painter.translate(-rect.x() + margin, -rect.y() + margin);

    auto parsePoints = [](const QJsonArray& arr) {
        QPolygonF poly;
        for (const auto& val : arr) {
            QJsonObject pt = val.toObject();
            poly << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
        }
        return poly;
    };

    for (const auto& prim : footprint.primitives()) {
        switch (prim.type) {
        case FootprintPrimitive::Line: {
            qreal x1 = prim.data.value("x1").toDouble();
            qreal y1 = prim.data.value("y1").toDouble();
            qreal x2 = prim.data.value("x2").toDouble();
            qreal y2 = prim.data.value("y2").toDouble();
            qreal w = prim.data.value("width").toDouble(0.15);
            painter.setPen(QPen(getFootprintLayerColor(prim.layer), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
            break;
        }
        case FootprintPrimitive::Rect: {
            qreal x = prim.data.value("x").toDouble();
            qreal y = prim.data.value("y").toDouble();
            qreal w = prim.data.value("width").toDouble();
            qreal h = prim.data.value("height").toDouble();
            qreal lw = prim.data.value("lineWidth").toDouble(0.15);
            bool filled = prim.data.value("filled").toBool();
            QColor color = getFootprintLayerColor(prim.layer);
            painter.setPen(QPen(color, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (filled) {
                painter.setBrush(color);
            } else {
                painter.setBrush(Qt::NoBrush);
            }
            painter.drawRect(QRectF(x, y, w, h));
            break;
        }
        case FootprintPrimitive::Circle: {
            qreal cx = prim.data.value("cx").toDouble();
            qreal cy = prim.data.value("cy").toDouble();
            qreal r = prim.data.value("radius").toDouble();
            qreal lw = prim.data.value("lineWidth").toDouble(0.15);
            bool filled = prim.data.value("filled").toBool();
            QColor color = getFootprintLayerColor(prim.layer);
            painter.setPen(QPen(color, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (filled) {
                painter.setBrush(color);
            } else {
                painter.setBrush(Qt::NoBrush);
            }
            painter.drawEllipse(QPointF(cx, cy), r, r);
            break;
        }
        case FootprintPrimitive::Arc: {
            qreal cx = prim.data.value("cx").toDouble();
            qreal cy = prim.data.value("cy").toDouble();
            qreal r = prim.data.value("radius").toDouble();
            qreal lw = prim.data.value("lineWidth").toDouble(0.15);
            qreal startAngle = prim.data.value("startAngle").toDouble();
            qreal spanAngle = prim.data.value("spanAngle").toDouble();
            painter.setPen(QPen(getFootprintLayerColor(prim.layer), lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(QRectF(cx - r, cy - r, r * 2.0, r * 2.0), qRound(startAngle * 16.0), qRound(spanAngle * 16.0));
            break;
        }
        case FootprintPrimitive::Polygon: {
            QPolygonF poly = parsePoints(prim.data.value("points").toArray());
            QColor color = getFootprintLayerColor(prim.layer);
            painter.setPen(QPen(color, 0.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(color);
            painter.drawPolygon(poly);
            break;
        }
        case FootprintPrimitive::Text: {
            if (!isHeadless) {
                qreal x = prim.data.value("x").toDouble();
                qreal y = prim.data.value("y").toDouble();
                qreal height = prim.data.value("height").toDouble(1.0);
                QString text = prim.data.value("text").toString();
                painter.setPen(getFootprintLayerColor(prim.layer));
                QFont font = painter.font();
                font.setPointSizeF(height);
                painter.setFont(font);
                painter.drawText(QPointF(x, y), text);
            }
            break;
        }
        case FootprintPrimitive::Pad: {
            qreal px = prim.data.value("x").toDouble();
            qreal py = prim.data.value("y").toDouble();
            qreal w = prim.data.value("width").toDouble();
            qreal h = prim.data.value("height").toDouble();
            QString shape = prim.data.value("shape").toString();
            QString num = prim.data.value("number").toString();
            qreal drill = prim.data.value("drill_size").toDouble();
            qreal rotation = prim.data.value("rotation").toDouble(0.0);
            bool isThroughHole = (drill > 0.001);

            QColor padColor = isThroughHole ? QColor(0, 150, 136) : getFootprintLayerColor(prim.layer);
            
            painter.save();
            painter.translate(px, py);
            if (qAbs(rotation) > 0.01) {
                painter.rotate(rotation);
            }

            painter.setPen(QPen(padColor.darker(150), 0.05));
            painter.setBrush(padColor);

            QRectF padRect(-w / 2.0, -h / 2.0, w, h);
            if (shape == "Circle" || shape == "Round" || shape == "Oval") {
                painter.drawEllipse(padRect);
            } else if (shape == "Oblong") {
                qreal r = std::min(w, h) / 2.0;
                painter.drawRoundedRect(padRect, r, r);
            } else if (shape == "RoundedRect") {
                qreal r = std::min(w, h) * 0.25;
                painter.drawRoundedRect(padRect, r, r);
            } else if (shape == "Custom" && prim.data.contains("custom_primitives")) {
                QJsonArray customPrims = prim.data.value("custom_primitives").toArray();
                for (const auto& val : customPrims) {
                    QJsonObject gp = val.toObject();
                    QPolygonF poly = parsePoints(gp.value("points").toArray());
                    painter.drawPolygon(poly);
                }
            } else {
                painter.drawRect(padRect);
            }

            if (isThroughHole) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(30, 30, 30));
                qreal drillY = prim.data.value("drill_size_y").toDouble(drill);
                if (drillY != drill) {
                    QRectF drillRect(-drill / 2.0, -drillY / 2.0, drill, drillY);
                    qreal r = std::min(drill, drillY) / 2.0;
                    painter.drawRoundedRect(drillRect, r, r);
                } else {
                    painter.drawEllipse(QPointF(0, 0), drill / 2.0, drill / 2.0);
                }
            }

            // Draw pad number
            if (!isHeadless) {
                painter.setPen(padColor.lightness() < 140 ? Qt::white : Qt::black);
                qreal fontSize = std::min(w, h) * 0.4;
                QFont numFont = painter.font();
                numFont.setPointSizeF(fontSize);
                painter.setFont(numFont);
                painter.drawText(padRect, Qt::AlignCenter, num);
            }

            painter.restore();
            break;
        }
        default:
            break;
        }
    }

    painter.end();
    return image.save(outPath);
}

class FootprintRenderCommand : public CLICommand {
public:
    QString name() const override { return "footprint-render"; }
    QString description() const override { return "Render a footprint definition (.json) to PNG."; }
    
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("transparent", "Render PNG with transparent background"));
        parser.addOption(QCommandLineOption("scale", "Render scale (default 20.0 for mm units)", "scale", "20"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
    }
    
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.json", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}};
    }
    
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"output", "string"}, {"transparent", "bool"}, {"scale", "number"}};
    }
    
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora footprint-render <file.json> <out.png> [options]" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        const QString outPath = args.at(1);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read footprint file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray bytes = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid footprint JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }

        QJsonObject obj = doc.object();
        FootprintDefinition footprint = FootprintDefinition::fromJson(obj);
        if (footprint.name().trimmed().isEmpty()) {
            footprint.setName(QFileInfo(filePath).completeBaseName());
        }

        const bool transparent = parser.isSet("transparent");
        bool ok = false;
        qreal scale = parser.value("scale").toDouble(&ok);
        if (!ok || scale <= 0.1) {
            scale = 20.0;
        }
        
        if (!renderFootprintToPng(footprint, outPath, transparent, scale)) {
            std::cerr << "Error: Failed to render footprint to " << outPath.toStdString() << std::endl;
            return 1;
        }
        
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["output"] = outPath;
            out["transparent"] = transparent;
            out["scale"] = scale;
            printJsonValue(out);
        } else {
            printInfoStd("Rendered footprint to " + outPath.toStdString());
        }
        return 0;
    }
};

class FootprintImportCommand : public CLICommand {
public:
    QString name() const override { return "footprint-import"; }
    QString description() const override { return "Import KiCad footprint (.kicad_mod) to VioraEDA (.json)."; }
    
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("render", "Auto-render imported footprints to PNG"));
        parser.addOption(QCommandLineOption("limit", "Limit number of footprints to process (default: unlimited)", "limit"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
    }
    
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"input_path", "output_dir"}}, {"options", QJsonObject{{"render", "bool"}, {"limit", "number"}, {"json", "bool"}}}};
    }
    
    QJsonObject outputSchema() const override {
        return QJsonObject{{"imported", "number"}, {"rendered", "number"}, {"results", "array"}};
    }
    
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora footprint-import <input_path> <output_dir> [options]" << std::endl;
            return 1;
        }
        
        const QString inputPath = args.at(0);
        const QString outputDir = args.at(1);
        
        QDir outDir(outputDir);
        if (!outDir.exists()) {
            outDir.mkpath(".");
        }
        
        QStringList filesToProcess;
        QFileInfo inputInfo(inputPath);
        if (inputInfo.isDir()) {
            QDir dir(inputPath);
            QStringList filters;
            filters << "*.kicad_mod";
            dir.setNameFilters(filters);
            QFileInfoList entryList = dir.entryInfoList(QDir::Files);
            for (const auto& info : entryList) {
                filesToProcess << info.absoluteFilePath();
            }
        } else if (inputInfo.exists()) {
            filesToProcess << inputInfo.absoluteFilePath();
        } else {
            std::cerr << "Error: Input path does not exist: " << inputPath.toStdString() << std::endl;
            return 1;
        }
        
        int limit = -1;
        if (parser.isSet("limit")) {
            bool ok = false;
            limit = parser.value("limit").toInt(&ok);
            if (!ok) limit = -1;
        }
        
        const bool autoRender = parser.isSet("render");
        int importedCount = 0;
        int renderedCount = 0;
        QJsonArray resultsArray;
        
        for (const QString& filePath : filesToProcess) {
            if (limit > 0 && importedCount >= limit) {
                break;
            }
            
            QStringList fpNames = KicadFootprintImporter::getFootprintNames(filePath);
            if (fpNames.isEmpty()) {
                FootprintDefinition footprint = KicadFootprintImporter::importFootprint(filePath);
                if (footprint.isValid()) {
                    QString name = footprint.name();
                    if (name.isEmpty()) {
                        name = QFileInfo(filePath).completeBaseName();
                        footprint.setName(name);
                    }
                    
                    QString cleanName = name;
                    cleanName.replace("/", "_").replace("\\", "_");
                    QString outPath = outDir.filePath(cleanName + ".json");
                    
                    QFile outFile(outPath);
                    if (outFile.open(QIODevice::WriteOnly)) {
                        QJsonDocument doc(footprint.toJson());
                        outFile.write(doc.toJson(QJsonDocument::Indented));
                        outFile.close();
                        
                        QJsonObject item;
                        item["name"] = name;
                        item["file"] = outPath;
                        
                        importedCount++;
                        
                        if (autoRender) {
                            QString pngPath = outDir.filePath(cleanName + ".png");
                            if (renderFootprintToPng(footprint, pngPath, true, 20.0)) {
                                item["rendered"] = pngPath;
                                renderedCount++;
                            }
                        }
                        resultsArray.append(item);
                    }
                }
            } else {
                for (const QString& fpName : fpNames) {
                    if (limit > 0 && importedCount >= limit) {
                        break;
                    }
                    
                    FootprintDefinition footprint = KicadFootprintImporter::importFootprint(filePath, fpName);
                    if (footprint.isValid()) {
                        QString name = footprint.name();
                        if (name.isEmpty()) name = fpName;
                        
                        QString cleanName = name;
                        cleanName.replace("/", "_").replace("\\", "_");
                        QString outPath = outDir.filePath(cleanName + ".json");
                        
                        QFile outFile(outPath);
                        if (outFile.open(QIODevice::WriteOnly)) {
                            QJsonDocument doc(footprint.toJson());
                            outFile.write(doc.toJson(QJsonDocument::Indented));
                            outFile.close();
                            
                            QJsonObject item;
                            item["name"] = name;
                            item["file"] = outPath;
                            
                            importedCount++;
                            
                            if (autoRender) {
                                QString pngPath = outDir.filePath(cleanName + ".png");
                                if (renderFootprintToPng(footprint, pngPath, true, 20.0)) {
                                    item["rendered"] = pngPath;
                                    renderedCount++;
                                }
                            }
                            resultsArray.append(item);
                        }
                    }
                }
            }
        }
        
        if (parser.isSet("json")) {
            QJsonObject out;
            out["imported"] = importedCount;
            out["rendered"] = renderedCount;
            out["results"] = resultsArray;
            printJsonValue(out);
        } else {
            printInfoStd("Successfully imported " + std::to_string(importedCount) + " footprints.");
            if (autoRender) {
                printInfoStd("Successfully rendered " + std::to_string(renderedCount) + " footprints to PNG.");
            }
        }
        
        return 0;
    }
};

} // namespace

void registerFootprintCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<FootprintRenderCommand>());
    reg.registerCommand(std::make_unique<FootprintImportCommand>());
}
