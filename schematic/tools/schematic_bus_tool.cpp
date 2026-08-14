#include "schematic_bus_tool.h"
#include "bus_item.h"
#include "bus_entry_item.h"
#include "wire_item.h"
#include "schematic_view.h"
#include "schematic_editor.h"
#include "../editor/schematic_commands.h"
#include "config_manager.h"
#include <QMouseEvent>
#include <QGraphicsScene>
#include <cmath>

SchematicBusTool::SchematicBusTool(QObject* parent)
    : SchematicTool("Bus", parent), m_currentBus(nullptr), m_isDrawing(false),
      m_hFirst(true), m_routingMode(ManhattanMode) {
    
    auto& config = ConfigManager::instance();
    m_width = config.toolProperty("Bus", "Width", 3.5).toDouble();
    m_color = config.toolProperty("Bus", "Color", "#3b82f6").toString();
    m_style = config.toolProperty("Bus", "Line Style", "Solid").toString();
}

QMap<QString, QVariant> SchematicBusTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Width"] = m_width;
    props["Color"] = m_color;
    props["Line Style"] = m_style;
    return props;
}

void SchematicBusTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Width") m_width = value.toDouble();
    else if (name == "Color") m_color = value.toString();
    else if (name == "Line Style") m_style = value.toString();

    ConfigManager::instance().setToolProperty("Bus", name, value);
}

QCursor SchematicBusTool::cursor() const {
    return Qt::CrossCursor;
}

void SchematicBusTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    reset();
}

void SchematicBusTool::deactivate() {
    finishBus();
    SchematicTool::deactivate();
}

void SchematicBusTool::reset() {
    if (m_currentBus) {
        if (view() && view()->scene()) view()->scene()->removeItem(m_currentBus);
        delete m_currentBus;
        m_currentBus = nullptr;
    }
    m_isDrawing = false;
    m_committedPoints.clear();
}

QPointF SchematicBusTool::snapPosition(const QPointF& scenePos, bool* outSnappedToItem) const {
    if (outSnappedToItem) *outSnappedToItem = false;
    if (!view() || !view()->scene()) return scenePos;

    // 1. Check for pin, symbol port or wire junction snap
    auto snapResult = view()->snapToGridOrPin(scenePos);
    if (snapResult.type == SchematicView::PinSnap) {
        if (outSnappedToItem) *outSnappedToItem = true;
        return snapResult.point;
    }

    // 2. Check for nearby Bus lines or Bus Entries
    constexpr qreal kSnapRadius = 18.0;
    qreal bestDistSq = kSnapRadius * kSnapRadius;
    QPointF bestPoint = snapResult.point;
    bool snapped = false;

    const QRectF searchRect(scenePos.x() - kSnapRadius, scenePos.y() - kSnapRadius,
                           kSnapRadius * 2.0, kSnapRadius * 2.0);
    for (QGraphicsItem* gi : view()->scene()->items(searchRect)) {
        if (gi == m_currentBus) continue;

        if (auto* bus = dynamic_cast<BusItem*>(gi)) {
            qreal dSq = 0.0;
            QPointF proj = bus->closestPointOnBus(scenePos, &dSq);
            if (dSq < bestDistSq) {
                bestDistSq = dSq;
                bestPoint = proj;
                snapped = true;
            }
        } else if (auto* entry = dynamic_cast<BusEntryItem*>(gi)) {
            for (const QPointF& p : { entry->sceneP1(), entry->sceneP2() }) {
                const QPointF diff = scenePos - p;
                const qreal dSq = diff.x() * diff.x() + diff.y() * diff.y();
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    bestPoint = p;
                    snapped = true;
                }
            }
        }
    }

    if (outSnappedToItem) *outSnappedToItem = snapped;
    return snapped ? bestPoint : snapResult.point;
}

QList<QPointF> SchematicBusTool::computeRoutePoints(const QPointF& start, const QPointF& target) const {
    QList<QPointF> pts;
    pts.append(start);

    if (start == target) return pts;

    const qreal dx = target.x() - start.x();
    const qreal dy = target.y() - start.y();

    if (std::abs(dx) < 0.5 || std::abs(dy) < 0.5) {
        pts.append(target);
        return pts;
    }

    if (m_routingMode == ManhattanMode) {
        if (m_hFirst) {
            pts.append(QPointF(target.x(), start.y()));
        } else {
            pts.append(QPointF(start.x(), target.y()));
        }
    } else { // FortyFiveMode
        if (std::abs(dx) > std::abs(dy)) {
            const qreal diagDx = (dx > 0) ? std::abs(dy) : -std::abs(dy);
            pts.append(QPointF(start.x() + diagDx, target.y()));
        } else {
            const qreal diagDy = (dy > 0) ? std::abs(dx) : -std::abs(dx);
            pts.append(QPointF(target.x(), start.y() + diagDy));
        }
    }

    pts.append(target);
    return pts;
}

void SchematicBusTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || !view()->scene()) return;

    if (event->button() == Qt::LeftButton) {
        bool snappedToItem = false;
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF snapped = snapPosition(scenePos, &snappedToItem);

        if (!m_isDrawing) {
            m_isDrawing = true;
            m_committedPoints.clear();
            m_committedPoints << snapped;
            m_currentBus = new BusItem(snapped, snapped);
            view()->scene()->addItem(m_currentBus);
        } else {
            QPointF start = m_committedPoints.last();
            QList<QPointF> segs = computeRoutePoints(start, snapped);
            for (int i = 1; i < segs.size(); ++i) {
                m_committedPoints.append(segs[i]);
            }

            // If user clicked directly on an existing bus segment or terminal, finish automatically
            if (snappedToItem && m_committedPoints.size() >= 2) {
                finishBus();
                return;
            }

            updatePreview();
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_isDrawing) {
            finishBus();
        } else {
            view()->setCurrentTool("Select");
        }
    }
}

void SchematicBusTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDrawing) {
        finishBus();
        event->accept();
    }
}

void SchematicBusTool::mouseMoveEvent(QMouseEvent* event) {
    QPointF scenePos = view()->mapToScene(event->pos());
    m_lastSnappedPos = snapPosition(scenePos);

    if (m_isDrawing) {
        updatePreview();
    }
}

void SchematicBusTool::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event)
}

void SchematicBusTool::undoLastSegment() {
    if (!m_isDrawing || m_committedPoints.size() <= 1) {
        reset();
        return;
    }

    m_committedPoints.removeLast();
    if (m_committedPoints.size() <= 1) {
        reset();
        return;
    }
    updatePreview();
}

void SchematicBusTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_isDrawing) {
            reset();
        } else if (view()) {
            view()->setCurrentTool("Select");
        }
    } else if (event->key() == Qt::Key_Space) {
        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+Space: toggle 90-degree vs 45-degree corner mode
            m_routingMode = (m_routingMode == ManhattanMode) ? FortyFiveMode : ManhattanMode;
        } else {
            // Space: toggle H-V vs V-H routing
            m_hFirst = !m_hFirst;
        }
        if (m_isDrawing) updatePreview();
    } else if (event->key() == Qt::Key_Backspace || (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier))) {
        undoLastSegment();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishBus();
    }
}

void SchematicBusTool::updatePreview() {
    if (!view() || !m_isDrawing || !m_currentBus || m_committedPoints.isEmpty()) return;

    QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
    QPointF snapped = snapPosition(view()->mapToScene(scenePos.toPoint()));
    
    QList<QPointF> previewPoints = m_committedPoints;
    QPointF start = m_committedPoints.last();
    QList<QPointF> routeSegs = computeRoutePoints(start, snapped);
    for (int i = 1; i < routeSegs.size(); ++i) {
        previewPoints.append(routeSegs[i]);
    }
    
    m_currentBus->setPoints(previewPoints);
}

void SchematicBusTool::finishBus() {
    if (!view() || !view()->scene()) return;

    if (m_isDrawing && m_currentBus) {
        QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
        QPointF snapped = snapPosition(view()->mapToScene(scenePos.toPoint()));
        
        QList<QPointF> finalPoints = m_committedPoints;
        QPointF start = m_committedPoints.last();
        if (start != snapped) {
            QList<QPointF> routeSegs = computeRoutePoints(start, snapped);
            for (int i = 1; i < routeSegs.size(); ++i) {
                finalPoints.append(routeSegs[i]);
            }
        }

        // Simplify redundant co-linear points
        QList<QPointF> cleanPoints;
        for (const QPointF& pt : finalPoints) {
            if (cleanPoints.size() >= 2) {
                const QPointF p0 = cleanPoints[cleanPoints.size() - 2];
                const QPointF p1 = cleanPoints.last();
                const QLineF l1(p0, p1);
                const QLineF l2(p1, pt);
                if (std::abs(l1.angle() - l2.angle()) < 0.1 && l1.length() > 0.1 && l2.length() > 0.1) {
                    cleanPoints.last() = pt;
                    continue;
                }
            }
            if (cleanPoints.isEmpty() || (cleanPoints.last() - pt).manhattanLength() > 0.5) {
                cleanPoints.append(pt);
            }
        }

        if (cleanPoints.size() >= 2) {
            BusItem* bus = new BusItem();
            bus->setPoints(cleanPoints);
            
            // Connect junctions with any intersecting buses or bus entries
            for (const QPointF& pt : cleanPoints) {
                for (QGraphicsItem* gi : view()->scene()->items(QRectF(pt.x() - 15, pt.y() - 15, 30, 30))) {
                    if (auto* otherBus = dynamic_cast<BusItem*>(gi)) {
                        if (otherBus != bus && otherBus->isNearBus(pt, 3.0)) {
                            otherBus->addJunction(pt);
                            bus->addJunction(pt);
                        }
                    } else if (auto* entry = dynamic_cast<BusEntryItem*>(gi)) {
                        if ((entry->sceneP1() - pt).manhattanLength() < 4.0 ||
                            (entry->sceneP2() - pt).manhattanLength() < 4.0) {
                            bus->addJunction(pt);
                        }
                    }
                }
            }

            if (view()->undoStack()) {
                view()->undoStack()->push(new AddItemCommand(view()->scene(), bus));
            } else {
                view()->scene()->addItem(bus);
            }
        }
    }
    reset();
}
