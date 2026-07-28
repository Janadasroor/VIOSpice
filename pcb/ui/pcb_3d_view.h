/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PCB_3D_VIEW_H
#define PCB_3D_VIEW_H

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGraphicsScene>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QUuid>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QColor>
#include <QLibrary>

class PCB3DView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PCB3DView(QWidget* parent = nullptr);
    ~PCB3DView() override;

    void setScene(QGraphicsScene* scene);
    void updateScene();

    void setShowSubstrate(bool enabled);
    void setShowCopper(bool enabled);
    void setShowTopCopper(bool enabled);
    void setShowBottomCopper(bool enabled);
    void setShowSilkscreen(bool enabled);
    void setShowComponents(bool enabled);
    void setShowGrid(bool enabled);
    void setShowStats(bool enabled);
    void setSelectedOnly(bool enabled);
    void setNetFilter(const QString& netName);
    void setSubstrateAlpha(float alpha);
    void setEnhancedLighting(bool enabled);
    void setOrthographic(bool enabled);
    void setExplodeAmount(float mm);
    void setSoldermaskAlpha(float alpha);
    void setSoldermaskColor(const QColor& color);
    void setCopperTopColor(const QColor& color);
    void setCopperBottomColor(const QColor& color);
    void setComponentColor(const QColor& color);
    void setComponentAlpha(float alpha);
    void setZoomDistance(float distance);
    float zoomDistance() const { return -m_zoom; }
    void setMeasureMode(bool enabled);
    void clearMeasurement();
    bool setSpaceMouseEnabled(bool enabled);
    int detectComponentCollisions();
    void resetCamera();
    void fitBoard();
    void focusComponent(const QUuid& id);
    void setViewPreset(const QString& preset);
    void setFrustumCullingEnabled(bool enabled);
    void setHoverInfoEnabled(bool enabled);
    void saveViewBookmark(int slot);
    void loadViewBookmark(int slot);

signals:
    void componentPicked(const QUuid& id);
    void measurementUpdated(double distanceMm);
    void zoomDistanceChanged(float distance);
    void statusUpdated(const QString& message);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class RenderMode { Full, Fast };
    enum class MaterialKind { SolderMask, Dielectric, CopperTop, CopperBottom, CopperInner, Plating, Silkscreen, Plastic, ComponentMetal, ComponentLED, Collision, AxisX, AxisY, AxisZ };
    enum class LayerFlag { Substrate, Dielectric, CopperTop, CopperInner, CopperBottom, Plating, Silkscreen, Component };

public:
    struct Vertex {
        QVector3D pos;
        QVector3D nrm;
    };
    struct TriMesh2D {
        QVector<QPointF> pts;
        QVector<int> idx;
    };

private:
    struct PickProxy {
        QUuid id;
        QVector3D bmin;
        QVector3D bmax;
    };
    struct ComponentDraw {
        QUuid id;
        MaterialKind material = MaterialKind::Plastic;
        float alpha = 1.0f;
        int batchIndex = -1;
        QMatrix4x4 placement;
        QVector3D wbmin;
        QVector3D wbmax;
    };
    struct GpuBatch {
        int first = 0;
        int count = 0;
        MaterialKind material = MaterialKind::Plastic;
        LayerFlag layer = LayerFlag::Substrate;
        bool castShadow = false;
        int componentIndex = -1;
        QVector3D bmin = QVector3D(0.0f, 0.0f, 0.0f);
        QVector3D bmax = QVector3D(0.0f, 0.0f, 0.0f);
    };
    struct ObjMesh {
        QVector<Vertex> vertices;
    };
    struct MeshBake {
        QVector<Vertex> vertices;
        QVector3D bmin;
        QVector3D bmax;
    };
    struct Frustum {
        QVector3D n[6];
        float d[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        bool intersectsAabb(const QVector3D& bmin, const QVector3D& bmax) const {
            for (int i = 0; i < 6; ++i) {
                const QVector3D p(
                    n[i].x() >= 0.0f ? bmax.x() : bmin.x(),
                    n[i].y() >= 0.0f ? bmax.y() : bmin.y(),
                    n[i].z() >= 0.0f ? bmax.z() : bmin.z());
                if (QVector3D::dotProduct(n[i], p) + d[i] < 0.0f) return false;
            }
            return true;
        }
    };
    struct PickGrid {
        bool valid = false;
        QVector3D bmin = QVector3D(0.0f, 0.0f, 0.0f);
        QVector3D bmax = QVector3D(0.0f, 0.0f, 0.0f);
        float cell = 25.0f;
        int nx = 1, ny = 1;
        QVector<QVector<int>> cells;
    };

    void initShaders();
    void initPickShader();
    void initShadowMap();
    void ensurePickFbo();
    void rebuildSceneCache();
    void updateProjectionMatrix();
    void applyMaterial(MaterialKind material, float alpha, bool hovered);
    bool batchVisible(const GpuBatch& b) const;
    void drawAxisTriadOverlay(QPainter& p);
    void drawGridOverlay(QPainter& p);
    void drawMeasurementOverlay(QPainter& p);
    void drawStatsOverlay(QPainter& p);
    void drawVignetteOverlay(QPainter& p);
    void drawHoverOverlay(QPainter& p);
    bool passesSelectionFilter(QGraphicsItem* item) const;

    ObjMesh loadOBJ(const QString& path);
    ObjMesh loadObjMeshFromText(const QString& text) const;
    ObjMesh loadVrmlMeshFromText(const QString& text) const;
    ObjMesh loadStepMeshFromText(const QString& text) const;
    ObjMesh loadIgesMeshFromText(const QString& text) const;
    QString expandModelEnvVars(const QString& rawPath) const;
    QStringList modelSearchRoots() const;
    QString resolveModelPath(const QString& rawPath) const;
    const MeshBake& bakeMesh(const QString& key, const QString& path,
                             const QVector3D& scale, const QVector3D& rotDeg);
    QVector<Vertex> makeBoxVertices(float hx, float hy, float hz) const;

    bool pickAt(const QPoint& pos, QUuid& outId) const;
    bool cpuPickAt(const QPoint& pos, QUuid& outId) const;
    bool gpuPickAt(const QPoint& pos, QUuid& outId);
    bool intersectBoardPlane(const QPoint& pos, QVector3D& out) const;
    QVector3D unprojectToWorld(const QPoint& p, float ndcZ) const;
    bool rayIntersectsAabb(const QVector3D& ro, const QVector3D& rd,
                           const QVector3D& bmin, const QVector3D& bmax,
                           float& outT) const;
    void updateHover(const QPoint& pos);

    QMatrix4x4 currentViewMatrix() const;
    QMatrix4x4 currentShadowMatrix() const;

    void beginInteractiveRender();
    void pollSpaceMouse();
    void tickCameraAnimation();
    void tickSpinAnimation();
    void tickInertia();
    void startCameraTransition(const QQuaternion& targetRot, float targetZoom,
                               const QVector3D& targetPan);
    bool handleTriadClick(const QPoint& pos);
    void checkGl(const char* what);
    Frustum extractFrustum(const QMatrix4x4& vp) const;
    void rebuildPickGrid();
    bool cpuPickGridAt(const QPoint& pos, QUuid& outId) const;
    bool projectWorldToScreen(const QVector3D& worldPos, QPointF& outScreen) const;
    void drawHoverOverlay();
    static void computeBatchAabb(const QVector<Vertex>& verts,
                                 int first, int count,
                                 QVector3D& bmin, QVector3D& bmax);

    QGraphicsScene* m_scene = nullptr;
    QOpenGLShaderProgram m_shader;
    QOpenGLShaderProgram m_pickShader;
    QOpenGLShaderProgram m_shadowShader;
    QOpenGLBuffer m_staticVbo{QOpenGLBuffer::VertexBuffer};
    mutable QOpenGLFramebufferObject* m_pickFbo = nullptr;
    mutable QOpenGLFramebufferObject* m_shadowFbo = nullptr;
    bool m_glSupported = true;

    QVector<GpuBatch> m_batches;
    QVector<ComponentDraw> m_componentDraws;
    QVector<PickProxy> m_pickProxies;
    QHash<QString, MeshBake> m_meshBakeCache;
    QMap<QString, ObjMesh> m_objCache;
    QSet<QUuid> m_collidedComponents;
    mutable QHash<int, QUuid> m_pickIdToUuid;

    TriMesh2D m_boardTri;
    QPolygonF m_boardOuterWall;
    QList<QPolygonF> m_boardHoleWalls;

    QVector3D m_boundsMin = QVector3D(0, 0, 0);
    QVector3D m_boundsMax = QVector3D(0, 0, 0);
    int m_totalTriangles = 0;

    bool m_sceneDirty = true;
    bool m_showSubstrate = true;
    bool m_showCopperTop = true;
    bool m_showCopperBottom = true;
    bool m_showSilkscreen = true;
    bool m_showComponents = true;
    bool m_showGrid = false;
    bool m_showStats = false;
    bool m_selectedOnly = false;
    bool m_enhancedLighting = false;
    bool m_orthographic = false;
    float m_explodeAmount = 0.0f;
    QString m_netFilter;
    float m_substrateAlpha = 1.0f;
    float m_soldermaskAlpha = 1.0f;
    float m_componentAlpha = 1.0f;
    QColor m_soldermaskColor = QColor(38, 132, 76);
    QColor m_copperTopColor = QColor(212, 71, 51);
    QColor m_copperBottomColor = QColor(46, 107, 219);
    QColor m_componentColor = QColor(82, 86, 96);
    RenderMode m_renderMode = RenderMode::Full;
    int m_quality = 0;
    float m_frameEmaMs = 16.0f;
    float m_rebuildMs = 0.0f;

    QTimer m_interactionTimer;
    QQuaternion m_rotation;
    QVector3D m_pan = QVector3D(0, 0, 0);
    float m_zoom = -300.0f;
    QPoint m_lastPos;
    QPoint m_pressPos;
    bool m_leftPressed = false;
    float m_velYaw = 0.0f;
    float m_velPitch = 0.0f;
    QTimer m_inertiaTimer;
    QUuid m_hoverId;
    QElapsedTimer m_hoverThrottle;
    Frustum m_frustum;
    PickGrid m_pickGrid;
    QHash<QUuid, QString> m_componentRefs;
    bool m_enableFrustumCulling = true;
    bool m_showHoverInfo = true;
    QVector<float> m_frameGraph;
    QHash<int, QQuaternion> m_bookmarkRot;
    QHash<int, QVector3D> m_bookmarkPan;
    QHash<int, float> m_bookmarkZoom;

    bool m_measureMode = false;
    bool m_measureHasFirst = false;
    bool m_measureHasSecond = false;
    QVector3D m_measureP1;
    QVector3D m_measureP2;

    QTimer m_cameraAnimTimer;
    QTimer m_spinTimer;
    float m_spinSpeedDeg = 0.0f;
    float m_cameraAnimT = 1.0f;
    QQuaternion m_rotFrom;
    QQuaternion m_rotTo;
    float m_zoomFrom = -300.0f;
    float m_zoomTo = -300.0f;
    QVector3D m_panFrom;
    QVector3D m_panTo;

    QTimer m_spaceMousePollTimer;
    QLibrary m_spaceMouseLib;
    bool m_spaceMouseEnabled = false;
    bool m_spaceMouseConnected = false;
    int (*m_spnavOpen)() = nullptr;
    int (*m_spnavClose)() = nullptr;
    int (*m_spnavPollEvent)(void*) = nullptr;

    QMatrix4x4 m_projection;
    QElapsedTimer m_frameTimer;
};

#endif // PCB_3D_VIEW_H
