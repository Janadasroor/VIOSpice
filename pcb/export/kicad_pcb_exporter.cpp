/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kicad_pcb_exporter.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/component_item.h"
#include "../items/copper_pour_item.h"
#include "../layers/pcb_layer.h"

#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QSet>
#include <QtMath>

namespace {

QString formatKiCadLayer(int layerId) {
    if (layerId == PCBLayerManager::TopCopper) return "F.Cu";
    if (layerId == PCBLayerManager::BottomCopper) return "B.Cu";
    if (layerId == PCBLayerManager::TopSilkscreen) return "F.SilkS";
    if (layerId == PCBLayerManager::BottomSilkscreen) return "B.SilkS";
    if (layerId == PCBLayerManager::TopSoldermask) return "F.Mask";
    if (layerId == PCBLayerManager::BottomSoldermask) return "B.Mask";
    if (layerId == PCBLayerManager::EdgeCuts) return "Edge.Cuts";

    // Inner copper layers
    if (layerId >= 2 && layerId <= 31) {
        return QString("In%1.Cu").arg(layerId - 1);
    }

    return "F.Cu";
}

} // namespace

KiCadPCBExporter::ExportStats KiCadPCBExporter::exportKiCadPCB(const QString& filePath, QGraphicsScene* scene) {
    ExportStats stats;
    if (!scene) {
        stats.error = "Invalid empty QGraphicsScene pointer";
        return stats;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        stats.error = "Could not open output file for writing: " + filePath;
        return stats;
    }

    QTextStream out(&file);

    // 1. Build Net Map
    QMap<QString, int> netMap;
    netMap[""] = 0; // Unconnected net ID = 0
    int nextNetId = 1;

    for (QGraphicsItem* item : scene->items()) {
        QString netName;
        if (auto* pi = dynamic_cast<PCBItem*>(item)) {
            netName = pi->netName().trimmed();
        }
        if (!netName.isEmpty() && !netMap.contains(netName)) {
            netMap[netName] = nextNetId++;
        }
    }
    stats.netsCount = netMap.size();

    // 2. KiCad 8 File Header
    out << "(kicad_pcb (version 20240108) (generator \"VioSpice EDA Exporter\")\n";

    // General & Setup
    out << "  (general\n";
    out << "    (thickness 1.6)\n";
    out << "  )\n";

    out << "  (paper \"A4\")\n";

    // Layers definition
    out << "  (layers\n";
    out << "    (0 \"F.Cu\" signal)\n";
    out << "    (31 \"B.Cu\" signal)\n";
    out << "    (36 \"B.SilkS\" user \"B.Silkscreen\")\n";
    out << "    (37 \"F.SilkS\" user \"F.Silkscreen\")\n";
    out << "    (38 \"B.Mask\" user \"B.Mask\")\n";
    out << "    (39 \"F.Mask\" user \"F.Mask\")\n";
    out << "    (44 \"Edge.Cuts\" user)\n";
    out << "  )\n\n";

    // Setup defaults
    out << "  (setup\n";
    out << "    (pad_to_mask_clearance 0.05)\n";
    out << "    (solder_mask_min_width 0.1)\n";
    out << "  )\n\n";

    // 3. Write Nets
    for (auto it = netMap.begin(); it != netMap.end(); ++it) {
        out << QString("  (net %1 \"%2\")\n").arg(it.value()).arg(it.key());
    }
    out << "\n";

    // Collect all pads owned by component footprints to prevent treating them as standalone pads
    QSet<PadItem*> componentPads;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            for (QGraphicsItem* child : comp->childItems()) {
                if (auto* pad = dynamic_cast<PadItem*>(child)) {
                    componentPads.insert(pad);
                }
            }
        }
    }

    // 4. Export Footprints & Components
    for (QGraphicsItem* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            stats.footprintsCount++;
            QPointF pos = comp->scenePos();
            double rot = comp->rotation();
            QString layerStr = formatKiCadLayer(comp->layer());
            QString refDes = comp->name().isEmpty() ? "U?" : comp->name();
            QString fpName = comp->componentType().isEmpty() ? "Component" : comp->componentType();

            out << QString("  (footprint \"VioSpice:%1\" (layer \"%2\") (at %3 %4 %5)\n")
                       .arg(fpName)
                       .arg(layerStr)
                       .arg(pos.x(), 0, 'f', 4)
                       .arg(pos.y(), 0, 'f', 4)
                       .arg(rot, 0, 'f', 1);

            out << QString("    (property \"Reference\" \"%1\" (at 0 -2.5 0) (layer \"%2.SilkS\") (effects (font (size 1 1) (thickness 0.15))))\n")
                       .arg(refDes)
                       .arg(layerStr.left(1));
            out << QString("    (property \"Value\" \"%1\" (at 0 2.5 0) (layer \"%2.Fab\") (effects (font (size 1 1) (thickness 0.15))))\n")
                       .arg(comp->value())
                       .arg(layerStr.left(1));

            // Export Pads inside component
            for (QGraphicsItem* child : comp->childItems()) {
                if (auto* pad = dynamic_cast<PadItem*>(child)) {
                    QPointF padPos = pad->pos();
                    QSizeF padSize = pad->size();
                    int netId = netMap.value(pad->netName().trimmed(), 0);
                    QString padNetName = pad->netName().trimmed();

                    QString padNum = (pad->model() && !pad->model()->number().isEmpty()) ? pad->model()->number() : "1";
                    QString padTypeStr = (pad->drillSize() > 0.001) ? "thru_hole" : "smd";
                    QString padShapeStr = "rect";
                    QString s = pad->padShape().toLower();
                    if (s == "round" || s == "circle") padShapeStr = "circle";
                    else if (s == "oblong" || s == "oval") padShapeStr = "oval";

                    out << QString("    (pad \"%1\" %2 %3 (at %4 %5) (size %6 %7)\n")
                               .arg(padNum)
                               .arg(padTypeStr)
                               .arg(padShapeStr)
                               .arg(padPos.x(), 0, 'f', 4)
                               .arg(padPos.y(), 0, 'f', 4)
                               .arg(padSize.width(), 0, 'f', 4)
                               .arg(padSize.height(), 0, 'f', 4);

                    if (padTypeStr == "thru_hole") {
                        out << QString("      (drill %1)\n").arg(pad->drillSize(), 0, 'f', 4);
                        out << "      (layers \"*.Cu\" \"*.Mask\")\n";
                    } else {
                        out << QString("      (layers \"%1\" \"%2.Mask\")\n")
                                   .arg(layerStr)
                                   .arg(layerStr.left(1));
                    }

                    if (netId > 0) {
                        out << QString("      (net %1 \"%2\")\n").arg(netId).arg(padNetName);
                    }
                    out << "    )\n";
                }
            }
            out << "  )\n\n";
        }
    }

    // 5. Export Standalone Pads (if any)
    for (QGraphicsItem* item : scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->parentItem() == nullptr && !componentPads.contains(pad)) {
                stats.footprintsCount++;
                QPointF pos = pad->scenePos();
                QSizeF padSize = pad->size();
                int netId = netMap.value(pad->netName().trimmed(), 0);
                QString padNum = (pad->model() && !pad->model()->number().isEmpty()) ? pad->model()->number() : "1";

                out << QString("  (footprint \"VioSpice:Pad\" (layer \"%1\") (at %2 %3)\n")
                           .arg(formatKiCadLayer(pad->layer()))
                           .arg(pos.x(), 0, 'f', 4)
                           .arg(pos.y(), 0, 'f', 4);

                out << QString("    (pad \"%1\" %2 rect (at 0 0) (size %3 %4)\n")
                           .arg(padNum)
                           .arg(pad->drillSize() > 0.001 ? "thru_hole" : "smd")
                           .arg(padSize.width(), 0, 'f', 4)
                           .arg(padSize.height(), 0, 'f', 4);

                if (pad->drillSize() > 0.001) {
                    out << QString("      (drill %1)\n").arg(pad->drillSize(), 0, 'f', 4);
                    out << "      (layers \"*.Cu\" \"*.Mask\")\n";
                } else {
                    out << QString("      (layers \"%1\")\n").arg(formatKiCadLayer(pad->layer()));
                }

                if (netId > 0) {
                    out << QString("      (net %1 \"%2\")\n").arg(netId).arg(pad->netName().trimmed());
                }
                out << "    )\n  )\n\n";
            }
        }
    }

    // 6. Export Traces (segment blocks)
    for (QGraphicsItem* item : scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            stats.tracesCount++;
            QPointF p1 = trace->mapToScene(trace->startPoint());
            QPointF p2 = trace->mapToScene(trace->endPoint());
            int netId = netMap.value(trace->netName().trimmed(), 0);

            out << QString("  (segment (start %1 %2) (end %3 %4) (width %5) (layer \"%6\") (net %7))\n")
                       .arg(p1.x(), 0, 'f', 4)
                       .arg(p1.y(), 0, 'f', 4)
                       .arg(p2.x(), 0, 'f', 4)
                       .arg(p2.y(), 0, 'f', 4)
                       .arg(trace->width(), 0, 'f', 4)
                       .arg(formatKiCadLayer(trace->layer()))
                       .arg(netId);
        }
    }
    out << "\n";

    // 7. Export Vias (via blocks)
    for (QGraphicsItem* item : scene->items()) {
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            stats.viasCount++;
            QPointF pos = via->scenePos();
            int netId = netMap.value(via->netName().trimmed(), 0);
            QString startL = formatKiCadLayer(via->startLayer());
            QString endL = formatKiCadLayer(via->endLayer());

            out << QString("  (via (at %1 %2) (size %3) (drill %4) (layers \"%5\" \"%6\") (net %7))\n")
                       .arg(pos.x(), 0, 'f', 4)
                       .arg(pos.y(), 0, 'f', 4)
                       .arg(via->diameter(), 0, 'f', 4)
                       .arg(via->drillSize(), 0, 'f', 4)
                       .arg(startL)
                       .arg(endL)
                       .arg(netId);
        }
    }
    out << "\n";

    // 8. Export Copper Pours (zone blocks)
    for (QGraphicsItem* item : scene->items()) {
        if (auto* pour = dynamic_cast<CopperPourItem*>(item)) {
            stats.zonesCount++;
            int netId = netMap.value(pour->netName().trimmed(), 0);

            out << QString("  (zone (net %1) (net_name \"%2\") (layer \"%3\") (hatch edge 0.5)\n")
                       .arg(netId)
                       .arg(pour->netName().trimmed())
                       .arg(formatKiCadLayer(pour->layer()));

            out << "    (connect_pads (clearance 0.5))\n";
            out << "    (min_thickness 0.25)\n";
            out << "    (filled_polygon\n";
            out << "      (pts\n";

            QPolygonF poly = pour->polygon();
            for (int i = 0; i < poly.size(); ++i) {
                QPointF pt = pour->mapToScene(poly[i]);
                out << QString("        (xy %1 %2)\n")
                           .arg(pt.x(), 0, 'f', 4)
                           .arg(pt.y(), 0, 'f', 4);
            }

            out << "      )\n";
            out << "    )\n";
            out << "  )\n\n";
        }
    }

    // Close Root (kicad_pcb)
    out << ")\n";

    file.close();
    stats.success = true;
    return stats;
}
