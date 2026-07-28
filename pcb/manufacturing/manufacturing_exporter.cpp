/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "manufacturing_exporter.h"

#include "../gerber/gerber_exporter.h"
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

    // Summary footer
    lines << "";
    lines << QString("# Total components: %1 (Top: %2, Bottom: %3)")
               .arg(topCount + bottomCount).arg(topCount).arg(bottomCount);

    return lines.join("\n") + "\n";
}
