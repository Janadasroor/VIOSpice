/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kicad_pcb_importer.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/component_item.h"
#include "../items/copper_pour_item.h"
#include "../layers/pcb_layer.h"
#include "../../footprints/models/footprint_definition.h"
#include "../../footprints/footprint_library.h"

#include <QFile>
#include <QTextStream>
#include <QGraphicsTextItem>
#include <QRegularExpression>
#include <QDebug>
#include <QtMath>
#include <cmath>

namespace {

struct SExp {
    QString name;
    QString value;
    QList<SExp> children;

    const SExp* child(const QString& tag) const {
        for (const auto& c : children) {
            if (c.name.compare(tag, Qt::CaseInsensitive) == 0) return &c;
        }
        return nullptr;
    }

    SExp* child(const QString& tag) {
        for (auto& c : children) {
            if (c.name.compare(tag, Qt::CaseInsensitive) == 0) return &c;
        }
        return nullptr;
    }

    QString childValue(const QString& tag) const {
        for (const auto& c : children) {
            if (c.name.compare(tag, Qt::CaseInsensitive) == 0) return c.value;
        }
        return QString();
    }
};

SExp parseSExpTokens(const QStringList& tokens, int& idx) {
    SExp node;
    if (idx >= tokens.size()) return node;

    if (tokens[idx] == "(") {
        idx++; // skip '('
        if (idx < tokens.size() && tokens[idx] != ")") {
            node.name = tokens[idx++];
        }
        while (idx < tokens.size() && tokens[idx] != ")") {
            if (tokens[idx] == "(") {
                node.children.append(parseSExpTokens(tokens, idx));
            } else {
                QString tok = tokens[idx++];
                tok.remove('"');
                if (node.value.isEmpty()) node.value = tok;
                else node.value += " " + tok;
            }
        }
        if (idx < tokens.size() && tokens[idx] == ")") idx++;
    }
    return node;
}

SExp parseSExpression(const QString& content) {
    QStringList tokens;
    QString curToken;
    bool inQuotes = false;

    for (int i = 0; i < content.length(); ++i) {
        QChar ch = content[i];
        if (ch == '"') {
            inQuotes = !inQuotes;
            curToken += ch;
        } else if (inQuotes) {
            curToken += ch;
        } else if (ch == '(' || ch == ')') {
            if (!curToken.trimmed().isEmpty()) {
                tokens.append(curToken.trimmed());
                curToken.clear();
            }
            tokens.append(QString(ch));
        } else if (ch.isSpace()) {
            if (!curToken.trimmed().isEmpty()) {
                tokens.append(curToken.trimmed());
                curToken.clear();
            }
        } else {
            curToken += ch;
        }
    }
    if (!curToken.trimmed().isEmpty()) {
        tokens.append(curToken.trimmed());
    }

    int idx = 0;
    return parseSExpTokens(tokens, idx);
}

int mapKiCadLayer(const QString& layerName) {
    QString l = layerName.trimmed().remove('"');
    if (l == "F.Cu" || l == "0") return PCBLayerManager::TopCopper;
    if (l == "B.Cu" || l == "31" || l == "2") return PCBLayerManager::BottomCopper;
    if (l == "Edge.Cuts" || l == "44" || l == "25") return PCBLayerManager::EdgeCuts;
    if (l == "F.SilkS" || l == "37" || l == "5") return PCBLayerManager::TopSilkscreen;
    if (l == "B.SilkS" || l == "36" || l == "7") return PCBLayerManager::BottomSilkscreen;
    if (l == "F.Mask" || l == "39" || l == "1") return PCBLayerManager::TopSoldermask;
    if (l == "B.Mask" || l == "38" || l == "3") return PCBLayerManager::BottomSoldermask;

    // Inner copper layers e.g. "In1.Cu", "In2.Cu", "In7.Cu"
    if (l.startsWith("In", Qt::CaseInsensitive)) {
        int idxDot = l.indexOf('.');
        QString numStr = (idxDot != -1) ? l.mid(2, idxDot - 2) : l.mid(2);
        bool ok;
        int n = numStr.toInt(&ok);
        if (ok && n >= 1) return 1 + n; // In1 -> 2, In2 -> 3, In3 -> 4...
    }

    // Check integer IDs e.g. 4 -> In1 (2), 6 -> In2 (3), 8 -> In3 (4)...
    bool isInt;
    int layerInt = l.toInt(&isInt);
    if (isInt) {
        if (layerInt >= 4 && layerInt <= 60 && (layerInt % 2 == 0)) {
            return 1 + (layerInt / 2);
        }
    }

    return PCBLayerManager::TopCopper;
}

QList<QPointF> discretize3PointArc(QPointF start, QPointF mid, QPointF end, int numSegments = 12) {
    QList<QPointF> pts;
    double ax = start.x(), ay = start.y();
    double bx = mid.x(),   by = mid.y();
    double cx = end.x(),   cy = end.y();

    double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-9) {
        pts.append(start);
        pts.append(end);
        return pts;
    }

    double ux = ((ax*ax + ay*ay)*(by - cy) + (bx*bx + by*by)*(cy - ay) + (cx*cx + cy*cy)*(ay - by)) / d;
    double uy = ((ax*ax + ay*ay)*(cx - bx) + (bx*bx + by*by)*(ax - cx) + (cx*cx + cy*cy)*(bx - ax)) / d;

    double aStart = std::atan2(start.y() - uy, start.x() - ux);
    double aMid   = std::atan2(mid.y() - uy,   mid.x() - ux);
    double aEnd   = std::atan2(end.y() - uy,   end.x() - ux);

    double dMid = std::remainder(aMid - aStart, 2 * M_PI);
    double dEnd = std::remainder(aEnd - aStart, 2 * M_PI);

    if ((dMid > 0 && dEnd < 0) || (dMid > 0 && dEnd > 0 && dMid > dEnd)) {
        if (dEnd < 0) dEnd += 2 * M_PI;
        else dEnd -= 2 * M_PI;
    } else if ((dMid < 0 && dEnd > 0) || (dMid < 0 && dEnd < 0 && dMid < dEnd)) {
        if (dEnd > 0) dEnd -= 2 * M_PI;
        else dEnd += 2 * M_PI;
    }

    double radius = std::hypot(start.x() - ux, start.y() - uy);

    for (int i = 0; i <= numSegments; ++i) {
        double t = static_cast<double>(i) / numSegments;
        double ang = aStart + t * dEnd;
        pts.append(QPointF(ux + radius * std::cos(ang), uy + radius * std::sin(ang)));
    }
    return pts;
}

} // namespace

KiCadPCBImporter::ImportStats KiCadPCBImporter::importKiCadPCB(const QString& filePath, QGraphicsScene* scene) {
    ImportStats stats;
    if (!scene) {
        stats.error = "Invalid QGraphicsScene";
        return stats;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        stats.error = "Could not open file: " + filePath;
        return stats;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    SExp root = parseSExpression(content);
    if (root.name.compare("kicad_pcb", Qt::CaseInsensitive) != 0) {
        stats.error = "Invalid KiCad PCB file format (missing kicad_pcb header)";
        return stats;
    }

    QMap<int, QString> netMap;
    netMap[0] = "";

    // Parse Nets
    for (const auto& child : root.children) {
        if (child.name.compare("net", Qt::CaseInsensitive) == 0) {
            QStringList parts = child.value.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 1) {
                int netId = parts[0].toInt();
                QString netName = (parts.size() >= 2) ? parts[1].remove('"') : "";
                netMap[netId] = netName;
                stats.netsCount++;
            }
        }
    }

    // Parse Elements
    for (const auto& child : root.children) {
        // Traces (segment)
        if (child.name.compare("segment", Qt::CaseInsensitive) == 0) {
            const SExp* startNode = child.child("start");
            const SExp* endNode = child.child("end");
            const SExp* widthNode = child.child("width");
            const SExp* layerNode = child.child("layer");
            const SExp* netNode = child.child("net");

            if (startNode && endNode) {
                QStringList sPts = startNode->value.split(' ', Qt::SkipEmptyParts);
                QStringList ePts = endNode->value.split(' ', Qt::SkipEmptyParts);
                if (sPts.size() >= 2 && ePts.size() >= 2) {
                    QPointF start(sPts[0].toDouble(), sPts[1].toDouble());
                    QPointF end(ePts[0].toDouble(), ePts[1].toDouble());

                    TraceItem* trace = new TraceItem(start, end);
                    if (widthNode) trace->setWidth(widthNode->value.toDouble());
                    if (layerNode) trace->setLayer(mapKiCadLayer(layerNode->value));
                    if (netNode) {
                        int netId = netNode->value.toInt();
                        trace->setNetName(netMap.value(netId, ""));
                    }

                    scene->addItem(trace);
                    stats.tracesCount++;
                }
            }
        }
        // Vias (via)
        else if (child.name.compare("via", Qt::CaseInsensitive) == 0) {
            const SExp* atNode = child.child("at");
            const SExp* sizeNode = child.child("size");
            const SExp* drillNode = child.child("drill");
            const SExp* netNode = child.child("net");
            const SExp* layersNode = child.child("layers");
            const SExp* typeNode = child.child("type");

            if (atNode) {
                QStringList atPts = atNode->value.split(' ', Qt::SkipEmptyParts);
                if (atPts.size() >= 2) {
                    QPointF pos(atPts[0].toDouble(), atPts[1].toDouble());
                    double dia = sizeNode ? sizeNode->value.toDouble() : 0.6;

                    ViaItem* via = new ViaItem(pos, dia);
                    if (drillNode) via->setDrillSize(drillNode->value.toDouble());
                    if (netNode) {
                        int netId = netNode->value.toInt();
                        via->setNetName(netMap.value(netId, ""));
                    }

                    if (layersNode) {
                        QStringList lList = layersNode->value.split(' ', Qt::SkipEmptyParts);
                        if (lList.size() >= 2) {
                            via->setStartLayer(mapKiCadLayer(lList[0]));
                            via->setEndLayer(mapKiCadLayer(lList[1]));
                        } else if (lList.size() == 1) {
                            via->setStartLayer(mapKiCadLayer(lList[0]));
                        }
                    }

                    if (typeNode) {
                        QString tVal = typeNode->value.trimmed().toLower();
                        if (tVal.contains("micro")) via->setMicrovia(true);
                    }

                    scene->addItem(via);
                    stats.viasCount++;
                }
            }
        }
        // Arcs (arc)
        else if (child.name.compare("arc", Qt::CaseInsensitive) == 0 ||
                 child.name.compare("gr_arc", Qt::CaseInsensitive) == 0) {
            const SExp* startNode = child.child("start");
            const SExp* midNode = child.child("mid");
            const SExp* endNode = child.child("end");
            const SExp* widthNode = child.child("width");
            const SExp* layerNode = child.child("layer");
            const SExp* netNode = child.child("net");

            if (startNode && midNode && endNode) {
                QStringList sPts = startNode->value.split(' ', Qt::SkipEmptyParts);
                QStringList mPts = midNode->value.split(' ', Qt::SkipEmptyParts);
                QStringList ePts = endNode->value.split(' ', Qt::SkipEmptyParts);
                if (sPts.size() >= 2 && mPts.size() >= 2 && ePts.size() >= 2) {
                    QPointF pStart(sPts[0].toDouble(), sPts[1].toDouble());
                    QPointF pMid(mPts[0].toDouble(), mPts[1].toDouble());
                    QPointF pEnd(ePts[0].toDouble(), ePts[1].toDouble());

                    QList<QPointF> arcPts = discretize3PointArc(pStart, pMid, pEnd, 12);
                    double w = widthNode ? widthNode->value.toDouble() : 0.25;
                    int lay = layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::TopCopper;
                    QString net = netNode ? netMap.value(netNode->value.toInt(), "") : "";

                    for (int i = 0; i < arcPts.size() - 1; ++i) {
                        TraceItem* t = new TraceItem(arcPts[i], arcPts[i + 1]);
                        t->setWidth(w);
                        t->setLayer(lay);
                        t->setNetName(net);
                        scene->addItem(t);
                        stats.tracesCount++;
                    }
                }
            }
        }
        // Board Outline / Graphics Lines (gr_line)
        else if (child.name.compare("gr_line", Qt::CaseInsensitive) == 0) {
            const SExp* startNode = child.child("start");
            const SExp* endNode = child.child("end");
            const SExp* layerNode = child.child("layer");

            if (startNode && endNode) {
                QStringList sPts = startNode->value.split(' ', Qt::SkipEmptyParts);
                QStringList ePts = endNode->value.split(' ', Qt::SkipEmptyParts);
                if (sPts.size() >= 2 && ePts.size() >= 2) {
                    QPointF start(sPts[0].toDouble(), sPts[1].toDouble());
                    QPointF end(ePts[0].toDouble(), ePts[1].toDouble());

                    double w = 0.2;
                    const SExp* widthNode = child.child("width");
                    if (widthNode) w = widthNode->value.toDouble();
                    else {
                        const SExp* strokeNode = child.child("stroke");
                        if (strokeNode) {
                            const SExp* sw = strokeNode->child("width");
                            if (sw) w = sw->value.toDouble();
                        }
                    }

                    TraceItem* edge = new TraceItem(start, end);
                    edge->setWidth(w);
                    edge->setLayer(layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::EdgeCuts);

                    scene->addItem(edge);
                    if (edge->layer() == PCBLayerManager::EdgeCuts) stats.edgeCutsCount++;
                }
            }
        }
        // Board Outline Rect (gr_rect)
        else if (child.name.compare("gr_rect", Qt::CaseInsensitive) == 0) {
            const SExp* startNode = child.child("start");
            const SExp* endNode = child.child("end");
            const SExp* layerNode = child.child("layer");

            if (startNode && endNode) {
                QStringList sPts = startNode->value.split(' ', Qt::SkipEmptyParts);
                QStringList ePts = endNode->value.split(' ', Qt::SkipEmptyParts);
                if (sPts.size() >= 2 && ePts.size() >= 2) {
                    double x1 = sPts[0].toDouble(), y1 = sPts[1].toDouble();
                    double x2 = ePts[0].toDouble(), y2 = ePts[1].toDouble();

                    double w = 0.2;
                    const SExp* widthNode = child.child("width");
                    if (widthNode) w = widthNode->value.toDouble();
                    else {
                        const SExp* strokeNode = child.child("stroke");
                        if (strokeNode) {
                            const SExp* sw = strokeNode->child("width");
                            if (sw) w = sw->value.toDouble();
                        }
                    }

                    int lay = layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::EdgeCuts;

                    QPointF p1(x1, y1), p2(x2, y1), p3(x2, y2), p4(x1, y2);
                    QList<QPair<QPointF, QPointF>> edges = {{p1, p2}, {p2, p3}, {p3, p4}, {p4, p1}};

                    for (const auto& edge : edges) {
                        TraceItem* t = new TraceItem(edge.first, edge.second);
                        t->setWidth(w);
                        t->setLayer(lay);
                        scene->addItem(t);
                        if (lay == PCBLayerManager::EdgeCuts) stats.edgeCutsCount++;
                    }
                }
            }
        }
        // Copper Zones / Pours (zone)
        else if (child.name.compare("zone", Qt::CaseInsensitive) == 0) {
            const SExp* layerNode = child.child("layer");
            const SExp* netNameNode = child.child("net_name");
            const SExp* netNode = child.child("net");

            const SExp* polyNode = child.child("filled_polygon");
            if (!polyNode) polyNode = child.child("polygon");

            if (polyNode) {
                const SExp* ptsNode = polyNode->child("pts");
                if (ptsNode) {
                    QPolygonF poly;
                    for (const auto& xyChild : ptsNode->children) {
                        if (xyChild.name.compare("xy", Qt::CaseInsensitive) == 0) {
                            QStringList pts = xyChild.value.split(' ', Qt::SkipEmptyParts);
                            if (pts.size() >= 2) {
                                poly.append(QPointF(pts[0].toDouble(), pts[1].toDouble()));
                            }
                        }
                    }
                    if (poly.size() >= 3) {
                        CopperPourItem* pour = new CopperPourItem();
                        if (layerNode) pour->setLayer(mapKiCadLayer(layerNode->value));
                        if (netNameNode) {
                            QString n = netNameNode->value;
                            n.remove('"');
                            pour->setNetName(n);
                        } else if (netNode) {
                            pour->setNetName(netMap.value(netNode->value.toInt(), ""));
                        }
                        pour->setPolygon(poly);
                        scene->addItem(pour);
                        stats.zonesCount++;
                    }
                }
            }
        }
        // Graphic Text (gr_text)
        else if (child.name.compare("gr_text", Qt::CaseInsensitive) == 0) {
            QString txtVal = child.value;
            int idxSpace = txtVal.indexOf(' ');
            if (idxSpace != -1) txtVal = txtVal.left(idxSpace);
            txtVal.remove('"');

            const SExp* atNode = child.child("at");
            const SExp* layerNode = child.child("layer");

            if (atNode && !txtVal.isEmpty()) {
                QStringList atPts = atNode->value.split(' ', Qt::SkipEmptyParts);
                if (atPts.size() >= 2) {
                    QPointF pos(atPts[0].toDouble(), atPts[1].toDouble());
                    int lay = layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::TopSilkscreen;

                    QGraphicsTextItem* textItem = scene->addText(txtVal);
                    textItem->setPos(pos);
                    PCBLayer* pLayer = PCBLayerManager::instance().layer(lay);
                    if (pLayer) textItem->setDefaultTextColor(pLayer->color());
                    else textItem->setDefaultTextColor(Qt::white);
                }
            }
        }
        // Mechanical Dimensions (dimension)
        else if (child.name.compare("dimension", Qt::CaseInsensitive) == 0) {
            const SExp* layerNode = child.child("layer");
            const SExp* ptsNode = child.child("pts");
            const SExp* textNode = child.child("gr_text");

            if (ptsNode) {
                QPolygonF poly;
                for (const auto& xyChild : ptsNode->children) {
                    if (xyChild.name.compare("xy", Qt::CaseInsensitive) == 0) {
                        QStringList pts = xyChild.value.split(' ', Qt::SkipEmptyParts);
                        if (pts.size() >= 2) {
                            poly.append(QPointF(pts[0].toDouble(), pts[1].toDouble()));
                        }
                    }
                }
                int lay = layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::UserDrawings;
                for (int i = 0; i < poly.size() - 1; ++i) {
                    TraceItem* t = new TraceItem(poly[i], poly[i + 1]);
                    t->setWidth(0.12);
                    t->setLayer(lay);
                    scene->addItem(t);
                }
            }

            if (textNode) {
                QString txtVal = textNode->value;
                txtVal.remove('"');
                const SExp* atNode = textNode->child("at");
                if (atNode && !txtVal.isEmpty()) {
                    QStringList atPts = atNode->value.split(' ', Qt::SkipEmptyParts);
                    if (atPts.size() >= 2) {
                        QPointF pos(atPts[0].toDouble(), atPts[1].toDouble());
                        QGraphicsTextItem* textItem = scene->addText(txtVal);
                        textItem->setPos(pos);
                        textItem->setDefaultTextColor(QColor(100, 200, 255));
                    }
                }
            }
        }
        // Graphic Polygons (gr_poly)
        else if (child.name.compare("gr_poly", Qt::CaseInsensitive) == 0) {
            const SExp* layerNode = child.child("layer");
            const SExp* ptsNode = child.child("pts");

            if (ptsNode) {
                QPolygonF poly;
                for (const auto& xyChild : ptsNode->children) {
                    if (xyChild.name.compare("xy", Qt::CaseInsensitive) == 0) {
                        QStringList pts = xyChild.value.split(' ', Qt::SkipEmptyParts);
                        if (pts.size() >= 2) {
                            poly.append(QPointF(pts[0].toDouble(), pts[1].toDouble()));
                        }
                    }
                }
                if (poly.size() >= 2) {
                    int lay = layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::EdgeCuts;
                    for (int i = 0; i < poly.size() - 1; ++i) {
                        TraceItem* t = new TraceItem(poly[i], poly[i + 1]);
                        t->setWidth(0.15);
                        t->setLayer(lay);
                        scene->addItem(t);
                        if (lay == PCBLayerManager::EdgeCuts) stats.edgeCutsCount++;
                    }
                }
            }
        }
        // Footprints (footprint / module)
        else if (child.name.compare("footprint", Qt::CaseInsensitive) == 0 ||
                 child.name.compare("module", Qt::CaseInsensitive) == 0) {
            const SExp* atNode = child.child("at");
            const SExp* layerNode = child.child("layer");

            QPointF compPos;
            double compRot = 0.0;
            if (atNode) {
                QStringList atPts = atNode->value.split(' ', Qt::SkipEmptyParts);
                if (atPts.size() >= 2) compPos = QPointF(atPts[0].toDouble(), atPts[1].toDouble());
                if (atPts.size() >= 3) compRot = atPts[2].toDouble();
            }

            QString ref = "REF";
            for (const auto& sub : child.children) {
                if (sub.name.compare("fp_text", Qt::CaseInsensitive) == 0 &&
                    sub.value.startsWith("reference", Qt::CaseInsensitive)) {
                    QStringList parts = sub.value.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) ref = parts[1].remove('"');
                }
            }

            ComponentItem* comp = new ComponentItem();
            comp->setName(ref);
            comp->setPos(compPos);
            comp->setRotation(compRot);
            comp->setLayer(layerNode ? mapKiCadLayer(layerNode->value) : PCBLayerManager::TopCopper);

            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            bool hasPads = false;

            // Parse Pads inside footprint
            for (const auto& sub : child.children) {
                if (sub.name.compare("pad", Qt::CaseInsensitive) == 0) {
                    const SExp* padAt = sub.child("at");
                    const SExp* padSize = sub.child("size");
                    const SExp* padDrill = sub.child("drill");
                    const SExp* padNet = sub.child("net");

                    double padW = 1.0, padH = 1.0;
                    if (padSize) {
                        QStringList s = padSize->value.split(' ', Qt::SkipEmptyParts);
                        if (s.size() >= 2) { padW = s[0].toDouble(); padH = s[1].toDouble(); }
                    }

                    double rx = 0.0, ry = 0.0;
                    if (padAt) {
                        QStringList atPts = padAt->value.split(' ', Qt::SkipEmptyParts);
                        if (atPts.size() >= 2) {
                            rx = atPts[0].toDouble();
                            ry = atPts[1].toDouble();
                            double rad = compRot * M_PI / 180.0;
                            double ax = compPos.x() + rx * std::cos(rad) - ry * std::sin(rad);
                            double ay = compPos.y() + rx * std::sin(rad) + ry * std::cos(rad);
                            QPointF absPadPos(ax, ay);
                            minX = std::min(minX, absPadPos.x() - padW / 2.0);
                            maxX = std::max(maxX, absPadPos.x() + padW / 2.0);
                            minY = std::min(minY, absPadPos.y() - padH / 2.0);
                            maxY = std::max(maxY, absPadPos.y() + padH / 2.0);
                        }
                    }

                    hasPads = true;

                    Flux::Model::PadModel* padModel = new Flux::Model::PadModel();
                    padModel->setPos(QPointF(rx, ry));
                    padModel->setSize(QSizeF(padW, padH));
                    PadItem* pad = new PadItem(padModel);
                    pad->setLayer(comp->layer());
                    pad->setParentItem(comp);
                    if (padDrill) pad->setDrillSize(padDrill->value.toDouble());
                    if (padNet) {
                        int netId = padNet->value.toInt();
                        pad->setNetName(netMap.value(netId, ""));
                    }
                }
            }

            if (hasPads && maxX > minX && maxY > minY) {
                comp->setSize(QSizeF(maxX - minX + 0.2, maxY - minY + 0.2));
            } else {
                comp->setSize(QSizeF(0.1, 0.1)); // Hide huge default fallback body
            }

            scene->addItem(comp);
            stats.footprintsCount++;
        }
    }

    stats.success = true;
    return stats;
}
