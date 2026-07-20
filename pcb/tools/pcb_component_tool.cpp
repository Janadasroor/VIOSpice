/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_component_tool.h"
#include "pcb_view.h"
#include "pcb_item_factory.h"
#include "pcb_commands.h"
#include <QGraphicsScene>
#include <QJsonObject>
#include <QDebug>
#include <QUndoStack>

PCBComponentTool::PCBComponentTool(QObject* parent)
    : PCBTool("Component", parent)
    , m_componentType("IC")
    , m_previewItem(nullptr) {
}

QCursor PCBComponentTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBComponentTool::activate(PCBView* view) {
    PCBTool::activate(view);
    updatePreview();
}

void PCBComponentTool::deactivate() {
    if (m_previewItem && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    PCBTool::deactivate();
}

void PCBComponentTool::setComponentType(const QString& type) {
    if (m_componentType == type) return;
    m_componentType = type;
    updatePreview();
}

void PCBComponentTool::updatePreview() {
    if (!view() || !view()->scene()) return;

    if (m_previewItem) {
        view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }

    auto& factory = PCBItemFactory::instance();
    QJsonObject properties;
    properties["componentType"] = m_componentType;
    
    m_previewItem = factory.createItem("Component", QPointF(0, 0), properties);
    if (m_previewItem) {
        m_previewItem->setOpacity(0.5);
        m_previewItem->setZValue(1000); // Always on top
        // Disable selection and interaction for preview item
        m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
        view()->scene()->addItem(m_previewItem);
        
        // Move to current mouse position if possible
        QPoint localPos = view()->mapFromGlobal(QCursor::pos());
        QPointF scenePos = view()->mapToScene(localPos);
        m_previewItem->setPos(view()->snapToGrid(scenePos));
    }
}

void PCBComponentTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewItem && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        m_previewItem->setPos(view()->snapToGrid(scenePos));
        event->accept();
    }
}

void PCBComponentTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }

    if (m_previewItem) {
        if (event->key() == Qt::Key_R) {
            m_previewItem->setRotation(m_previewItem->rotation() + 90);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_H || event->key() == Qt::Key_M) {
            m_previewItem->setTransform(QTransform().scale(-1, 1), true);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F) {
            m_previewItem->setLayer(m_previewItem->layer() == 0 ? 1 : 0);
            event->accept();
            return;
        }
    }

    PCBTool::keyPressEvent(event);
}

void PCBComponentTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    if (event->button() == Qt::RightButton) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    auto& factory = PCBItemFactory::instance();
    QJsonObject properties;
    properties["componentType"] = m_componentType;

    PCBItem* component = factory.createItem("Component", snappedPos, properties);
    if (component) {
        if (m_previewItem) {
            component->setRotation(m_previewItem->rotation());
            component->setTransform(m_previewItem->transform());
            component->setLayer(m_previewItem->layer());
        }
        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), component));
        } else {
            view()->scene()->addItem(component);
        }
        qDebug() << "Placed" << m_componentType << "component at" << snappedPos;
        
        // RE-FOCUS view after adding item to scene
        view()->setFocus();
    }
    event->accept();
}
