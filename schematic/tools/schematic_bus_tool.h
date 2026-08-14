/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATICBUSTOOL_H
#define SCHEMATICBUSTOOL_H

#include "schematic_tool.h"

class BusItem;

class SchematicBusTool : public SchematicTool {
    Q_OBJECT

public:
    SchematicBusTool(QObject* parent = nullptr);

    // SchematicTool interface
    QString tooltip() const override { return "Group multiple signals into a bus"; }
    QString iconName() const override { return "bus"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    void activate(SchematicView* view) override;
    void deactivate() override;

    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

private:
    enum RoutingMode {
        ManhattanMode,
        FortyFiveMode
    };

    void finishBus();
    void updatePreview();
    void reset();
    void undoLastSegment();
    QList<QPointF> computeRoutePoints(const QPointF& start, const QPointF& target) const;
    QPointF snapPosition(const QPointF& scenePos, bool* outSnappedToItem = nullptr) const;

    BusItem* m_currentBus;
    bool m_isDrawing;
    bool m_hFirst;
    RoutingMode m_routingMode;
    QList<QPointF> m_committedPoints;
    QPointF m_lastSnappedPos;

    double m_width;
    QString m_color;
    QString m_style;
};

#endif // SCHEMATICBUSTOOL_H
