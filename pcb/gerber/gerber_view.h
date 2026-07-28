/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GERBER_VIEW_H
#define GERBER_VIEW_H

#include <QColor>
#include <QGraphicsView>
#include <QGraphicsScene>
#include "gerber_layer.h"

/**
 * @brief High-performance renderer for Gerber layers
 */
class GerberView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GerberView(QWidget* parent = nullptr);
    
    void addLayer(GerberLayer* layer);
    void setLayers(const QList<GerberLayer*>& layers);
    void clear();
    void zoomFit();
    void setBackgroundColor(const QColor& color);
    void setMonochrome(bool enabled);
    void setWireframeMode(bool enabled);
    void setShowDCodes(bool enabled);
    void setMeasureMode(bool enabled);

    bool wireframeMode() const { return m_wireframeMode; }
    bool showDCodes() const { return m_showDCodes; }
    bool measureMode() const { return m_measureMode; }

    QRectF plotBounds() const;
    QColor backgroundColor() const { return m_backgroundColor; }

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuildScene();
    bool isEdgeLayer(const GerberLayer* layer) const;
    bool isDrillLayer(const GerberLayer* layer) const;
    QColor displayColorForLayer(const GerberLayer* layer) const;
    QPainterPath boardOutlinePath() const;
    void renderLayer(GerberLayer* layer);

    QGraphicsScene* m_scene;
    QList<GerberLayer*> m_layers;
    QColor m_backgroundColor;
    bool m_monochrome = false;
    bool m_wireframeMode = false;
    bool m_showDCodes = false;
    bool m_measureMode = false;

    bool m_measureHasFirst = false;
    bool m_measureHasSecond = false;
    QPointF m_measureP1;
    QPointF m_measureP2;
    QPointF m_currentMousePos;

    bool m_isPanning;
    QPoint m_lastPanPoint;
};

#endif // GERBER_VIEW_H
