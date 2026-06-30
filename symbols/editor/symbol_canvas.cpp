/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_canvas.h"
#include "../symbol_editor.h"
#include "theme_manager.h"
#include "../symbol_commands.h"
#include "../../ui/property_editor.h"
#include "../ui/text_properties_dialog.h"
#include "../../core/visuals/text_resolver.h"

#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QMessageBox>
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <cmath>

// --- Helper classes to suppress default selection drawing ---
class FilteredRectItem : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsRectItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsRectItem::shape());
        hit.addPath(QGraphicsRectItem::shape());
        return hit;
    }
};

class FilteredEllipseItem : public QGraphicsEllipseItem {
public:
    using QGraphicsEllipseItem::QGraphicsEllipseItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsEllipseItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsEllipseItem::shape());
        hit.addPath(QGraphicsEllipseItem::shape());
        return hit;
    }
};

class FilteredLineItem : public QGraphicsLineItem {
public:
    using QGraphicsLineItem::QGraphicsLineItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsLineItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10); // 10px hit area
        return stroker.createStroke(QGraphicsLineItem::shape());
    }
};

class FilteredPathItem : public QGraphicsPathItem {
public:
    using QGraphicsPathItem::QGraphicsPathItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPathItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10); // 10px hit area
        return stroker.createStroke(QGraphicsPathItem::shape());
    }
};

class FilteredPolygonItem : public QGraphicsPolygonItem {
public:
    using QGraphicsPolygonItem::QGraphicsPolygonItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPolygonItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsPolygonItem::shape());
        hit.addPath(QGraphicsPolygonItem::shape());
        return hit;
    }
};

class FilteredGroupItem : public QGraphicsItemGroup {
public:
    using QGraphicsItemGroup::QGraphicsItemGroup;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsItemGroup::paint(p, &opt, w);
    }
};

class FilteredSimpleTextItem : public QGraphicsSimpleTextItem {
public:
    using QGraphicsSimpleTextItem::QGraphicsSimpleTextItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsSimpleTextItem::paint(p, &opt, w);
    }
};

class FilteredPixmapItem : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPixmapItem::paint(p, &opt, w);
    }
};

SymbolCanvas::SymbolCanvas(SymbolEditor* editor, QWidget* parent)
    : SymbolEditorView(parent), m_editor(editor) {
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-500, -500, 1000, 1000);
    setScene(m_scene);
    setGridSize(15.0);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &SymbolCanvas::onSelectionChanged);
    connect(this, &SymbolEditorView::pointClicked, this, &SymbolCanvas::onPointClicked);
    connect(this, &SymbolEditorView::mouseMoved, this, &SymbolCanvas::onMouseMoved);
    connect(this, &SymbolEditorView::drawingFinished, this, &SymbolCanvas::onDrawingFinished);

    // Pen tool signals
    connect(this, &SymbolEditorView::penPointAdded, this, &SymbolCanvas::onPenPointAdded);
    connect(this, &SymbolEditorView::penHandleDragged, this, &SymbolCanvas::onPenHandleDragged);
    connect(this, &SymbolEditorView::penPointFinished, this, &SymbolCanvas::onPenPointFinished);
    connect(this, &SymbolEditorView::penPathClosed, this, &SymbolCanvas::onPenPathClosed);
    connect(this, &SymbolEditorView::penClicked, this, &SymbolCanvas::onPenClicked);
    connect(this, &SymbolEditorView::penDoubleClicked, this, &SymbolCanvas::onPenDoubleClicked);
    
    // Bezier edit signals (Select mode)
    connect(this, &SymbolEditorView::bezierEditPointClicked, this, &SymbolCanvas::onBezierEditPointClicked);
    connect(this, &SymbolEditorView::bezierEditPointDragged, this, &SymbolCanvas::onBezierEditPointDragged);
    connect(this, &SymbolEditorView::rectResizeStarted, this, &SymbolCanvas::onRectResizeStarted);
    connect(this, &SymbolEditorView::rectResizeUpdated, this, &SymbolCanvas::onRectResizeUpdated);
    connect(this, &SymbolEditorView::rectResizeFinished, this, &SymbolCanvas::onRectResizeFinished);

    // Right click / escape to clear/finalize path and switch tool
    connect(this, &SymbolEditorView::rightClicked, this, [this]() {
        if (currentTool() == 7 && m_polyPoints.size() > 2) { // Polygon
            SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_polyPoints, false);
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            if (visual) m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
        } else if (currentTool() == 11 && m_polyPoints.size() >= 2) { // Bezier
            // If we have at least start and end, we could finalize but Bezier really needs 4.
        } else if (currentTool() == 13 && m_penPoints.size() >= 2) { // Pen
            finalizePenPath();
        }

        // Switch to select tool
        setCurrentTool(0); // Select
        setDragMode(QGraphicsView::RubberBandDrag);
        
        // Cleanup state
        m_polyPoints.clear();
        clearPenState();
        
        // Update toolbar check state
        if (m_editor && m_editor->m_selectAction) m_editor->m_selectAction->setChecked(true);
    });

    connect(this, &SymbolEditorView::pinRotateRequested, this, [this]() {
        if (currentTool() != 6) return; // Pin tool
        if (m_previewOrientation == "Right") m_previewOrientation = "Down";
        else if (m_previewOrientation == "Down") m_previewOrientation = "Left";
        else if (m_previewOrientation == "Left") m_previewOrientation = "Up";
        else m_previewOrientation = "Right";
        updatePinPreview(snapToGrid(mapToScene(mapFromGlobal(QCursor::pos()))));
        if (m_editor) {
            m_editor->statusBar()->showMessage("Pin orientation: " + m_previewOrientation, 1200);
        }
    });

    connect(this, &SymbolEditorView::pinFlipHRequested, this, [this]() {
        if (currentTool() != 6) return; // Pin tool
        if (m_previewOrientation == "Right") m_previewOrientation = "Left";
        else if (m_previewOrientation == "Left") m_previewOrientation = "Right";
        updatePinPreview(snapToGrid(mapToScene(mapFromGlobal(QCursor::pos()))));
        if (m_editor) {
            m_editor->statusBar()->showMessage("Pin orientation: " + m_previewOrientation, 1200);
        }
    });

    // Items dragged in Select mode -> move primitives via undo command
    connect(this, &SymbolEditorView::itemsMoved, this, [this](QPointF delta) {
        QList<int> indices;
        bool referenceMoved = false;
        bool nameMoved = false;
        QPointF newRefPos, newNamePos;

        for (QGraphicsItem* item : m_scene->selectedItems()) {
            if (item->data(0).toString() == "label") {
                QString type = item->data(1).toString();
                if (type == "reference") { referenceMoved = true; newRefPos = item->pos(); }
                else if (type == "name") { nameMoved = true; newNamePos = item->pos(); }
                continue;
            }

            int idx = primitiveIndex(item);
            if (idx != -1 && !indices.contains(idx))
                indices.append(idx);
        }
        
        if (indices.isEmpty() && !referenceMoved && !nameMoved) return;

        SymbolDefinition oldDef = m_editor->symbolDefinition();
        SymbolDefinition newDef = oldDef;
        const QSet<int> selectedSet = QSet<int>(indices.begin(), indices.end());

        if (referenceMoved) newDef.setReferencePos(newRefPos);
        if (nameMoved) newDef.setNamePos(newNamePos);

        for (int idx : indices) {
            SymbolPrimitive& prim = newDef.primitives()[idx];
            qreal localDx = delta.x();
            qreal localDy = delta.y();

            // Smart guide snap for moved pins: align to nearby unselected pin X/Y.
            if (prim.type == SymbolPrimitive::Pin) {
                const qreal oldX = oldDef.primitives()[idx].data.value("x").toDouble();
                const qreal oldY = oldDef.primitives()[idx].data.value("y").toDouble();
                qreal newX = oldX + localDx;
                qreal newY = oldY + localDy;
                const qreal threshold = qMax<qreal>(2.0, gridSize() * 0.4);
                qreal bestX = threshold + 1.0;
                qreal bestY = threshold + 1.0;
                bool snapX = false;
                bool snapY = false;
                qreal targetX = newX;
                qreal targetY = newY;

                for (int j = 0; j < oldDef.primitives().size(); ++j) {
                    if (selectedSet.contains(j)) continue;
                    const SymbolPrimitive& other = oldDef.primitives().at(j);
                    if (other.type != SymbolPrimitive::Pin) continue;
                    const qreal ox = other.data.value("x").toDouble();
                    const qreal oy = other.data.value("y").toDouble();

                    const qreal dxAbs = qAbs(newX - ox);
                    if (dxAbs < bestX && dxAbs <= threshold) {
                        bestX = dxAbs;
                        targetX = ox;
                        snapX = true;
                    }
                    const qreal dyAbs = qAbs(newY - oy);
                    if (dyAbs < bestY && dyAbs <= threshold) {
                        bestY = dyAbs;
                        targetY = oy;
                        snapY = true;
                    }
                }

                if (snapX) localDx = targetX - oldX;
                if (snapY) localDy = targetY - oldY;
            }

            auto shift = [&](const char* k, qreal d) {
                if (prim.data.contains(k)) prim.data[k] = prim.data[k].toDouble() + d;
            };
            shift("x",  localDx); shift("y",  localDy);
            shift("x1", localDx); shift("y1", localDy);
            shift("x2", localDx); shift("y2", localDy);
            shift("cx", localDx); shift("cy", localDy);
            shift("centerX", localDx); shift("centerY", localDy);

            if (prim.type == SymbolPrimitive::Polygon) {
                QJsonArray pts = prim.data["points"].toArray();
                QJsonArray newPts;
                for (auto v : pts) {
                    QJsonObject o = v.toObject();
                    o["x"] = o["x"].toDouble() + localDx;
                    o["y"] = o["y"].toDouble() + localDy;
                    newPts.append(o);
                }
                prim.data["points"] = newPts;
            }
        }
        m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Move Items"));
    });
}

SymbolCanvas::~SymbolCanvas() {
    clearScene();
}

void SymbolCanvas::addVisualItem(QGraphicsItem* visual, int index) {
    if (!visual) return;
    visual->setData(1, index);
    m_scene->addItem(visual);
    m_drawnItems.append(visual);
}

void SymbolCanvas::removeVisualItem(int index) {
    if (index >= 0 && index < m_drawnItems.size()) {
        QGraphicsItem* item = m_drawnItems.at(index);
        m_scene->removeItem(item);
        m_drawnItems.removeAt(index);
        delete item;
    }
}

void SymbolCanvas::insertVisualItem(int index, QGraphicsItem* visual) {
    if (!visual) return;
    int safeIdx = qBound(0, index, m_drawnItems.size());
    visual->setData(1, safeIdx);
    m_scene->addItem(visual);
    m_drawnItems.insert(safeIdx, visual);
}

int SymbolCanvas::primitiveIndex(QGraphicsItem* item) const {
    while (item) {
        if (item->data(10).toString() == "inherited") return -1; // Block inherited items

        const int drawnIdx = m_drawnItems.indexOf(item);
        if (drawnIdx >= 0) {
            int inheritedCount = m_editor->m_symbol.effectivePrimitives().size() - m_editor->m_symbol.primitives().size();
            int localIdx = drawnIdx - inheritedCount;
            if (localIdx >= 0 && localIdx < m_editor->m_symbol.primitives().size()) return localIdx;
        }

        bool ok = false;
        int idx = item->data(1).toInt(&ok);
        if (ok) {
            int inheritedCount = m_editor->m_symbol.effectivePrimitives().size() - m_editor->m_symbol.primitives().size();
            int localIdx = idx - inheritedCount;
            if (localIdx >= 0 && localIdx < m_editor->m_symbol.primitives().size()) return localIdx;
            return -1;
        }
        item = item->parentItem();
    }
    return -1;
}

void SymbolCanvas::removeOverlayItems() {
    for (QGraphicsItem* item : m_overlayItems) {
        if (m_scene) m_scene->removeItem(item);
        delete item;
    }
    m_overlayItems.clear();
}

void SymbolCanvas::clearResizeHandles() {
    for (QGraphicsRectItem* h : m_resizeHandles) {
        if (!h) continue;
        if (m_scene) m_scene->removeItem(h);
        delete h;
    }
    m_resizeHandles.clear();
}

void SymbolCanvas::clearScene() {
    m_overlayItems.clear();
    clearResizeHandles();
    m_drawnItems.clear();
    if (m_previewItem) {
        if (m_scene && m_previewItem->scene())
            m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    m_polyPoints.clear();
    if (m_scene) {
        m_scene->clear();
    }
}

void SymbolCanvas::updateOverlayLabels() {
    removeOverlayItems();

    SymbolDefinition def = m_editor->symbolDefinition();
    QRectF bounds = def.boundingRect();
    if (bounds.isNull() || bounds.width() < 10)
        bounds = QRectF(-20, -20, 40, 40);

    auto makeLabel = [&](const QString& text, const QColor& color,
                         const QPointF& defaultPos, const QPointF& savedPos, const QString& type) -> QGraphicsSimpleTextItem* {
        auto* lbl = new QGraphicsSimpleTextItem(text);
        lbl->setBrush(color);
        lbl->setFont(QFont("SansSerif", 10, QFont::Bold));
        
        if (savedPos != QPointF(0, 0)) {
            lbl->setPos(savedPos);
        } else {
            lbl->setPos(defaultPos);
        }

        lbl->setFlag(QGraphicsItem::ItemIsSelectable, true);
        lbl->setFlag(QGraphicsItem::ItemIsMovable,    true);
        lbl->setData(0, "label");
        lbl->setData(1, type); // "reference" or "name"
        m_scene->addItem(lbl);
        m_overlayItems.append(lbl);
        return lbl;
    };

    makeLabel(def.referencePrefix() + "?",
              themePinLabelColor(),
              QPointF(bounds.left(), bounds.top() - 25),
              def.referencePos(),
              "reference");

    makeLabel(def.name(),
              themeTextColor().lighter(105),
              QPointF(bounds.left(), bounds.bottom() + 5),
              def.namePos(),
              "name");
}

void SymbolCanvas::updateResizeHandles() {
    clearResizeHandles();
    if (!m_scene || currentTool() != 0) return; // Select tool

    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = primitiveIndex(selected.first());
    if (idx < 0 || idx >= m_editor->m_symbol.primitives().size()) return;
    const SymbolPrimitive& prim = m_editor->m_symbol.primitives().at(idx);
    QList<QPair<QString, QPointF>> handles;
    qreal handleSize = 8.0;
    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        const qreal x = prim.data.value("x").toDouble();
        const qreal y = prim.data.value("y").toDouble();
        const qreal w = prim.data.contains("width") ? prim.data.value("width").toDouble() : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height") ? prim.data.value("height").toDouble() : prim.data.value("h").toDouble();
        QRectF r(x, y, w, h);
        r = r.normalized();
        if (r.isNull()) return;
        const qreal minDim = qMin(r.width(), r.height());
        handleSize = qBound<qreal>(3.0, minDim * 0.22, 8.0);
        const qreal edgeOffset = (minDim < 16.0) ? qBound<qreal>(1.0, handleSize * 0.7, 3.5) : 0.0;
        handles = {
            {"tl", r.topLeft() + QPointF(-edgeOffset, -edgeOffset)},
            {"tr", r.topRight() + QPointF(edgeOffset, -edgeOffset)},
            {"br", r.bottomRight() + QPointF(edgeOffset, edgeOffset)},
            {"bl", r.bottomLeft() + QPointF(-edgeOffset, edgeOffset)}
        };
    } else if (prim.type == SymbolPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        const qreal len = QLineF(p1, p2).length();
        handleSize = qBound<qreal>(3.0, len * 0.12, 7.5);
        handles = {{"p1", p1}, {"p2", p2}};
    } else if (prim.type == SymbolPrimitive::Circle) {
        const qreal cx = prim.data.contains("centerX") ? prim.data.value("centerX").toDouble() : prim.data.value("cx").toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data.value("centerY").toDouble() : prim.data.value("cy").toDouble();
        const qreal r = prim.data.contains("radius") ? prim.data.value("radius").toDouble() : prim.data.value("r").toDouble();
        if (r <= 0.0) return;
        const qreal d = r * 2.0;
        handleSize = qBound<qreal>(3.0, d * 0.22, 7.5);
        const qreal radialOffset = (r < 10.0) ? qBound<qreal>(1.5, handleSize * 0.9, 3.5) : 0.0;
        const qreal rr = r + radialOffset;
        handles = {
            {"east", QPointF(cx + rr, cy)},
            {"west", QPointF(cx - rr, cy)},
            {"north", QPointF(cx, cy - rr)},
            {"south", QPointF(cx, cy + rr)}
        };
    } else {
        return;
    }

    const qreal hs = handleSize;
    for (const auto& h : handles) {
        auto* handle = new QGraphicsRectItem(h.second.x() - hs / 2.0, h.second.y() - hs / 2.0, hs, hs);
        handle->setBrush(QColor(96, 165, 250));
        handle->setPen(QPen(QColor(255, 255, 255), 1.0));
        handle->setZValue(3000);
        handle->setData(0, "resize_handle");
        handle->setData(1, h.first);
        handle->setData(2, idx);
        handle->setFlag(QGraphicsItem::ItemIsSelectable, false);
        handle->setFlag(QGraphicsItem::ItemIsMovable, false);
        m_scene->addItem(handle);
        m_resizeHandles.append(handle);
    }
}

void SymbolCanvas::applyShapeStyle(QAbstractGraphicsShapeItem* shape, const SymbolPrimitive& prim) const {
    qreal width = prim.data.value("lineWidth").toDouble();
    if (width <= 0.0) width = 1.5;

    Qt::PenStyle penStyle = Qt::SolidLine;
    const QString s = prim.data.value("lineStyle").toString();
    if (s == "Dash")    penStyle = Qt::DashLine;
    else if (s == "Dot")      penStyle = Qt::DotLine;
    else if (s == "DashDot")  penStyle = Qt::DashDotLine;

    shape->setPen(QPen(themeLineColor(), width, penStyle));

    if (prim.data.value("filled").toBool()) {
        QColor fill(prim.data.value("fillColor").toString());
        if (!fill.isValid()) fill = QColor(0, 122, 204, 50);
        shape->setBrush(fill);
    } else {
        shape->setBrush(QColor(255, 255, 255, 15));
    }
}

void SymbolCanvas::updateGuideAnchors() {
    QList<QPointF> anchors;

    for (const auto& prim : m_editor->m_symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Pin:
            anchors.append(QPointF(prim.data["x"].toDouble(),
                                   prim.data["y"].toDouble()));
            break;
        case SymbolPrimitive::Line:
            anchors.append(QPointF(prim.data["x1"].toDouble(),
                                   prim.data["y1"].toDouble()));
            anchors.append(QPointF(prim.data["x2"].toDouble(),
                                   prim.data["y2"].toDouble()));
            break;
        case SymbolPrimitive::Rect: {
            double x  = prim.data["x"].toDouble();
            double y  = prim.data["y"].toDouble();
            double w  = prim.data["w"].toDouble();
            double h  = prim.data["h"].toDouble();
            double x2 = x + w, y2 = y + h;
            double mx = x + w / 2.0, my = y + h / 2.0;
            anchors.append(QPointF(x,  y));
            anchors.append(QPointF(x2, y));
            anchors.append(QPointF(x,  y2));
            anchors.append(QPointF(x2, y2));
            anchors.append(QPointF(mx, y));
            anchors.append(QPointF(mx, y2));
            anchors.append(QPointF(x,  my));
            anchors.append(QPointF(x2, my));
            anchors.append(QPointF(mx, my));
            break;
        }
        case SymbolPrimitive::Circle:
            anchors.append(QPointF(prim.data["cx"].toDouble(),
                                   prim.data["cy"].toDouble()));
            break;
        case SymbolPrimitive::Arc: {
            double x  = prim.data["x"].toDouble();
            double y  = prim.data["y"].toDouble();
            double w  = prim.data["w"].toDouble();
            double h  = prim.data["h"].toDouble();
            anchors.append(QPointF(x + w / 2.0, y + h / 2.0));
            break;
        }
        case SymbolPrimitive::Bezier:
            anchors.append(QPointF(prim.data["x1"].toDouble(),
                                   prim.data["y1"].toDouble()));
            anchors.append(QPointF(prim.data["x4"].toDouble(),
                                   prim.data["y4"].toDouble()));
            break;
        case SymbolPrimitive::Text:
            anchors.append(QPointF(prim.data["x"].toDouble(),
                                   prim.data["y"].toDouble()));
            break;
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = prim.data["points"].toArray();
            for (const auto& v : pts) {
                QJsonObject p = v.toObject();
                anchors.append(QPointF(p["x"].toDouble(),
                                       p["y"].toDouble()));
            }
            break;
        }
        default:
            break;
        }
    }

    setGuideAnchorPoints(anchors);
}

QGraphicsItem* SymbolCanvas::buildVisual(const SymbolPrimitive& prim, int index) const {
    QGraphicsItem* visual = nullptr;
    const QColor lineColor = themeLineColor();
    const QColor textDefaultColor = themeTextColor();
    const QColor pinLabelColor = themePinLabelColor();

    switch (prim.type) {

    case SymbolPrimitive::Line: {
        auto* item = new FilteredLineItem(
            QLineF(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble(),
                   prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble()));
        qreal w = prim.data.value("lineWidth").toDouble();
        if (w <= 0) w = 1.5;
        Qt::PenStyle ps = Qt::SolidLine;
        const QString ls = prim.data.value("lineStyle").toString();
        if (ls == "Dash")    ps = Qt::DashLine;
        else if (ls == "Dot")     ps = Qt::DotLine;
        else if (ls == "DashDot") ps = Qt::DashDotLine;
        item->setPen(QPen(lineColor, w, ps));
        visual = item;
        break;
    }

    case SymbolPrimitive::Rect: {
        const qreal w = prim.data.contains("width")
                      ? prim.data.value("width").toDouble()
                      : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height")
                      ? prim.data.value("height").toDouble()
                      : prim.data.value("h").toDouble();
        auto* item = new FilteredRectItem(
            prim.data.value("x").toDouble(),
            prim.data.value("y").toDouble(), w, h);
        applyShapeStyle(item, prim);
        item->setZValue(-1);
        visual = item;
        break;
    }

    case SymbolPrimitive::Circle: {
        auto dbl = [&](const char* a, const char* b) {
            return prim.data.contains(a)
                 ? prim.data.value(a).toDouble()
                 : prim.data.value(b).toDouble();
        };
        const qreal cx = dbl("centerX", "cx");
        const qreal cy = dbl("centerY", "cy");
        const qreal r  = dbl("radius",  "r");
        auto* item = new FilteredEllipseItem(cx - r, cy - r, r * 2, r * 2);
        applyShapeStyle(item, prim);
        visual = item;
        break;
    }

    case SymbolPrimitive::Arc: {
        const qreal x  = prim.data.value("x").toDouble();
        const qreal y  = prim.data.value("y").toDouble();
        const qreal w  = prim.data.contains("width")  ? prim.data.value("width").toDouble()
                                                       : prim.data.value("w").toDouble();
        const qreal h  = prim.data.contains("height") ? prim.data.value("height").toDouble()
                                                       : prim.data.value("h").toDouble();
        const int sa   = prim.data.value("startAngle").toInt(0);
        const int span = prim.data.value("spanAngle").toInt(180 * 16);
        QPainterPath path;
        QRectF rect(x, y, w, h);
        path.arcMoveTo(rect, sa / 16.0);
        path.arcTo(rect, sa / 16.0, span / 16.0);
        auto* item = new FilteredPathItem(path);
        qreal lw = prim.data.value("lineWidth").toDouble();
        if (lw <= 0) lw = 1.5;
        item->setPen(QPen(lineColor, lw));
        visual = item;
        break;
    }

    case SymbolPrimitive::Text: {
        QString rawText = prim.data.value("text").toString();
        QMap<QString, QString> vars;
        vars["REFERENCE"] = m_editor->m_symbol.referencePrefix() + "?";
        vars["VALUE"]     = m_editor->m_symbol.defaultValue().isEmpty() ? "Value" : m_editor->m_symbol.defaultValue();
        vars["NAME"]      = m_editor->m_symbol.name();
        vars["DATE"]      = QDate::currentDate().toString(Qt::ISODate);
        
        QString resolved = TextResolver::resolve(rawText, vars);
        auto* item = new FilteredSimpleTextItem(resolved);
        const qreal anchorX = prim.data.value("x").toDouble();
        const qreal anchorY = prim.data.value("y").toDouble();
        int fs = prim.data.value("fontSize").toInt(10);
        if (fs <= 0) fs = 10;
        item->setFont(QFont("SansSerif", fs));
        
        QColor c = textDefaultColor;
        if (prim.data.contains("color")) {
            c = QColor(prim.data["color"].toString());
        }
        item->setBrush(c);

        const QRectF tb = item->boundingRect();
        qreal dx = 0.0;
        const QString hAlign = prim.data.value("hAlign").toString("left").toLower();
        const QString vAlign = prim.data.value("vAlign").toString("baseline").toLower();
        if (hAlign == "center") dx = -tb.width() * 0.5;
        else if (hAlign == "right") dx = -tb.width();
        qreal py = anchorY;
        if (vAlign == "center") py -= tb.height() * 0.5;
        else if (vAlign == "bottom") py -= tb.height();
        else if (vAlign == "baseline") py -= QFontMetricsF(item->font()).ascent();
        item->setPos(anchorX + dx, py);

        const qreal rotDeg = prim.data.value("rotation").toDouble(0.0);
        if (std::abs(rotDeg) > 1e-6) {
            item->setTransformOriginPoint(0.0, 0.0);
            item->setRotation(-rotDeg);
        }
        visual = item;
        break;
    }

    case SymbolPrimitive::Pin: {
        auto* group = new FilteredGroupItem();
        const qreal px = prim.data.value("x").toDouble();
        const qreal py = prim.data.value("y").toDouble();
        qreal len = prim.data.value("length").toDouble();
        if (len <= 0) len = 15.0;
        
        bool isVisible = prim.data.value("visible").toBool(true);
        if (!isVisible) group->setOpacity(0.3);
        
        const QString orient = prim.data.value("orientation").toString("Right");

        QPointF endPt;
        if      (orient == "Left")  endPt = QPointF(px - len, py);
        else if (orient == "Up")    endPt = QPointF(px, py - len);
        else if (orient == "Down")  endPt = QPointF(px, py + len);
        else                        endPt = QPointF(px + len, py);

        auto* line = new FilteredLineItem(px, py, endPt.x(), endPt.y());
        QPen pinLeadPen(lineColor, 2.0, isVisible ? Qt::SolidLine : Qt::DashLine);
        pinLeadPen.setCapStyle(Qt::FlatCap);
        line->setPen(pinLeadPen);
        group->addToGroup(line);

        QString shape = prim.data.value("pinShape").toString("Line");
        if (shape == "Inverted" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
            qreal cr = 3.0;
            QPointF cPos = endPt;
            auto* circle = new FilteredEllipseItem(cPos.x() - cr, cPos.y() - cr, cr * 2, cr * 2);
            circle->setPen(QPen(lineColor, 1.5));
            circle->setBrush(ThemeManager::theme() ? ThemeManager::theme()->panelBackground() : Qt::black);
            group->addToGroup(circle);
            
            QLineF l(px, py, endPt.x(), endPt.y());
            if (l.length() > cr) l.setLength(l.length() - cr);
            line->setLine(l);
        }
        
        if (shape == "Clock" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
            QPointF p1, p2, p3;
            qreal w = 4.0;
            QPointF wedgeBase = endPt;

            if (shape != "Clock") {
                qreal offset = 6.0;
                if      (orient == "Left")  wedgeBase.setX(endPt.x() + offset);
                else if (orient == "Right") wedgeBase.setX(endPt.x() - offset);
                else if (orient == "Up")    wedgeBase.setY(endPt.y() + offset);
                else                        wedgeBase.setY(endPt.y() - offset);
            }

            if (orient == "Left" || orient == "Right") {
                qreal dir = (orient == "Left") ? 1 : -1;
                p1 = wedgeBase + QPointF(0, -w);
                p2 = wedgeBase + QPointF(dir * w, 0);
                p3 = wedgeBase + QPointF(0, w);
            } else {
                qreal dir = (orient == "Up") ? 1 : -1;
                p1 = wedgeBase + QPointF(-w, 0);
                p2 = wedgeBase + QPointF(0, dir * w);
                p3 = wedgeBase + QPointF(w, 0);
            }
            auto* wedge = new FilteredPathItem();
            QPainterPath wp; wp.moveTo(p1); wp.lineTo(p2); wp.lineTo(p3);
            wedge->setPath(wp);
            wedge->setPen(QPen(lineColor, 1.5));
            group->addToGroup(wedge);
        }

        QColor dotBrush = ThemeManager::theme() ? ThemeManager::theme()->schematicComponent() : QColor(12, 12, 12);
        auto* dot = new FilteredEllipseItem(px - 2.5, py - 2.5, 5.0, 5.0);
        dot->setBrush(dotBrush);
        dot->setPen(QPen(lineColor, 2.0));
        group->addToGroup(dot);

        QJsonValue numVal = prim.data["number"];
        if (numVal.isUndefined()) numVal = prim.data["num"];
        QString numStr = numVal.isString() ? numVal.toString() : QString::number(numVal.toInt());
        
        QColor textColor = pinLabelColor;

        QString fullNumStr = numStr;
        QString stacked = prim.data.value("stackedNumbers").toString();
        if (!stacked.isEmpty()) {
            int count = stacked.split(",", Qt::SkipEmptyParts).size();
            fullNumStr += QString(" [+%1]").arg(count);
        }

        auto* numLabel = new FilteredSimpleTextItem(fullNumStr);
        numLabel->setBrush(textColor);
        int nsz = prim.data.value("numSize").toInt(7);
        numLabel->setFont(QFont("Monospace", nsz > 0 ? nsz : 7));
        numLabel->setParentItem(group);

        QString nameStr = prim.data.value("name").toString();
        nameStr.replace("~{", "");
        nameStr.replace("}", "");
        if (nameStr.startsWith("~")) nameStr = nameStr.mid(1);
        if (nameStr.startsWith("\\overline{") && nameStr.endsWith("}")) {
            nameStr = nameStr.mid(10, nameStr.length() - 11);
        }

        auto* nameLabel = new FilteredSimpleTextItem(nameStr);
        nameLabel->setBrush(textColor);
        int asz = prim.data.value("nameSize").toInt(7);
        nameLabel->setFont(QFont("SansSerif", asz > 0 ? asz : 7));
        nameLabel->setParentItem(group);

        if (prim.data.value("hideNum").toBool()) numLabel->hide();
        if (prim.data.value("hideName").toBool()) nameLabel->hide();

        const QRectF nb = nameLabel->boundingRect();
        const QRectF numB = numLabel->boundingRect();
        
        if (orient == "Left") {
            numLabel->setPos(px - len/2.0 - numB.width()/2.0, py - numB.height() - 2);
            nameLabel->setPos(endPt.x() - nb.width() - 4, py - nb.height()/2.0);
        } else if (orient == "Right") {
            numLabel->setPos(px + len/2.0 - numB.width()/2.0, py - numB.height() - 2);
            nameLabel->setPos(endPt.x() + 4, py - nb.height()/2.0);
        } else if (orient == "Up") {
            numLabel->setPos(px + 4, py - len/2.0 - numB.height()/2.0);
            nameLabel->setTransformOriginPoint(nb.center());
            nameLabel->setRotation(-90);
            nameLabel->setPos(endPt.x() - nb.height()/2.0, endPt.y() - nb.width() - 4);
        } else if (orient == "Down") {
            numLabel->setPos(px + 4, py + len/2.0 - numB.height()/2.0);
            nameLabel->setTransformOriginPoint(nb.center());
            nameLabel->setRotation(-90);
            nameLabel->setPos(endPt.x() - nb.height()/2.0, endPt.y() + 4);
        }

        visual = group;
        break;
    }

    case SymbolPrimitive::Polygon: {
        QPolygonF poly;
        const QJsonArray pts = prim.data.value("points").toArray();
        poly.reserve(pts.size());
        for (const auto& v : pts) {
            const QJsonObject o = v.toObject();
            poly.append(QPointF(o.value("x").toDouble(), o.value("y").toDouble()));
        }
        auto* item = new FilteredPolygonItem(poly);
        applyShapeStyle(item, prim);
        visual = item;
        break;
    }

    case SymbolPrimitive::Bezier: {
        QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
        QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
        QPointF p3(prim.data["x3"].toDouble(), prim.data["y3"].toDouble());
        QPointF p4(prim.data["x4"].toDouble(), prim.data["y4"].toDouble());
        
        QPainterPath path;
        path.moveTo(p1);
        path.cubicTo(p2, p3, p4);
        
        auto* item = new FilteredPathItem(path);
        qreal lw = prim.data.value("lineWidth").toDouble();
        if (lw <= 0) lw = 1.5;
        item->setPen(QPen(lineColor, lw));
        visual = item;
        break;
    }

    case SymbolPrimitive::Image: {
        QByteArray ba = QByteArray::fromBase64(prim.data["image"].toString().toLatin1());
        QPixmap pix;
        pix.loadFromData(ba);
        if (pix.isNull()) break;

        qreal x = prim.data["x"].toDouble();
        qreal y = prim.data["y"].toDouble();
        qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
        qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();

        auto* item = new FilteredPixmapItem(pix.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        item->setPos(x, y);
        visual = item;
        break;
    }

    default:
        break;
    }

    if (visual) {
        visual->setFlag(QGraphicsItem::ItemIsSelectable);
        visual->setFlag(QGraphicsItem::ItemIsMovable);
        visual->setData(1, index);
        if (prim.type == SymbolPrimitive::Pin) {
            visual->setData(2, "pin");
            visual->setData(3, prim.data.value("x").toDouble());
            visual->setData(4, prim.data.value("y").toDouble());
        }
    }
    return visual;
}

void SymbolCanvas::updateVisualForPrimitive(int index, const SymbolPrimitive& prim) {
    if (index < 0 || index >= m_drawnItems.size()) return;
    QGraphicsItem* old = m_drawnItems[index];
    QGraphicsItem* fresh = buildVisual(prim, index);
    if (fresh) {
        m_scene->removeItem(old);
        delete old;
        m_scene->addItem(fresh);
        m_drawnItems[index] = fresh;
    }
}

QColor SymbolCanvas::themeLineColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_editor->m_colorPreset) {
    case 1: return QColor(245, 248, 255);  // High Contrast
    case 2: return QColor(175, 245, 220);  // Emerald
    case 3: return QColor(255, 214, 148);  // Amber CAD
    case 4: return QColor(220, 220, 220);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->schematicLine() : Qt::white;
}

QColor SymbolCanvas::themeTextColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_editor->m_colorPreset) {
    case 1: return QColor(245, 248, 255);  // High Contrast
    case 2: return QColor(180, 245, 220);  // Emerald
    case 3: return QColor(255, 220, 160);  // Amber CAD
    case 4: return QColor(220, 220, 220);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->textColor() : Qt::white;
}

QColor SymbolCanvas::themePinLabelColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_editor->m_colorPreset) {
    case 1: return QColor(245, 248, 255);  // High Contrast
    case 2: return QColor(175, 245, 220);  // Emerald
    case 3: return QColor(255, 214, 148);  // Amber CAD
    case 4: return QColor(220, 220, 220);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->accentColor().lighter(120) : QColor(140, 190, 255);
}

void SymbolCanvas::updatePinPreview(QPointF pos) {
    if (currentTool() != 6) return; // Pin tool

    if (m_previewItem) {
        if (m_previewItem->scene()) m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }

    SymbolPrimitive tempPin = SymbolPrimitive::createPin(pos, 0, "", m_previewOrientation);
    m_previewItem = buildVisual(tempPin, -1);
    if (m_previewItem) {
        m_previewItem->setOpacity(0.5);
        m_previewItem->setZValue(1000);
        m_previewItem->setAcceptedMouseButtons(Qt::NoButton);
        m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
        m_scene->addItem(m_previewItem);
    }
}

void SymbolCanvas::onPointClicked(QPointF pos) {
    pos = snapToGrid(pos);

    if (currentTool() == 6) { // Pin tool
        int pinCount = 0;
        for (const auto& p : m_editor->m_symbol.primitives()) {
            if (p.type == SymbolPrimitive::Pin) ++pinCount;
        }
        int pinNum = pinCount + 1;
        SymbolPrimitive prim = SymbolPrimitive::createPin(pos, pinNum,
                                                         QString::number(pinNum),
                                                         m_previewOrientation);
        prim.setUnit(m_editor->m_currentUnit);
        prim.setBodyStyle(m_editor->m_currentStyle);
        QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
        if (visual)
            m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));

    } else if (currentTool() == 5) { // Text tool
        TextPropertiesDialog dlg(m_editor);
        if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
            SymbolPrimitive prim = SymbolPrimitive::createText(dlg.text(), pos, dlg.fontSize(), dlg.color());
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            if (visual)
                m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
        }

    } else if (currentTool() == 1 || currentTool() == 3 || currentTool() == 4 || currentTool() == 2) { // Line, Circle, Arc, Rect
        m_polyPoints.append(pos);
        if (m_polyPoints.size() == 2) {
            QPointF p1 = m_polyPoints[0];
            QPointF p2 = m_polyPoints[1];
            SymbolPrimitive prim;
            
            if (currentTool() == 1) { // Line
                prim = SymbolPrimitive::createLine(p1, p2);
            } else if (currentTool() == 2) { // Rect
                prim = SymbolPrimitive::createRect(QRectF(p1, p2).normalized(), false);
            } else if (currentTool() == 3) { // Circle
                prim = SymbolPrimitive::createCircle(p1, QLineF(p1, p2).length(), false);
            } else { // Arc
                qreal rx = qAbs(p2.x() - p1.x());
                qreal ry = qAbs(p2.y() - p1.y());
                prim = SymbolPrimitive::createArc(QRectF(p1.x()-rx, p1.y()-ry, rx*2, ry*2), 0, 180 * 16);
            }
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);

            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            m_polyPoints.clear();
            if (visual) m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
        }

    } else if (currentTool() == 7) { // Polygon
        m_polyPoints.append(pos);
        if (m_polyPoints.size() > 2
            && QLineF(pos, m_polyPoints.first()).length() < 8.0) {
            m_polyPoints.removeLast();
            SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_polyPoints, false);
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            m_polyPoints.clear();
            if (m_previewItem) {
                m_scene->removeItem(m_previewItem);
                delete m_previewItem;
                m_previewItem = nullptr;
            }
            if (visual)
                m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
        }

    } else if (currentTool() == 11) { // Bezier
        m_polyPoints.append(pos);
        if (m_polyPoints.size() == 4) {
            SymbolPrimitive prim = SymbolPrimitive::createBezier(m_polyPoints[0], m_polyPoints[2], m_polyPoints[3], m_polyPoints[1]);
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            m_polyPoints.clear();
            if (m_previewItem) {
                m_scene->removeItem(m_previewItem);
                delete m_previewItem;
                m_previewItem = nullptr;
            }
            if (visual) m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
        }

    } else if (currentTool() == 10) { // Anchor
        SymbolDefinition oldDef = m_editor->symbolDefinition();
        SymbolDefinition newDef = oldDef;
        
        newDef.setReferencePos(oldDef.referencePos() - pos);
        newDef.setNamePos(oldDef.namePos() - pos);

        for (SymbolPrimitive& prim : newDef.primitives()) {
            auto shift = [&](const char* k, qreal d) {
                if (prim.data.contains(k)) prim.data[k] = prim.data[k].toDouble() - d;
            };
            shift("x",  pos.x()); shift("y",  pos.y());
            shift("x1", pos.x()); shift("y1", pos.y());
            shift("x2", pos.x()); shift("y2", pos.y());
            shift("x3", pos.x()); shift("y3", pos.y());
            shift("x4", pos.x()); shift("y4", pos.y());
            shift("cx", pos.x()); shift("cy", pos.y());
            shift("centerX", pos.x()); shift("centerY", pos.y());
            
            if (prim.type == SymbolPrimitive::Polygon) {
                QJsonArray pts = prim.data["points"].toArray();
                QJsonArray newPts;
                for (auto v : pts) {
                    QJsonObject o = v.toObject();
                    o["x"] = o["x"].toDouble() - pos.x();
                    o["y"] = o["y"].toDouble() - pos.y();
                    newPts.append(o);
                }
                prim.data["points"] = newPts;
            }
        }
        m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, oldDef, newDef, "Set Anchor"));
    }
}

void SymbolCanvas::onMouseMoved(QPointF pos) {
    if (m_previewItem) {
        m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    
    if (currentTool() == 0 || currentTool() == 8 || currentTool() == 10) return; // Select, Erase, Anchor

    const QPen previewPen(Qt::cyan, 1, Qt::DashLine);
    QPointF start = m_polyPoints.isEmpty() ? pos : m_polyPoints.first();
    QPointF end = pos;

    switch (currentTool()) {
    case 1: // Line
        m_previewItem = m_scene->addLine(QLineF(start, end), previewPen);
        break;
    case 2: { // Rect
        QRectF r = QRectF(start, end).normalized();
        m_previewItem = m_scene->addRect(r, previewPen);
        break;
    }
    case 3: { // Circle
        qreal rad = QLineF(start, end).length();
        m_previewItem = m_scene->addEllipse(
            start.x()-rad, start.y()-rad, rad*2, rad*2, previewPen);
        break;
    }
    case 11: { // Bezier
        if (m_polyPoints.isEmpty()) {
            m_previewItem = m_scene->addLine(QLineF(pos, pos), previewPen);
        } else if (m_polyPoints.size() == 1) {
            m_previewItem = m_scene->addLine(QLineF(m_polyPoints[0], pos), previewPen);
        } else if (m_polyPoints.size() == 2) {
            QPainterPath path;
            path.moveTo(m_polyPoints[0]);
            path.cubicTo(pos, pos, m_polyPoints[1]);
            m_previewItem = m_scene->addPath(path, previewPen);
        } else if (m_polyPoints.size() == 3) {
            QPainterPath path;
            path.moveTo(m_polyPoints[0]);
            path.cubicTo(m_polyPoints[2], pos, m_polyPoints[1]);
            m_previewItem = m_scene->addPath(path, previewPen);
        }
        break;
    }
    case 4: { // Arc
        qreal rx = qAbs(end.x() - start.x());
        qreal ry = qAbs(end.y() - start.y());
        QRectF r(start.x()-rx, start.y()-ry, rx*2, ry*2);
        QPainterPath path;
        path.arcMoveTo(r, 0);
        path.arcTo(r, 0, 180);
        m_previewItem = m_scene->addPath(path, previewPen);
        break;
    }
    case 7: { // Polygon
        if (m_polyPoints.isEmpty()) break;
        QPolygonF poly = m_polyPoints;
        poly.append(end);
        m_previewItem = m_scene->addPolygon(poly, previewPen);
        break;
    }
    case 9: { // ZoomArea
        QRectF r = QRectF(start, end).normalized();
        m_previewItem = m_scene->addRect(r, previewPen);
        break;
    }
    case 6: // Pin
        updatePinPreview(pos);
        break;
    default: break;
    }
}

void SymbolCanvas::onDrawingFinished(QPointF start, QPointF end) {
    if (currentTool() == 9) { // ZoomArea
        QRectF r = QRectF(start, end).normalized();
        if (r.width() > 5 && r.height() > 5)
            fitInView(r, Qt::KeepAspectRatio);
    }
}

void SymbolCanvas::onPenPointAdded(QPointF pos) {
    if (currentTool() != 13) return; // Pen tool
    
    if (m_penPoints.size() > 2) {
        if (QLineF(pos, m_penPoints.first().pos).length() < 8.0) {
            onPenPathClosed();
            return;
        }
    }
    
    PenPoint newPoint;
    newPoint.pos = pos;
    newPoint.handleIn = QPointF(0, 0);
    newPoint.handleOut = QPointF(0, 0);
    newPoint.smooth = false;
    newPoint.corner = false;
    m_penPoints.append(newPoint);
    updatePenPreview();
}

void SymbolCanvas::onPenHandleDragged(QPointF handlePos) {
     if (currentTool() != 13 || m_penPoints.isEmpty()) return; // Pen tool
     
     if (m_selectedPenMidpoint != -1) {
         int segIdx = m_selectedPenMidpoint;
         if (segIdx >= 0 && segIdx < m_penPoints.size()) {
             PenPoint& p1 = m_penPoints[segIdx];
             PenPoint& p2 = m_penPoints[(segIdx + 1) % m_penPoints.size()];
             
             QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
             qreal dragDist = QLineF(handlePos, midpoint).length();
             
             if (dragDist > 2.0) {
                 PenPoint newPoint;
                 newPoint.pos = handlePos;
                 newPoint.handleIn = QPointF(0, 0);
                 newPoint.handleOut = QPointF(0, 0);
                 newPoint.smooth = false;
                 newPoint.corner = true;
                 
                 m_penPoints.insert(segIdx + 1, newPoint);
                 m_selectedPenMidpoint = -1;
                 updatePenPreview();
             }
         }
     } else if (m_selectedPenPoint == -1) {
         PenPoint& lastPoint = m_penPoints.last();
         QPointF delta = handlePos - lastPoint.pos;
         lastPoint.handleOut = delta;
         if (lastPoint.smooth) {
             lastPoint.handleIn = -delta;
         }
     } else if (m_selectedPenPoint >= 0 && m_selectedPenPoint < m_penPoints.size()) {
         PenPoint& selectedPoint = m_penPoints[m_selectedPenPoint];
         QPointF delta = handlePos - selectedPoint.pos;
         
         if (m_selectedPenHandle == 0) {
             selectedPoint.handleIn = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleOut = -delta;
             }
         } else if (m_selectedPenHandle == 1) {
             selectedPoint.handleOut = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleIn = -delta;
             }
         } else {
             selectedPoint.handleOut = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleIn = -delta;
             }
         }
     }
     
     updatePenPreview();
}

void SymbolCanvas::onPenPointFinished() {
    if (currentTool() != 13) return; // Pen tool
    updatePenPreview();
}

void SymbolCanvas::onPenPathClosed() {
    if (currentTool() != 13 || m_penPoints.size() < 3) return; // Pen tool
    finalizePenPath();
}

void SymbolCanvas::finalizePenPath() {
    if (m_penFinalizing) return;
    if (m_penPoints.size() < 2) {
        clearPenState();
        return;
    }
    m_penFinalizing = true;
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        PenPoint& p1 = m_penPoints[i];
        PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
        
        QPointF cp1 = p1.pos + p1.handleOut;
        QPointF cp2 = p2.pos + p2.handleIn;
        QPointF end = p2.pos;
        
        if (p1.handleOut != QPointF(0, 0) || p2.handleIn != QPointF(0, 0)) {
            SymbolPrimitive prim = SymbolPrimitive::createBezier(p1.pos, cp1, cp2, end);
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            if (visual) {
                m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
            }
        } else {
            SymbolPrimitive prim = SymbolPrimitive::createLine(p1.pos, end);
            prim.setUnit(m_editor->m_currentUnit);
            prim.setBodyStyle(m_editor->m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_editor->m_symbol.primitives().size());
            if (visual) {
                m_editor->m_undoStack->push(new AddPrimitiveCommand(m_editor, prim, visual));
            }
        }
    }
    
    clearPenState();
}

void SymbolCanvas::clearPenState() {
     m_penPoints.clear();
     m_selectedPenPoint = -1;
     m_selectedPenHandle = -1;
     m_selectedPenMidpoint = -1;
     
     if (m_penPreviewItem) {
         m_scene->removeItem(m_penPreviewItem);
         delete m_penPreviewItem;
         m_penPreviewItem = nullptr;
     }
     
     for (auto* marker : m_penPointMarkers) {
         m_scene->removeItem(marker);
         delete marker;
     }
     m_penPointMarkers.clear();
     
     for (auto* line : m_penHandleLines) {
         m_scene->removeItem(line);
         delete line;
     }
     m_penHandleLines.clear();
     
     for (auto* dot : m_penHandleDots) {
         m_scene->removeItem(dot);
         delete dot;
     }
     m_penHandleDots.clear();
     
     for (auto* midDot : m_penMidpointDots) {
         m_scene->removeItem(midDot);
         delete midDot;
     }
     m_penMidpointDots.clear();
     m_penFinalizing = false;
}

void SymbolCanvas::updatePenPreview() {
     if (currentTool() != 13) return; // Pen tool
     
     if (m_penPreviewItem) {
         m_scene->removeItem(m_penPreviewItem);
         delete m_penPreviewItem;
         m_penPreviewItem = nullptr;
     }
     for (auto* marker : m_penPointMarkers) {
         m_scene->removeItem(marker);
         delete marker;
     }
     m_penPointMarkers.clear();
     for (auto* line : m_penHandleLines) {
         m_scene->removeItem(line);
         delete line;
     }
     m_penHandleLines.clear();
     for (auto* dot : m_penHandleDots) {
         m_scene->removeItem(dot);
         delete dot;
     }
     m_penHandleDots.clear();
     for (auto* midDot : m_penMidpointDots) {
         m_scene->removeItem(midDot);
         delete midDot;
     }
     m_penMidpointDots.clear();
    
    if (m_penPoints.isEmpty()) return;
    
    QPainterPath path;
    path.moveTo(m_penPoints.first().pos);
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        PenPoint& p = m_penPoints[i];
        PenPoint& next = m_penPoints[(i + 1) % m_penPoints.size()];
        
        QPointF cp1 = p.pos + p.handleOut;
        QPointF cp2 = next.pos + next.handleIn;
        
        if (p.handleOut != QPointF(0, 0) || next.handleIn != QPointF(0, 0)) {
            path.cubicTo(cp1, cp2, next.pos);
        } else {
            path.lineTo(next.pos);
        }
    }
    
    m_penPreviewItem = m_scene->addPath(path, QPen(Qt::cyan, 1.5, Qt::DashLine), QBrush());
    m_penPreviewItem->setZValue(1000);
    
     for (int i = 0; i < m_penPoints.size(); ++i) {
         PenPoint& p = m_penPoints[i];
         
         QColor anchorColor;
         if (i == m_selectedPenPoint) {
             anchorColor = QColor(66, 165, 245);
         } else if (i == 0) {
             anchorColor = Qt::green;
         } else {
             anchorColor = Qt::yellow;
         }
         
         int size = (i == m_selectedPenPoint) ? 10 : 8;
         int offset = size / 2;
         auto* marker = m_scene->addEllipse(p.pos.x() - offset, p.pos.y() - offset, size, size, 
                                           QPen(anchorColor, 1.5), QBrush(anchorColor));
         marker->setZValue(1001);
         m_penPointMarkers.append(marker);
         
         if (p.handleIn != QPointF(0, 0)) {
             QPointF handlePos = p.pos + p.handleIn;
             QColor handleColor = (i == m_selectedPenPoint && m_selectedPenHandle == 0) 
                                 ? QColor(255, 152, 0)
                                 : QColor(156, 39, 176);
             
             auto* handleLine = m_scene->addLine(QLineF(p.pos, handlePos), QPen(handleColor, 1.5));
             handleLine->setZValue(1001);
             m_penHandleLines.append(handleLine);
             
             auto* handleDot = m_scene->addEllipse(handlePos.x() - 3, handlePos.y() - 3, 6, 6,
                                                   QPen(handleColor, 1.5), QBrush(handleColor));
             handleDot->setZValue(1001);
             m_penHandleDots.append(handleDot);
         }
         
         if (p.handleOut != QPointF(0, 0)) {
             QPointF handlePos = p.pos + p.handleOut;
             QColor handleColor = (i == m_selectedPenPoint && m_selectedPenHandle == 1) 
                                 ? QColor(255, 152, 0)
                                 : QColor(156, 39, 176);
             
             auto* handleLine = m_scene->addLine(QLineF(p.pos, handlePos), QPen(handleColor, 1.5));
             handleLine->setZValue(1001);
             m_penHandleLines.append(handleLine);
             
             auto* handleDot = m_scene->addEllipse(handlePos.x() - 3, handlePos.y() - 3, 6, 6,
                                                   QPen(handleColor, 1.5), QBrush(handleColor));
             handleDot->setZValue(1001);
             m_penHandleDots.append(handleDot);
         }
     }
     
     for (int i = 0; i < m_penPoints.size(); ++i) {
         PenPoint& p1 = m_penPoints[i];
         PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
         QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
         QColor midpointColor = (i == m_selectedPenMidpoint) 
                               ? QColor(0, 255, 255)
                               : QColor(0, 200, 200);
         
         auto* midpointDot = m_scene->addEllipse(midpoint.x() - 4, midpoint.y() - 4, 8, 8,
                                                 QPen(midpointColor, 1.5), QBrush(midpointColor));
         midpointDot->setZValue(1000);
         m_penMidpointDots.append(midpointDot);
     }
}

QPointF SymbolCanvas::calculateBezierPoint(const PenPoint& p1, const PenPoint& p2, qreal t) const {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    
    qreal mt = 1.0 - t;
    qreal mt2 = mt * mt;
    qreal mt3 = mt2 * mt;
    qreal t2 = t * t;
    qreal t3 = t2 * t;
    
    QPointF p0 = p1.pos;
    QPointF p3 = p2.pos;
    QPointF cp1 = p1.pos + p1.handleOut;
    QPointF cp2 = p2.pos + p2.handleIn;
    
    return mt3 * p0 + 3 * mt2 * t * cp1 + 3 * mt * t2 * cp2 + t3 * p3;
}

void SymbolCanvas::onPenClicked(QPointF pos, int pointIndex, int handleIndex) {
     if (currentTool() != 13) return; // Pen tool
     
     const qreal HIT_RADIUS = 10.0;
     int closestPoint = -1;
     int closestHandle = -1;
     int closestMidpoint = -1;
     qreal closestDist = HIT_RADIUS + 1;
     
     for (int i = 0; i < m_penPoints.size(); ++i) {
         qreal distToPos = QLineF(pos, m_penPoints[i].pos).length();
         if (distToPos < closestDist) {
             closestDist = distToPos;
             closestPoint = i;
             closestHandle = -1;
             closestMidpoint = -1;
         }
         
         if (m_penPoints[i].handleIn != QPointF(0, 0)) {
             QPointF handlePos = m_penPoints[i].pos + m_penPoints[i].handleIn;
             qreal dist = QLineF(pos, handlePos).length();
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = i;
                 closestHandle = 0;
                 closestMidpoint = -1;
             }
         }
         
         if (m_penPoints[i].handleOut != QPointF(0, 0)) {
             QPointF handlePos = m_penPoints[i].pos + m_penPoints[i].handleOut;
             qreal dist = QLineF(pos, handlePos).length();
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = i;
                 closestHandle = 1;
                 closestMidpoint = -1;
             }
         }
     }
     
     if (m_penPoints.size() >= 2) {
         for (int i = 0; i < m_penPoints.size(); ++i) {
             PenPoint& p1 = m_penPoints[i];
             PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
             QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
             qreal dist = QLineF(pos, midpoint).length();
             
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = -1;
                 closestHandle = -1;
                 closestMidpoint = i;
             }
         }
     }
     
     if (closestMidpoint != -1) {
         m_selectedPenMidpoint = closestMidpoint;
         updatePenPreview();
     } else if (closestPoint == -1) {
         if (m_penPoints.size() > 2) {
             if (QLineF(pos, m_penPoints.first().pos).length() < 8.0) {
                 onPenPathClosed();
                 return;
             }
         }
         
         PenPoint newPoint;
         newPoint.pos = pos;
         newPoint.handleIn = QPointF(0, 0);
         newPoint.handleOut = QPointF(0, 0);
         newPoint.smooth = false;
         newPoint.corner = true;
         m_penPoints.append(newPoint);
         updatePenPreview();
     } else {
         bool isAltPressed = (handleIndex == 1);
         
         if (closestHandle == -1) {
             if (isAltPressed) {
                 m_penPoints[closestPoint].smooth = !m_penPoints[closestPoint].smooth;
                 m_penPoints[closestPoint].corner = !m_penPoints[closestPoint].corner;
                 if (m_penPoints[closestPoint].smooth && 
                     m_penPoints[closestPoint].handleOut != QPointF(0, 0)) {
                     m_penPoints[closestPoint].handleIn = -m_penPoints[closestPoint].handleOut;
                 }
             } else {
                 if (m_selectedPenPoint == closestPoint) {
                     m_selectedPenPoint = -1;
                 } else {
                     m_selectedPenPoint = closestPoint;
                     m_selectedPenHandle = -1;
                 }
             }
         } else {
             m_selectedPenPoint = closestPoint;
             m_selectedPenHandle = closestHandle;
         }
         updatePenPreview();
     }
}

void SymbolCanvas::onPenDoubleClicked(QPointF pos, int pointIndex) {
    if (currentTool() != 13) return; // Pen tool
    
    const qreal HIT_RADIUS = 10.0;
    int closestPoint = -1;
    qreal closestDist = HIT_RADIUS + 1;
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        qreal dist = QLineF(pos, m_penPoints[i].pos).length();
        if (dist < closestDist) {
            closestDist = dist;
            closestPoint = i;
        }
    }
    
    if (closestPoint != -1 && m_penPoints.size() > 2) {
        m_penPoints.removeAt(closestPoint);
        if (m_selectedPenPoint == closestPoint) {
            m_selectedPenPoint = -1;
        }
        updatePenPreview();
    }
}

void SymbolCanvas::updateBezierEditPreview() {
    for (auto* marker : m_bezierEditMarkers) {
        m_scene->removeItem(marker);
        delete marker;
    }
    m_bezierEditMarkers.clear();
    for (auto* line : m_bezierEditLines) {
        m_scene->removeItem(line);
        delete line;
    }
    m_bezierEditLines.clear();
    
    if (m_editingBezierIndex < 0 || m_editingBezierIndex >= m_editor->m_symbol.primitives().size()) {
        m_editingBezierIndex = -1;
        m_selectedBezierPoint = -1;
        return;
    }
    
    const SymbolPrimitive& prim = m_editor->m_symbol.primitives()[m_editingBezierIndex];
    if (prim.type != SymbolPrimitive::Bezier) {
        m_editingBezierIndex = -1;
        return;
    }
    
     auto getPoint = [&](const QString& x_key, const QString& y_key) -> QPointF {
         double x = prim.data.contains(x_key) ? prim.data.value(x_key).toDouble() : 0.0;
         double y = prim.data.contains(y_key) ? prim.data.value(y_key).toDouble() : 0.0;
         return QPointF(x, y);
     };
    
    QPointF p1 = getPoint("x1", "y1");
    QPointF cp1 = getPoint("x2", "y2");
    QPointF cp2 = getPoint("x3", "y3");
    QPointF p4 = getPoint("x4", "y4");
    
    m_bezierEditPoints.clear();
    m_bezierEditPoints.append({0, p1});
    m_bezierEditPoints.append({1, cp1});
    m_bezierEditPoints.append({2, cp2});
    m_bezierEditPoints.append({3, p4});
    
    auto drawHandle = [&](QPointF anchor, QPointF control) {
        auto* line = m_scene->addLine(QLineF(anchor, control), 
                                     QPen(QColor(150, 150, 150), 1.5, Qt::DashLine));
        line->setZValue(1000);
        m_bezierEditLines.append(line);
    };
    
    drawHandle(p1, cp1);
    drawHandle(p4, cp2);
    
    for (int i = 0; i < m_bezierEditPoints.size(); ++i) {
        const BezierEditPoint& bp = m_bezierEditPoints[i];
        
        QColor color;
        int size;
        
        if (i == m_selectedBezierPoint) {
            color = QColor(66, 165, 245);
            size = 10;
        } else if (i == 0) {
            color = Qt::green;
            size = 8;
        } else if (i == 3) {
            color = QColor(33, 150, 243);
            size = 8;
        } else {
            color = QColor(156, 39, 176);
            size = 6;
        }
        
        int offset = size / 2;
        auto* marker = m_scene->addEllipse(bp.pos.x() - offset, bp.pos.y() - offset, size, size,
                                          QPen(color, 1.5), QBrush(color));
        marker->setZValue(1001);
        m_bezierEditMarkers.append(marker);
    }
}

void SymbolCanvas::onBezierEditPointClicked(QPointF pos) {
    if (m_editingBezierIndex < 0 || m_bezierEditPoints.isEmpty()) return;
    
    const double HIT_RADIUS = 10.0;
    
    int clickedPoint = -1;
    for (int i = 0; i < m_bezierEditPoints.size(); ++i) {
        QPointF delta = m_bezierEditPoints[i].pos - pos;
        double dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (dist <= HIT_RADIUS) {
            clickedPoint = i;
            break;
        }
    }
    
    if (clickedPoint >= 0) {
        m_selectedBezierPoint = clickedPoint;
        updateBezierEditPreview();
    } else {
        m_selectedBezierPoint = -1;
        updateBezierEditPreview();
    }
}

void SymbolCanvas::onBezierEditPointDragged(QPointF newPos) {
    if (m_editingBezierIndex < 0 || m_selectedBezierPoint < 0) return;
    if (m_editingBezierIndex >= m_editor->m_symbol.primitives().size()) return;
    
    SymbolPrimitive& prim = m_editor->m_symbol.primitives()[m_editingBezierIndex];
    
    const QStringList keys = {"x1", "y1", "x2", "y2", "x3", "y3", "x4", "y4"};
    int pointType = m_selectedBezierPoint;
    
    if (pointType >= 0 && pointType < 4) {
        int x_idx = pointType * 2;
        int y_idx = pointType * 2 + 1;
        
        prim.data[keys[x_idx]] = newPos.x();
        prim.data[keys[y_idx]] = newPos.y();
        
        updateVisualForPrimitive(m_editingBezierIndex, prim);
        updateBezierEditPreview();
    }
}

void SymbolCanvas::onRectResizeStarted(const QString& corner, QPointF scenePos) {
    if (!m_scene || currentTool() != 0) return; // Select tool
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = primitiveIndex(selected.first());
    if (idx < 0 || idx >= m_editor->m_symbol.primitives().size()) return;
    const SymbolPrimitive& prim = m_editor->m_symbol.primitives().at(idx);
    m_rectResizeSessionActive = true;
    m_rectResizePrimIdx = idx;
    m_rectResizeCorner = corner;
    m_rectResizeOldDef = m_editor->symbolDefinition();
    m_rectResizeAnchor = QPointF();
    m_resizeLineOtherEnd = QPointF();
    m_resizeCircleCenter = QPointF();

    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        const qreal x = prim.data.value("x").toDouble();
        const qreal y = prim.data.value("y").toDouble();
        const qreal w = prim.data.contains("width") ? prim.data.value("width").toDouble() : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height") ? prim.data.value("height").toDouble() : prim.data.value("h").toDouble();
        QRectF r(x, y, w, h);
        r = r.normalized();
        if (r.isNull()) {
            m_rectResizeSessionActive = false;
            return;
        }
        if (corner == "tl") m_rectResizeAnchor = r.bottomRight();
        else if (corner == "tr") m_rectResizeAnchor = r.bottomLeft();
        else if (corner == "bl") m_rectResizeAnchor = r.topRight();
        else m_rectResizeAnchor = r.topLeft(); // "br"
    } else if (prim.type == SymbolPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        m_resizeLineOtherEnd = (corner == "p1") ? p2 : p1;
    } else if (prim.type == SymbolPrimitive::Circle) {
        const qreal cx = prim.data.contains("centerX") ? prim.data.value("centerX").toDouble() : prim.data.value("cx").toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data.value("centerY").toDouble() : prim.data.value("cy").toDouble();
        m_resizeCircleCenter = QPointF(cx, cy);
    } else {
        m_rectResizeSessionActive = false;
        return;
    }

    onRectResizeUpdated(scenePos);
}

void SymbolCanvas::onRectResizeUpdated(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;
    if (m_rectResizePrimIdx < 0 || m_rectResizePrimIdx >= m_editor->m_symbol.primitives().size()) return;

    SymbolPrimitive& prim = m_editor->m_symbol.primitives()[m_rectResizePrimIdx];
    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        QPointF p = scenePos;
        const qreal minSize = qMax<qreal>(1.0, gridSize() * 0.5);
        QRectF r(m_rectResizeAnchor, p);
        r = r.normalized();
        if (r.width() < minSize) r.setWidth(minSize);
        if (r.height() < minSize) r.setHeight(minSize);

        prim.data["x"] = r.left();
        prim.data["y"] = r.top();
        prim.data["width"] = r.width();
        prim.data["height"] = r.height();
        prim.data["w"] = r.width();
        prim.data["h"] = r.height();
    } else if (prim.type == SymbolPrimitive::Line) {
        if (m_rectResizeCorner == "p1") {
            prim.data["x1"] = scenePos.x();
            prim.data["y1"] = scenePos.y();
            prim.data["x2"] = m_resizeLineOtherEnd.x();
            prim.data["y2"] = m_resizeLineOtherEnd.y();
        } else {
            prim.data["x2"] = scenePos.x();
            prim.data["y2"] = scenePos.y();
            prim.data["x1"] = m_resizeLineOtherEnd.x();
            prim.data["y1"] = m_resizeLineOtherEnd.y();
        }
    } else if (prim.type == SymbolPrimitive::Circle) {
        qreal radius = 1.0;
        if (m_rectResizeCorner == "east" || m_rectResizeCorner == "west") {
            radius = qAbs(scenePos.x() - m_resizeCircleCenter.x());
        } else if (m_rectResizeCorner == "north" || m_rectResizeCorner == "south") {
            radius = qAbs(scenePos.y() - m_resizeCircleCenter.y());
        } else {
            radius = QLineF(scenePos, m_resizeCircleCenter).length();
        }
        const qreal minR = qMax<qreal>(0.5, gridSize() * 0.25);
        if (radius < minR) radius = minR;
        prim.data["centerX"] = m_resizeCircleCenter.x();
        prim.data["centerY"] = m_resizeCircleCenter.y();
        prim.data["cx"] = m_resizeCircleCenter.x();
        prim.data["cy"] = m_resizeCircleCenter.y();
        prim.data["radius"] = radius;
        prim.data["r"] = radius;
    } else {
        return;
    }

    updateVisualForPrimitive(m_rectResizePrimIdx, prim);
    updateResizeHandles();
}

void SymbolCanvas::onRectResizeFinished(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;
    onRectResizeUpdated(scenePos);

    SymbolDefinition newDef = m_editor->symbolDefinition();
    const bool changed = (QJsonDocument(newDef.toJson()).toJson(QJsonDocument::Compact) !=
                          QJsonDocument(m_rectResizeOldDef.toJson()).toJson(QJsonDocument::Compact));
    if (changed) {
        m_editor->m_undoStack->push(new UpdateSymbolCommand(m_editor, m_rectResizeOldDef, newDef, "Resize Rectangle"));
    } else {
        m_editor->applySymbolDefinition(m_rectResizeOldDef);
    }

    m_rectResizeSessionActive = false;
    m_rectResizePrimIdx = -1;
    m_rectResizeCorner.clear();
}

void SymbolCanvas::onSelectionChanged() {
    if (currentTool() == 0) { // Select tool
        m_editingBezierIndex = -1;
        m_selectedBezierPoint = -1;
        
        int selectedBezierIndex = -1;
        int selectedCount = 0;
        
        for (QGraphicsItem* item : m_scene->selectedItems()) {
            bool isPrimitive = item->data(1).isValid();
            if (isPrimitive) {
                selectedCount++;
                int primIndex = primitiveIndex(item);
                if (primIndex >= 0 && primIndex < m_editor->m_symbol.primitives().size()) {
                    if (m_editor->m_symbol.primitives()[primIndex].type == SymbolPrimitive::Bezier) {
                        selectedBezierIndex = primIndex;
                    }
                }
            }
        }
        
        if (selectedCount == 1 && selectedBezierIndex >= 0) {
            m_editingBezierIndex = selectedBezierIndex;
            updateBezierEditPreview();
        }
    }

    for (QGraphicsItem* item : m_scene->items()) {
        bool isPrimitive = item->data(1).isValid();
        bool isLabel = (item->data(0).toString() == "label");
        
        if (isPrimitive || isLabel) {
            if (item->isSelected()) {
                if (!item->graphicsEffect()) {
                    auto* glow = new QGraphicsDropShadowEffect();
                    glow->setBlurRadius(15);
                    glow->setOffset(0);
                    glow->setColor(QColor("#71717a"));
                    item->setGraphicsEffect(glow);
                }
            } else {
                if (item->graphicsEffect()) {
                    item->setGraphicsEffect(nullptr);
                }
            }
        }
    }
}
