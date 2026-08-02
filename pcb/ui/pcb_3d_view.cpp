// File: pcb/ui/pcb_3d_view.cpp
// ============================================================================
/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */
#include "pcb_3d_view.h"
#include "../items/component_item.h"
#include "../items/pad_item.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/copper_pour_item.h"
#include "../items/image_item.h"
#include "../layers/pcb_layer.h"
#include "config_manager.h"
#include "../../footprints/footprint_library.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLineF>
#include <QPainter>
#include <QRegularExpression>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QKeyEvent>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

using Flux::Model::Footprint3DModel;

namespace {

constexpr float kBoardThickness = 1.6f;
constexpr float kCopperZTop = 0.82f;
constexpr float kCopperZBottom = -0.82f;
constexpr float kCopperThickness = 0.05f;
constexpr int kSpnavEventMotion = 1;
constexpr int kSpnavEventButton = 2;

struct SpnavMotionEvent {
    int type;
    int x, y, z;
    int rx, ry, rz;
    unsigned int period;
};
struct SpnavButtonEvent {
    int type;
    int press;
    int bnum;
};
union SpnavEvent {
    int type;
    SpnavMotionEvent motion;
    SpnavButtonEvent button;
};

QVector3D colorToVec3(const QColor& c) {
    return QVector3D(c.redF(), c.greenF(), c.blueF());
}

bool isFinite3(const QVector3D& v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

void appendTri(QVector<PCB3DView::Vertex>& out, const QVector3D& a, const QVector3D& b, const QVector3D& c) {
    const QVector3D n = QVector3D::normal(a, b, c);
    out.push_back({a, n});
    out.push_back({b, n});
    out.push_back({c, n});
}

void appendQuad(QVector<PCB3DView::Vertex>& out,
                const QVector3D& a, const QVector3D& b, const QVector3D& c, const QVector3D& d,
                const QVector3D& normal) {
    out.push_back({a, normal});
    out.push_back({b, normal});
    out.push_back({c, normal});
    out.push_back({a, normal});
    out.push_back({c, normal});
    out.push_back({d, normal});
}

void appendRing(QVector<PCB3DView::Vertex>& out,
                float cx, float cy, float rIn, float rOut, float z, bool upNormal,
                int segments = 24) {
    if (rOut <= rIn + 1e-6f) return;
    const QVector3D n = upNormal ? QVector3D(0, 0, 1) : QVector3D(0, 0, -1);
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(i) * float(2.0 * M_PI / segments);
        const float a1 = float(i + 1) * float(2.0 * M_PI / segments);
        const QVector3D o0(cx + rOut * std::cos(a0), cy + rOut * std::sin(a0), z);
        const QVector3D o1(cx + rOut * std::cos(a1), cy + rOut * std::sin(a1), z);
        const QVector3D i1(cx + rIn * std::cos(a1), cy + rIn * std::sin(a1), z);
        const QVector3D i0(cx + rIn * std::cos(a0), cy + rIn * std::sin(a0), z);
        if (upNormal) appendQuad(out, o0, o1, i1, i0, n);
        else appendQuad(out, o0, i0, i1, o1, n);
    }
}

void appendCylinder(QVector<PCB3DView::Vertex>& out,
                    float cx, float cy, float radius, float zTop, float zBot,
                    int segments = 24) {
    if (radius <= 1e-6f || zTop <= zBot) return;
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(i) * float(2.0 * M_PI / segments);
        const float a1 = float(i + 1) * float(2.0 * M_PI / segments);
        const QVector3D p0(cx + radius * std::cos(a0), cy + radius * std::sin(a0), zTop);
        const QVector3D p1(cx + radius * std::cos(a1), cy + radius * std::sin(a1), zTop);
        const QVector3D p2(cx + radius * std::cos(a1), cy + radius * std::sin(a1), zBot);
        const QVector3D p3(cx + radius * std::cos(a0), cy + radius * std::sin(a0), zBot);
        const QVector3D n(std::cos((a0 + a1) * 0.5f), std::sin((a0 + a1) * 0.5f), 0.0f);
        appendQuad(out, p0, p1, p2, p3, n);
    }
}

void appendThickSegment(QVector<PCB3DView::Vertex>& out,
                        const QPointF& a, const QPointF& b,
                        float width, float z, const QVector3D& normal) {
    QVector2D d(float(b.x() - a.x()), float(b.y() - a.y()));
    if (d.lengthSquared() < 1e-9f) return;
    d.normalize();
    QVector2D n(-d.y(), d.x());
    const float hw = std::max(0.01f, width * 0.5f);
    const QVector3D p1(float(a.x()), float(-a.y()), z);
    const QVector3D p2(float(b.x()), float(-b.y()), z);
    const QVector3D o(n.x() * hw, -n.y() * hw, 0.0f);
    if (normal.z() > 0) appendQuad(out, p1 - o, p2 - o, p2 + o, p1 + o, normal);
    else appendQuad(out, p1 - o, p1 + o, p2 + o, p2 - o, normal);
}

void appendTraceCapFace(QVector<PCB3DView::Vertex>& out,
                        const QVector3D& center, const QVector3D& dir, const QVector3D& side,
                        float radius, float startAngle, float endAngle,
                        const QVector3D& normal, int segments = 12) {
    QVector3D prev = center + (std::cos(startAngle) * dir + std::sin(startAngle) * side) * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = startAngle + (endAngle - startAngle) * (float(i) / float(segments));
        const QVector3D cur = center + (std::cos(t) * dir + std::sin(t) * side) * radius;
        if (normal.z() > 0.0f) appendTri(out, center, prev, cur);
        else appendTri(out, center, cur, prev);
        prev = cur;
    }
}

void appendTraceCapWall(QVector<PCB3DView::Vertex>& out,
                        const QVector3D& centerTop, const QVector3D& centerBot,
                        const QVector3D& dir, const QVector3D& side,
                        float radius, float startAngle, float endAngle,
                        int segments = 12) {
    QVector3D prevTop = centerTop + (std::cos(startAngle) * dir + std::sin(startAngle) * side) * radius;
    QVector3D prevBot = centerBot + (std::cos(startAngle) * dir + std::sin(startAngle) * side) * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = startAngle + (endAngle - startAngle) * (float(i) / float(segments));
        const QVector3D curTop = centerTop + (std::cos(t) * dir + std::sin(t) * side) * radius;
        const QVector3D curBot = centerBot + (std::cos(t) * dir + std::sin(t) * side) * radius;
        QVector3D mid = ((prevTop + curTop) * 0.5f) - centerTop;
        QVector3D normal(mid.x(), mid.y(), 0.0f);
        if (normal.lengthSquared() < 1e-9f) normal = QVector3D(side.x(), side.y(), 0.0f);
        normal.normalize();
        appendQuad(out, prevTop, curTop, curBot, prevBot, normal);
        prevTop = curTop;
        prevBot = curBot;
    }
}

void appendCopperTrace(QVector<PCB3DView::Vertex>& out,
                       const QPointF& a, const QPointF& b,
                       float width, float zFace, float zBase, bool topLayer) {
    QVector2D d(float(b.x() - a.x()), float(-(b.y() - a.y())));
    if (d.lengthSquared() < 1e-9f) return;
    d.normalize();
    const QVector2D n(-d.y(), d.x());
    const float hw = std::max(0.01f, width * 0.5f);
    const QVector3D dir(d.x(), d.y(), 0.0f);
    const QVector3D side(n.x(), n.y(), 0.0f);
    const QVector3D normal = topLayer ? QVector3D(0, 0, 1) : QVector3D(0, 0, -1);
    const QVector3D aTop(float(a.x()), float(-a.y()), zFace);
    const QVector3D bTop(float(b.x()), float(-b.y()), zFace);
    const QVector3D aBot(float(a.x()), float(-a.y()), zBase);
    const QVector3D bBot(float(b.x()), float(-b.y()), zBase);
    const QVector3D left = side * hw;
    const QVector3D right = side * -hw;
    if (topLayer) appendQuad(out, aTop + left, bTop + left, bTop + right, aTop + right, normal);
    else appendQuad(out, aTop + left, aTop + right, bTop + right, bTop + left, normal);
    appendTraceCapFace(out, aTop, -dir, side, hw, -float(M_PI_2), float(M_PI_2), normal);
    appendTraceCapFace(out, bTop, dir, side, hw, -float(M_PI_2), float(M_PI_2), normal);
    appendQuad(out, aTop + left, bTop + left, bBot + left, aBot + left, side);
    appendQuad(out, bTop + right, aTop + right, aBot + right, bBot + right, -side);
    appendTraceCapWall(out, aTop, aBot, -dir, side, hw, -float(M_PI_2), float(M_PI_2));
    appendTraceCapWall(out, bTop, bBot, dir, side, hw, -float(M_PI_2), float(M_PI_2));
}

void appendMappedQuad(QVector<PCB3DView::Vertex>& out,
                      const QPointF& tl, const QPointF& tr, const QPointF& br, const QPointF& bl,
                      float z, const QVector3D& normal) {
    const QVector3D a(float(tl.x()), float(-tl.y()), z);
    const QVector3D b(float(tr.x()), float(-tr.y()), z);
    const QVector3D c(float(br.x()), float(-br.y()), z);
    const QVector3D d(float(bl.x()), float(-bl.y()), z);
    if (normal.z() > 0.0f) appendQuad(out, a, d, c, b, normal);
    else appendQuad(out, a, b, c, d, normal);
}

void appendPolygonWalls(QVector<PCB3DView::Vertex>& out,
                        const QPolygonF& poly, float zTop, float zBot) {
    if (poly.size() < 2) return;
    const int last = poly.size() - 1;
    for (int i = 0; i < last; ++i) {
        const QPointF& p0 = poly[i];
        const QPointF& p1 = poly[i + 1];
        if (QLineF(p0, p1).length() < 1e-6) continue;
        const QVector3D a(float(p0.x()), float(-p0.y()), zBot);
        const QVector3D b(float(p1.x()), float(-p1.y()), zBot);
        const QVector3D c(float(p1.x()), float(-p1.y()), zTop);
        const QVector3D d(float(p0.x()), float(-p0.y()), zTop);
        QVector3D normal = QVector3D::crossProduct(c - b, a - b).normalized();
        if (normal.lengthSquared() < 1e-9f) continue;
        appendQuad(out, a, b, c, d, normal);
    }
}

qreal polygonSignedArea(const QPolygonF& poly) {
    if (poly.size() < 3) return 0.0;
    qreal area = 0.0;
    const int last = poly.size() - 1;
    for (int i = 0; i < last; ++i) {
        const QPointF& a = poly[i];
        const QPointF& b = poly[i + 1];
        area += (a.x() * b.y()) - (b.x() * a.y());
    }
    return area * 0.5;
}

QPolygonF ensureClosedPolygon(QPolygonF poly) {
    if (poly.size() >= 3 && poly.first() != poly.last()) poly << poly.first();
    return poly;
}

bool pointsNear2D(const QPointF& a, const QPointF& b, qreal eps = 1e-3) {
    return QLineF(a, b).length() <= eps;
}

QList<QPolygonF> buildClosedPolygonsFromSegments(const QList<QPair<QPointF, QPointF>>& segments) {
    QList<QPolygonF> polygons;
    if (segments.isEmpty()) return polygons;
    QVector<bool> used(segments.size(), false);
    for (int i = 0; i < segments.size(); ++i) {
        if (used[i]) continue;
        QPolygonF poly;
        QPointF start = segments[i].first;
        QPointF current = segments[i].second;
        poly << start << current;
        used[i] = true;
        bool advanced = true;
        while (advanced) {
            advanced = false;
            for (int j = 0; j < segments.size(); ++j) {
                if (used[j]) continue;
                const QPointF& a = segments[j].first;
                const QPointF& b = segments[j].second;
                if (pointsNear2D(a, current)) {
                    current = b; poly << current; used[j] = true; advanced = true; break;
                }
                if (pointsNear2D(b, current)) {
                    current = a; poly << current; used[j] = true; advanced = true; break;
                }
            }
            if (poly.size() >= 4 && pointsNear2D(current, start)) {
                if (!pointsNear2D(poly.last(), poly.first())) poly << poly.first();
                break;
            }
        }
        if (poly.size() >= 4 && pointsNear2D(poly.first(), poly.last()))
            polygons.append(ensureClosedPolygon(poly));
    }
    return polygons;
}

// ---------------------------------------------------------------------------
// Polygon triangulation: hole bridging + ear clipping (replaces grid tiling)
// ---------------------------------------------------------------------------
bool pointInTri(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) {
    const qreal d1 = (p.x() - b.x()) * (a.y() - b.y()) - (a.x() - b.x()) * (p.y() - b.y());
    const qreal d2 = (p.x() - c.x()) * (b.y() - c.y()) - (b.x() - c.x()) * (p.y() - c.y());
    const qreal d3 = (p.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (p.y() - a.y());
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

QVector<int> earClipTriangulate(const QPolygonF& poly) {
    QVector<int> out;
    const int n = poly.size();
    if (n < 3) return out;
    out.reserve((n - 2) * 3);
    QVector<int> rem;
    rem.reserve(n);
    if (polygonSignedArea(poly) >= 0.0) {
        for (int i = 0; i < n; ++i) rem.push_back(i);
    } else {
        for (int i = n - 1; i >= 0; --i) rem.push_back(i);
    }
    int count = rem.size();
    int idx = 0;
    int misses = 0;
    while (count > 3) {
        const int i0 = idx % count;
        const int i1 = (idx + 1) % count;
        const int i2 = (idx + 2) % count;
        const QPointF& A = poly[rem[i0]];
        const QPointF& B = poly[rem[i1]];
        const QPointF& C = poly[rem[i2]];
        const qreal cross = (B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x());
        bool ear = cross > 1e-9;
        if (ear) {
            for (int j = 0; j < count; ++j) {
                if (j == i0 || j == i1 || j == i2) continue;
                if (pointInTri(poly[rem[j]], A, B, C)) { ear = false; break; }
            }
        }
        if (ear || misses > count) { // force-clip guarantees termination on degenerate input
            out << rem[i0] << rem[i1] << rem[i2];
            rem.removeAt(i1);
            --count;
            misses = 0;
        } else {
            ++idx;
            ++misses;
        }
    }
    out << rem[0] << rem[1] << rem[2];
    return out;
}

QPolygonF bridgeHoleIntoOutline(QPolygonF outline, const QPolygonF& holeIn) {
    if (holeIn.size() < 3) return outline;
    QPolygonF hole = holeIn;
    if (polygonSignedArea(hole) > 0.0) std::reverse(hole.begin(), hole.end()); // holes must be CW
    if (hole.size() >= 4 && pointsNear2D(hole.first(), hole.last())) hole.removeLast();
    if (outline.size() >= 4 && pointsNear2D(outline.first(), outline.last())) outline.removeLast();
    int hj = 0;
    for (int i = 1; i < hole.size(); ++i)
        if (hole[i].x() > hole[hj].x()) hj = i;
    const QPointF hp = hole[hj];
    int bestVert = -1;
    qreal bestX = std::numeric_limits<qreal>::max();
    const int on = outline.size();
    for (int i = 0; i < on; ++i) {
        const QPointF& a = outline[i];
        const QPointF& b = outline[(i + 1) % on];
        if ((a.y() > hp.y()) == (b.y() > hp.y())) continue;
        const qreal t = (hp.y() - a.y()) / (b.y() - a.y());
        const qreal xInt = a.x() + t * (b.x() - a.x());
        if (xInt < hp.x() - 1e-9) continue;
        if (xInt < bestX) {
            bestX = xInt;
            bestVert = (a.x() >= b.x()) ? i : (i + 1) % on;
        }
    }
    if (bestVert < 0) return outline;
    QPolygonF merged;
    merged.reserve(outline.size() + hole.size() + 3);
    for (int i = 0; i <= bestVert; ++i) merged << outline[i];
    for (int i = 0; i <= hole.size(); ++i) merged << hole[(hj + i) % hole.size()];
    for (int i = bestVert; i < on; ++i) merged << outline[i];
    return merged;
}

PCB3DView::TriMesh2D triangulateBoard(QPolygonF outer, const QList<QPolygonF>& holes) {
    PCB3DView::TriMesh2D tm;
    if (outer.size() < 3) return tm;
    if (polygonSignedArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());
    if (outer.size() >= 4 && pointsNear2D(outer.first(), outer.last())) outer.removeLast();
    QPolygonF merged = outer;
    for (const QPolygonF& h : holes) merged = bridgeHoleIntoOutline(merged, h);
    tm.pts = merged;
    tm.idx = earClipTriangulate(merged);
    return tm;
}

void emitTriMeshFace(QVector<PCB3DView::Vertex>& out, const PCB3DView::TriMesh2D& tm,
                     float z, bool up) {
    for (int i = 0; i + 2 < tm.idx.size(); i += 3) {
        const QPointF& a = tm.pts[tm.idx[i]];
        const QPointF& b = tm.pts[tm.idx[i + 1]];
        const QPointF& c = tm.pts[tm.idx[i + 2]];
        const QVector3D va(a.x(), -a.y(), z);
        const QVector3D vb(b.x(), -b.y(), z);
        const QVector3D vc(c.x(), -c.y(), z);
        if (up) appendTri(out, va, vc, vb);
        else appendTri(out, va, vb, vc);
    }
}

void transformAabb(const QMatrix4x4& m, const QVector3D& bmin, const QVector3D& bmax,
                   QVector3D& obmin, QVector3D& obmax) {
    obmin = QVector3D(1e9f, 1e9f, 1e9f);
    obmax = QVector3D(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < 8; ++i) {
        const QVector3D c(i & 1 ? bmax.x() : bmin.x(),
                          i & 2 ? bmax.y() : bmin.y(),
                          i & 4 ? bmax.z() : bmin.z());
        const QVector3D w = (m * QVector4D(c, 1.0f)).toVector3D();
        obmin.setX(std::min(obmin.x(), w.x()));
        obmin.setY(std::min(obmin.y(), w.y()));
        obmin.setZ(std::min(obmin.z(), w.z()));
        obmax.setX(std::max(obmax.x(), w.x()));
        obmax.setY(std::max(obmax.y(), w.y()));
        obmax.setZ(std::max(obmax.z(), w.z()));
    }
}

} // namespace

PCB3DView::PCB3DView(QWidget* parent)
    : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4); // MSAA
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    setFormat(fmt);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
                 QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f);

    m_interactionTimer.setSingleShot(true);
    m_interactionTimer.setInterval(200);
    connect(&m_interactionTimer, &QTimer::timeout, this, [this]() {
        if (m_renderMode != RenderMode::Full) {
            m_renderMode = RenderMode::Full;
            m_sceneDirty = true;
            update();
        }
    });
    m_cameraAnimTimer.setInterval(16);
    connect(&m_cameraAnimTimer, &QTimer::timeout, this, &PCB3DView::tickCameraAnimation);
    m_spinTimer.setInterval(16);
    connect(&m_spinTimer, &QTimer::timeout, this, &PCB3DView::tickSpinAnimation);
    m_inertiaTimer.setInterval(16);
    connect(&m_inertiaTimer, &QTimer::timeout, this, &PCB3DView::tickInertia);
    m_spaceMousePollTimer.setInterval(8);
    connect(&m_spaceMousePollTimer, &QTimer::timeout, this, &PCB3DView::pollSpaceMouse);

    m_hoverThrottle.start();
    m_frameTimer.start();
}

PCB3DView::~PCB3DView() {
    if (m_spnavClose && m_spaceMouseConnected) {
        m_spnavClose();
        m_spaceMouseConnected = false;
    }
    makeCurrent();
    if (m_staticVbo.isCreated()) m_staticVbo.destroy();
    delete m_pickFbo;
    m_pickFbo = nullptr;
    delete m_shadowFbo;
    m_shadowFbo = nullptr;
    doneCurrent();
}

void PCB3DView::setScene(QGraphicsScene* scene) {
    m_scene = scene;
    m_sceneDirty = true;
    update();
}

void PCB3DView::updateScene() {
    m_sceneDirty = true;
    update();
}

void PCB3DView::setShowSubstrate(bool enabled) { m_showSubstrate = enabled; update(); }
void PCB3DView::setShowCopper(bool enabled) {
    m_showCopperTop = enabled;
    m_showCopperBottom = enabled;
    update();
}
void PCB3DView::setShowTopCopper(bool enabled) { m_showCopperTop = enabled; update(); }
void PCB3DView::setShowBottomCopper(bool enabled) { m_showCopperBottom = enabled; update(); }
void PCB3DView::setShowSilkscreen(bool enabled) { m_showSilkscreen = enabled; update(); }
void PCB3DView::setShowComponents(bool enabled) { m_showComponents = enabled; update(); }
void PCB3DView::setShowGrid(bool enabled) { m_showGrid = enabled; update(); }
void PCB3DView::setShowStats(bool enabled) { m_showStats = enabled; update(); }
void PCB3DView::setSelectedOnly(bool enabled) { m_selectedOnly = enabled; m_sceneDirty = true; update(); }
void PCB3DView::setNetFilter(const QString& netName) { m_netFilter = netName; m_sceneDirty = true; update(); }
void PCB3DView::setEnhancedLighting(bool enabled) { m_enhancedLighting = enabled; update(); }

void PCB3DView::setOrthographic(bool enabled) {
    if (m_orthographic == enabled) return;
    m_orthographic = enabled;
    updateProjectionMatrix();
    update();
}

void PCB3DView::setExplodeAmount(float mm) {
    const float clamped = std::clamp(mm, 0.0f, 50.0f);
    if (std::abs(m_explodeAmount - clamped) < 1e-6f) return;
    m_explodeAmount = clamped;
    m_sceneDirty = true;
    update();
}

void PCB3DView::setSoldermaskColor(const QColor& color) {
    if (!color.isValid()) return;
    m_soldermaskColor = color;
    update();
}
void PCB3DView::setCopperTopColor(const QColor& color) {
    if (!color.isValid()) return;
    m_copperTopColor = color;
    update();
}
void PCB3DView::setCopperBottomColor(const QColor& color) {
    if (!color.isValid()) return;
    m_copperBottomColor = color;
    update();
}
void PCB3DView::setComponentColor(const QColor& color) {
    if (!color.isValid()) return;
    m_componentColor = color;
    update();
}
void PCB3DView::setComponentAlpha(float alpha) {
    m_componentAlpha = std::clamp(alpha, 0.0f, 1.0f);
    update();
}

void PCB3DView::setZoomDistance(float distance) {
    const float clamped = std::clamp(distance, 20.0f, 2000.0f);
    const float newZoom = -clamped;
    if (std::abs(m_zoom - newZoom) < 1e-3f) return;
    m_cameraAnimTimer.stop();
    m_zoom = newZoom;
    Q_EMIT zoomDistanceChanged(clamped);
    update();
}

void PCB3DView::setMeasureMode(bool enabled) {
    m_measureMode = enabled;
    if (!enabled) clearMeasurement();
    update();
}

void PCB3DView::clearMeasurement() {
    m_measureHasFirst = false;
    m_measureHasSecond = false;
    emit measurementUpdated(-1.0);
    update();
}

bool PCB3DView::setSpaceMouseEnabled(bool enabled) {
    m_spaceMouseEnabled = enabled;
    if (!enabled) {
        m_spaceMousePollTimer.stop();
        return true;
    }
    if (!m_spaceMouseConnected) {
        if (!m_spaceMouseLib.isLoaded()) {
#ifdef Q_OS_WIN
            m_spaceMouseLib.setFileName("siapp.dll");
            if (!m_spaceMouseLib.load()) {
                m_spaceMouseLib.setFileName("siapp");
                m_spaceMouseLib.load();
            }
#elif defined(Q_OS_MACOS)
            m_spaceMouseLib.setFileName("libspnav.dylib");
            if (!m_spaceMouseLib.load()) {
                m_spaceMouseLib.setFileName("libspnav");
                m_spaceMouseLib.load();
            }
#else
            m_spaceMouseLib.setFileName("libspnav.so.0");
            if (!m_spaceMouseLib.load()) {
                m_spaceMouseLib.setFileName("libspnav.so");
                m_spaceMouseLib.load();
            }
#endif
        }
        if (!m_spaceMouseLib.isLoaded()) return false;
        m_spnavOpen = reinterpret_cast<int (*)()>(m_spaceMouseLib.resolve("spnav_open"));
        m_spnavClose = reinterpret_cast<int (*)()>(m_spaceMouseLib.resolve("spnav_close"));
        m_spnavPollEvent = reinterpret_cast<int (*)(void*)>(m_spaceMouseLib.resolve("spnav_poll_event"));
        if (!m_spnavOpen || !m_spnavClose || !m_spnavPollEvent) return false;
        if (m_spnavOpen() == -1) return false;
        m_spaceMouseConnected = true;
    }
    m_spaceMousePollTimer.start();
    return true;
}

void PCB3DView::setSubstrateAlpha(float alpha) {
    m_substrateAlpha = std::clamp(alpha, 0.05f, 1.0f);
    update();
}

void PCB3DView::setSoldermaskAlpha(float alpha) {
    m_soldermaskAlpha = std::clamp(alpha, 0.05f, 1.0f);
    update();
}

void PCB3DView::resetCamera() {
    m_inertiaTimer.stop();
    m_velYaw = m_velPitch = 0.0f;
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
                 QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f);
    const QVector3D center = (m_boundsMin + m_boundsMax) * 0.5f;
    const QVector3D rc = m_rotation.rotatedVector(center);
    m_pan = QVector3D(-rc.x(), -rc.y(), 0.0f);
    m_zoom = -300.0f;
    m_cameraAnimTimer.stop();
    m_cameraAnimT = 1.0f;
    Q_EMIT zoomDistanceChanged(-m_zoom);
    update();
}

void PCB3DView::fitBoard() {
    const float dx = m_boundsMax.x() - m_boundsMin.x();
    const float dy = m_boundsMax.y() - m_boundsMin.y();
    if (dx <= 1e-3f && dy <= 1e-3f) {
        resetCamera();
        return;
    }
    const float half = std::max(dx, dy) * 0.5f + 12.0f;
    const float dist = std::clamp(half / 0.4142f * 1.15f, 40.0f, 2000.0f);
    const QVector3D center = (m_boundsMin + m_boundsMax) * 0.5f;
    const QVector3D rc = m_rotation.rotatedVector(center);
    const QVector3D targetPan(-rc.x(), -rc.y(), 0.0f);
    startCameraTransition(m_rotation, -dist, targetPan);
}

void PCB3DView::focusComponent(const QUuid& id) {
    for (const ComponentDraw& cd : m_componentDraws) {
        if (cd.id != id) continue;
        const QVector3D center = (cd.wbmin + cd.wbmax) * 0.5f;
        const QVector3D rc = m_rotation.rotatedVector(center);
        QVector3D pan = m_pan;
        pan.setX(-rc.x());
        pan.setY(-rc.y());
        const float extent = (cd.wbmax - cd.wbmin).length() * 0.5f + 20.0f;
        const float dist = std::clamp(extent / 0.4142f, 60.0f, 2000.0f);
        startCameraTransition(m_rotation, -dist, pan);
        return;
    }
}

void PCB3DView::setViewPreset(const QString& preset) {
    const QString p = preset.toLower();
    if (p != "spin cw" && p != "spin ccw") {
        m_spinTimer.stop();
        m_spinSpeedDeg = 0.0f;
    }
    if (p == "top") {
        startCameraTransition(QQuaternion(), -260.0f, m_pan);
    } else if (p == "bottom") {
        startCameraTransition(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 180.0f), -260.0f, m_pan);
    } else if (p == "front") {
        startCameraTransition(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -90.0f), -300.0f, m_pan);
    } else if (p == "back") {
        startCameraTransition(
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 90.0f) *
            QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 180.0f),
            -300.0f, m_pan);
    } else if (p == "flip board") {
        m_cameraAnimTimer.stop();
        const QQuaternion target =
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 180.0f) * m_rotation;
        startCameraTransition(target, m_zoom, m_pan);
    } else if (p == "spin cw") {
        m_cameraAnimTimer.stop();
        m_spinSpeedDeg = 0.7f;
        if (!m_spinTimer.isActive()) m_spinTimer.start();
    } else if (p == "spin ccw") {
        m_cameraAnimTimer.stop();
        m_spinSpeedDeg = -0.7f;
        if (!m_spinTimer.isActive()) m_spinTimer.start();
    } else if (p == "spin stop") {
        m_spinTimer.stop();
        m_spinSpeedDeg = 0.0f;
    } else {
        startCameraTransition(
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
            QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f),
            -300.0f, m_pan);
    }
}

void PCB3DView::initializeGL() {
    initializeOpenGLFunctions();

    const QSurfaceFormat f = format();
    m_glSupported = (f.majorVersion() > 3) || (f.majorVersion() == 3 && f.minorVersion() >= 3);
    if (!m_glSupported) {
        qWarning() << "PCB3DView: OpenGL 3.3 core required, got"
                   << f.majorVersion() << "." << f.minorVersion();
        return;
    }

    glClearColor(0.11f, 0.14f, 0.18f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initShaders();
    initPickShader();
    initShadowMap();

    m_staticVbo.create();
    m_staticVbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    checkGl("initializeGL");
}

void PCB3DView::initShadowMap() {
    m_shadowShader.removeAllShaders();
    if (!m_shadowShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uModel;
        uniform mat4 uShadowViewProj;
        void main() {
            gl_Position = uShadowViewProj * uModel * vec4(aPos, 1.0);
        }
    )")) {
        qWarning() << "PCB3DView: shadow vertex shader failed:" << m_shadowShader.log();
    }
    if (!m_shadowShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        void main() {
            // Depth is written automatically
        }
    )")) {
        qWarning() << "PCB3DView: shadow fragment shader failed:" << m_shadowShader.log();
    }
    if (!m_shadowShader.link()) qWarning() << "PCB3DView: shadow link failed:" << m_shadowShader.log();

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    m_shadowFbo = new QOpenGLFramebufferObject(1024, 1024, format);
    if (!m_shadowFbo->isValid()) {
        qWarning() << "PCB3DView: shadow FBO incomplete; shadows disabled";
        delete m_shadowFbo;
        m_shadowFbo = nullptr;
    }
}

QMatrix4x4 PCB3DView::currentShadowMatrix() const {
    const QVector3D lightPos(350.0f, -280.0f, 450.0f);
    const QVector3D target(0, 0, 0);
    QMatrix4x4 view;
    view.lookAt(lightPos, target, QVector3D(0, 0, 1));
    QMatrix4x4 proj;
    proj.ortho(-250, 250, -200, 200, 100, 1200);
    return proj * view;
}

void PCB3DView::resizeGL(int w, int h) {
    Q_UNUSED(w)
    Q_UNUSED(h)
    updateProjectionMatrix();
}

void PCB3DView::paintGL() {
    if (!m_glSupported) {
        QPainter p(this);
        p.fillRect(rect(), QColor(16, 21, 27));
        p.setPen(QColor(220, 230, 240));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("OpenGL 3.3 (core profile) is required for the 3D viewer.\n"
                                  "Please update your graphics drivers."));
        return;
    }

    // Flush any stale GL errors from Qt window/context state transitions
    while (glGetError() != GL_NO_ERROR);

    // Frame pacing / adaptive quality
    const float intervalMs = std::clamp(float(m_frameTimer.restart()) / 1e6f, 0.1f, 100.0f);
    m_frameEmaMs = m_frameEmaMs * 0.9f + intervalMs * 0.1f;
    if (m_frameEmaMs > 33.0f) m_quality = 1;
    else if (m_frameEmaMs < 20.0f) m_quality = 0;

    if (m_sceneDirty) rebuildSceneCache();
    updateProjectionMatrix();

    m_frameGraph.push_back(m_frameEmaMs);
    if (m_frameGraph.size() > 120) m_frameGraph.removeFirst();

    const QMatrix4x4 vp = m_projection * currentViewMatrix();
    m_frustum = extractFrustum(vp);

    const QMatrix4x4 shadowVP = currentShadowMatrix();
    const Frustum lightFrustum = extractFrustum(shadowVP);

    // Pass 1: shadow map (static VBO, no re-upload)
    if (m_shadowFbo && m_shadowFbo->isValid()) {
        m_shadowFbo->bind();
        glViewport(0, 0, 2048, 2048);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glCullFace(GL_FRONT);
        m_shadowShader.bind();
        m_shadowShader.setUniformValue("uShadowViewProj", shadowVP);
        if (!m_batches.isEmpty() && m_totalTriangles > 0 && m_staticVbo.bind()) {
            m_shadowShader.enableAttributeArray(0);
            m_shadowShader.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
            for (const GpuBatch& b : m_batches) {
                if (!b.castShadow || b.count <= 0 || !batchVisible(b)) continue;
                if (m_enableFrustumCulling && !lightFrustum.intersectsAabb(b.bmin, b.bmax)) continue;
                QMatrix4x4 model;
                if (b.componentIndex >= 0)
                    model = m_componentDraws[b.componentIndex].placement;
                m_shadowShader.setUniformValue("uModel", model);
                glDrawArrays(GL_TRIANGLES, b.first, b.count);
            }
            m_shadowShader.disableAttributeArray(0);
            m_staticVbo.release();
        }
        m_shadowShader.release();
        m_shadowFbo->release();
    }

    // Pass 2: main render
    glViewport(0, 0, std::max(1, int(width() * devicePixelRatioF())),
               std::max(1, int(height() * devicePixelRatioF())));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    const QMatrix4x4 view = currentViewMatrix();
    m_shader.bind();
    m_shader.setUniformValue("uView", view);
    m_shader.setUniformValue("uProj", m_projection);
    m_shader.setUniformValue("uShadowVP", shadowVP);
    m_shader.setUniformValue("uLightPos", QVector3D(260.0f, -180.0f, 520.0f));
    m_shader.setUniformValue("uCamPos", QVector3D(0.0f, 0.0f, -m_zoom));
    m_shader.setUniformValue("uEnhanced", m_enhancedLighting ? 1 : 0);
    m_shader.setUniformValue("uQuality", m_quality);

    const bool hasShadows = (m_shadowFbo && m_shadowFbo->isValid());
    m_shader.setUniformValue("uHasShadowMap", hasShadows ? 1 : 0);
    if (hasShadows) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_shadowFbo->texture());
        m_shader.setUniformValue("uShadowMap", 0);
    }

    if (!m_batches.isEmpty() && m_totalTriangles > 0 && m_staticVbo.bind()) {
        m_shader.enableAttributeArray(0);
        m_shader.enableAttributeArray(1);
        m_shader.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
        m_shader.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, nrm), 3, sizeof(Vertex));
        for (const GpuBatch& b : m_batches) {
            if (b.count <= 0 || !batchVisible(b)) continue;
            if (m_enableFrustumCulling && !m_frustum.intersectsAabb(b.bmin, b.bmax)) continue;
            QMatrix4x4 model;
            MaterialKind mat = b.material;
            float alpha = 1.0f;
            bool hovered = false;
            switch (b.layer) {
            case LayerFlag::Substrate: alpha = m_soldermaskAlpha; break;
            case LayerFlag::Dielectric: alpha = std::clamp(m_substrateAlpha * 0.85f, 0.2f, 0.95f); break;
            case LayerFlag::CopperInner: alpha = 0.95f; break;
            default: break;
            }
            if (b.componentIndex >= 0) {
                const ComponentDraw& cd = m_componentDraws[b.componentIndex];
                model = cd.placement;
                hovered = (cd.id == m_hoverId);
                if (m_collidedComponents.contains(cd.id)) {
                    mat = MaterialKind::Collision;
                    alpha = 1.0f;
                } else {
                    alpha = std::clamp(cd.alpha * m_componentAlpha, 0.0f, 1.0f);
                }
            }
            applyMaterial(mat, alpha, hovered);
            m_shader.setUniformValue("uModel", model);
            glDrawArrays(GL_TRIANGLES, b.first, b.count);
        }
        m_shader.disableAttributeArray(0);
        m_shader.disableAttributeArray(1);
        m_staticVbo.release();
    }
    m_shader.release();
    checkGl("paintGL");

    // Overlays (single QPainter pass)
    {
        QPainter p(this);
        drawVignetteOverlay(p);
        drawGridOverlay(p);
        drawAxisTriadOverlay(p);
        drawMeasurementOverlay(p);
        drawHoverOverlay(p);
        if (m_showStats) drawStatsOverlay(p);
    }
}

void PCB3DView::initShaders() {
    m_shader.removeAllShaders();
    if (!m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNrm;
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProj;
        uniform mat4 uShadowVP;
        out vec3 vWorldPos;
        out vec3 vNormal;
        out vec4 vShadowPos;
        void main() {
            vec4 wp = uModel * vec4(aPos, 1.0);
            vWorldPos = wp.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNrm;
            vShadowPos = uShadowVP * wp;
            gl_Position = uProj * uView * wp;
        }
    )")) {
        qWarning() << "PCB3DView: main vertex shader failed:" << m_shader.log();
    }
    if (!m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec3 vWorldPos;
        in vec3 vNormal;
        in vec4 vShadowPos;
        uniform vec3 uCamPos;
        uniform vec3 uLightPos;
        uniform vec3 uAlbedo;
        uniform float uMetallic;
        uniform float uRoughness;
        uniform float uSpecularStrength;
        uniform vec3 uEmissive;
        uniform float uAlpha;
        uniform int uEnhanced;
        uniform int uQuality;
        uniform int uHasShadowMap;
        uniform sampler2D uShadowMap;
        out vec4 FragColor;

        float calculateShadow() {
            if (uHasShadowMap == 0) return 0.0;
            vec3 projCoords = vShadowPos.xyz / vShadowPos.w;
            projCoords = projCoords * 0.5 + 0.5;
            if (projCoords.z > 1.0) return 0.0;
            float currentDepth = projCoords.z;
            float bias = max(0.005 * (1.0 - dot(normalize(vNormal), normalize(uLightPos - vWorldPos))), 0.0005);
            if (uQuality == 1) {
                float d = texture(uShadowMap, projCoords.xy).r;
                return currentDepth - bias > d ? 1.0 : 0.0;
            }
            float shadow = 0.0;
            vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    float d = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
                    shadow += currentDepth - bias > d ? 1.0 : 0.0;
                }
            }
            return shadow / 9.0;
        }

        vec3 environmentColor(vec3 dir) {
            vec3 d = normalize(dir);
            float up = clamp(d.z * 0.5 + 0.5, 0.0, 1.0);
            vec3 skyHorizon = vec3(0.42, 0.52, 0.62);
            vec3 skyZenith = vec3(0.13, 0.20, 0.32);
            vec3 ground = vec3(0.16, 0.13, 0.10);
            vec3 sky = mix(skyHorizon, skyZenith, smoothstep(0.45, 1.0, up));
            return mix(ground, sky, smoothstep(0.25, 0.75, up));
        }

        float approxSSAO(vec3 N) {
            vec3 n = normalize(N);
            float normalVar = clamp(length(fwidth(n)), 0.0, 1.0);
            float depthVar = clamp(length(vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z))) * 220.0, 0.0, 1.0);
            float cavity = clamp(normalVar * 0.85 + depthVar * 0.55, 0.0, 1.0);
            return 1.0 - cavity * 0.38;
        }

        void main() {
            vec3 N = normalize(vNormal);
            vec3 L = normalize(uLightPos - vWorldPos);
            vec3 V = normalize(uCamPos - vWorldPos);
            vec3 H = normalize(L + V);
            float ndl = max(dot(N, L), 0.0);
            float shininess = mix(128.0, 8.0, clamp(uRoughness, 0.0, 1.0));
            float specTerm = pow(max(dot(N, H), 0.0), shininess) * uSpecularStrength;
            vec3 F0 = mix(vec3(0.04), uAlbedo, uMetallic);
            vec3 specular = mix(vec3(specTerm), F0 * specTerm, uMetallic);
            float shadow = calculateShadow();
            float lit = 1.0 - shadow;
            float hemi = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
            vec3 ambient = (0.20 + 0.10 * hemi) * uAlbedo;
            vec3 diffuse = ndl * uAlbedo * (1.0 - 0.2 * uMetallic) * lit;
            vec3 color = ambient + diffuse + specular * lit + uEmissive;

            vec3 R = reflect(-V, N);
            vec3 env = environmentColor(R);
            float fresnelEnv = pow(1.0 - max(dot(N, V), 0.0), 4.0);
            color += env * mix(0.04, 0.36, uMetallic) * (0.35 + 0.65 * fresnelEnv) * lit;

            if (uQuality == 0) color *= approxSSAO(N);

            if (uEnhanced == 1) {
                vec3 L2 = normalize(vec3(-260.0, 180.0, 320.0) - vWorldPos);
                vec3 H2 = normalize(L2 + V);
                float ndl2 = max(dot(N, L2), 0.0);
                float spec2 = pow(max(dot(N, H2), 0.0), max(12.0, shininess * 0.65)) * (uSpecularStrength * 0.65);
                float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
                float horizon = smoothstep(-0.2, 0.8, N.z);
                vec3 bounce = 0.08 * uAlbedo * vec3(0.88, 0.92, 1.0) * (1.0 - horizon);
                vec3 skyBounce = environmentColor(N) * (0.10 + 0.12 * (1.0 - uRoughness));
                vec3 groundBounce = environmentColor(-N) * (0.06 + 0.06 * horizon);
                vec3 bentNormal = normalize(mix(N, R, 0.35));
                vec3 indirectProbe = environmentColor(bentNormal) * (0.07 + 0.10 * (1.0 - uRoughness));
                vec3 gi = (skyBounce + groundBounce + indirectProbe) * uAlbedo;
                vec3 reflDir = reflect(-V, N);
                vec3 reflCol = environmentColor(reflDir);
                float eta = 1.0 / 1.32;
                vec3 refrDir = refract(-V, N, eta);
                vec3 refrCol = environmentColor(refrDir);
                vec3 transmissionTint = mix(vec3(1.0), uAlbedo, 0.35);
                vec3 glassCol = refrCol * transmissionTint;
                vec3 rrMix = mix(glassCol, reflCol, clamp(fresnel * 1.15, 0.0, 1.0));
                color = color
                      + 0.35 * ndl2 * uAlbedo * lit
                      + spec2 * mix(vec3(0.55), F0, 0.7) * lit
                      + fresnel * (0.25 + 0.35 * uMetallic)
                      + bounce
                      + gi
                      + rrMix * (0.10 + 0.22 * (1.0 - uRoughness))
                      + uEmissive * 0.55;
                color = color / (color + vec3(1.0));
            }
            FragColor = vec4(color, uAlpha);
        }
    )")) {
        qWarning() << "PCB3DView: main fragment shader failed:" << m_shader.log();
    }
    if (!m_shader.link()) qWarning() << "PCB3DView: main link failed:" << m_shader.log();
}

void PCB3DView::initPickShader() {
    m_pickShader.removeAllShaders();
    if (!m_pickShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProj;
        void main() {
            gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
        }
    )")) {
        qWarning() << "PCB3DView: pick vertex shader failed:" << m_pickShader.log();
    }
    if (!m_pickShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        uniform vec3 uIdColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(uIdColor, 1.0);
        }
    )")) {
        qWarning() << "PCB3DView: pick fragment shader failed:" << m_pickShader.log();
    }
    if (!m_pickShader.link()) qWarning() << "PCB3DView: pick link failed:" << m_pickShader.log();
}

void PCB3DView::updateProjectionMatrix() {
    const float w = std::max(1, width());
    const float h = std::max(1, height());
    const float aspect = w / h;
    m_projection.setToIdentity();
    if (m_orthographic) {
        const float halfHeight = std::max(10.0f, std::abs(m_zoom) * 0.5f);
        const float halfWidth = halfHeight * aspect;
        m_projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -4000.0f, 4000.0f);
    } else {
        m_projection.perspective(45.0f, aspect, 0.1f, 4000.0f);
    }
}

void PCB3DView::applyMaterial(MaterialKind material, float alpha, bool hovered) {
    QVector3D emissive(0.0f, 0.0f, 0.0f);
    switch (material) {
    case MaterialKind::SolderMask:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_soldermaskColor));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.80f);
        m_shader.setUniformValue("uSpecularStrength", 0.18f);
        break;
    case MaterialKind::Dielectric:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.86f, 0.80f, 0.62f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.92f);
        m_shader.setUniformValue("uSpecularStrength", 0.06f);
        break;
    case MaterialKind::CopperTop:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_copperTopColor));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.18f);
        m_shader.setUniformValue("uSpecularStrength", 1.15f);
        break;
    case MaterialKind::CopperBottom:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_copperBottomColor));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.18f);
        m_shader.setUniformValue("uSpecularStrength", 1.15f);
        break;
    case MaterialKind::CopperInner:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.58f, 0.22f));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.22f);
        m_shader.setUniformValue("uSpecularStrength", 1.05f);
        break;
    case MaterialKind::Plating:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.95f, 0.68f, 0.28f));
        m_shader.setUniformValue("uMetallic", 0.98f);
        m_shader.setUniformValue("uRoughness", 0.15f);
        m_shader.setUniformValue("uSpecularStrength", 1.2f);
        break;
    case MaterialKind::Silkscreen:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.96f, 0.96f, 0.96f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.86f);
        m_shader.setUniformValue("uSpecularStrength", 0.05f);
        break;
    case MaterialKind::Plastic:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_componentColor));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.84f);
        m_shader.setUniformValue("uSpecularStrength", 0.16f);
        break;
    case MaterialKind::ComponentMetal:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.70f, 0.72f, 0.76f));
        m_shader.setUniformValue("uMetallic", 0.92f);
        m_shader.setUniformValue("uRoughness", 0.24f);
        m_shader.setUniformValue("uSpecularStrength", 1.10f);
        break;
    case MaterialKind::ComponentLED:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.94f, 0.96f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.22f);
        m_shader.setUniformValue("uSpecularStrength", 0.75f);
        emissive = QVector3D(0.22f, 0.26f, 0.10f);
        break;
    case MaterialKind::Collision:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.22f, 0.22f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.55f);
        m_shader.setUniformValue("uSpecularStrength", 0.20f);
        emissive = QVector3D(0.08f, 0.01f, 0.01f);
        break;
    case MaterialKind::AxisX:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.95f, 0.25f, 0.25f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    case MaterialKind::AxisY:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.25f, 0.9f, 0.35f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    case MaterialKind::AxisZ:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.3f, 0.55f, 0.95f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    }
    if (hovered) emissive += QVector3D(0.14f, 0.18f, 0.24f);
    m_shader.setUniformValue("uEmissive", emissive);
    m_shader.setUniformValue("uAlpha", alpha);
}

bool PCB3DView::batchVisible(const GpuBatch& b) const {
    switch (b.layer) {
    case LayerFlag::Substrate: return m_showSubstrate;
    case LayerFlag::Dielectric: return m_showSubstrate;
    case LayerFlag::CopperTop: return m_showCopperTop;
    case LayerFlag::CopperBottom: return m_showCopperBottom;
    case LayerFlag::CopperInner: return m_showCopperTop || m_showCopperBottom;
    case LayerFlag::Plating: return m_showCopperTop || m_showCopperBottom;
    case LayerFlag::Silkscreen: return m_showSilkscreen;
    case LayerFlag::Component: return m_showComponents;
    }
    return true;
}

void PCB3DView::rebuildSceneCache() {
    QElapsedTimer rt;
    rt.start();

    m_batches.clear();
    m_componentDraws.clear();
    m_pickProxies.clear();
    m_componentRefs.clear();
    m_pickGrid = PickGrid();
    m_boardTri = TriMesh2D();
    m_boardOuterWall.clear();
    m_boardHoleWalls.clear();
    if (m_meshBakeCache.size() > 400) m_meshBakeCache.clear();

    if (!m_scene) {
        m_staticVbo.bind();
        m_staticVbo.allocate(nullptr, 0);
        m_staticVbo.release();
        m_totalTriangles = 0;
        m_sceneDirty = false;
        return;
    }

    QVector<Vertex> verts;      // final GPU build buffer
    QVector<Vertex> copperTopV;
    QVector<Vertex> copperBottomV;
    QVector<Vertex> platingV;
    QVector<Vertex> silkV;
    QVector<Vertex> silkCompV;

    bool hasContentBounds = false;
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    auto includePoint = [&](float x, float y) {
        if (!hasContentBounds) {
            minX = maxX = x;
            minY = maxY = y;
            hasContentBounds = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };

    bool hasEdgeCuts = false;
    float ecMinX = 0.0f, ecMinY = 0.0f, ecMaxX = 0.0f, ecMaxY = 0.0f;
    QList<QPolygonF> edgeCutPolygons;
    QList<QPair<QPointF, QPointF>> edgeCutSegments;
    auto includeEdgePoint = [&](float x, float y) {
        if (!hasEdgeCuts) {
            ecMinX = ecMaxX = x;
            ecMinY = ecMaxY = y;
            hasEdgeCuts = true;
            return;
        }
        ecMinX = std::min(ecMinX, x);
        ecMinY = std::min(ecMinY, y);
        ecMaxX = std::max(ecMaxX, x);
        ecMaxY = std::max(ecMaxY, y);
    };

    // ---- Copper / pads / vias / silk images -------------------------------
    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(item);
        if (!pcb) continue;
        if (!passesSelectionFilter(pcb)) continue;

        if (pcb->layer() == PCBLayerManager::EdgeCuts) {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(pcb)) {
                const QPointF startScene = trace->mapToScene(trace->startPoint());
                const QPointF endScene = trace->mapToScene(trace->endPoint());
                includeEdgePoint(float(startScene.x()), float(-startScene.y()));
                includeEdgePoint(float(endScene.x()), float(-endScene.y()));
                edgeCutSegments.append({startScene, endScene});
            } else if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(pcb)) {
                QPolygonF poly;
                const QPolygonF src = pour->polygon();
                for (const QPointF& point : src) {
                    const QPointF scenePoint = pour->mapToScene(point);
                    poly << scenePoint;
                    includeEdgePoint(float(scenePoint.x()), float(-scenePoint.y()));
                }
                if (poly.size() >= 3) edgeCutPolygons.append(ensureClosedPolygon(poly));
            }
            continue;
        }

        if (!m_netFilter.isEmpty() && pcb->netName() != m_netFilter) continue;

        float cz = kCopperZTop;
        if (pcb->layer() == PCBLayerManager::TopCopper) {
            cz = kCopperZTop;
        } else if (pcb->layer() == PCBLayerManager::BottomCopper) {
            cz = kCopperZBottom - kCopperThickness;
        } else if (pcb->layer() >= 100) {
            const int internalIdx = pcb->layer() - 100;
            const float factor = (internalIdx == 1) ? 0.34f : -0.34f;
            cz = (kBoardThickness * 0.5f) * factor;
        }
        const float faceZ = (pcb->layer() == PCBLayerManager::BottomCopper) ? cz : (cz + kCopperThickness);

        if (TraceItem* trace = dynamic_cast<TraceItem*>(pcb)) {
            QPointF s2 = trace->mapToScene(trace->startPoint());
            QPointF e2 = trace->mapToScene(trace->endPoint());
            QVector2D d(float(e2.x() - s2.x()), float(-(e2.y() - s2.y())));
            if (d.lengthSquared() < 1e-9f) continue;
            includePoint(float(s2.x()), float(-s2.y()));
            includePoint(float(e2.x()), float(-e2.y()));
            if (faceZ > 0) appendCopperTrace(copperTopV, s2, e2, float(trace->width()), faceZ, kCopperZTop, true);
            else appendCopperTrace(copperBottomV, s2, e2, float(trace->width()), faceZ, kCopperZBottom, false);
            continue;
        }

        if (PadItem* pad = dynamic_cast<PadItem*>(pcb)) {
            QRectF r = pad->boundingRect();
            QPointF c1 = pad->mapToScene(r.topLeft());
            QPointF c2 = pad->mapToScene(r.topRight());
            QPointF c3 = pad->mapToScene(r.bottomRight());
            QPointF c4 = pad->mapToScene(r.bottomLeft());
            QVector3D a(float(c1.x()), float(-c1.y()), faceZ);
            QVector3D b(float(c2.x()), float(-c2.y()), faceZ);
            QVector3D c(float(c3.x()), float(-c3.y()), faceZ);
            QVector3D d(float(c4.x()), float(-c4.y()), faceZ);
            includePoint(a.x(), a.y());
            includePoint(c.x(), c.y());
            if (faceZ > 0) appendQuad(copperTopV, a, d, c, b, {0, 0, 1.0f});
            else appendQuad(copperBottomV, a, b, c, d, {0, 0, -1.0f});
            const float drill = float(pad->drillSize());
            if (drill > 0.01f) {
                const QPointF sc = pad->mapToScene(0, 0);
                const float px = float(sc.x());
                const float py = float(-sc.y());
                const float rIn = std::max(0.01f, drill * 0.5f);
                const float rOut = std::max(rIn + 0.015f, std::min(float(r.width()), float(r.height())) * 0.5f);
                appendRing(copperTopV, px, py, rIn, rOut, kCopperZTop + kCopperThickness, true);
                appendRing(copperBottomV, px, py, rIn, rOut, kCopperZBottom - kCopperThickness, false);
                appendCylinder(platingV, px, py, rIn + 0.008f, kCopperZTop + kCopperThickness, kCopperZBottom - kCopperThickness);
            }
            continue;
        }

        if (ViaItem* via = dynamic_cast<ViaItem*>(pcb)) {
            const QPointF sp = via->scenePos();
            const float px = float(sp.x());
            const float py = float(-sp.y());
            const float outer = std::max(0.02f, float(via->diameter()) * 0.5f);
            const float drill = std::max(0.01f, float(via->drillSize()) * 0.5f);
            appendRing(copperTopV, px, py, drill, outer, kCopperZTop + kCopperThickness, true);
            appendRing(copperBottomV, px, py, drill, outer, kCopperZBottom - kCopperThickness, false);
            appendCylinder(platingV, px, py, drill + 0.008f, kCopperZTop + kCopperThickness, kCopperZBottom - kCopperThickness);
            includePoint(px - outer, py - outer);
            includePoint(px + outer, py + outer);
            continue;
        }

        if (PCBImageItem* image = dynamic_cast<PCBImageItem*>(pcb)) {
            const QImage& stencil = image->fabPreview();
            if (stencil.isNull()) continue;
            const QRectF localRect = image->boundingRect();
            const float silkZ = (pcb->layer() == PCBLayerManager::BottomCopper)
                ? (kCopperZBottom - kCopperThickness - 0.01f)
                : (kCopperZTop + kCopperThickness + 0.01f);
            const QVector3D silkNormal = (pcb->layer() == PCBLayerManager::BottomCopper)
                ? QVector3D(0, 0, -1) : QVector3D(0, 0, 1);
            const int w = stencil.width();
            const int h = stencil.height();
            if (w <= 0 || h <= 0) continue;
            for (int y = 0; y < h; ++y) {
                int x = 0;
                while (x < w) {
                    while (x < w && QColor::fromRgba(stencil.pixel(x, y)).alpha() < 10) ++x;
                    if (x >= w) break;
                    const int runStart = x;
                    while (x < w && QColor::fromRgba(stencil.pixel(x, y)).alpha() >= 10) ++x;
                    const int runEnd = x;
                    const qreal x0 = localRect.left() + (qreal(runStart) / qreal(w)) * localRect.width();
                    const qreal x1 = localRect.left() + (qreal(runEnd) / qreal(w)) * localRect.width();
                    const qreal y0 = localRect.top() + (qreal(y) / qreal(h)) * localRect.height();
                    const qreal y1 = localRect.top() + (qreal(y + 1) / qreal(h)) * localRect.height();
                    const QPointF tl = image->mapToScene(QPointF(x0, y0));
                    const QPointF tr = image->mapToScene(QPointF(x1, y0));
                    const QPointF br = image->mapToScene(QPointF(x1, y1));
                    const QPointF bl = image->mapToScene(QPointF(x0, y1));
                    appendMappedQuad(silkV, tl, tr, br, bl, silkZ, silkNormal);
                    includePoint(float(tl.x()), float(-tl.y()));
                    includePoint(float(br.x()), float(-br.y()));
                }
            }
            continue;
        }
    }

    const QList<QPolygonF> traceEdgeCutPolygons = buildClosedPolygonsFromSegments(edgeCutSegments);
    for (const QPolygonF& poly : traceEdgeCutPolygons)
        edgeCutPolygons.append(ensureClosedPolygon(poly));

    // ---- Components (shared baked meshes + placement matrices) ------------
    float compZmin = 1e9f, compZmax = -1e9f;
    for (QGraphicsItem* item : m_scene->items()) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp) continue;
        if (!passesSelectionFilter(comp)) continue;

        QRectF r = comp->boundingRect();
        const float hx = float(r.width() * 0.5);
        const float hy = float(r.height() * 0.5);
        const float hz = float((comp->height() > 0.0) ? comp->height() : 2.0);
        const QPointF cp = comp->scenePos();
        const bool isBottom = (comp->layer() == PCBLayerManager::BottomCopper);
        const float compZ = isBottom ? (kCopperZBottom - kCopperThickness - 0.02f - m_explodeAmount)
                                     : (kCopperZTop + kCopperThickness + 0.02f + m_explodeAmount);

        m_componentRefs.insert(comp->id(), comp->name());
        const QString typeUpper = comp->componentType().toUpper();
        const QString nameUpper = comp->name().toUpper();
        const bool isLed = typeUpper.contains("LED") || nameUpper.startsWith("D");
        const bool isMetal = typeUpper.contains("CONN") || typeUpper.contains("USB") ||
                             typeUpper.contains("SHIELD") || typeUpper.contains("SMA") ||
                             typeUpper.contains("ANT") || typeUpper.contains("COAX") ||
                             typeUpper.contains("BAT");
        const MaterialKind compMat = isLed ? MaterialKind::ComponentLED
                                           : (isMetal ? MaterialKind::ComponentMetal : MaterialKind::Plastic);

        auto makePlacement = [&](const QVector3D& off, const QVector3D& rot) {
            QMatrix4x4 m;
            m.translate(float(cp.x()), float(-cp.y()), compZ);
            m.rotate(float(comp->rotation()), 0, 0, 1);
            if (isBottom) m.rotate(180.0f, 0, 1, 0);
            m.translate(off.x(), -off.y(), off.z());
            m.rotate(rot.z(), 0, 0, 1);
            m.rotate(rot.y(), 0, 1, 0);
            m.rotate(rot.x(), 1, 0, 0);
            return m;
        };

        struct LocalPart {
            QString key;
            QMatrix4x4 placement;
            float alpha;
        };
        QVector<LocalPart> parts;

        if (m_renderMode == RenderMode::Full) {
            if (!comp->modelPath().isEmpty()) {
                const QVector3D vs = comp->modelScale3D();
                const QVector3D scale(float(comp->modelScale()) * vs.x(),
                                      float(comp->modelScale()) * vs.y(),
                                      float(comp->modelScale()) * vs.z());
                const QVector3D mrot = comp->modelRotation();
                const QString key = QStringLiteral("p|%1|%2|%3|%4|%5|%6|%7")
                    .arg(comp->modelPath())
                    .arg(scale.x(), 0, 'g', 5).arg(scale.y(), 0, 'g', 5).arg(scale.z(), 0, 'g', 5)
                    .arg(mrot.x(), 0, 'g', 5).arg(mrot.y(), 0, 'g', 5).arg(mrot.z(), 0, 'g', 5);
                bakeMesh(key, comp->modelPath(), scale, mrot);
                LocalPart p;
                p.key = key;
                p.placement = makePlacement(comp->modelOffset(), mrot);
                p.alpha = 1.0f;
                parts.push_back(p);
            } else if (FootprintLibraryManager::instance().hasFootprint(comp->componentType())) {
                const FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(comp->componentType());
                QList<Footprint3DModel> models = def.models3D();
                if (models.isEmpty() && !def.model3D().filename.trimmed().isEmpty())
                    models.append(def.model3D());
                int mi = 0;
                for (const Footprint3DModel& m3 : models) {
                    ++mi;
                    if (!m3.visible || m3.filename.trimmed().isEmpty()) continue;
                    const QString key = QStringLiteral("f|%1|%2|%3|%4|%5|%6|%7|%8")
                        .arg(m3.filename).arg(mi)
                        .arg(m3.scale.x(), 0, 'g', 5).arg(m3.scale.y(), 0, 'g', 5).arg(m3.scale.z(), 0, 'g', 5)
                        .arg(m3.rotation.x(), 0, 'g', 5).arg(m3.rotation.y(), 0, 'g', 5).arg(m3.rotation.z(), 0, 'g', 5);
                    bakeMesh(key, m3.filename, m3.scale, m3.rotation);
                    LocalPart p;
                    p.key = key;
                    p.placement = makePlacement(m3.offset, m3.rotation);
                    p.alpha = m3.opacity;
                    parts.push_back(p);
                }
            }
        }

        if (parts.isEmpty()) {
            const QString boxKey = QStringLiteral("box|%1|%2|%3")
                .arg(hx, 0, 'g', 5).arg(hy, 0, 'g', 5).arg(hz, 0, 'g', 5);
            if (!m_meshBakeCache.contains(boxKey)) {
                MeshBake bake;
                bake.vertices = makeBoxVertices(hx, hy, hz);
                bake.bmin = QVector3D(-hx, -hy, 0.0f);
                bake.bmax = QVector3D(hx, hy, hz);
                m_meshBakeCache.insert(boxKey, bake);
            }
            LocalPart p;
            p.key = boxKey;
            p.placement = makePlacement(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
            p.alpha = 1.0f;
            parts.push_back(p);
        }

        QVector3D wbmin(1e9f, 1e9f, 1e9f), wbmax(-1e9f, -1e9f, -1e9f);
        for (const LocalPart& p : parts) {
            auto pit = m_meshBakeCache.constFind(p.key);
            if (pit == m_meshBakeCache.constEnd() || pit.value().vertices.isEmpty()) continue;
            const MeshBake& bake = pit.value();

            ComponentDraw cd;
            cd.id = comp->id();
            cd.material = compMat;
            cd.alpha = std::clamp(p.alpha, 0.05f, 1.0f);
            cd.placement = p.placement;
            cd.batchIndex = int(m_batches.size());
            transformAabb(p.placement, bake.bmin, bake.bmax, cd.wbmin, cd.wbmax);

            GpuBatch b;
            b.first = int(verts.size());
            b.count = int(bake.vertices.size());
            b.layer = LayerFlag::Component;
            b.material = compMat;
            b.castShadow = true;
            b.componentIndex = int(m_componentDraws.size());
            verts.append(bake.vertices);
            b.bmin = cd.wbmin;
            b.bmax = cd.wbmax;
            m_batches.push_back(b);
            m_componentDraws.push_back(cd);

            wbmin.setX(std::min(wbmin.x(), cd.wbmin.x()));
            wbmin.setY(std::min(wbmin.y(), cd.wbmin.y()));
            wbmin.setZ(std::min(wbmin.z(), cd.wbmin.z()));
            wbmax.setX(std::max(wbmax.x(), cd.wbmax.x()));
            wbmax.setY(std::max(wbmax.y(), cd.wbmax.y()));
            wbmax.setZ(std::max(wbmax.z(), cd.wbmax.z()));
        }
        if (wbmin.x() < 1e8f) {
            m_pickProxies.push_back({comp->id(), wbmin, wbmax});
            compZmin = std::min(compZmin, wbmin.z());
            compZmax = std::max(compZmax, wbmax.z());
            includePoint(wbmin.x(), wbmin.y());
            includePoint(wbmax.x(), wbmax.y());
        }

        // High-resolution silkscreen from footprint graphics child items
        const float silkZ = isBottom ? (kCopperZBottom - kCopperThickness - 0.01f)
                                     : (kCopperZTop + kCopperThickness + 0.01f);
        const QVector3D silkNormal = isBottom ? QVector3D(0, 0, -1) : QVector3D(0, 0, 1);
        for (QGraphicsItem* child : comp->childItems()) {
            if (!child || !child->isVisible()) continue;
            auto addLine = [&](const QPointF& la, const QPointF& lb, float lw) {
                const QPointF a2 = comp->mapToScene(child->mapToParent(la));
                const QPointF b2 = comp->mapToScene(child->mapToParent(lb));
                appendThickSegment(silkCompV, a2, b2, lw, silkZ, silkNormal);
                includePoint(float(a2.x()), float(-a2.y()));
                includePoint(float(b2.x()), float(-b2.y()));
            };
            if (auto* li = dynamic_cast<QGraphicsLineItem*>(child)) {
                const QLineF ln = li->line();
                addLine(ln.p1(), ln.p2(), std::max(0.08f, float(li->pen().widthF())));
                continue;
            }
            if (auto* ri = dynamic_cast<QGraphicsRectItem*>(child)) {
                const QRectF rr = ri->rect();
                const float lw = std::max(0.08f, float(ri->pen().widthF()));
                addLine(rr.topLeft(), rr.topRight(), lw);
                addLine(rr.topRight(), rr.bottomRight(), lw);
                addLine(rr.bottomRight(), rr.bottomLeft(), lw);
                addLine(rr.bottomLeft(), rr.topLeft(), lw);
                continue;
            }
            if (auto* ei = dynamic_cast<QGraphicsEllipseItem*>(child)) {
                const QRectF rr = ei->rect();
                const float lw = std::max(0.08f, float(ei->pen().widthF()));
                const int seg = 72;
                QPointF prev(rr.center().x() + rr.width() * 0.5, rr.center().y());
                for (int s = 1; s <= seg; ++s) {
                    const float a = float(2.0 * M_PI * s / seg);
                    const QPointF cur(rr.center().x() + std::cos(a) * rr.width() * 0.5,
                                      rr.center().y() + std::sin(a) * rr.height() * 0.5);
                    addLine(prev, cur, lw);
                    prev = cur;
                }
                continue;
            }
            if (auto* pi = dynamic_cast<QGraphicsPathItem*>(child)) {
                const QList<QPolygonF> polys = pi->path().toSubpathPolygons();
                const float lw = std::max(0.08f, float(pi->pen().widthF()));
                for (const QPolygonF& poly : polys) {
                    if (poly.size() < 2) continue;
                    for (int i = 1; i < poly.size(); ++i) addLine(poly[i - 1], poly[i], lw);
                }
                continue;
            }
            if (auto* gi = dynamic_cast<QGraphicsPolygonItem*>(child)) {
                const QPolygonF poly = gi->polygon();
                if (poly.size() < 2) continue;
                const float lw = std::max(0.08f, float(gi->pen().widthF()));
                for (int i = 1; i < poly.size(); ++i) addLine(poly[i - 1], poly[i], lw);
                addLine(poly.back(), poly.front(), lw);
                continue;
            }
        }
    }

    // ---- Board outline / stackup -------------------------------------------
    if (hasEdgeCuts) {
        minX = ecMinX; maxX = ecMaxX;
        minY = ecMinY; maxY = ecMaxY;
    } else if (hasContentBounds && minX < maxX && minY < maxY) {
        // Bounds already computed via includePoint()
    } else {
        QRectF br = m_scene->itemsBoundingRect();
        if (br.isEmpty()) br = QRectF(-50, -50, 100, 100);
        minX = float(br.left());
        maxX = float(br.right());
        minY = float(-br.bottom());
        maxY = float(-br.top());
    }
    if (minX >= maxX) { minX = -50.0f; maxX = 50.0f; }
    if (minY >= maxY) { minY = -50.0f; maxY = 50.0f; }
    const float margin = 1.0f;
    const float x1 = minX - margin;
    const float y1 = minY - margin;
    const float x2 = maxX + margin;
    const float y2 = maxY + margin;
    const float z = kBoardThickness * 0.5f;

    QPolygonF outerEdgeCutOutline;
    QList<QPolygonF> innerEdgeCutCutouts;
    qreal outerEdgeCutArea = 0.0;
    for (const QPolygonF& poly : edgeCutPolygons) {
        const qreal area = std::abs(polygonSignedArea(poly));
        if (area > outerEdgeCutArea) {
            if (!outerEdgeCutOutline.isEmpty()) innerEdgeCutCutouts.append(outerEdgeCutOutline);
            outerEdgeCutOutline = poly;
            outerEdgeCutArea = area;
        } else {
            innerEdgeCutCutouts.append(poly);
        }
    }
    const bool hasPolygonOutline = outerEdgeCutOutline.size() >= 4;

    auto flushLayer = [&](QVector<Vertex>& src, LayerFlag layer, MaterialKind mat, bool shadow) {
        if (src.isEmpty()) return;
        GpuBatch b;
        b.first = int(verts.size());
        b.count = int(src.size());
        b.layer = layer;
        b.material = mat;
        b.castShadow = shadow;
        verts.append(src);
        m_batches.push_back(b);
        computeBatchAabb(verts, m_batches.last().first, m_batches.last().count,
                         m_batches.last().bmin, m_batches.last().bmax);
    };

    flushLayer(copperTopV, LayerFlag::CopperTop, MaterialKind::CopperTop, true);
    flushLayer(copperBottomV, LayerFlag::CopperBottom, MaterialKind::CopperBottom, true);
    flushLayer(platingV, LayerFlag::Plating, MaterialKind::Plating, false);

    // Substrate
    {
        GpuBatch b;
        b.first = int(verts.size());
        b.layer = LayerFlag::Substrate;
        b.material = MaterialKind::SolderMask;
        b.castShadow = true;
        if (hasPolygonOutline) {
            QPolygonF openPoly = outerEdgeCutOutline;
            if (openPoly.first() == openPoly.last()) openPoly.removeLast();
            m_boardTri = triangulateBoard(openPoly, innerEdgeCutCutouts);
            m_boardOuterWall = openPoly;
            m_boardOuterWall << openPoly.first();
            emitTriMeshFace(verts, m_boardTri, z, true);
            emitTriMeshFace(verts, m_boardTri, -z, false);
            appendPolygonWalls(verts, m_boardOuterWall, z, -z);
            for (QPolygonF cutout : innerEdgeCutCutouts) {
                if (cutout.first() == cutout.last()) cutout.removeLast();
                if (cutout.size() < 3) continue;
                if (polygonSignedArea(cutout) > 0.0) std::reverse(cutout.begin(), cutout.end());
                QPolygonF closed = cutout;
                closed << cutout.first();
                m_boardHoleWalls.append(closed);
                appendPolygonWalls(verts, closed, z, -z);
            }
        } else {
            appendQuad(verts, {x1, y1, z}, {x2, y1, z}, {x2, y2, z}, {x1, y2, z}, {0, 0, 1});
            appendQuad(verts, {x1, y2, -z}, {x2, y2, -z}, {x2, y1, -z}, {x1, y1, -z}, {0, 0, -1});
            appendQuad(verts, {x1, y1, -z}, {x2, y1, -z}, {x2, y1, z}, {x1, y1, z}, {0, 1, 0});
            appendQuad(verts, {x2, y1, -z}, {x2, y2, -z}, {x2, y2, z}, {x2, y1, z}, {1, 0, 0});
            appendQuad(verts, {x2, y2, -z}, {x1, y2, -z}, {x1, y2, z}, {x2, y2, z}, {0, -1, 0});
            appendQuad(verts, {x1, y2, -z}, {x1, y1, -z}, {x1, y1, z}, {x1, y2, z}, {-1, 0, 0});
        }
        b.count = int(verts.size()) - b.first;
        m_batches.push_back(b);
        computeBatchAabb(verts, m_batches.last().first, m_batches.last().count,
                         m_batches.last().bmin, m_batches.last().bmax);
    }

    // Dielectric slabs
    {
        GpuBatch b;
        b.first = int(verts.size());
        b.layer = LayerFlag::Dielectric;
        b.material = MaterialKind::Dielectric;
        b.castShadow = false;
        const float innerZ1 = z * 0.34f;
        const float innerZ2 = -z * 0.34f;
        const float slabTops[3] = {z * 0.78f, innerZ1 - kCopperThickness * 0.4f, innerZ2 - kCopperThickness * 0.4f};
        const float slabBots[3] = {innerZ1 + kCopperThickness * 0.4f, innerZ2 + kCopperThickness * 0.4f, -z * 0.78f};
        for (int s = 0; s < 3; ++s) {
            const float za = slabTops[s];
            const float zb = slabBots[s];
            if (za <= zb + 1e-6f) continue;
            if (hasPolygonOutline) {
                emitTriMeshFace(verts, m_boardTri, za, true);
                emitTriMeshFace(verts, m_boardTri, zb, false);
                appendPolygonWalls(verts, m_boardOuterWall, za, zb);
                for (const QPolygonF& closed : m_boardHoleWalls)
                    appendPolygonWalls(verts, closed, za, zb);
            } else {
                appendQuad(verts, {x1, y1, za}, {x2, y1, za}, {x2, y2, za}, {x1, y2, za}, {0, 0, 1});
                appendQuad(verts, {x1, y2, zb}, {x2, y2, zb}, {x2, y1, zb}, {x1, y1, zb}, {0, 0, -1});
                appendQuad(verts, {x1, y1, zb}, {x2, y1, zb}, {x2, y1, za}, {x1, y1, za}, {0, 1, 0});
                appendQuad(verts, {x2, y1, zb}, {x2, y2, zb}, {x2, y2, za}, {x2, y1, za}, {1, 0, 0});
                appendQuad(verts, {x2, y2, zb}, {x1, y2, zb}, {x1, y2, za}, {x2, y2, za}, {0, -1, 0});
                appendQuad(verts, {x1, y2, zb}, {x1, y1, zb}, {x1, y1, za}, {x1, y2, za}, {-1, 0, 0});
            }
        }
        b.count = int(verts.size()) - b.first;
        if (b.count > 0) {
            m_batches.push_back(b);
            computeBatchAabb(verts, m_batches.last().first, m_batches.last().count,
                             m_batches.last().bmin, m_batches.last().bmax);
        }
    }

    // Internal copper planes
    {
        GpuBatch b;
        b.first = int(verts.size());
        b.layer = LayerFlag::CopperInner;
        b.material = MaterialKind::CopperInner;
        b.castShadow = false;
        const float innerZ1 = z * 0.34f;
        const float innerZ2 = -z * 0.34f;
        if (hasPolygonOutline) {
            emitTriMeshFace(verts, m_boardTri, innerZ1, true);
            emitTriMeshFace(verts, m_boardTri, innerZ2, false);
        } else {
            appendQuad(verts, {x1, y1, innerZ1}, {x2, y1, innerZ1}, {x2, y2, innerZ1}, {x1, y2, innerZ1}, {0, 0, 1});
            appendQuad(verts, {x1, y2, innerZ2}, {x2, y2, innerZ2}, {x2, y1, innerZ2}, {x1, y1, innerZ2}, {0, 0, -1});
        }
        b.count = int(verts.size()) - b.first;
        if (b.count > 0) {
            m_batches.push_back(b);
            computeBatchAabb(verts, m_batches.last().first, m_batches.last().count,
                             m_batches.last().bmin, m_batches.last().bmax);
        }
    }

    // Silkscreen (images + component graphics)
    silkV.append(silkCompV);
    flushLayer(silkV, LayerFlag::Silkscreen, MaterialKind::Silkscreen, false);

    // Draw order: board first, components last (stable within groups)
    auto layerPriority = [](LayerFlag l) {
        switch (l) {
        case LayerFlag::Substrate: return 0;
        case LayerFlag::Dielectric: return 1;
        case LayerFlag::CopperInner: return 2;
        case LayerFlag::CopperTop: return 3;
        case LayerFlag::CopperBottom: return 4;
        case LayerFlag::Plating: return 5;
        case LayerFlag::Silkscreen: return 6;
        case LayerFlag::Component: return 7;
        }
        return 8;
    };
    std::stable_sort(m_batches.begin(), m_batches.end(),
                     [&](const GpuBatch& a, const GpuBatch& b) {
                         return layerPriority(a.layer) < layerPriority(b.layer);
                     });

    // Single upload per rebuild (no per-frame re-upload anymore)
    m_staticVbo.bind();
    if (verts.isEmpty()) m_staticVbo.allocate(nullptr, 0);
    else m_staticVbo.allocate(verts.constData(), int(verts.size() * sizeof(Vertex)));
    m_staticVbo.release();

    m_totalTriangles = int(verts.size()) / 3;

    m_boundsMin = QVector3D(minX, minY, -z);
    m_boundsMax = QVector3D(maxX, maxY, z);
    if (compZmin < 1e8f) {
        m_boundsMin.setZ(std::min(m_boundsMin.z(), compZmin));
        m_boundsMax.setZ(std::max(m_boundsMax.z(), compZmax));
    }

    // Prune stale collision marks
    if (!m_collidedComponents.isEmpty()) {
        QSet<QUuid> live;
        for (const PickProxy& p : m_pickProxies) live.insert(p.id);
        QSet<QUuid> pruned;
        for (const QUuid& id : m_collidedComponents)
            if (live.contains(id)) pruned.insert(id);
        m_collidedComponents = pruned;
    }

    rebuildPickGrid();

    m_sceneDirty = false;
    m_rebuildMs = float(rt.elapsed());
    Q_EMIT statusUpdated(QStringLiteral("3D scene rebuilt in %1 ms · %2 triangles")
                             .arg(m_rebuildMs, 0, 'f', 1)
                             .arg(m_totalTriangles));
}

int PCB3DView::detectComponentCollisions() {
    if (m_sceneDirty) rebuildSceneCache();
    m_collidedComponents.clear();
    const int n = m_pickProxies.size();
    if (n < 2) {
        update();
        return 0;
    }
    // Sweep-and-prune along X
    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return m_pickProxies[a].bmin.x() < m_pickProxies[b].bmin.x();
    });
    constexpr float eps = 0.02f;
    int pairs = 0;
    for (int i = 0; i < n; ++i) {
        const PickProxy& A = m_pickProxies[order[i]];
        for (int j = i + 1; j < n; ++j) {
            const PickProxy& B = m_pickProxies[order[j]];
            if (B.bmin.x() > A.bmax.x() + eps) break;
            if (A.bmax.y() < B.bmin.y() + eps || B.bmax.y() < A.bmin.y() + eps) continue;
            if (A.bmax.z() < B.bmin.z() + eps || B.bmax.z() < A.bmin.z() + eps) continue;
            m_collidedComponents.insert(A.id);
            m_collidedComponents.insert(B.id);
            ++pairs;
        }
    }
    update();
    return pairs;
}

const PCB3DView::MeshBake& PCB3DView::bakeMesh(const QString& key, const QString& path,
                                               const QVector3D& scale, const QVector3D& rotDeg) {
    auto it = m_meshBakeCache.constFind(key);
    if (it != m_meshBakeCache.constEnd()) return it.value();

    MeshBake bake;
    bake.bmin = QVector3D(0, 0, 0);
    bake.bmax = QVector3D(0, 0, 0);
    const ObjMesh mesh = loadOBJ(path);
    if (!mesh.vertices.isEmpty()) {
        QMatrix4x4 rot;
        rot.rotate(rotDeg.z(), 0, 0, 1);
        rot.rotate(rotDeg.y(), 0, 1, 0);
        rot.rotate(rotDeg.x(), 1, 0, 0);
        const float sx = std::max(1e-4f, scale.x());
        const float sy = std::max(1e-4f, scale.y());
        const float sz = std::max(1e-4f, scale.z());
        bake.vertices.reserve(mesh.vertices.size());
        bake.bmin = QVector3D(1e9f, 1e9f, 1e9f);
        bake.bmax = QVector3D(-1e9f, -1e9f, -1e9f);
        for (const Vertex& v : mesh.vertices) {
            if (!isFinite3(v.pos)) continue;
            Vertex t;
            t.pos = QVector3D(v.pos.x() * sx, v.pos.y() * sy, v.pos.z() * sz);
            QVector3D nrm(v.nrm.x() / sx, v.nrm.y() / sy, v.nrm.z() / sz);
            nrm = rot.mapVector(nrm);
            if (nrm.lengthSquared() < 1e-12f) nrm = QVector3D(0, 0, 1);
            t.nrm = nrm.normalized();
            bake.vertices.push_back(t);
            bake.bmin.setX(std::min(bake.bmin.x(), t.pos.x()));
            bake.bmin.setY(std::min(bake.bmin.y(), t.pos.y()));
            bake.bmin.setZ(std::min(bake.bmin.z(), t.pos.z()));
            bake.bmax.setX(std::max(bake.bmax.x(), t.pos.x()));
            bake.bmax.setY(std::max(bake.bmax.y(), t.pos.y()));
            bake.bmax.setZ(std::max(bake.bmax.z(), t.pos.z()));
        }
    }
    it = m_meshBakeCache.insert(key, bake);
    return it.value();
}

void PCB3DView::setFrustumCullingEnabled(bool enabled) {
    m_enableFrustumCulling = enabled;
    update();
}

void PCB3DView::setHoverInfoEnabled(bool enabled) {
    m_showHoverInfo = enabled;
    update();
}

void PCB3DView::computeBatchAabb(const QVector<Vertex>& verts,
                                 int first, int count,
                                 QVector3D& bmin, QVector3D& bmax) {
    bmin = QVector3D(1e9f, 1e9f, 1e9f);
    bmax = QVector3D(-1e9f, -1e9f, -1e9f);
    if (count <= 0 || first < 0 || first + count > verts.size()) {
        bmin = QVector3D(0.0f, 0.0f, 0.0f);
        bmax = QVector3D(0.0f, 0.0f, 0.0f);
        return;
    }
    for (int i = first; i < first + count; ++i) {
        const QVector3D& p = verts[i].pos;
        bmin.setX(std::min(bmin.x(), p.x()));
        bmin.setY(std::min(bmin.y(), p.y()));
        bmin.setZ(std::min(bmin.z(), p.z()));
        bmax.setX(std::max(bmax.x(), p.x()));
        bmax.setY(std::max(bmax.y(), p.y()));
        bmax.setZ(std::max(bmax.z(), p.z()));
    }
}

PCB3DView::Frustum PCB3DView::extractFrustum(const QMatrix4x4& vp) const {
    Frustum f;
    auto row = [&](int r) { return QVector4D(vp(r, 0), vp(r, 1), vp(r, 2), vp(r, 3)); };
    const QVector4D r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto setPlane = [&](int index, const QVector4D& plane) {
        f.n[index] = QVector3D(plane.x(), plane.y(), plane.z());
        f.d[index] = plane.w();
        const float len = f.n[index].length();
        if (len > 1e-8f) { f.n[index] /= len; f.d[index] /= len; }
    };
    setPlane(0, r3 + r0); setPlane(1, r3 - r0);
    setPlane(2, r3 + r1); setPlane(3, r3 - r1);
    setPlane(4, r3 + r2); setPlane(5, r3 - r2);
    return f;
}

void PCB3DView::rebuildPickGrid() {
    m_pickGrid = PickGrid();
    const int n = int(m_pickProxies.size());
    if (n == 0) return;
    QVector3D bmin(1e9f, 1e9f, 1e9f), bmax(-1e9f, -1e9f, -1e9f);
    for (const PickProxy& p : m_pickProxies) {
        bmin.setX(std::min(bmin.x(), p.bmin.x()));
        bmin.setY(std::min(bmin.y(), p.bmin.y()));
        bmin.setZ(std::min(bmin.z(), p.bmin.z()));
        bmax.setX(std::max(bmax.x(), p.bmax.x()));
        bmax.setY(std::max(bmax.y(), p.bmax.y()));
        bmax.setZ(std::max(bmax.z(), p.bmax.z()));
    }
    const float dx = std::max(1.0f, bmax.x() - bmin.x());
    const float dy = std::max(1.0f, bmax.y() - bmin.y());
    float cell = std::sqrt(std::max(1.0f, (dx * dy) / float(n)));
    cell = std::clamp(cell, 10.0f, 80.0f);
    const int nx = std::max(1, int(std::ceil(dx / cell)));
    const int ny = std::max(1, int(std::ceil(dy / cell)));
    m_pickGrid.valid = true;
    m_pickGrid.bmin = bmin;
    m_pickGrid.bmax = bmax;
    m_pickGrid.cell = cell;
    m_pickGrid.nx = nx;
    m_pickGrid.ny = ny;
    m_pickGrid.cells.resize(nx * ny);
    auto clampX = [&](int v) { return std::clamp(v, 0, nx - 1); };
    auto clampY = [&](int v) { return std::clamp(v, 0, ny - 1); };
    for (int i = 0; i < n; ++i) {
        const PickProxy& p = m_pickProxies[i];
        int x0 = clampX(int(std::floor((p.bmin.x() - bmin.x()) / cell)));
        int x1 = clampX(int(std::floor((p.bmax.x() - bmin.x()) / cell)));
        int y0 = clampY(int(std::floor((p.bmin.y() - bmin.y()) / cell)));
        int y1 = clampY(int(std::floor((p.bmax.y() - bmin.y()) / cell)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                m_pickGrid.cells[y * nx + x].push_back(i);
    }
}

bool PCB3DView::cpuPickGridAt(const QPoint& pos, QUuid& outId) const {
    if (!m_pickGrid.valid || m_pickProxies.isEmpty()) return false;
    const QVector3D ro = unprojectToWorld(pos, -1.0f);
    const QVector3D rf = unprojectToWorld(pos, 1.0f);
    const QVector3D rd = (rf - ro).normalized();
    auto rayInterval = [&](const QVector3D& boxMin, const QVector3D& boxMax,
                           float& tmin, float& tmax) -> bool {
        tmin = 0.0f; tmax = 1e30f;
        for (int axis = 0; axis < 3; ++axis) {
            const float origin = ro[axis], dir = rd[axis];
            if (std::abs(dir) < 1e-9f) {
                if (origin < boxMin[axis] || origin > boxMax[axis]) return false;
                continue;
            }
            float t1 = (boxMin[axis] - origin) / dir;
            float t2 = (boxMax[axis] - origin) / dir;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        return true;
    };
    float t0 = 0.0f, t1 = 0.0f;
    if (!rayInterval(m_pickGrid.bmin, m_pickGrid.bmax, t0, t1) || t1 < 0.0f) return false;
    t0 = std::max(0.0f, t0);
    const float step = std::max(m_pickGrid.cell * 0.5f, 0.5f);
    const int steps = std::clamp(int((t1 - t0) / step), 8, 96);
    QVector<char> tested(int(m_pickProxies.size()), 0);
    float bestT = 1e30f;
    QUuid bestId;
    bool hit = false;
    for (int s = 0; s <= steps; ++s) {
        const float t = t0 + (t1 - t0) * (float(s) / float(steps));
        const QVector3D p = ro + rd * t;
        const int cx = int(std::floor((p.x() - m_pickGrid.bmin.x()) / m_pickGrid.cell));
        const int cy = int(std::floor((p.y() - m_pickGrid.bmin.y()) / m_pickGrid.cell));
        if (cx < 0 || cy < 0 || cx >= m_pickGrid.nx || cy >= m_pickGrid.ny) continue;
        for (int idx : m_pickGrid.cells[cy * m_pickGrid.nx + cx]) {
            if (idx < 0 || idx >= int(m_pickProxies.size()) || tested[idx]) continue;
            tested[idx] = 1;
            float tt = 0.0f;
            if (rayIntersectsAabb(ro, rd, m_pickProxies[idx].bmin, m_pickProxies[idx].bmax, tt) && tt < bestT) {
                bestT = tt; bestId = m_pickProxies[idx].id; hit = true;
            }
        }
    }
    if (hit) outId = bestId;
    return hit;
}

bool PCB3DView::projectWorldToScreen(const QVector3D& worldPos, QPointF& outScreen) const {
    const QVector4D clip = m_projection * currentViewMatrix() * QVector4D(worldPos, 1.0f);
    if (std::abs(clip.w()) < 1e-8f) return false;
    const QVector3D ndc = (clip / clip.w()).toVector3D();
    if (ndc.z() < -1.0f || ndc.z() > 1.0f) return false;
    outScreen.setX((ndc.x() * 0.5f + 0.5f) * width());
    outScreen.setY((1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
    return true;
}

void PCB3DView::drawHoverOverlay(QPainter& p) {
    if (!m_showHoverInfo || m_hoverId.isNull()) return;
    const PickProxy* target = nullptr;
    for (const PickProxy& p : m_pickProxies) {
        if (p.id == m_hoverId) { target = &p; break; }
    }
    if (!target) return;
    QVector<QPointF> pts;
    pts.reserve(8);
    for (int i = 0; i < 8; ++i) {
        const QVector3D c(
            (i & 1) ? target->bmax.x() : target->bmin.x(),
            (i & 2) ? target->bmax.y() : target->bmin.y(),
            (i & 4) ? target->bmax.z() : target->bmin.z());
        QPointF sp;
        if (projectWorldToScreen(c, sp)) pts.push_back(sp);
    }
    if (pts.size() < 2) return;
    QPointF mn = pts.first(), mx = pts.first();
    for (const QPointF& pt : pts) {
        mn.setX(std::min(mn.x(), pt.x()));
        mn.setY(std::min(mn.y(), pt.y()));
        mx.setX(std::max(mx.x(), pt.x()));
        mx.setY(std::max(mx.y(), pt.y()));
    }
    QRectF box = QRectF(mn, mx).normalized().adjusted(-6.0, -6.0, 6.0, 6.0);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(120, 210, 255, 220), 1.6, Qt::DashLine));
    p.setBrush(QColor(120, 210, 255, 22));
    p.drawRoundedRect(box, 5.0, 5.0);
    const QString label = m_componentRefs.value(m_hoverId, QStringLiteral("Component"));
    QRectF textRect = p.fontMetrics().boundingRect(label).adjusted(-8, -4, 8, 4);
    textRect.moveBottomLeft(QPointF(box.left(), box.top() - 4.0));
    if (textRect.top() < 2.0) textRect.moveTop(box.bottom() + 4.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(14, 20, 28, 220));
    p.drawRoundedRect(textRect, 4.0, 4.0);
    p.setPen(QColor(170, 230, 255));
    p.drawText(textRect, Qt::AlignCenter, label);
}

void PCB3DView::saveViewBookmark(int slot) {
    if (slot < 1 || slot > 9) return;
    m_bookmarkRot[slot] = m_rotation;
    m_bookmarkPan[slot] = m_pan;
    m_bookmarkZoom[slot] = m_zoom;
    Q_EMIT statusUpdated(QStringLiteral("View %1 saved").arg(slot));
}

void PCB3DView::loadViewBookmark(int slot) {
    if (slot < 1 || slot > 9) return;
    if (!m_bookmarkRot.contains(slot) || !m_bookmarkPan.contains(slot) || !m_bookmarkZoom.contains(slot)) {
        Q_EMIT statusUpdated(QStringLiteral("View %1 is empty").arg(slot));
        return;
    }
    startCameraTransition(m_bookmarkRot.value(slot), m_bookmarkZoom.value(slot), m_bookmarkPan.value(slot));
    Q_EMIT statusUpdated(QStringLiteral("View %1 loaded").arg(slot));
}

PCB3DView::ObjMesh PCB3DView::loadOBJ(const QString& path) {
    if (path.isEmpty()) return {};
    const QString finalPath = resolveModelPath(path);
    if (finalPath.isEmpty()) return {};
    if (m_objCache.contains(finalPath)) return m_objCache[finalPath];
    if (m_objCache.contains(path)) return m_objCache[path];

    QFile file(finalPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ObjMesh out;
        m_objCache[finalPath] = out;
        m_objCache[path] = out;
        return out;
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QString ext = QFileInfo(finalPath).suffix().toLower();
    ObjMesh out;
    if (ext == "obj") out = loadObjMeshFromText(text);
    else if (ext == "wrl" || ext == "vrml") out = loadVrmlMeshFromText(text);
    else if (ext == "step" || ext == "stp") out = loadStepMeshFromText(text);
    else if (ext == "igs" || ext == "iges") out = loadIgesMeshFromText(text);
    else out = loadObjMeshFromText(text);
    m_objCache[finalPath] = out;
    m_objCache[path] = out;
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadObjMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> temp;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        if (parts[0] == "v" && parts.size() >= 4) {
            const QVector3D p(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
            if (isFinite3(p)) temp.push_back(p);
            continue;
        }
        if (parts[0] != "f" || parts.size() < 4) continue;
        auto idx = [&](int p) { return parts[p].split('/')[0].toInt() - 1; };
        for (int i = 1; i <= parts.size() - 3; ++i) {
            QVector3D a = temp.value(idx(1));
            QVector3D b = temp.value(idx(i + 1));
            QVector3D c = temp.value(idx(i + 2));
            QVector3D n = QVector3D::normal(a, b, c);
            out.vertices.push_back({a, n});
            out.vertices.push_back({b, n});
            out.vertices.push_back({c, n});
        }
    }
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadVrmlMeshFromText(const QString& text) const {
    ObjMesh out;
    static const QRegularExpression pointBlockRe(
        "point\\s*\\[([^\\]]+)\\]",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression coordIndexRe(
        "coordIndex\\s*\\[([^\\]]+)\\]",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch pm = pointBlockRe.match(text);
    if (!pm.hasMatch()) return out;
    QVector<QVector3D> points;
    const QString pointBlock = pm.captured(1);
    static const QRegularExpression numRe("([\\-+]?\\d+(?:\\.\\d+)?(?:[EeDd][\\-+]?\\d+)?)");
    QRegularExpressionMatchIterator it = numRe.globalMatch(pointBlock);
    QVector<double> nums;
    while (it.hasNext()) {
        const QString tok = it.next().captured(1);
        bool ok = false;
        const double v = tok.toDouble(&ok);
        if (ok && std::isfinite(v)) nums.push_back(v);
    }
    for (int i = 0; i + 2 < nums.size(); i += 3)
        points.push_back(QVector3D(float(nums[i]), float(nums[i + 1]), float(nums[i + 2])));
    if (points.isEmpty()) return out;

    const QRegularExpressionMatch cm = coordIndexRe.match(text);
    if (!cm.hasMatch()) {
        for (int i = 0; i + 2 < points.size(); i += 3) {
            const QVector3D n = QVector3D::normal(points[i], points[i + 1], points[i + 2]);
            out.vertices.push_back({points[i], n});
            out.vertices.push_back({points[i + 1], n});
            out.vertices.push_back({points[i + 2], n});
        }
        return out;
    }
    QVector<int> indices;
    static const QRegularExpression idxRe("-?\\d+");
    it = idxRe.globalMatch(cm.captured(1));
    while (it.hasNext()) indices.push_back(it.next().captured(0).toInt());
    QVector<int> face;
    auto emitFace = [&]() {
        if (face.size() < 3) return;
        const int i0 = face[0];
        for (int k = 1; k + 1 < face.size(); ++k) {
            const int i1 = face[k];
            const int i2 = face[k + 1];
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            if (i0 >= points.size() || i1 >= points.size() || i2 >= points.size()) continue;
            const QVector3D n = QVector3D::normal(points[i0], points[i1], points[i2]);
            out.vertices.push_back({points[i0], n});
            out.vertices.push_back({points[i1], n});
            out.vertices.push_back({points[i2], n});
        }
    };
    for (int idx : indices) {
        if (idx == -1) {
            emitFace();
            face.clear();
            continue;
        }
        face.push_back(idx);
    }
    emitFace();
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadStepMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> points;
    static const QRegularExpression ptRe(
        "CARTESIAN_POINT\\s*\\([^\\(]*\\(\\s*([\\-+0-9.EeDd]+)\\s*,\\s*([\\-+0-9.EeDd]+)\\s*,\\s*([\\-+0-9.EeDd]+)\\s*\\)\\s*\\)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = ptRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        auto cvt = [](QString s) {
            s.replace('D', 'E');
            s.replace('d', 'e');
            bool ok = false;
            const double v = s.toDouble(&ok);
            return (ok && std::isfinite(v)) ? v : 0.0;
        };
        points.push_back(QVector3D(float(cvt(m.captured(1))), float(cvt(m.captured(2))), float(cvt(m.captured(3)))));
        if (points.size() >= 10000) break;
    }
    if (points.isEmpty()) return out;

    // STEP Triangulated Mesh Index Parsing (AP214 / AP242)
    static const QRegularExpression triRe(
        "TRIANGULATED_FACE\\s*\\([^,]+,\\s*\\([^\\)]+\\),\\s*\\(\\s*([0-9\\s,]+)\\s*\\)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator triIt = triRe.globalMatch(text);
    bool parsedTriangles = false;

    while (triIt.hasNext()) {
        const QRegularExpressionMatch m = triIt.next();
        const QStringList idxTokens = m.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        QVector<int> triIndices;
        for (const QString& tok : idxTokens) {
            bool ok = false;
            int val = tok.toInt(&ok);
            if (ok && val > 0 && val <= points.size()) {
                triIndices.push_back(val - 1);
            }
        }
        for (int k = 0; k + 2 < triIndices.size(); k += 3) {
            QVector3D p0 = points[triIndices[k]];
            QVector3D p1 = points[triIndices[k+1]];
            QVector3D p2 = points[triIndices[k+2]];
            QVector3D n = QVector3D::normal(p0, p1, p2);
            out.vertices.push_back({p0, n});
            out.vertices.push_back({p1, n});
            out.vertices.push_back({p2, n});
            parsedTriangles = true;
        }
    }

    if (parsedTriangles && !out.vertices.isEmpty()) {
        return out;
    }

    // Convex Hull / Bounding Box Mesh fallback for STEP files
    QVector3D pmin(1e9f, 1e9f, 1e9f), pmax(-1e9f, -1e9f, -1e9f);
    for (const QVector3D& p : points) {
        pmin.setX(std::min(pmin.x(), p.x())); pmin.setY(std::min(pmin.y(), p.y())); pmin.setZ(std::min(pmin.z(), p.z()));
        pmax.setX(std::max(pmax.x(), p.x())); pmax.setY(std::max(pmax.y(), p.y())); pmax.setZ(std::max(pmax.z(), p.z()));
    }
    const float span = (pmax - pmin).length();
    const float h = std::max(0.02f, span * 0.003f);
    for (const QVector3D& p : points) {
        const QVector3D a = p + QVector3D(-h, -h, -h);
        const QVector3D b = p + QVector3D( h, -h, -h);
        const QVector3D c = p + QVector3D( h,  h, -h);
        const QVector3D d = p + QVector3D(-h,  h, -h);
        const QVector3D e = p + QVector3D(-h, -h,  h);
        const QVector3D f = p + QVector3D( h, -h,  h);
        const QVector3D g = p + QVector3D( h,  h,  h);
        const QVector3D q = p + QVector3D(-h,  h,  h);
        appendQuad(out.vertices, e, f, g, q, {0, 0, 1});
        appendQuad(out.vertices, a, b, f, e, {0, -1, 0});
        appendQuad(out.vertices, b, c, g, f, {1, 0, 0});
        appendQuad(out.vertices, c, d, q, g, {0, 1, 0});
        appendQuad(out.vertices, d, a, e, q, {-1, 0, 0});
        appendQuad(out.vertices, a, d, c, b, {0, 0, -1});
    }
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadIgesMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> points;
    const QStringList lines = text.split('\n');
    static const QRegularExpression numRe("([\\-+]?\\d+(?:\\.\\d+)?(?:[EeDd][\\-+]?\\d+)?)");
    for (const QString& line : lines) {
        if (!line.contains(',')) continue;
        QRegularExpressionMatchIterator it = numRe.globalMatch(line);
        QVector<double> nums;
        while (it.hasNext()) {
            QString t = it.next().captured(1);
            t.replace('D', 'E');
            t.replace('d', 'e');
            bool ok = false;
            const double v = t.toDouble(&ok);
            if (ok && std::isfinite(v)) nums.push_back(v);
        }
        for (int i = 0; i + 2 < nums.size(); i += 3) {
            points.push_back(QVector3D(float(nums[i]), float(nums[i + 1]), float(nums[i + 2])));
            if (points.size() >= 2500) break;
        }
        if (points.size() >= 2500) break;
    }
    if (points.isEmpty()) return out;
    QVector3D pmin(1e9f, 1e9f, 1e9f), pmax(-1e9f, -1e9f, -1e9f);
    for (const QVector3D& p : points) {
        pmin.setX(std::min(pmin.x(), p.x())); pmin.setY(std::min(pmin.y(), p.y())); pmin.setZ(std::min(pmin.z(), p.z()));
        pmax.setX(std::max(pmax.x(), p.x())); pmax.setY(std::max(pmax.y(), p.y())); pmax.setZ(std::max(pmax.z(), p.z()));
    }
    const float span = (pmax - pmin).length();
    const float h = std::max(0.02f, span * 0.0025f);
    for (const QVector3D& p : points) {
        const QVector3D a = p + QVector3D(-h, -h, -h);
        const QVector3D b = p + QVector3D( h, -h, -h);
        const QVector3D c = p + QVector3D( h,  h, -h);
        const QVector3D d = p + QVector3D(-h,  h, -h);
        const QVector3D e = p + QVector3D(-h, -h,  h);
        const QVector3D f = p + QVector3D( h, -h,  h);
        const QVector3D g = p + QVector3D( h,  h,  h);
        const QVector3D q = p + QVector3D(-h,  h,  h);
        appendQuad(out.vertices, e, f, g, q, {0, 0, 1});
        appendQuad(out.vertices, a, b, f, e, {0, -1, 0});
        appendQuad(out.vertices, b, c, g, f, {1, 0, 0});
        appendQuad(out.vertices, c, d, q, g, {0, 1, 0});
        appendQuad(out.vertices, d, a, e, q, {-1, 0, 0});
        appendQuad(out.vertices, a, d, c, b, {0, 0, -1});
    }
    return out;
}

QString PCB3DView::expandModelEnvVars(const QString& rawPath) const {
    QString out = rawPath.trimmed();
    if (out.isEmpty()) return out;
    static const QRegularExpression braceVar("\\$\\{([^}]+)\\}");
    QRegularExpressionMatch m = braceVar.match(out);
    while (m.hasMatch()) {
        const QString var = m.captured(1).trimmed();
        const QString val = qEnvironmentVariable(var.toUtf8().constData());
        out.replace(m.capturedStart(0), m.capturedLength(0), val);
        m = braceVar.match(out);
    }
    static const QRegularExpression plainVar("\\$([A-Za-z_][A-Za-z0-9_]*)");
    m = plainVar.match(out);
    while (m.hasMatch()) {
        const QString var = m.captured(1).trimmed();
        const QString val = qEnvironmentVariable(var.toUtf8().constData());
        out.replace(m.capturedStart(0), m.capturedLength(0), val);
        m = plainVar.match(out);
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(out));
}

QStringList PCB3DView::modelSearchRoots() const {
    QStringList roots;
    auto appendRoot = [&roots](const QString& p) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p.trimmed()));
        if (clean.isEmpty()) return;
        if (!QFileInfo(clean).isDir()) return;
        if (!roots.contains(clean, Qt::CaseInsensitive)) roots.push_back(clean);
    };
    for (const QString& p : ConfigManager::instance().modelPaths()) appendRoot(p);
    appendRoot(qEnvironmentVariable("KISYS3DMOD"));
    for (int v = 5; v <= 9; ++v)
        appendRoot(qEnvironmentVariable(QString("KICAD%1_3DMODEL_DIR").arg(v).toUtf8().constData()));
    return roots;
}

QString PCB3DView::resolveModelPath(const QString& rawPath) const {
    const QString expanded = expandModelEnvVars(rawPath);
    if (expanded.isEmpty()) return QString();
    auto pickExisting = [](const QString& p) -> QString {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p));
        QFileInfo fi(clean);
        if (fi.exists() && fi.isFile()) return fi.absoluteFilePath();
        const QString suffix = fi.suffix().toLower();
        if (suffix == "wrl" || suffix == "step" || suffix == "stp") {
            const QString objPath = fi.path() + "/" + fi.completeBaseName() + ".obj";
            QFileInfo objFi(objPath);
            if (objFi.exists() && objFi.isFile()) return objFi.absoluteFilePath();
        }
        return QString();
    };
    if (QFileInfo(expanded).isAbsolute()) return pickExisting(expanded);
    {
        const QString local = pickExisting(expanded);
        if (!local.isEmpty()) return local;
    }
    for (const QString& root : modelSearchRoots()) {
        const QString candidate = QDir(root).filePath(expanded);
        const QString found = pickExisting(candidate);
        if (!found.isEmpty()) return found;
    }
    return QString();
}

QVector<PCB3DView::Vertex> PCB3DView::makeBoxVertices(float hx, float hy, float hz) const {
    QVector<Vertex> v;
    QVector3D p000(-hx, -hy, 0.0f), p100(hx, -hy, 0.0f), p110(hx, hy, 0.0f), p010(-hx, hy, 0.0f);
    QVector3D p001(-hx, -hy, hz), p101(hx, -hy, hz), p111(hx, hy, hz), p011(-hx, hy, hz);
    appendQuad(v, p001, p101, p111, p011, {0, 0, 1});
    appendQuad(v, p000, p100, p101, p001, {0, -1, 0});
    appendQuad(v, p100, p110, p111, p101, {1, 0, 0});
    appendQuad(v, p110, p010, p011, p111, {0, 1, 0});
    appendQuad(v, p010, p000, p001, p011, {-1, 0, 0});
    appendQuad(v, p000, p010, p110, p100, {0, 0, -1});
    return v;
}

bool PCB3DView::passesSelectionFilter(QGraphicsItem* item) const {
    if (!m_selectedOnly) return true;
    if (!item) return false;
    if (item->isSelected()) return true;
    QGraphicsItem* p = item->parentItem();
    while (p) {
        if (p->isSelected()) return true;
        p = p->parentItem();
    }
    return false;
}

void PCB3DView::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    m_inertiaTimer.stop();
    m_velYaw = m_velPitch = 0.0f;
    m_lastPos = event->pos();
    if (event->button() == Qt::LeftButton) {
        if (handleTriadClick(event->pos())) return;
        m_leftPressed = true;
        m_pressPos = event->pos();
    }
}

void PCB3DView::mouseMoveEvent(QMouseEvent* event) {
    const int dx = int(event->position().x()) - m_lastPos.x();
    const int dy = int(event->position().y()) - m_lastPos.y();
    if (event->buttons() & Qt::LeftButton) {
        beginInteractiveRender();
        m_cameraAnimTimer.stop();
        const float yaw = 0.45f * dx;
        const float pitch = 0.45f * dy;
        m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), yaw) * m_rotation;
        m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), pitch) * m_rotation;
        m_velYaw = yaw;
        m_velPitch = pitch;
        update();
    } else if (event->buttons() == Qt::NoButton) {
        updateHover(event->pos());
    }
    m_lastPos = event->pos();
}

void PCB3DView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_leftPressed) {
        m_leftPressed = false;
        if ((event->pos() - m_pressPos).manhattanLength() <= 3) {
            m_velYaw = m_velPitch = 0.0f;
            if (m_measureMode) {
                QVector3D hit;
                if (intersectBoardPlane(event->pos(), hit)) {
                    if (!m_measureHasFirst || m_measureHasSecond) {
                        m_measureP1 = hit;
                        m_measureHasFirst = true;
                        m_measureHasSecond = false;
                        emit measurementUpdated(-1.0);
                    } else {
                        m_measureP2 = hit;
                        m_measureHasSecond = true;
                        const double dist = QLineF(
                            QPointF(m_measureP1.x(), m_measureP1.y()),
                            QPointF(m_measureP2.x(), m_measureP2.y())).length();
                        emit measurementUpdated(dist);
                    }
                    update();
                }
                return;
            }
            QUuid id;
            if (pickAt(event->pos(), id)) emit componentPicked(id);
        } else if (std::abs(m_velYaw) + std::abs(m_velPitch) > 0.6f) {
            m_inertiaTimer.start();
        } else {
            m_velYaw = m_velPitch = 0.0f;
        }
    }
}

void PCB3DView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    QUuid id;
    if (pickAt(event->pos(), id)) {
        focusComponent(id);
        emit componentPicked(id);
    } else {
        fitBoard();
    }
}

void PCB3DView::wheelEvent(QWheelEvent* event) {
    beginInteractiveRender();
    m_cameraAnimTimer.stop();
    const float steps = event->angleDelta().y() / 120.0f;
    const float currentDistance = -m_zoom;
    const float factor = std::pow(0.85f, steps);
    const float newDistance = std::clamp(currentDistance * factor, 20.0f, 2000.0f);
    m_zoom = -newDistance;
    Q_EMIT zoomDistanceChanged(newDistance);
    update();
    event->accept();
}

void PCB3DView::keyPressEvent(QKeyEvent* event) {
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        const int slot = int(event->key() - Qt::Key_1) + 1;
        if (event->modifiers() & Qt::ControlModifier)
            saveViewBookmark(slot);
        else
            loadViewBookmark(slot);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        fitBoard();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R) {
        resetCamera();
        event->accept();
        return;
    }
    const float distance = -m_zoom;
    const float baseStep = std::max(2.0f, distance * 0.05f);
    const float step = event->modifiers().testFlag(Qt::ShiftModifier) ? baseStep * 3.0f : baseStep;
    QVector3D delta(0.0f, 0.0f, 0.0f);
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(step); break;
    case Qt::Key_Right: delta.setX(-step); break;
    case Qt::Key_Up: delta.setY(-step); break;
    case Qt::Key_Down: delta.setY(step); break;
    default:
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    beginInteractiveRender();
    m_cameraAnimTimer.stop();
    m_pan += delta;
    update();
    event->accept();
}

void PCB3DView::beginInteractiveRender() {
    m_inertiaTimer.stop();
    if (m_renderMode != RenderMode::Fast) {
        m_renderMode = RenderMode::Fast;
        m_sceneDirty = true;
    }
    m_interactionTimer.start();
}

void PCB3DView::pollSpaceMouse() {
    if (!m_spaceMouseEnabled || !m_spaceMouseConnected || !m_spnavPollEvent) return;
    bool changed = false;
    SpnavEvent ev;
    int guard = 0;
    while (m_spnavPollEvent(&ev) > 0 && guard < 64) {
        ++guard;
        if (ev.type == kSpnavEventMotion) {
            const float panScale = std::max(0.02f, std::abs(m_zoom) * 0.0012f);
            const float rotScale = 0.02f;
            m_pan += QVector3D(ev.motion.x * panScale, -ev.motion.y * panScale, 0.0f);
            m_zoom += ev.motion.z * 0.12f;
            m_zoom = std::clamp(m_zoom, -2000.0f, -20.0f);
            Q_EMIT zoomDistanceChanged(-m_zoom);
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), -ev.motion.rx * rotScale) * m_rotation;
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), ev.motion.ry * rotScale) * m_rotation;
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), ev.motion.rz * rotScale) * m_rotation;
            changed = true;
        } else if (ev.type == kSpnavEventButton) {
            if (ev.button.press && ev.button.bnum == 0) {
                resetCamera();
                changed = true;
            }
        }
    }
    if (changed) update();
}

void PCB3DView::startCameraTransition(const QQuaternion& targetRot, float targetZoom,
                                      const QVector3D& targetPan) {
    m_inertiaTimer.stop();
    m_velYaw = m_velPitch = 0.0f;
    m_rotFrom = m_rotation;
    m_rotTo = targetRot;
    m_zoomFrom = m_zoom;
    m_zoomTo = targetZoom;
    m_panFrom = m_pan;
    m_panTo = targetPan;
    m_cameraAnimT = 0.0f;
    Q_EMIT zoomDistanceChanged(-m_zoom);
    m_cameraAnimTimer.start();
}

void PCB3DView::tickCameraAnimation() {
    m_cameraAnimT = std::min(1.0f, m_cameraAnimT + 0.06f);
    const float s = m_cameraAnimT * m_cameraAnimT * (3.0f - 2.0f * m_cameraAnimT);
    m_rotation = QQuaternion::slerp(m_rotFrom, m_rotTo, s);
    m_zoom = m_zoomFrom + (m_zoomTo - m_zoomFrom) * s;
    m_pan = m_panFrom + (m_panTo - m_panFrom) * s;
    Q_EMIT zoomDistanceChanged(-m_zoom);
    update();
    if (m_cameraAnimT >= 1.0f) m_cameraAnimTimer.stop();
}

void PCB3DView::tickSpinAnimation() {
    if (std::abs(m_spinSpeedDeg) < 1e-6f) {
        m_spinTimer.stop();
        return;
    }
    // Camera-only motion: no scene rebuild required
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), m_spinSpeedDeg) * m_rotation;
    update();
}

void PCB3DView::tickInertia() {
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), m_velYaw) * m_rotation;
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), m_velPitch) * m_rotation;
    m_velYaw *= 0.90f;
    m_velPitch *= 0.90f;
    if (std::abs(m_velYaw) + std::abs(m_velPitch) < 0.05f) {
        m_inertiaTimer.stop();
        m_velYaw = m_velPitch = 0.0f;
    }
    update();
}

bool PCB3DView::handleTriadClick(const QPoint& pos) {
    const QPoint c(width() - 54, 54);
    const float scale = 24.0f;
    auto proj = [&](const QVector3D& axis) {
        const QVector3D v = m_rotation.rotatedVector(axis);
        return QPointF(c.x() + v.x() * scale, c.y() - v.y() * scale);
    };
    struct Opt {
        QVector3D axis;
        QQuaternion rot;
    };
    const Opt opts[3] = {
        {QVector3D(1, 0, 0), QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), -90.0f)},
        {QVector3D(0, 1, 0), QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 90.0f)},
        {QVector3D(0, 0, 1), QQuaternion()},
    };
    for (const Opt& o : opts) {
        const QPointF p = proj(o.axis);
        if (QLineF(QPointF(pos), p).length() <= 14.0) {
            m_spinTimer.stop();
            m_spinSpeedDeg = 0.0f;
            startCameraTransition(o.rot, m_zoom, m_pan);
            return true;
        }
    }
    return false;
}

void PCB3DView::updateHover(const QPoint& pos) {
    if (m_leftPressed || m_measureMode) {
        if (!m_hoverId.isNull()) {
            m_hoverId = QUuid();
            setCursor(Qt::ArrowCursor);
            update();
        }
        return;
    }
    if (m_hoverThrottle.elapsed() < 30) return;
    m_hoverThrottle.restart();
    QUuid id;
    cpuPickAt(pos, id);
    if (id != m_hoverId) {
        m_hoverId = id;
        setCursor(id.isNull() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

QVector3D PCB3DView::unprojectToWorld(const QPoint& p, float ndcZ) const {
    const float x = (2.0f * p.x()) / float(width()) - 1.0f;
    const float y = 1.0f - (2.0f * p.y()) / float(height());
    QVector4D clip(x, y, ndcZ, 1.0f);
    QMatrix4x4 view;
    view.translate(m_pan.x(), m_pan.y(), m_pan.z());
    view.translate(0.0f, 0.0f, m_zoom);
    view.rotate(m_rotation);
    QMatrix4x4 inv = (m_projection * view).inverted();
    QVector4D w = inv * clip;
    if (std::abs(w.w()) < 1e-8f) return QVector3D();
    return (w / w.w()).toVector3D();
}

bool PCB3DView::rayIntersectsAabb(const QVector3D& ro, const QVector3D& rd,
                                  const QVector3D& bmin, const QVector3D& bmax,
                                  float& outT) const {
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ro[axis];
        const float dir = rd[axis];
        if (std::abs(dir) < 1e-9f) {
            if (origin < bmin[axis] || origin > bmax[axis]) return false;
            continue;
        }
        float t1 = (bmin[axis] - origin) / dir;
        float t2 = (bmax[axis] - origin) / dir;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    outT = tmin;
    return true;
}

bool PCB3DView::cpuPickAt(const QPoint& pos, QUuid& outId) const {
    if (m_pickGrid.valid && cpuPickGridAt(pos, outId)) return true;
    if (m_pickProxies.isEmpty()) return false;
    const QVector3D ro = unprojectToWorld(pos, -1.0f);
    const QVector3D rf = unprojectToWorld(pos, 1.0f);
    const QVector3D rd = (rf - ro).normalized();
    float bestT = 1e30f;
    bool hit = false;
    for (const PickProxy& proxy : m_pickProxies) {
        float t = 0.0f;
        if (rayIntersectsAabb(ro, rd, proxy.bmin, proxy.bmax, t) && t < bestT) {
            bestT = t;
            outId = proxy.id;
            hit = true;
        }
    }
    return hit;
}

bool PCB3DView::pickAt(const QPoint& pos, QUuid& outId) const {
    if (!m_showComponents) return false;
    if (const_cast<PCB3DView*>(this)->gpuPickAt(pos, outId)) return true;
    return cpuPickAt(pos, outId);
}

bool PCB3DView::intersectBoardPlane(const QPoint& pos, QVector3D& out) const {
    const QVector3D ro = unprojectToWorld(pos, -1.0f);
    const QVector3D rf = unprojectToWorld(pos, 1.0f);
    const QVector3D rd = (rf - ro).normalized();
    if (std::abs(rd.z()) < 1e-8f) return false;
    const float zPlane = kCopperZTop + kCopperThickness;
    const float t = (zPlane - ro.z()) / rd.z();
    if (t < 0.0f) return false;
    out = ro + rd * t;
    return true;
}

void PCB3DView::ensurePickFbo() {
    const int pw = std::max(1, int(width() * devicePixelRatioF()));
    const int ph = std::max(1, int(height() * devicePixelRatioF()));
    if (m_pickFbo && m_pickFbo->size() == QSize(pw, ph)) return;
    delete m_pickFbo;
    m_pickFbo = nullptr;
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::Depth);
    fmt.setTextureTarget(GL_TEXTURE_2D);
    m_pickFbo = new QOpenGLFramebufferObject(pw, ph, fmt);
    if (!m_pickFbo->isValid()) {
        qWarning() << "PCB3DView: pick FBO incomplete";
        delete m_pickFbo;
        m_pickFbo = nullptr;
    }
}

QMatrix4x4 PCB3DView::currentViewMatrix() const {
    QMatrix4x4 view;
    view.translate(m_pan.x(), m_pan.y(), m_pan.z());
    view.translate(0.0f, 0.0f, m_zoom);
    view.rotate(m_rotation);
    return view;
}

bool PCB3DView::gpuPickAt(const QPoint& pos, QUuid& outId) {
    if (!context() || !isValid() || !m_glSupported) return false;
    if (m_sceneDirty) rebuildSceneCache();
    if (m_componentDraws.isEmpty()) return false;
    makeCurrent();
    ensurePickFbo();
    if (!m_pickFbo) {
        doneCurrent();
        return false;
    }
    m_pickFbo->bind();
    const int pw = m_pickFbo->width();
    const int ph = m_pickFbo->height();
    glViewport(0, 0, pw, ph);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_pickIdToUuid.clear();
    m_pickShader.bind();
    m_pickShader.setUniformValue("uView", currentViewMatrix());
    m_pickShader.setUniformValue("uProj", m_projection);
    m_staticVbo.bind();
    m_pickShader.enableAttributeArray(0);
    m_pickShader.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
    int id = 1;
    for (const GpuBatch& b : m_batches) {
        if (b.componentIndex < 0 || b.count <= 0) continue;
        const ComponentDraw& cd = m_componentDraws[b.componentIndex];
        const int rid = id & 0xFF;
        const int gid = (id >> 8) & 0xFF;
        const int bid = (id >> 16) & 0xFF;
        m_pickShader.setUniformValue("uIdColor", QVector3D(rid / 255.0f, gid / 255.0f, bid / 255.0f));
        m_pickShader.setUniformValue("uModel", cd.placement);
        m_pickIdToUuid[id] = cd.id;
        glDrawArrays(GL_TRIANGLES, b.first, b.count);
        ++id;
    }
    m_pickShader.disableAttributeArray(0);
    m_staticVbo.release();
    m_pickShader.release();
    const int px = std::clamp(int(pos.x() * devicePixelRatioF()), 0, pw - 1);
    const int py = std::clamp(int(pos.y() * devicePixelRatioF()), 0, ph - 1);
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(px, ph - 1 - py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    m_pickFbo->release();
    glViewport(0, 0, std::max(1, int(width() * devicePixelRatioF())),
               std::max(1, int(height() * devicePixelRatioF())));
    glEnable(GL_BLEND);
    doneCurrent();
    const int pickedId = int(rgba[0]) | (int(rgba[1]) << 8) | (int(rgba[2]) << 16);
    if (pickedId <= 0 || !m_pickIdToUuid.contains(pickedId)) return false;
    outId = m_pickIdToUuid.value(pickedId);
    return !outId.isNull();
}

void PCB3DView::drawAxisTriadOverlay(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPoint c(width() - 54, 54);
    const float scale = 24.0f;
    auto proj = [&](const QVector3D& axis) {
        QVector3D v = m_rotation.rotatedVector(axis);
        return QPointF(c.x() + v.x() * scale, c.y() - v.y() * scale);
    };
    p.setPen(QPen(QColor(245, 70, 70), 2));
    p.drawLine(c, proj(QVector3D(1, 0, 0)));
    p.drawText(proj(QVector3D(1, 0, 0)) + QPointF(2, -2), "X");
    p.setPen(QPen(QColor(70, 220, 100), 2));
    p.drawLine(c, proj(QVector3D(0, 1, 0)));
    p.drawText(proj(QVector3D(0, 1, 0)) + QPointF(2, -2), "Y");
    p.setPen(QPen(QColor(90, 140, 255), 2));
    p.drawLine(c, proj(QVector3D(0, 0, 1)));
    p.drawText(proj(QVector3D(0, 0, 1)) + QPointF(2, -2), "Z");
    p.setPen(QPen(QColor(180, 190, 210, 180), 1));
    p.setBrush(QColor(25, 30, 38, 170));
    p.drawEllipse(c, 4, 4);
}

void PCB3DView::drawGridOverlay(QPainter& p) {
    if (!m_showGrid) return;
    const float minX = m_boundsMin.x();
    const float minY = m_boundsMin.y();
    const float maxX = m_boundsMax.x();
    const float maxY = m_boundsMax.y();
    if (maxX <= minX || maxY <= minY) return;

    auto chooseStep = [](float span) -> float {
        if (span <= 0.0f) return 1.0f;
        const float raw = span / 10.0f;
        const float p10 = std::pow(10.0f, std::floor(std::log10(raw)));
        const float n = raw / p10;
        float m = 1.0f;
        if (n > 5.0f) m = 10.0f;
        else if (n > 2.0f) m = 5.0f;
        else if (n > 1.0f) m = 2.0f;
        return m * p10;
    };
    const float span = std::max(maxX - minX, maxY - minY);
    const float majorStep = chooseStep(span);
    const float minorStep = std::max(majorStep / 5.0f, 0.5f);
    const float zPlane = kCopperZTop + kCopperThickness + 0.002f;

    auto project = [&](const QVector3D& w, QPointF& outPt) -> bool {
        const QMatrix4x4 view = currentViewMatrix();
        const QVector4D clip = m_projection * view * QVector4D(w, 1.0f);
        if (std::abs(clip.w()) < 1e-8f) return false;
        const QVector3D ndc = (clip / clip.w()).toVector3D();
        outPt.setX((ndc.x() * 0.5f + 0.5f) * width());
        outPt.setY((1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        return true;
    };

    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor minorColor(150, 170, 205, 90);
    const QColor majorColor(205, 220, 245, 150);

    const float startMinorX = std::floor(minX / minorStep) * minorStep;
    const float endMinorX = std::ceil(maxX / minorStep) * minorStep;
    const float startMinorY = std::floor(minY / minorStep) * minorStep;
    const float endMinorY = std::ceil(maxY / minorStep) * minorStep;
    p.setPen(QPen(minorColor, 1.0));
    for (float x = startMinorX; x <= endMinorX + 1e-6f; x += minorStep) {
        QPointF a, b;
        if (project(QVector3D(x, minY, zPlane), a) && project(QVector3D(x, maxY, zPlane), b))
            p.drawLine(a, b);
    }
    for (float y = startMinorY; y <= endMinorY + 1e-6f; y += minorStep) {
        QPointF a, b;
        if (project(QVector3D(minX, y, zPlane), a) && project(QVector3D(maxX, y, zPlane), b))
            p.drawLine(a, b);
    }
    const float startMajorX = std::floor(minX / majorStep) * majorStep;
    const float endMajorX = std::ceil(maxX / majorStep) * majorStep;
    const float startMajorY = std::floor(minY / majorStep) * majorStep;
    const float endMajorY = std::ceil(maxY / majorStep) * majorStep;
    p.setPen(QPen(majorColor, 1.35));
    for (float x = startMajorX; x <= endMajorX + 1e-6f; x += majorStep) {
        QPointF a, b;
        if (project(QVector3D(x, minY, zPlane), a) && project(QVector3D(x, maxY, zPlane), b))
            p.drawLine(a, b);
    }
    for (float y = startMajorY; y <= endMajorY + 1e-6f; y += majorStep) {
        QPointF a, b;
        if (project(QVector3D(minX, y, zPlane), a) && project(QVector3D(maxX, y, zPlane), b))
            p.drawLine(a, b);
    }
}

void PCB3DView::drawMeasurementOverlay(QPainter& p) {
    if (!m_measureMode || !m_measureHasFirst) return;
    auto project = [&](const QVector3D& w, QPointF& outPt) -> bool {
        const QMatrix4x4 view = currentViewMatrix();
        const QVector4D clip = m_projection * view * QVector4D(w, 1.0f);
        if (std::abs(clip.w()) < 1e-8f) return false;
        const QVector3D ndc = (clip / clip.w()).toVector3D();
        outPt.setX((ndc.x() * 0.5f + 0.5f) * width());
        outPt.setY((1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        return true;
    };
    QPointF p1;
    if (!project(m_measureP1, p1)) return;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 212, 59), 2.0));
    p.setBrush(QBrush(QColor(255, 212, 59, 140)));
    p.drawEllipse(p1, 4, 4);
    if (!m_measureHasSecond) {
        p.setPen(QPen(QColor(255, 212, 59, 180), 1.0, Qt::DashLine));
        p.drawText(p1 + QPointF(8, -8), "Pick second point");
        return;
    }
    QPointF p2;
    if (!project(m_measureP2, p2)) return;
    p.drawEllipse(p2, 4, 4);
    p.setPen(QPen(QColor(255, 212, 59), 1.5));
    p.drawLine(p1, p2);
    const double dist = QLineF(
        QPointF(m_measureP1.x(), m_measureP1.y()),
        QPointF(m_measureP2.x(), m_measureP2.y())).length();
    const double ddx = std::abs(double(m_measureP2.x() - m_measureP1.x()));
    const double ddy = std::abs(double(m_measureP2.y() - m_measureP1.y()));
    const QString label = QStringLiteral("%1 mm  (dX %2 · dY %3)")
                              .arg(dist, 0, 'f', 3)
                              .arg(ddx, 0, 'f', 2)
                              .arg(ddy, 0, 'f', 2);
    const QPointF mid = (p1 + p2) * 0.5;
    QRectF textRect = p.fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
    textRect.moveCenter(mid + QPointF(0, -12));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 24, 31, 210));
    p.drawRoundedRect(textRect, 4, 4);
    p.setPen(QPen(QColor(255, 232, 140), 1.0));
    p.drawText(textRect, Qt::AlignCenter, label);
}

void PCB3DView::drawStatsOverlay(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    const float fps = m_frameEmaMs > 0.01f ? 1000.0f / m_frameEmaMs : 0.0f;
    const QString line1 = QStringLiteral("%1 FPS · %2 ms/frame")
                              .arg(fps, 0, 'f', 0)
                              .arg(m_frameEmaMs, 0, 'f', 1);
    const QString line2 = QStringLiteral("%1 tris · rebuild %2 ms · %3%4")
                              .arg(m_totalTriangles)
                              .arg(m_rebuildMs, 0, 'f', 1)
                              .arg(m_quality ? "fast" : "full")
                              .arg(m_enhancedLighting ? " · enhanced" : "");
    const QRectF box(10, 10, 260, 98);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(15, 20, 26, 190));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QColor(160, 220, 170));
    p.drawText(QRectF(18, 14, 244, 16), Qt::AlignLeft, line1);
    p.setPen(QColor(150, 175, 205));
    p.drawText(QRectF(18, 32, 244, 16), Qt::AlignLeft, line2);
    const QRectF graphBox(18, 54, 244, 32);
    p.setPen(QColor(70, 90, 110));
    p.setBrush(QColor(20, 28, 36, 210));
    p.drawRoundedRect(graphBox, 4, 4);
    if (m_frameGraph.size() > 1) {
        float maxMs = 33.0f;
        for (float v : m_frameGraph) maxMs = std::max(maxMs, v);
        const float targetY = graphBox.bottom() - (16.0f / maxMs) * graphBox.height();
        p.setPen(QPen(QColor(255, 210, 90, 140), 1.0, Qt::DotLine));
        p.drawLine(QPointF(graphBox.left(), targetY), QPointF(graphBox.right(), targetY));
        QVector<QPointF> pts;
        pts.reserve(m_frameGraph.size());
        for (int i = 0; i < m_frameGraph.size(); ++i) {
            const float x = graphBox.left() + graphBox.width() * (float(i) / float(m_frameGraph.size() - 1));
            const float y = graphBox.bottom() - std::clamp(m_frameGraph[i] / maxMs, 0.0f, 1.0f) * graphBox.height();
            pts.push_back(QPointF(x, y));
        }
        p.setPen(QPen(QColor(90, 220, 140), 1.6));
        p.drawPolyline(pts.constData(), int(pts.size()));
    }
    p.setPen(QColor(120, 140, 160));
    p.drawText(QRectF(18, 88, 244, 10), Qt::AlignLeft, QStringLiteral("green=frame time · yellow=16.6 ms"));
}

void PCB3DView::drawVignetteOverlay(QPainter& p) {
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0.0, QColor(255, 255, 255, 10));
    g.setColorAt(0.5, QColor(255, 255, 255, 0));
    g.setColorAt(1.0, QColor(0, 0, 0, 38));
    p.fillRect(rect(), g);
}

void PCB3DView::checkGl(const char* what) {
    GLenum err = glGetError();
    while (err != GL_NO_ERROR) {
        if (err != 1282) { // 1282 is Mesa software rasterizer non-fatal state notification
            qWarning() << "PCB3DView: GL error in" << what << ":" << err;
        }
        err = glGetError();
    }
}

// ============================================================================
