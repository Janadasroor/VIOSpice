/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_pad_tool.h"
#include "pcb_view.h"
#include "pcb_item_factory.h"
#include <QDebug>
#include "pcb_commands.h"
#include <QUndoStack>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QKeyEvent>

PCBPadTool::PCBPadTool(QObject* parent)
    : PCBTool("Pad", parent)
    , m_previewPad(nullptr) {
}

QCursor PCBPadTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBPadTool::activate(PCBView* view) {
    PCBTool::activate(view);
    updatePreview();
}

void PCBPadTool::deactivate() {
    if (m_previewPad && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewPad);
        delete m_previewPad;
        m_previewPad = nullptr;
    }
    PCBTool::deactivate();
}

void PCBPadTool::updatePreview() {
    if (!view() || !view()->scene()) return;

    if (m_previewPad) {
        view()->scene()->removeItem(m_previewPad);
        delete m_previewPad;
        m_previewPad = nullptr;
    }

    auto& factory = PCBItemFactory::instance();
    m_previewPad = factory.createItem("Pad", QPointF(0, 0));
    if (m_previewPad) {
        m_previewPad->setOpacity(0.6);
        m_previewPad->setZValue(1000);
        m_previewPad->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_previewPad->setFlag(QGraphicsItem::ItemIsMovable, false);
        view()->scene()->addItem(m_previewPad);

        QPoint localPos = view()->mapFromGlobal(QCursor::pos());
        QPointF scenePos = view()->mapToScene(localPos);
        m_previewPad->setPos(view()->snapToGrid(scenePos));
    }
}

void PCBPadTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewPad && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        m_previewPad->setPos(view()->snapToGrid(scenePos));
        event->accept();
    }
}

void PCBPadTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }

    if (m_previewPad) {
        if (event->key() == Qt::Key_R) {
            m_previewPad->setRotation(m_previewPad->rotation() + 90);
            event->accept();
            return;
        }
    }

    PCBTool::keyPressEvent(event);
}

void PCBPadTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    if (event->button() == Qt::RightButton) {
        event->ignore(); // Let PCBView handle return to Select tool
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    auto& factory = PCBItemFactory::instance();
    PCBItem* pad = factory.createItem("Pad", snappedPos);
    if (pad) {
        if (m_previewPad) {
            pad->setRotation(m_previewPad->rotation());
        }
        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), pad));
        } else {
            view()->scene()->addItem(pad);
        }
        qDebug() << "Placed pad at" << snappedPos;
        
        // RE-FOCUS view after adding item to scene
        view()->setFocus();
    } else {
        qWarning() << "Failed to create pad item";
    }
    event->accept();
}
