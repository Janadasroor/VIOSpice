/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_TOOL_MANAGER_H
#define SYMBOL_TOOL_MANAGER_H

#include <QObject>
#include <QPointF>
#include <QString>
#include <QList>
#include <QMap>

class SymbolCanvas;

class SymbolTool : public QObject {
    Q_OBJECT
public:
    explicit SymbolTool(SymbolCanvas* canvas, QObject* parent = nullptr);
    ~SymbolTool() override = default;

    virtual void activate() {}
    virtual void deactivate() {}

    virtual void onPointClicked(QPointF pos) { Q_UNUSED(pos); }
    virtual void onMouseMoved(QPointF pos) { Q_UNUSED(pos); }
    virtual void onDrawingFinished(QPointF start, QPointF end) { Q_UNUSED(start); Q_UNUSED(end); }
    virtual void onSelectionChanged() {}

    // Pen tool events
    virtual void onPenPointAdded(QPointF pos) { Q_UNUSED(pos); }
    virtual void onPenHandleDragged(QPointF handlePos) { Q_UNUSED(handlePos); }
    virtual void onPenPointFinished() {}
    virtual void onPenPathClosed() {}
    virtual void onPenClicked(QPointF pos, int pointIndex = -1, int handleIndex = -1) { Q_UNUSED(pos); Q_UNUSED(pointIndex); Q_UNUSED(handleIndex); }
    virtual void onPenDoubleClicked(QPointF pos, int pointIndex = -1) { Q_UNUSED(pos); Q_UNUSED(pointIndex); }

    // Bezier editing
    virtual void onBezierEditPointClicked(QPointF pos) { Q_UNUSED(pos); }
    virtual void onBezierEditPointDragged(QPointF newPos) { Q_UNUSED(newPos); }

    // Rect resize
    virtual void onRectResizeStarted(const QString& corner, QPointF scenePos) { Q_UNUSED(corner); Q_UNUSED(scenePos); }
    virtual void onRectResizeUpdated(QPointF scenePos) { Q_UNUSED(scenePos); }
    virtual void onRectResizeFinished(QPointF scenePos) { Q_UNUSED(scenePos); }

    // Pin orientation
    virtual void rotatePin() {}
    virtual void flipPin() {}

    // Items moved
    virtual void onItemsMoved(QPointF delta) { Q_UNUSED(delta); }

    // Right clicked
    virtual void onRightClicked() {}

protected:
    SymbolCanvas* m_canvas;
};

// SelectTool (for tool type 0)
class SelectTool : public SymbolTool {
    Q_OBJECT
public:
    using SymbolTool::SymbolTool;

    void onSelectionChanged() override;
    void onBezierEditPointClicked(QPointF pos) override;
    void onBezierEditPointDragged(QPointF newPos) override;
    void onRectResizeStarted(const QString& corner, QPointF scenePos) override;
    void onRectResizeUpdated(QPointF scenePos) override;
    void onRectResizeFinished(QPointF scenePos) override;
    void onItemsMoved(QPointF delta) override;
};

// DrawPinTool (for tool type 6)
class DrawPinTool : public SymbolTool {
    Q_OBJECT
public:
    using SymbolTool::SymbolTool;

    void activate() override;
    void deactivate() override;
    void onPointClicked(QPointF pos) override;
    void onMouseMoved(QPointF pos) override;
    void rotatePin() override;
    void flipPin() override;
};

// DrawLineTool (for tool types 1, 2, 3, 4)
class DrawLineTool : public SymbolTool {
    Q_OBJECT
public:
    using SymbolTool::SymbolTool;

    void onPointClicked(QPointF pos) override;
    void onMouseMoved(QPointF pos) override;
};

// DrawShapeTool (for tool types 5, 7, 8, 9, 10, 11, 13)
class DrawShapeTool : public SymbolTool {
    Q_OBJECT
public:
    using SymbolTool::SymbolTool;

    void onPointClicked(QPointF pos) override;
    void onMouseMoved(QPointF pos) override;
    void onDrawingFinished(QPointF start, QPointF end) override;
    
    // Pen tool events
    void onPenPointAdded(QPointF pos) override;
    void onPenHandleDragged(QPointF handlePos) override;
    void onPenPointFinished() override;
    void onPenPathClosed() override;
    void onPenClicked(QPointF pos, int pointIndex = -1, int handleIndex = -1) override;
    void onPenDoubleClicked(QPointF pos, int pointIndex = -1) override;

    void onRightClicked() override;
};

class SymbolToolManager : public QObject {
    Q_OBJECT
public:
    explicit SymbolToolManager(SymbolCanvas* canvas, QObject* parent = nullptr);
    ~SymbolToolManager() override;

    SymbolTool* activeTool() const { return m_activeTool; }
    void setActiveTool(int toolType);

private:
    SymbolCanvas* m_canvas;
    SymbolTool* m_activeTool;
    QMap<int, SymbolTool*> m_tools;
};

#endif // SYMBOL_TOOL_MANAGER_H
