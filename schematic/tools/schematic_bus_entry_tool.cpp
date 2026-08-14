#include "schematic_bus_entry_tool.h"
#include "bus_entry_item.h"
#include "bus_item.h"
#include "wire_item.h"
#include "schematic_view.h"
#include "../editor/schematic_commands.h"
#include <QMouseEvent>
#include <QUndoStack>
#include <QGraphicsScene>
#include <limits>
#include <cmath>

SchematicBusEntryTool::SchematicBusEntryTool(QObject* parent)
    : SchematicTool("Bus Entry", parent), m_previewItem(nullptr), m_manualFlipped(false), m_manualRotation(0.0) {
}

SchematicBusEntryTool::~SchematicBusEntryTool() {
    deactivate();
}

void SchematicBusEntryTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    if (view && view->scene()) {
        if (!m_previewItem) {
            m_previewItem = new BusEntryItem(QPointF(0, 0), m_manualFlipped);
            m_previewItem->setOpacity(0.65);
            m_previewItem->setZValue(100);
            view->scene()->addItem(m_previewItem);
        }
        m_previewItem->setVisible(true);
        QPointF scenePos = view->mapFromGlobal(QCursor::pos());
        updatePreview(view->mapToScene(scenePos.toPoint()));
    }
}

void SchematicBusEntryTool::deactivate() {
    if (m_previewItem) {
        if (m_previewItem->scene()) {
            m_previewItem->scene()->removeItem(m_previewItem);
        }
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    SchematicTool::deactivate();
}

SchematicBusEntryTool::PlacementTarget SchematicBusEntryTool::calculateTarget(const QPointF& scenePos) const {
    PlacementTarget target;
    target.flipped = m_manualFlipped;
    target.rotation = m_manualRotation;
    target.center = view() ? view()->snapToGrid(scenePos) : scenePos;

    if (!view() || !view()->scene()) return target;

    // Search for nearby bus lines within snap radius
    constexpr qreal kBusSnapRadius = 32.0;
    qreal bestDistSq = kBusSnapRadius * kBusSnapRadius;
    BusItem* bestBus = nullptr;
    QPointF bestBusContact;
    int bestSegIdx = -1;

    const QRectF searchRect(scenePos.x() - kBusSnapRadius, scenePos.y() - kBusSnapRadius,
                           kBusSnapRadius * 2.0, kBusSnapRadius * 2.0);
    for (QGraphicsItem* gi : view()->scene()->items(searchRect)) {
        if (auto* bus = dynamic_cast<BusItem*>(gi)) {
            qreal dSq = 0.0;
            int segIdx = -1;
            QPointF proj = bus->closestPointOnBus(scenePos, &dSq, &segIdx);
            if (dSq < bestDistSq && segIdx >= 0) {
                bestDistSq = dSq;
                bestBus = bus;
                bestBusContact = proj;
                bestSegIdx = segIdx;
            }
        }
    }

    if (bestBus && bestSegIdx >= 0 && bestSegIdx + 1 < bestBus->points().size()) {
        const QPointF a = bestBus->points()[bestSegIdx];
        const QPointF b = bestBus->points()[bestSegIdx + 1];
        const QPointF snappedBusContact = view()->snapToGrid(bestBusContact);

        target.attachedBus = bestBus;
        target.busContactPoint = snappedBusContact;

        const bool isHorizontal = std::abs(b.y() - a.y()) <= std::abs(b.x() - a.x());

        if (isHorizontal) {
            target.rotation = 0.0;
            const bool cursorAbove = (scenePos.y() < snappedBusContact.y());
            const bool cursorLeft = (scenePos.x() < snappedBusContact.x());

            if (cursorAbove) {
                // Bottom endpoint touches bus
                if (cursorLeft) {
                    target.flipped = false; // "\" - P2 (10, 10) touches bus
                    target.center = QPointF(snappedBusContact.x() - 10, snappedBusContact.y() - 10);
                } else {
                    target.flipped = true;  // "/" - P1 (-10, 10) touches bus
                    target.center = QPointF(snappedBusContact.x() + 10, snappedBusContact.y() - 10);
                }
            } else {
                // Top endpoint touches bus
                if (cursorLeft) {
                    target.flipped = true;  // "/" - P2 (10, -10) touches bus
                    target.center = QPointF(snappedBusContact.x() - 10, snappedBusContact.y() + 10);
                } else {
                    target.flipped = false; // "\" - P1 (-10, -10) touches bus
                    target.center = QPointF(snappedBusContact.x() + 10, snappedBusContact.y() + 10);
                }
            }
        } else {
            // Vertical bus
            target.rotation = 0.0;
            const bool cursorLeft = (scenePos.x() < snappedBusContact.x());
            const bool cursorAbove = (scenePos.y() < snappedBusContact.y());

            if (cursorLeft) {
                // Right endpoint touches bus
                if (cursorAbove) {
                    target.flipped = true;  // "/" - P2 (10, -10) touches bus
                    target.center = QPointF(snappedBusContact.x() - 10, snappedBusContact.y() + 10);
                } else {
                    target.flipped = false; // "\" - P2 (10, 10) touches bus
                    target.center = QPointF(snappedBusContact.x() - 10, snappedBusContact.y() - 10);
                }
            } else {
                // Left endpoint touches bus
                if (cursorAbove) {
                    target.flipped = false; // "\" - P1 (-10, -10) touches bus
                    target.center = QPointF(snappedBusContact.x() + 10, snappedBusContact.y() + 10);
                } else {
                    target.flipped = true;  // "/" - P1 (-10, 10) touches bus
                    target.center = QPointF(snappedBusContact.x() + 10, snappedBusContact.y() - 10);
                }
            }
        }

        // Apply manual flip toggle inversion if user pressed space
        if (m_manualFlipped) {
            target.flipped = !target.flipped;
        }
    }

    return target;
}

void SchematicBusEntryTool::updatePreview(const QPointF& scenePos) {
    if (!m_previewItem) return;

    PlacementTarget target = calculateTarget(scenePos);
    m_previewItem->setPos(target.center);
    m_previewItem->setFlipped(target.flipped);
    m_previewItem->setRotation(target.rotation);
    m_previewItem->update();
}

void SchematicBusEntryTool::mouseMoveEvent(QMouseEvent* event) {
    if (!view()) return;
    QPointF scenePos = view()->mapToScene(event->pos());
    updatePreview(scenePos);
}

void SchematicBusEntryTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || !view()->scene()) return;

    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = view()->mapToScene(event->pos());
        PlacementTarget target = calculateTarget(scenePos);

        BusEntryItem* item = new BusEntryItem(target.center, target.flipped);
        item->setRotation(target.rotation);

        if (view()->undoStack()) {
            view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
        } else {
            view()->scene()->addItem(item);
        }

        // If placed on an attached bus, register junction dot on the bus
        if (target.attachedBus) {
            target.attachedBus->addJunction(target.busContactPoint);
        } else {
            // Check if either endpoint touches an existing bus
            for (QGraphicsItem* gi : view()->scene()->items(QRectF(target.center.x() - 20, target.center.y() - 20, 40, 40))) {
                if (auto* bus = dynamic_cast<BusItem*>(gi)) {
                    if (bus->isNearBus(item->sceneP1(), 3.0)) bus->addJunction(item->sceneP1());
                    if (bus->isNearBus(item->sceneP2(), 3.0)) bus->addJunction(item->sceneP2());
                }
            }
        }

        // Update preview item location for next placement
        updatePreview(scenePos);
    } else if (event->button() == Qt::RightButton) {
        // Right click finishes and returns to Select tool
        view()->setCurrentTool("Select");
    }
}

void SchematicBusEntryTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        m_manualFlipped = !m_manualFlipped;
        if (view()) {
            QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
            updatePreview(view()->mapToScene(scenePos.toPoint()));
        }
    } else if (event->key() == Qt::Key_R) {
        m_manualRotation = std::fmod(m_manualRotation + 90.0, 360.0);
        if (view()) {
            QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
            updatePreview(view()->mapToScene(scenePos.toPoint()));
        }
    } else if (event->key() == Qt::Key_Escape) {
        if (view()) {
            view()->setCurrentTool("Select");
        }
    }
}
