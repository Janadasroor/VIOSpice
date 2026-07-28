/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_via_tool.h"
#include "pcb_view.h"
#include "via_item.h"
#include "net_class.h"
#include "../layers/pcb_layer.h"
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QDebug>
#include "config_manager.h"
#include "pcb_commands.h"
#include <QUndoStack>
#include <algorithm>

namespace {
int copperLayerOrderIndex(int layerId) {
    if (layerId == PCBLayerManager::TopCopper) return 0;
    if (layerId >= 100) return 1 + (layerId - 100);
    if (layerId == PCBLayerManager::BottomCopper) return 1000;
    return layerId;
}

QString findNetAt(QGraphicsScene* scene, const QPointF& pos) {
    if (!scene) return "";
    QList<QGraphicsItem*> items = scene->items(pos);
    for (auto* item : items) {
        if (PCBItem* pcbItem = dynamic_cast<PCBItem*>(item)) {
            QString net = pcbItem->netName();
            if (!net.isEmpty() && net != "No Net") return net;
        }
    }
    return "";
}
} // namespace

PCBViaTool::PCBViaTool(QObject* parent)
    : PCBTool("Via", parent)
    , m_viaDiameter(0.8)   // Default 0.8mm via
    , m_holeDiameter(0.4)  // Default 0.4mm drill
    , m_startLayer(PCBLayerManager::TopCopper)
    , m_endLayer(PCBLayerManager::BottomCopper)
    , m_microviaMode(false)
    , m_previewVia(nullptr)
{
}

QCursor PCBViaTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBViaTool::activate(PCBView* view) {
    PCBTool::activate(view);
    updatePreview();
}

void PCBViaTool::deactivate() {
    if (m_previewVia && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewVia);
        delete m_previewVia;
        m_previewVia = nullptr;
    }
    PCBTool::deactivate();
}

void PCBViaTool::updatePreview() {
    if (!view() || !view()->scene()) return;

    if (m_previewVia) {
        view()->scene()->removeItem(m_previewVia);
        delete m_previewVia;
        m_previewVia = nullptr;
    }

    m_previewVia = new ViaItem(QPointF(0, 0), m_viaDiameter);
    m_previewVia->setDrillSize(m_holeDiameter);
    m_previewVia->setStartLayer(m_startLayer);
    m_previewVia->setEndLayer(m_endLayer);
    m_previewVia->setMicrovia(m_microviaMode);
    m_previewVia->setLayer(m_startLayer);

    m_previewVia->setOpacity(0.6);
    m_previewVia->setZValue(1000);
    m_previewVia->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_previewVia->setFlag(QGraphicsItem::ItemIsMovable, false);
    view()->scene()->addItem(m_previewVia);

    QPoint localPos = view()->mapFromGlobal(QCursor::pos());
    QPointF scenePos = view()->mapToScene(localPos);
    m_previewVia->setPos(view()->snapToGrid(scenePos));
}

void PCBViaTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewVia && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        m_previewVia->setPos(view()->snapToGrid(scenePos));
        event->accept();
    }
}

void PCBViaTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }
    PCBTool::keyPressEvent(event);
}

void PCBViaTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    if (event->button() == Qt::RightButton) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    // Smart Design Rules: Auto-select size based on Net
    QString net = findNetAt(view()->scene(), snappedPos);
    double diameter = m_viaDiameter;
    double drill = m_holeDiameter;

    if (!net.isEmpty()) {
        NetClass nc = NetClassManager::instance().getClassForNet(net);
        diameter = nc.viaDiameter;
        drill = nc.viaDrill;
    }

    // Create via at clicked position
    ViaItem* via = new ViaItem(snappedPos, diameter);
    via->setDrillSize(drill);
    via->setNetName(net);
    int start = m_startLayer;
    int end = m_endLayer;
    if (m_microviaMode) {
        // Microvia must span only adjacent copper layers.
        QList<PCBLayer*> copper = PCBLayerManager::instance().copperLayers();
        std::sort(copper.begin(), copper.end(), [](PCBLayer* a, PCBLayer* b) {
            return copperLayerOrderIndex(a->id()) < copperLayerOrderIndex(b->id());
        });
        int startIdx = 0;
        for (int i = 0; i < copper.size(); ++i) {
            if (copper[i] && copper[i]->id() == start) {
                startIdx = i;
                break;
            }
        }
        if (!copper.isEmpty()) {
            if (startIdx + 1 < copper.size()) end = copper[startIdx + 1]->id();
            else if (startIdx - 1 >= 0) end = copper[startIdx - 1]->id();
        }
    }
    via->setStartLayer(start);
    via->setEndLayer(end);
    via->setMicrovia(m_microviaMode);
    via->setLayer(m_startLayer);
    
    if (view()->undoStack()) {
        view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), via));
    } else {
        view()->scene()->addItem(via);
    }

    qDebug() << "Placed via at" << snappedPos;
    view()->setFocus();
    event->accept();
}

QMap<QString, QVariant> PCBViaTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Via Diameter (mm)"] = m_viaDiameter;
    props["Via Drill (mm)"] = m_holeDiameter;
    props["Start Layer"] = m_startLayer;
    props["End Layer"] = m_endLayer;
    props["Microvia"] = m_microviaMode;
    return props;
}

void PCBViaTool::setViaDiameter(double diameter) {
    m_viaDiameter = diameter;
    updatePreview();
}

void PCBViaTool::setHoleDiameter(double diameter) {
    m_holeDiameter = diameter;
    updatePreview();
}

void PCBViaTool::setStartLayer(int layer) {
    m_startLayer = layer;
    updatePreview();
}

void PCBViaTool::setEndLayer(int layer) {
    m_endLayer = layer;
    updatePreview();
}

void PCBViaTool::setMicroviaMode(bool enable) {
    m_microviaMode = enable;
    updatePreview();
}

void PCBViaTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Via Diameter (mm)") {
        m_viaDiameter = value.toDouble();
        ConfigManager::instance().setToolProperty("via_tool", "via_diameter", m_viaDiameter);
    } else if (name == "Via Drill (mm)") {
        m_holeDiameter = value.toDouble();
        ConfigManager::instance().setToolProperty("via_tool", "hole_diameter", m_holeDiameter);
    } else if (name == "Start Layer") {
        m_startLayer = value.toInt();
    } else if (name == "End Layer") {
        m_endLayer = value.toInt();
    } else if (name == "Microvia") {
        m_microviaMode = value.toBool() || value.toString() == "True";
        if (m_microviaMode) {
            m_viaDiameter = std::min(m_viaDiameter, 0.35);
            m_holeDiameter = std::min(m_holeDiameter, 0.15);
            ConfigManager::instance().setToolProperty("via_tool", "via_diameter", m_viaDiameter);
            ConfigManager::instance().setToolProperty("via_tool", "hole_diameter", m_holeDiameter);
        }
    }
    updatePreview();
}
