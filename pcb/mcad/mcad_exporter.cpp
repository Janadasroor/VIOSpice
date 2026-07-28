/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mcad_exporter.h"
#include "../items/pcb_item.h"
#include "../items/component_item.h"
#include "../items/pad_item.h"
#include "../items/via_item.h"
#include "../items/trace_item.h"
#include <QGraphicsScene>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>

namespace {
QString f6(double v) { return QString::number(v, 'f', 6); }
}

bool MCADExporter::exportSTEPWireframe(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    const QString ts = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss");

    // Minimal STEP Part 21 wireframe content (polyline geometry), readable by many CAD tools.
    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('Viora EDA STEP wireframe export'),'2;1');\n";
    out << "FILE_NAME('" << QFileInfo(filePath).fileName() << "','" << ts << "',('Viora EDA'),('Viora EDA'),'Viora EDA','Viora EDA','');\n";
    out << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\n";
    out << "ENDSEC;\n";
    out << "DATA;\n";

    int id = 1;
    out << "#" << id++ << "=CARTESIAN_POINT('BOARD_LL',(" << f6(bb.left()) << "," << f6(bb.top()) << ",0.0));\n";
    out << "#" << id++ << "=CARTESIAN_POINT('BOARD_LR',(" << f6(bb.right()) << "," << f6(bb.top()) << ",0.0));\n";
    out << "#" << id++ << "=CARTESIAN_POINT('BOARD_UR',(" << f6(bb.right()) << "," << f6(bb.bottom()) << ",0.0));\n";
    out << "#" << id++ << "=CARTESIAN_POINT('BOARD_UL',(" << f6(bb.left()) << "," << f6(bb.bottom()) << ",0.0));\n";
    out << "#" << id++ << "=POLYLINE('BOARD_OUTLINE',(#1,#2,#3,#4,#1));\n";

    int compCount = 0;
    for (QGraphicsItem* g : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(g);
        if (!item) continue;
        if (item->parentItem()) continue;

        QRectF r = item->sceneBoundingRect();
        const QString base = QString("ITEM_%1").arg(++compCount);
        const int p1 = id++;
        const int p2 = id++;
        const int p3 = id++;
        const int p4 = id++;
        out << "#" << p1 << "=CARTESIAN_POINT('" << base << "_LL',(" << f6(r.left()) << "," << f6(r.top()) << ",0.0));\n";
        out << "#" << p2 << "=CARTESIAN_POINT('" << base << "_LR',(" << f6(r.right()) << "," << f6(r.top()) << ",0.0));\n";
        out << "#" << p3 << "=CARTESIAN_POINT('" << base << "_UR',(" << f6(r.right()) << "," << f6(r.bottom()) << ",0.0));\n";
        out << "#" << p4 << "=CARTESIAN_POINT('" << base << "_UL',(" << f6(r.left()) << "," << f6(r.bottom()) << ",0.0));\n";
        out << "#" << id++ << "=POLYLINE('" << base << "',(#" << p1 << ",#" << p2 << ",#" << p3 << ",#" << p4 << ",#" << p1 << "));\n";
    }

    out << "ENDSEC;\n";
    out << "END-ISO-10303-21;\n";
    return true;
}

bool MCADExporter::exportIGESWireframe(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    out << "IGES-WIREFRAME-VioraEDA\n";
    out << "BOARD_BBOX," << f6(bb.left()) << "," << f6(bb.top()) << "," << f6(bb.right()) << "," << f6(bb.bottom()) << "\n";
    out << "TYPE,NAME,X1,Y1,X2,Y2\n";

    out << "LINE,BOARD_BOTTOM," << f6(bb.left()) << "," << f6(bb.top()) << "," << f6(bb.right()) << "," << f6(bb.top()) << "\n";
    out << "LINE,BOARD_RIGHT," << f6(bb.right()) << "," << f6(bb.top()) << "," << f6(bb.right()) << "," << f6(bb.bottom()) << "\n";
    out << "LINE,BOARD_TOP," << f6(bb.right()) << "," << f6(bb.bottom()) << "," << f6(bb.left()) << "," << f6(bb.bottom()) << "\n";
    out << "LINE,BOARD_LEFT," << f6(bb.left()) << "," << f6(bb.bottom()) << "," << f6(bb.left()) << "," << f6(bb.top()) << "\n";

    int index = 0;
    for (QGraphicsItem* g : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(g);
        if (!item || item->parentItem()) continue;
        QRectF r = item->sceneBoundingRect();
        const QString n = QString("%1_%2").arg(item->itemTypeName()).arg(++index);
        out << "RECT," << n << "," << f6(r.left()) << "," << f6(r.top()) << "," << f6(r.right()) << "," << f6(r.bottom()) << "\n";
    }

    return true;
}

bool MCADExporter::exportVRMLAssembly(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    if (bb.isEmpty()) {
        if (error) *error = "Scene has no geometry";
        return false;
    }

    const double boardThickness = 1.6;
    const double boardW = bb.width();
    const double boardH = bb.height();
    const double boardCx = (bb.left() + bb.right()) * 0.5;
    const double boardCy = (bb.top() + bb.bottom()) * 0.5;

    out << "#VRML V2.0 utf8\n";
    out << "WorldInfo { title \"Viora EDA Board Assembly\" }\n";
    out << "Background { skyColor [ 0.07 0.09 0.12 ] }\n";
    out << "NavigationInfo { type [\"EXAMINE\", \"ANY\"] }\n";
    out << "Transform {\n";
    out << "  translation " << f6(boardCx) << " " << f6(-boardCy) << " 0\n";
    out << "  children [\n";
    out << "    Shape {\n";
    out << "      appearance Appearance { material Material { diffuseColor 0.06 0.26 0.11 specularColor 0.1 0.1 0.1 shininess 0.2 } }\n";
    out << "      geometry Box { size " << f6(boardW) << " " << f6(boardH) << " " << f6(boardThickness) << " }\n";
    out << "    }\n";
    out << "  ]\n";
    out << "}\n";

    int compIndex = 0;
    for (QGraphicsItem* g : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(g);
        if (!item || item->parentItem()) continue;

        QRectF r = item->sceneBoundingRect();
        if (r.isEmpty()) continue;

        const double h = (item->height() > 0.0) ? item->height() : 2.0;
        const double w = qMax(0.1, r.width());
        const double d = qMax(0.1, r.height());
        const double cx = (r.left() + r.right()) * 0.5;
        const double cy = (r.top() + r.bottom()) * 0.5;
        const double cz = boardThickness * 0.5 + h * 0.5;
        const double rotRad = -item->rotation() * M_PI / 180.0;

        // Simple assembly solids for MCAD envelope usage.
        out << "Transform {\n";
        out << "  translation " << f6(cx) << " " << f6(-cy) << " " << f6(cz) << "\n";
        if (std::abs(item->rotation()) > 1e-6) {
            out << "  rotation 0 0 1 " << f6(rotRad) << "\n";
        }
        out << "  children [\n";
        out << "    Shape {\n";
        out << "      appearance Appearance { material Material { diffuseColor 0.22 0.22 0.25 transparency 0.0 } }\n";
        out << "      geometry Box { size " << f6(w) << " " << f6(d) << " " << f6(h) << " }\n";
        out << "    }\n";
        out << "  ]\n";
        out << "}\n";
        ++compIndex;
    }

    if (compIndex == 0) {
        out << "# No top-level components found; only board body exported.\n";
    }
    return true;
}

namespace {
void writeBoxSTL(QTextStream& out, const QPointF& minPt, const QPointF& maxPt, double zMin, double zMax) {
    double x1 = minPt.x(), y1 = minPt.y();
    double x2 = maxPt.x(), y2 = maxPt.y();
    double z1 = zMin, z2 = zMax;

    auto facet = [&](double nx, double ny, double nz, double ax, double ay, double az, double bx, double by, double bz, double cx, double cy, double cz) {
        out << "facet normal " << f6(nx) << " " << f6(ny) << " " << f6(nz) << "\n";
        out << "  outer loop\n";
        out << "    vertex " << f6(ax) << " " << f6(ay) << " " << f6(az) << "\n";
        out << "    vertex " << f6(bx) << " " << f6(by) << " " << f6(bz) << "\n";
        out << "    vertex " << f6(cx) << " " << f6(cy) << " " << f6(cz) << "\n";
        out << "  endloop\n";
        out << "endfacet\n";
    };

    // Top (+Z)
    facet(0, 0, 1, x1, y1, z2, x2, y1, z2, x2, y2, z2);
    facet(0, 0, 1, x1, y1, z2, x2, y2, z2, x1, y2, z2);
    // Bottom (-Z)
    facet(0, 0, -1, x1, y1, z1, x2, y2, z1, x2, y1, z1);
    facet(0, 0, -1, x1, y1, z1, x1, y2, z1, x2, y2, z1);
    // Front (+Y)
    facet(0, 1, 0, x1, y2, z1, x2, y2, z1, x2, y2, z2);
    facet(0, 1, 0, x1, y2, z1, x2, y2, z2, x1, y2, z2);
    // Back (-Y)
    facet(0, -1, 0, x1, y1, z1, x1, y1, z2, x2, y1, z2);
    facet(0, -1, 0, x1, y1, z1, x2, y1, z2, x2, y1, z1);
    // Right (+X)
    facet(1, 0, 0, x2, y1, z1, x2, y1, z2, x2, y2, z2);
    facet(1, 0, 0, x2, y1, z1, x2, y2, z2, x2, y2, z1);
    // Left (-X)
    facet(-1, 0, 0, x1, y1, z1, x1, y2, z1, x1, y2, z2);
    facet(-1, 0, 0, x1, y1, z1, x1, y2, z2, x1, y1, z2);
}
} // namespace

bool MCADExporter::exportSTL3D(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    const double boardThick = 1.6;

    out << "solid VioraEDA_3D_Board\n";
    // Substrate
    writeBoxSTL(out, bb.topLeft(), bb.bottomRight(), -boardThick * 0.5, boardThick * 0.5);

    // Components
    for (QGraphicsItem* g : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(g);
        if (!item || item->parentItem()) continue;
        QRectF r = item->sceneBoundingRect();
        double h = (item->height() > 0.0) ? item->height() : 2.0;
        writeBoxSTL(out, r.topLeft(), r.bottomRight(), boardThick * 0.5, boardThick * 0.5 + h);
    }

    out << "endsolid VioraEDA_3D_Board\n";
    return true;
}

bool MCADExporter::exportOBJ3D(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    const QRectF bb = scene->itemsBoundingRect();
    const double boardThick = 1.6;

    out << "# Viora EDA Wavefront OBJ 3D Exporter\n";
    out << "o Board_Substrate\n";

    // Board vertices
    out << "v " << f6(bb.left()) << " " << f6(bb.top()) << " " << f6(-boardThick * 0.5) << "\n";
    out << "v " << f6(bb.right()) << " " << f6(bb.top()) << " " << f6(-boardThick * 0.5) << "\n";
    out << "v " << f6(bb.right()) << " " << f6(bb.bottom()) << " " << f6(-boardThick * 0.5) << "\n";
    out << "v " << f6(bb.left()) << " " << f6(bb.bottom()) << " " << f6(-boardThick * 0.5) << "\n";

    out << "v " << f6(bb.left()) << " " << f6(bb.top()) << " " << f6(boardThick * 0.5) << "\n";
    out << "v " << f6(bb.right()) << " " << f6(bb.top()) << " " << f6(boardThick * 0.5) << "\n";
    out << "v " << f6(bb.right()) << " " << f6(bb.bottom()) << " " << f6(boardThick * 0.5) << "\n";
    out << "v " << f6(bb.left()) << " " << f6(bb.bottom()) << " " << f6(boardThick * 0.5) << "\n";

    out << "f 1 2 3 4\n";
    out << "f 5 6 7 8\n";
    out << "f 1 2 6 5\n";
    out << "f 2 3 7 6\n";
    out << "f 3 4 8 7\n";
    out << "f 4 1 5 8\n";

    int vIdx = 9;
    int cIdx = 0;
    for (QGraphicsItem* g : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(g);
        if (!item || item->parentItem()) continue;
        QRectF r = item->sceneBoundingRect();
        double h = (item->height() > 0.0) ? item->height() : 2.0;

        out << "o Comp_" << ++cIdx << "\n";
        out << "v " << f6(r.left()) << " " << f6(r.top()) << " " << f6(boardThick * 0.5) << "\n";
        out << "v " << f6(r.right()) << " " << f6(r.top()) << " " << f6(boardThick * 0.5) << "\n";
        out << "v " << f6(r.right()) << " " << f6(r.bottom()) << " " << f6(boardThick * 0.5) << "\n";
        out << "v " << f6(r.left()) << " " << f6(r.bottom()) << " " << f6(boardThick * 0.5) << "\n";

        out << "v " << f6(r.left()) << " " << f6(r.top()) << " " << f6(boardThick * 0.5 + h) << "\n";
        out << "v " << f6(r.right()) << " " << f6(r.top()) << " " << f6(boardThick * 0.5 + h) << "\n";
        out << "v " << f6(r.right()) << " " << f6(r.bottom()) << " " << f6(boardThick * 0.5 + h) << "\n";
        out << "v " << f6(r.left()) << " " << f6(r.bottom()) << " " << f6(boardThick * 0.5 + h) << "\n";

        out << "f " << vIdx << " " << (vIdx+1) << " " << (vIdx+2) << " " << (vIdx+3) << "\n";
        out << "f " << (vIdx+4) << " " << (vIdx+5) << " " << (vIdx+6) << " " << (vIdx+7) << "\n";
        out << "f " << vIdx << " " << (vIdx+1) << " " << (vIdx+5) << " " << (vIdx+4) << "\n";
        out << "f " << (vIdx+1) << " " << (vIdx+2) << " " << (vIdx+6) << " " << (vIdx+5) << "\n";
        out << "f " << (vIdx+2) << " " << (vIdx+3) << " " << (vIdx+7) << " " << (vIdx+6) << "\n";
        out << "f " << (vIdx+3) << " " << vIdx << " " << (vIdx+4) << " " << (vIdx+7) << "\n";
        vIdx += 8;
    }

    return true;
}

bool MCADExporter::exportGLTF3D(QGraphicsScene* scene, const QString& filePath, QString* error) {
    if (!scene) {
        if (error) *error = "Invalid scene";
        return false;
    }
    // Minimal valid glTF 2.0 JSON representation for web and CAD viewers
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Unable to open file for writing";
        return false;
    }

    QTextStream out(&file);
    out << "{\n";
    out << "  \"asset\": { \"version\": \"2.0\", \"generator\": \"Viora EDA 3D GLTF Exporter\" },\n";
    out << "  \"scenes\": [{ \"nodes\": [0] }],\n";
    out << "  \"nodes\": [{ \"name\": \"PCB_Assembly\" }]\n";
    out << "}\n";
    return true;
}
