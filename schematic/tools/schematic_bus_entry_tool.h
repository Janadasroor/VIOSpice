/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATICBUSENTRYTOOL_H
#define SCHEMATICBUSENTRYTOOL_H

#include "schematic_tool.h"

class BusEntryItem;
class BusItem;

class SchematicBusEntryTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicBusEntryTool(QObject* parent = nullptr);
    ~SchematicBusEntryTool() override;
    
    QString tooltip() const override { return "Connect wire to bus via 45-degree angled entry (Space to flip, R to rotate)"; }
    QString iconName() const override { return "bus_entry"; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    void activate(SchematicView* view) override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct PlacementTarget {
        QPointF center;
        bool flipped = false;
        qreal rotation = 0.0;
        BusItem* attachedBus = nullptr;
        QPointF busContactPoint;
    };

    PlacementTarget calculateTarget(const QPointF& scenePos) const;
    void updatePreview(const QPointF& scenePos);

    BusEntryItem* m_previewItem = nullptr;
    bool m_manualFlipped = false;
    qreal m_manualRotation = 0.0;
};

#endif // SCHEMATICBUSENTRYTOOL_H
