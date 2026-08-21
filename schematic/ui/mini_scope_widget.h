/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINI_SCOPE_WIDGET_H
#define MINI_SCOPE_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>

#include <QMap>
#include <QString>

/**
 * @brief A lightweight waveform preview widget for the Logic Editor.
 */
class MiniScopeWidget : public QWidget {
    Q_OBJECT
public:
    explicit MiniScopeWidget(QWidget* parent = nullptr);

    /**
     * @brief Updates the waveform data for multiple traces.
     */
    void setMultiTraceData(const QMap<QString, QVector<QPointF>>& traces, const QMap<QString, QColor>& colors = {});
    void appendMultiTraceData(const QMap<QString, QVector<QPointF>>& traces, const QMap<QString, QColor>& colors = {});
    void setData(const QVector<QPointF>& points);
    
    /**
     * @brief Snapshot current traces and store in memory.
     */
    void freezeCurrentTraces();
    
    /**
     * @brief Clear all frozen traces.
     */
    void clearMemories();
    
    void clear();

    /**
     * @brief Render the oscilloscope waveform screen to an image (used for CLI, export, or AI visual inspection).
     */
    void renderToPainter(QPainter& painter, const QSize& size);
    QImage renderToImage(const QSize& size = QSize(1000, 600));

public:
    enum CursorMode {
        CursorNone = 0,
        CursorTime = 1,       // Vertical cursors measuring dt, 1/dt
        CursorVoltage = 2,    // Horizontal cursors measuring dV
        CursorBoth = 3
    };

    CursorMode cursorMode() const { return m_cursorMode; }
    void setCursorMode(CursorMode mode);
    
    double cursorTimeA() const { return m_timeCursorA; }
    double cursorTimeB() const { return m_timeCursorB; }
    double cursorVoltA() const { return m_voltCursorA; }
    double cursorVoltB() const { return m_voltCursorB; }
    
    void setCursorTimeA(double t) { m_timeCursorA = t; update(); }
    void setCursorTimeB(double t) { m_timeCursorB = t; update(); }
    void setCursorVoltA(double v) { m_voltCursorA = v; update(); }
    void setCursorVoltB(double v) { m_voltCursorB = v; update(); }

    void exportToCsv(const QString& filePath) const;

    void zoomToFit();
    void fitYAxis();

Q_SIGNALS:
    void cursorsChanged(double dt, double freq, double dv);
    void propertiesRequested();
    void zoomToFitRequested();
    void fitYAxisRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct TraceData {
        QVector<QPointF> points;
        QColor color;
        double minV = 0.0;
        double maxV = 0.0;
        double rms = 0.0;
        double freq = 0.0;
    };

    void calculateMeasurements(const QString& name, const QVector<QPointF>& points);
    
    QMap<QString, TraceData> m_traces;
    QList<QMap<QString, TraceData>> m_memories;
    double m_globalMinY = -1.0;
    double m_globalMaxY = 1.0;
    double m_minX = 0.0;
    double m_maxX = 0.02;

    // Cursors state
    CursorMode m_cursorMode = CursorNone;
    double m_timeCursorA = 0.0;
    double m_timeCursorB = 0.0;
    double m_voltCursorA = 0.0;
    double m_voltCursorB = 0.0;
    bool m_cursorsInitialized = false;

    enum ActiveDrag {
        DragNone,
        DragTimeA,
        DragTimeB,
        DragVoltA,
        DragVoltB
    };
    ActiveDrag m_activeDrag = DragNone;
};

#endif // MINI_SCOPE_WIDGET_H
