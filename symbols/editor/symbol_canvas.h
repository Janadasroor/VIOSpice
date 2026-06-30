/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_CANVAS_H
#define SYMBOL_CANVAS_H

#include "../symbol_editor_view.h"
#include "../models/symbol_definition.h"
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QList>
#include <QPointF>
#include <QMap>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class SymbolEditor;
class QAbstractGraphicsShapeItem;
class SymbolToolManager;

class SymbolCanvas : public SymbolEditorView {
    Q_OBJECT

public:
    explicit SymbolCanvas(SymbolEditor* editor, QWidget* parent = nullptr);
    ~SymbolCanvas() override;

    void setCurrentTool(int tool);

    QGraphicsScene* scene() const { return m_scene; }

    // Primitive visual management
    QGraphicsItem* buildVisual(const SymbolPrimitive& prim, int index) const;
    void updateVisualForPrimitive(int index, const SymbolPrimitive& prim);
    void applyShapeStyle(QAbstractGraphicsShapeItem* shape, const SymbolPrimitive& prim) const;
    int primitiveIndex(QGraphicsItem* item) const;

    void addVisualItem(QGraphicsItem* visual, int index);
    void removeVisualItem(int index);
    void insertVisualItem(int index, QGraphicsItem* visual);

    void clearScene();
    void updateOverlayLabels();
    void updateGuideAnchors();
    void updateResizeHandles();
    void clearResizeHandles();
    void removeOverlayItems();

    // Pen tool preview
    void updatePinPreview(QPointF pos);

    // Helpers
    QColor themeLineColor() const;
    QColor themeTextColor() const;
    QColor themePinLabelColor() const;

Q_SIGNALS:
    void primitivesChanged();

private Q_SLOTS:
    void onSelectionChanged();
    void onPointClicked(QPointF pos);
    void onMouseMoved(QPointF pos);
    void onDrawingFinished(QPointF start, QPointF end);

    // Pen tool
    void onPenPointAdded(QPointF pos);
    void onPenHandleDragged(QPointF handlePos);
    void onPenPointFinished();
    void onPenPathClosed();
    void finalizePenPath();
    void clearPenState();
    void updatePenPreview();
    void onPenClicked(QPointF pos, int pointIndex = -1, int handleIndex = -1);
    void onPenDoubleClicked(QPointF pos, int pointIndex = -1);

    // Bezier editing
    void onBezierEditPointClicked(QPointF pos);
    void onBezierEditPointDragged(QPointF newPos);
    void updateBezierEditPreview();

    // Rectangle resize
    void onRectResizeStarted(const QString& corner, QPointF scenePos);
    void onRectResizeUpdated(QPointF scenePos);
    void onRectResizeFinished(QPointF scenePos);

private:
    struct PenPoint {
        QPointF pos;           // Main anchor point
        QPointF handleIn;      // Control handle coming INTO this point (relative)
        QPointF handleOut;     // Control handle going OUT of this point (relative)
        bool smooth;           // Whether handles are locked (smooth curve)
        bool corner;           // True = corner point, False = curve point
    };

    QPointF calculateBezierPoint(const PenPoint& p1, const PenPoint& p2, qreal t) const;

    SymbolEditor* m_editor = nullptr;
    QGraphicsScene* m_scene = nullptr;

    // Drawing state
    QList<QPointF> m_polyPoints;
    QGraphicsItem* m_previewItem = nullptr;
    QString m_previewOrientation = "Right";
    QList<QGraphicsItem*> m_drawnItems;
    QList<QGraphicsItem*> m_overlayItems;

    // Pen tool state
    QList<PenPoint> m_penPoints;
    int m_selectedPenPoint = -1;           // Index of selected point
    int m_selectedPenHandle = -1;          // -1=none, 0=in, 1=out (for selected point)
    int m_selectedPenMidpoint = -1;        // Index of selected midpoint (segment edge point)
    QGraphicsPathItem* m_penPreviewItem = nullptr;
    QList<QGraphicsEllipseItem*> m_penPointMarkers;
    QList<QGraphicsLineItem*> m_penHandleLines;
    QList<QGraphicsEllipseItem*> m_penHandleDots;
    QList<QGraphicsEllipseItem*> m_penMidpointDots;   // Midpoint dots on segment edges
    bool m_penFinalizing = false;  // Guard against double finalization
    QPointF m_penLastClickPos;     // Track for detecting double-click
    double m_penDoubleClickTimeout = 300.0;  // milliseconds
    double m_penLastClickTime = 0.0;

    // Bezier edit state (Select mode)
    int m_editingBezierIndex = -1;                    // Index of bezier primitive being edited (-1 = none)
    struct BezierEditPoint {
        int pointType;  // 0=start, 1=cp1, 2=cp2, 3=end
        QPointF pos;
    };
    QList<BezierEditPoint> m_bezierEditPoints;        // Current bezier edit points for visualization
    QList<QGraphicsEllipseItem*> m_bezierEditMarkers; // Visual edit point markers
    QList<QGraphicsLineItem*> m_bezierEditLines;      // Handle lines
    int m_selectedBezierPoint = -1;                   // Which point is selected for dragging

    // Rectangle resize handles/session
    QList<QGraphicsRectItem*> m_resizeHandles;
    bool m_rectResizeSessionActive = false;
    int m_rectResizePrimIdx = -1;
    QString m_rectResizeCorner;
    QPointF m_rectResizeAnchor;
    QPointF m_resizeLineOtherEnd;
    QPointF m_resizeCircleCenter;
    SymbolDefinition m_rectResizeOldDef;

    SymbolToolManager* m_toolManager = nullptr;

    friend class SymbolEditor;
    friend class AddPrimitiveCommand;
    friend class RemovePrimitiveCommand;
    friend class UpdateSymbolCommand;
    friend class SymbolTool;
    friend class SelectTool;
    friend class DrawPinTool;
    friend class DrawLineTool;
    friend class DrawShapeTool;
    friend class SymbolToolManager;
};

#endif // SYMBOL_CANVAS_H
