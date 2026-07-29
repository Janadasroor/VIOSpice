/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "manufacturing_exporter.h"

#include "../gerber/gerber_exporter.h"
#include "../gerber/nc_drill_exporter.h"
#include "../items/component_item.h"
#include "../items/pad_item.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../layers/pcb_layer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

bool ManufacturingExporter::exportIPC2581(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene.";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open IPC-2581 output file.";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<IPC-2581 revision=\"C\" xmlns=\"http://webstds.ipc.org/2581\">\n";
    out << "  <Header>\n";
    out << "    <Source software=\"Viora EDA\" version=\"0.2\"/>\n";
    out << "    <Date>" << ts << "</Date>\n";
    out << "    <Units>MM</Units>\n";
    out << "    <Spec revision=\"C\"/>\n";
    out << "  </Header>\n";

    // Layer Stackup & Dielectrics
    out << "  <Content>\n";
    out << "    <LayerStackup name=\"PRIMARY_STACKUP\">\n";
    const auto stackup = PCBLayerManager::instance().stackup();
    for (const auto& layer : stackup.stack) {
        out << "      <Layer name=\"" << layer.name << "\" type=\"" << layer.type
            << "\" thicknessMm=\"" << QString::number(layer.thickness, 'f', 4) << "\" material=\"" << layer.material
            << "\" er=\"" << QString::number(layer.dielectricConstant, 'f', 2) << "\" copperWeightOz=\"" << layer.copperWeightOz
            << "\"/>\n";
    }
    out << "    </LayerStackup>\n";
    out << "  </Content>\n";

    // BOM Assembly Information
    out << "  <Bom name=\"BOM_PRIMARY\">\n";
    for (QGraphicsItem* g : scene->items()) {
        if (ComponentItem* comp = dynamic_cast<ComponentItem*>(g)) {
            out << "    <BomItem refDes=\"" << comp->name()
                << "\" package=\"" << comp->componentType()
                << "\" val=\"" << comp->value()
                << "\"/>\n";
        }
    }
    out << "  </Bom>\n";

    // ECAD Geometry & Layout Features
    out << "  <Ecad name=\"ECAD_PRIMARY\">\n";
    out << "    <CadHeader>\n";
    out << "      <Units>MM</Units>\n";
    out << "      <BoundingBox minX=\"" << QString::number(bb.left(), 'f', 4)
        << "\" minY=\"" << QString::number(bb.top(), 'f', 4)
        << "\" maxX=\"" << QString::number(bb.right(), 'f', 4)
        << "\" maxY=\"" << QString::number(bb.bottom(), 'f', 4) << "\"/>\n";
    out << "    </CadHeader>\n";
    out << "    <CadData>\n";
    out << "      <Step name=\"BOARD\">\n";

    // Components Placement
    out << "        <Components>\n";
    for (QGraphicsItem* g : scene->items()) {
        if (ComponentItem* comp = dynamic_cast<ComponentItem*>(g)) {
            out << "          <Component refDes=\"" << comp->name()
                << "\" pkg=\"" << comp->componentType()
                << "\" x=\"" << QString::number(comp->pos().x(), 'f', 4)
                << "\" y=\"" << QString::number(comp->pos().y(), 'f', 4)
                << "\" rotation=\"" << QString::number(comp->rotation(), 'f', 2)
                << "\" layer=\"" << (comp->layer() == PCBLayer::Top ? "TOP" : "BOTTOM")
                << "\"/>\n";
        }
    }
    out << "        </Components>\n";

    // Copper Traces, Vias & Pads Features
    out << "        <LayerFeatures>\n";
    for (QGraphicsItem* g : scene->items()) {
        if (TraceItem* tr = dynamic_cast<TraceItem*>(g)) {
            out << "          <Polyline net=\"" << tr->netName()
                << "\" layer=\"" << tr->layer()
                << "\" width=\"" << QString::number(tr->width(), 'f', 4)
                << "\" startX=\"" << QString::number(tr->startPoint().x(), 'f', 4)
                << "\" startY=\"" << QString::number(tr->startPoint().y(), 'f', 4)
                << "\" endX=\"" << QString::number(tr->endPoint().x(), 'f', 4)
                << "\" endY=\"" << QString::number(tr->endPoint().y(), 'f', 4)
                << "\"/>\n";
        } else if (ViaItem* via = dynamic_cast<ViaItem*>(g)) {
            out << "          <Via net=\"" << via->netName()
                << "\" x=\"" << QString::number(via->pos().x(), 'f', 4)
                << "\" y=\"" << QString::number(via->pos().y(), 'f', 4)
                << "\" diameter=\"" << QString::number(via->diameter(), 'f', 4)
                << "\" drill=\"" << QString::number(via->drillSize(), 'f', 4)
                << "\" startLayer=\"" << via->startLayer()
                << "\" endLayer=\"" << via->endLayer()
                << "\"/>\n";
        } else if (PadItem* pad = dynamic_cast<PadItem*>(g)) {
            out << "          <Pad net=\"" << pad->netName()
                << "\" x=\"" << QString::number(pad->pos().x(), 'f', 4)
                << "\" y=\"" << QString::number(pad->pos().y(), 'f', 4)
                << "\" sizeX=\"" << QString::number(pad->size().width(), 'f', 4)
                << "\" sizeY=\"" << QString::number(pad->size().height(), 'f', 4)
                << "\" layer=\"" << pad->layer()
                << "\"/>\n";
        }
    }
    out << "        </LayerFeatures>\n";

    out << "      </Step>\n";
    out << "    </CadData>\n";
    out << "  </Ecad>\n";
    out << "</IPC-2581>\n";
    return true;
}

bool ManufacturingExporter::exportODBppPackage(QGraphicsScene* scene, const QString& outputDirectory, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene.";
        return false;
    }
    if (outputDirectory.isEmpty()) {
        if (error) *error = "Output directory is empty.";
        return false;
    }

    QDir root(outputDirectory);
    if (!root.exists() && !root.mkpath(".")) {
        if (error) *error = "Cannot create output directory.";
        return false;
    }

    const QString jobDir = root.filePath("odbpp_job");
    QDir().mkpath(jobDir + "/steps/pcb/layers");
    QDir().mkpath(jobDir + "/steps/pcb/drill");

    QFile matrix(jobDir + "/matrix");
    if (!matrix.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Cannot write ODB++ matrix file.";
        return false;
    }
    QTextStream m(&matrix);
    m << "# ODB++ matrix generated by Viora EDA\n";
    m << "# name,type,side\n";

    GerberExportSettings settings;
    settings.outputDirectory = jobDir + "/steps/pcb/layers";

    for (const PCBLayer& l : PCBLayerManager::instance().layers()) {
        if (l.type() != PCBLayer::Copper &&
            l.type() != PCBLayer::Silkscreen &&
            l.type() != PCBLayer::Soldermask &&
            l.type() != PCBLayer::EdgeCuts) {
            continue;
        }
        const QString safe = QString(l.name()).replace(" ", "_");
        const QString layerFile = jobDir + "/steps/pcb/layers/" + safe + ".gbr";
        GerberExporter::exportLayer(scene, l.id(), layerFile, settings);
        m << safe << "," << l.typeString() << "," << l.sideString() << "\n";
    }

    GerberExporter::generateDrillFile(scene, jobDir + "/steps/pcb/drill/drills.drl");

    QFile info(jobDir + "/README.txt");
    if (info.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream i(&info);
        i << "Viora EDA ODB++ package (folder form)\n";
        i << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    }

    return true;
}

// ============================================================================
// Pick-and-Place / Centroid Export
// ============================================================================

bool ManufacturingExporter::exportPickPlace(QGraphicsScene* scene, const QString& filePath,
                                             const PickPlaceOptions& options, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene.";
        return false;
    }
    if (filePath.isEmpty()) {
        if (error) *error = "Output file path is empty.";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open output file: " + filePath;
        return false;
    }

    QString content = generatePickPlaceContent(scene, options, error);
    if (content.isEmpty() && error && !error->isEmpty()) {
        return false;
    }

    QTextStream out(&file);
    out << content;
    return true;
}

QString ManufacturingExporter::generatePickPlaceContent(QGraphicsScene* scene,
                                                         const PickPlaceOptions& options,
                                                         QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene.";
        return QString();
    }

    const char sep = (options.format == CSV) ? ',' : '\t';
    const double unitFactor = options.useMillimeters ? 1.0 : 0.0393701; // mm to inches
    const QString unit = options.useMillimeters ? "mm" : "in";

    QStringList lines;

    // Header comment
    lines << "# Pick and Place file generated by Viora EDA";
    lines << "# Date: " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    lines << "# Units: " + unit;
    lines << "";

    // Column header
    QStringList header = {"Designator", "Mid X", "Mid Y", "Layer", "Rotation"};
    if (options.includeFootprint) header << "Footprint";
    if (options.includeValue) header << "Value";
    lines << header.join(QString(sep));

    // Component data
    int topCount = 0, bottomCount = 0;

    for (QGraphicsItem* g : scene->items()) {
        auto* comp = dynamic_cast<ComponentItem*>(g);
        if (!comp) continue;

        // Filter by side
        int layerId = comp->layer();
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (!layer) continue;

        bool isTop = layer->side() == PCBLayer::Top;
        bool isBottom = layer->side() == PCBLayer::Bottom;

        if (isTop && !options.includeTopSide) continue;
        if (isBottom && !options.includeBottomSide) continue;

        // Skip fiducials (typically named "FID" or similar)
        QString ref = comp->name();
        if (!options.includeFiducials && ref.startsWith("FID", Qt::CaseInsensitive)) continue;

        // Skip test points
        if (!options.includeTestPoints && ref.startsWith("TP", Qt::CaseInsensitive)) continue;

        // Position in selected units
        double x = comp->pos().x() * unitFactor;
        double y = comp->pos().y() * unitFactor;
        double rot = comp->rotation();

        // Layer string (Top/Bottom)
        QString sideStr = isTop ? "Top" : "Bottom";

        // For bottom-side components, mirror X and adjust rotation
        if (isBottom) {
            x = -x;
            rot = 360.0 - rot;
            if (rot >= 360.0) rot -= 360.0;
        }

        // Build row
        QStringList row;
        row << ref;
        row << QString::number(x, 'f', 4);
        row << QString::number(y, 'f', 4);
        row << sideStr;
        row << QString::number(rot, 'f', 2);
        if (options.includeFootprint) row << comp->componentType();
        if (options.includeValue) row << comp->value();

        lines << row.join(QString(sep));

        if (isTop) topCount++;
        else if (isBottom) bottomCount++;
    }

    return lines.join("\n") + "\n";
}

QString ManufacturingExporter::generateBOMCSV(QGraphicsScene* scene) {
    if (!scene) return QString();

    struct GroupedComp {
        QString comment;
        QString footprint;
        QStringList designators;
        QString lcscPartNumber;
    };
    QMap<QString, GroupedComp> groups;

    for (QGraphicsItem* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            QString ref = comp->name();
            if (ref.startsWith("FID", Qt::CaseInsensitive) || ref.startsWith("TP", Qt::CaseInsensitive)) continue;

            QString val = comp->value().isEmpty() ? "10k" : comp->value();
            QString fp = comp->componentType().isEmpty() ? "SMD" : comp->componentType();
            QString key = val + "___" + fp;

            if (!groups.contains(key)) {
                groups[key] = {val, fp, {ref}, ""};
            } else {
                groups[key].designators.append(ref);
            }
        }
    }

    QStringList csvLines;
    csvLines << "Comment/Value,Designator,Footprint,Quantity,LCSC Part #";

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        it.value().designators.sort(Qt::CaseInsensitive);
        QString desList = "\"" + it.value().designators.join(",") + "\"";
        csvLines << QString("\"%1\",%2,\"%3\",%4,\"\"")
                       .arg(it.value().comment)
                       .arg(desList)
                       .arg(it.value().footprint)
                       .arg(it.value().designators.size());
    }

    return csvLines.join("\n") + "\n";
}

QString ManufacturingExporter::generateCPLCSV(QGraphicsScene* scene, FabricatorPreset preset) {
    if (!scene) return QString();

    QStringList csvLines;

    if (preset == PCBWay) {
        csvLines << "Designator,Footprint,Mid X,Mid Y,Layer,Rotation,Comment";
    } else if (preset == Eurocircuits) {
        csvLines << "Designator,Center X(mm),Center Y(mm),Layer,Rotation,Package,Value";
    } else {
        // Default JLCPCB Format
        csvLines << "Designator,Val,Package,Mid X,Mid Y,Rotation,Layer";
    }

    for (QGraphicsItem* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            QString ref = comp->name();
            if (ref.isEmpty() || ref.startsWith("FID", Qt::CaseInsensitive) || ref.startsWith("TP", Qt::CaseInsensitive)) continue;

            const PCBLayer* layer = PCBLayerManager::instance().layer(comp->layer());
            bool isTop = (!layer || layer->side() != PCBLayer::Bottom);
            QString sideStr = isTop ? "Top" : "Bottom";

            double x = comp->pos().x();
            double y = comp->pos().y();
            double rot = comp->rotation();

            if (!isTop) {
                x = -x;
                rot = std::fmod(360.0 - rot, 360.0);
            }

            QString val = comp->value().isEmpty() ? "10k" : comp->value();
            QString pkg = comp->componentType().isEmpty() ? "Component" : comp->componentType();

            if (preset == PCBWay) {
                csvLines << QString("%1,\"%2\",%3,%4,%5,%6,\"%7\"")
                               .arg(ref).arg(pkg)
                               .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4)
                               .arg(sideStr).arg(rot, 0, 'f', 2).arg(val);
            } else if (preset == Eurocircuits) {
                csvLines << QString("%1,%2,%3,%4,%5,\"%6\",\"%7\"")
                               .arg(ref)
                               .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4)
                               .arg(sideStr).arg(rot, 0, 'f', 2).arg(pkg).arg(val);
            } else {
                // JLCPCB Standard
                csvLines << QString("%1,\"%2\",\"%3\",%4,%5,%6,%7")
                               .arg(ref).arg(val).arg(pkg)
                               .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4)
                               .arg(rot, 0, 'f', 2).arg(sideStr);
            }
        }
    }

    return csvLines.join("\n") + "\n";
}

bool ManufacturingExporter::exportManufacturingPackage(QGraphicsScene* scene,
                                                       const QString& outputPath,
                                                       const ManufacturingPackageOptions& options,
                                                       QString* error) {
    if (!scene) {
        if (error) *error = "Invalid PCB scene pointer.";
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) *error = "Failed to create temporary export directory.";
        return false;
    }

    QDir tempDirHandle(tempDir.path());

    // 1. Export Gerber Layers
    if (options.includeGerbers) {
        QList<int> exportLayers = {
            PCBLayerManager::TopCopper,
            PCBLayerManager::BottomCopper,
            PCBLayerManager::TopSilkscreen,
            PCBLayerManager::BottomSilkscreen,
            PCBLayerManager::TopSoldermask,
            PCBLayerManager::BottomSoldermask,
            PCBLayerManager::TopPaste,
            PCBLayerManager::BottomPaste,
            PCBLayerManager::EdgeCuts
        };

        for (int layerId : exportLayers) {
            PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
            if (!layer) continue;

            QString ext = ".gbr";
            if (options.preset == PCBWay) {
                if (layerId == PCBLayerManager::TopCopper) ext = ".gtl";
                else if (layerId == PCBLayerManager::BottomCopper) ext = ".gbl";
                else if (layerId == PCBLayerManager::TopSilkscreen) ext = ".gto";
                else if (layerId == PCBLayerManager::BottomSilkscreen) ext = ".gbo";
                else if (layerId == PCBLayerManager::TopSoldermask) ext = ".gts";
                else if (layerId == PCBLayerManager::BottomSoldermask) ext = ".gbs";
                else if (layerId == PCBLayerManager::TopPaste) ext = ".gtp";
                else if (layerId == PCBLayerManager::BottomPaste) ext = ".gbp";
                else if (layerId == PCBLayerManager::EdgeCuts) ext = ".gm1";
            }

            QString fileName = layer->name().replace(" ", "_") + ext;
            QString gbrPath = tempDirHandle.filePath(fileName);
            GerberExporter::exportLayer(scene, layerId, gbrPath, GerberExportSettings());
        }
    }

    // 2. Export NC Drill Files
    if (options.includeDrill) {
        NCDrillExporter::DrillOptions drillOpts;
        NCDrillExporter::exportDrills(scene, tempDir.path(), drillOpts);
    }

    // 3. Export BOM & CPL CSVs
    if (options.includeBOM) {
        QString bomContent = generateBOMCSV(scene);
        QFile bomFile(tempDirHandle.filePath("BOM_JLCPCB.csv"));
        if (bomFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&bomFile);
            out << bomContent;
            bomFile.close();
        }
    }

    if (options.includeCPL) {
        QString cplContent = generateCPLCSV(scene, options.preset);
        QFile cplFile(tempDirHandle.filePath("CPL_JLCPCB.csv"));
        if (cplFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&cplFile);
            out << cplContent;
            cplFile.close();
        }
    }

    // 4. Compress output into ZIP file or Copy to Target Dir
    if (options.zipPackage || outputPath.endsWith(".zip", Qt::CaseInsensitive)) {
        QFileInfo outFi(outputPath);
        QDir().mkpath(outFi.absolutePath());

        // Use zip command
        QProcess zipProc;
        zipProc.setWorkingDirectory(tempDir.path());
        zipProc.start("zip", QStringList() << "-r" << outputPath << ".");
        zipProc.waitForFinished(10000);

        if (zipProc.exitCode() != 0 || !QFile::exists(outputPath)) {
            if (error) *error = "Failed to execute zip command. Ensure zip is installed.";
            return false;
        }
    } else {
        QDir targetDir(outputPath);
        QDir().mkpath(outputPath);
        for (const QString& file : tempDirHandle.entryList(QDir::Files)) {
            QFile::copy(tempDirHandle.filePath(file), targetDir.filePath(file));
        }
    }

    return true;
}
