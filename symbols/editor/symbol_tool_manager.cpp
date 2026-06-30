/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_tool_manager.h"
#include "symbol_canvas.h"
#include "../symbol_editor.h"
#include "theme_manager.h"
#include "../symbol_commands.h"
#include "../../ui/property_editor.h"
#include "../ui/text_properties_dialog.h"
#include "../../core/visuals/text_resolver.h"

#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMessageBox>
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QStatusBar>
#include <cmath>

// --- SymbolTool ---
SymbolTool::SymbolTool(SymbolCanvas* canvas, QObject* parent)
    : QObject(parent), m_canvas(canvas) {}

// --- SelectTool ---
void SelectTool::onSelectionChanged() {
    m_canvas->m_editingBezierIndex = -1;
    m_canvas->m_selectedBezierPoint = -1;
    
    int selectedBezierIndex = -1;
    int selectedCount = 0;
    
    for (QGraphicsItem* item : m_canvas->m_scene->selectedItems()) {
        bool isPrimitive = item->data(1).isValid();
        if (isPrimitive) {
            selectedCount++;
            int primIndex = m_canvas->primitiveIndex(item);
            if (primIndex >= 0 && primIndex < m_canvas->m_editor->m_symbol.primitives().size()) {
                if (m_canvas->m_editor->m_symbol.primitives()[primIndex].type == SymbolPrimitive::Bezier) {
                    selectedBezierIndex = primIndex;
                }
            }
        }
    }
    
    if (selectedCount == 1 && selectedBezierIndex >= 0) {
        m_canvas->m_editingBezierIndex = selectedBezierIndex;
        m_canvas->updateBezierEditPreview();
    }
}

void SelectTool::onBezierEditPointClicked(QPointF pos) {
    if (m_canvas->m_editingBezierIndex < 0 || m_canvas->m_bezierEditPoints.isEmpty()) return;
    
    const double HIT_RADIUS = 10.0;
    
    int clickedPoint = -1;
    for (int i = 0; i < m_canvas->m_bezierEditPoints.size(); ++i) {
        QPointF delta = m_canvas->m_bezierEditPoints[i].pos - pos;
        double dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (dist <= HIT_RADIUS) {
            clickedPoint = i;
            break;
        }
    }
    
    if (clickedPoint >= 0) {
        m_canvas->m_selectedBezierPoint = clickedPoint;
        m_canvas->updateBezierEditPreview();
    } else {
        m_canvas->m_selectedBezierPoint = -1;
        m_canvas->updateBezierEditPreview();
    }
}

void SelectTool::onBezierEditPointDragged(QPointF newPos) {
    if (m_canvas->m_editingBezierIndex < 0 || m_canvas->m_selectedBezierPoint < 0) return;
    if (m_canvas->m_editingBezierIndex >= m_canvas->m_editor->m_symbol.primitives().size()) return;
    
    SymbolPrimitive& prim = m_canvas->m_editor->m_symbol.primitives()[m_canvas->m_editingBezierIndex];
    
    const QStringList keys = {"x1", "y1", "x2", "y2", "x3", "y3", "x4", "y4"};
    int pointType = m_canvas->m_selectedBezierPoint;
    
    if (pointType >= 0 && pointType < 4) {
        int x_idx = pointType * 2;
        int y_idx = pointType * 2 + 1;
        
        prim.data[keys[x_idx]] = newPos.x();
        prim.data[keys[y_idx]] = newPos.y();
        
        m_canvas->updateVisualForPrimitive(m_canvas->m_editingBezierIndex, prim);
        m_canvas->updateBezierEditPreview();
    }
}

void SelectTool::onRectResizeStarted(const QString& corner, QPointF scenePos) {
    if (!m_canvas->m_scene) return;
    const QList<QGraphicsItem*> selected = m_canvas->m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = m_canvas->primitiveIndex(selected.first());
    if (idx < 0 || idx >= m_canvas->m_editor->m_symbol.primitives().size()) return;
    const SymbolPrimitive& prim = m_canvas->m_editor->m_symbol.primitives().at(idx);
    m_canvas->m_rectResizeSessionActive = true;
    m_canvas->m_rectResizePrimIdx = idx;
    m_canvas->m_rectResizeCorner = corner;
    m_canvas->m_rectResizeOldDef = m_canvas->m_editor->symbolDefinition();
    m_canvas->m_rectResizeAnchor = QPointF();
    m_canvas->m_resizeLineOtherEnd = QPointF();
    m_canvas->m_resizeCircleCenter = QPointF();

    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        const qreal x = prim.data.value("x").toDouble();
        const qreal y = prim.data.value("y").toDouble();
        const qreal w = prim.data.contains("width") ? prim.data.value("width").toDouble() : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height") ? prim.data.value("height").toDouble() : prim.data.value("h").toDouble();
        QRectF r(x, y, w, h);
        r = r.normalized();
        if (r.isNull()) {
            m_canvas->m_rectResizeSessionActive = false;
            return;
        }
        if (corner == "tl") m_canvas->m_rectResizeAnchor = r.bottomRight();
        else if (corner == "tr") m_canvas->m_rectResizeAnchor = r.bottomLeft();
        else if (corner == "bl") m_canvas->m_rectResizeAnchor = r.topRight();
        else m_canvas->m_rectResizeAnchor = r.topLeft(); // "br"
    } else if (prim.type == SymbolPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        m_canvas->m_resizeLineOtherEnd = (corner == "p1") ? p2 : p1;
    } else if (prim.type == SymbolPrimitive::Circle) {
        const qreal cx = prim.data.contains("centerX") ? prim.data.value("centerX").toDouble() : prim.data.value("cx").toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data.value("centerY").toDouble() : prim.data.value("cy").toDouble();
        m_canvas->m_resizeCircleCenter = QPointF(cx, cy);
    } else {
        m_canvas->m_rectResizeSessionActive = false;
        return;
    }

    onRectResizeUpdated(scenePos);
}

void SelectTool::onRectResizeUpdated(QPointF scenePos) {
    if (!m_canvas->m_rectResizeSessionActive) return;
    if (m_canvas->m_rectResizePrimIdx < 0 || m_canvas->m_rectResizePrimIdx >= m_canvas->m_editor->m_symbol.primitives().size()) return;

    SymbolPrimitive& prim = m_canvas->m_editor->m_symbol.primitives()[m_canvas->m_rectResizePrimIdx];
    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        QPointF p = scenePos;
        const qreal minSize = qMax<qreal>(1.0, m_canvas->gridSize() * 0.5);
        QRectF r(m_canvas->m_rectResizeAnchor, p);
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
        if (m_canvas->m_rectResizeCorner == "p1") {
            prim.data["x1"] = scenePos.x();
            prim.data["y1"] = scenePos.y();
            prim.data["x2"] = m_canvas->m_resizeLineOtherEnd.x();
            prim.data["y2"] = m_canvas->m_resizeLineOtherEnd.y();
        } else {
            prim.data["x2"] = scenePos.x();
            prim.data["y2"] = scenePos.y();
            prim.data["x1"] = m_canvas->m_resizeLineOtherEnd.x();
            prim.data["y1"] = m_canvas->m_resizeLineOtherEnd.y();
        }
    } else if (prim.type == SymbolPrimitive::Circle) {
        qreal radius = 1.0;
        if (m_canvas->m_rectResizeCorner == "east" || m_canvas->m_rectResizeCorner == "west") {
            radius = qAbs(scenePos.x() - m_canvas->m_resizeCircleCenter.x());
        } else if (m_canvas->m_rectResizeCorner == "north" || m_canvas->m_rectResizeCorner == "south") {
            radius = qAbs(scenePos.y() - m_canvas->m_resizeCircleCenter.y());
        } else {
            radius = QLineF(scenePos, m_canvas->m_resizeCircleCenter).length();
        }
        const qreal minR = qMax<qreal>(0.5, m_canvas->gridSize() * 0.25);
        if (radius < minR) radius = minR;
        prim.data["centerX"] = m_canvas->m_resizeCircleCenter.x();
        prim.data["centerY"] = m_canvas->m_resizeCircleCenter.y();
        prim.data["cx"] = m_canvas->m_resizeCircleCenter.x();
        prim.data["cy"] = m_canvas->m_resizeCircleCenter.y();
        prim.data["radius"] = radius;
        prim.data["r"] = radius;
    } else {
        return;
    }

    m_canvas->updateVisualForPrimitive(m_canvas->m_rectResizePrimIdx, prim);
    m_canvas->updateResizeHandles();
}

void SelectTool::onRectResizeFinished(QPointF scenePos) {
    if (!m_canvas->m_rectResizeSessionActive) return;
    onRectResizeUpdated(scenePos);

    SymbolDefinition newDef = m_canvas->m_editor->symbolDefinition();
    const bool changed = (QJsonDocument(newDef.toJson()).toJson(QJsonDocument::Compact) !=
                          QJsonDocument(m_canvas->m_rectResizeOldDef.toJson()).toJson(QJsonDocument::Compact));
    if (changed) {
        m_canvas->m_editor->m_undoStack->push(new UpdateSymbolCommand(m_canvas->m_editor, m_canvas->m_rectResizeOldDef, newDef, "Resize Rectangle"));
    } else {
        m_canvas->m_editor->applySymbolDefinition(m_canvas->m_rectResizeOldDef);
    }

    m_canvas->m_rectResizeSessionActive = false;
    m_canvas->m_rectResizePrimIdx = -1;
    m_canvas->m_rectResizeCorner.clear();
}

void SelectTool::onItemsMoved(QPointF delta) {
    QList<int> indices;
    bool referenceMoved = false;
    bool nameMoved = false;
    QPointF newRefPos, newNamePos;

    for (QGraphicsItem* item : m_canvas->m_scene->selectedItems()) {
        if (item->data(0).toString() == "label") {
            QString type = item->data(1).toString();
            if (type == "reference") { referenceMoved = true; newRefPos = item->pos(); }
            else if (type == "name") { nameMoved = true; newNamePos = item->pos(); }
            continue;
        }

        int idx = m_canvas->primitiveIndex(item);
        if (idx != -1 && !indices.contains(idx))
            indices.append(idx);
    }
    
    if (indices.isEmpty() && !referenceMoved && !nameMoved) return;

    SymbolDefinition oldDef = m_canvas->m_editor->symbolDefinition();
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
            const qreal threshold = qMax<qreal>(2.0, m_canvas->gridSize() * 0.4);
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
    m_canvas->m_editor->m_undoStack->push(new UpdateSymbolCommand(m_canvas->m_editor, oldDef, newDef, "Move Items"));
}

// --- DrawPinTool ---
void DrawPinTool::activate() {
    m_canvas->updatePinPreview(m_canvas->snapToGrid(m_canvas->mapToScene(m_canvas->mapFromGlobal(QCursor::pos()))));
}

void DrawPinTool::deactivate() {
    if (m_canvas->m_previewItem) {
        if (m_canvas->m_previewItem->scene()) m_canvas->m_scene->removeItem(m_canvas->m_previewItem);
        delete m_canvas->m_previewItem;
        m_canvas->m_previewItem = nullptr;
    }
}

void DrawPinTool::onPointClicked(QPointF pos) {
    pos = m_canvas->snapToGrid(pos);
    int pinCount = 0;
    for (const auto& p : m_canvas->m_editor->m_symbol.primitives()) {
        if (p.type == SymbolPrimitive::Pin) ++pinCount;
    }
    int pinNum = pinCount + 1;
    SymbolPrimitive prim = SymbolPrimitive::createPin(pos, pinNum,
                                                     QString::number(pinNum),
                                                     m_canvas->m_previewOrientation);
    prim.setUnit(m_canvas->m_editor->m_currentUnit);
    prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);
    QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
    if (visual) {
        m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
    }
}

void DrawPinTool::onMouseMoved(QPointF pos) {
    m_canvas->updatePinPreview(pos);
}

void DrawPinTool::rotatePin() {
    if (m_canvas->m_previewOrientation == "Right") m_canvas->m_previewOrientation = "Down";
    else if (m_canvas->m_previewOrientation == "Down") m_canvas->m_previewOrientation = "Left";
    else if (m_canvas->m_previewOrientation == "Left") m_canvas->m_previewOrientation = "Up";
    else m_canvas->m_previewOrientation = "Right";
    m_canvas->updatePinPreview(m_canvas->snapToGrid(m_canvas->mapToScene(m_canvas->mapFromGlobal(QCursor::pos()))));
    if (m_canvas->m_editor) {
        m_canvas->m_editor->statusBar()->showMessage("Pin orientation: " + m_canvas->m_previewOrientation, 1200);
    }
}

void DrawPinTool::flipPin() {
    if (m_canvas->m_previewOrientation == "Right") m_canvas->m_previewOrientation = "Left";
    else if (m_canvas->m_previewOrientation == "Left") m_canvas->m_previewOrientation = "Right";
    m_canvas->updatePinPreview(m_canvas->snapToGrid(m_canvas->mapToScene(m_canvas->mapFromGlobal(QCursor::pos()))));
    if (m_canvas->m_editor) {
        m_canvas->m_editor->statusBar()->showMessage("Pin orientation: " + m_canvas->m_previewOrientation, 1200);
    }
}

// --- DrawLineTool ---
void DrawLineTool::onPointClicked(QPointF pos) {
    pos = m_canvas->snapToGrid(pos);
    m_canvas->m_polyPoints.append(pos);
    if (m_canvas->m_polyPoints.size() == 2) {
        QPointF p1 = m_canvas->m_polyPoints[0];
        QPointF p2 = m_canvas->m_polyPoints[1];
        SymbolPrimitive prim;
        
        int tool = m_canvas->currentTool();
        if (tool == 1) { // Line
            prim = SymbolPrimitive::createLine(p1, p2);
        } else if (tool == 2) { // Rect
            prim = SymbolPrimitive::createRect(QRectF(p1, p2).normalized(), false);
        } else if (tool == 3) { // Circle
            prim = SymbolPrimitive::createCircle(p1, QLineF(p1, p2).length(), false);
        } else { // Arc
            qreal rx = qAbs(p2.x() - p1.x());
            qreal ry = qAbs(p2.y() - p1.y());
            prim = SymbolPrimitive::createArc(QRectF(p1.x()-rx, p1.y()-ry, rx*2, ry*2), 0, 180 * 16);
        }
        prim.setUnit(m_canvas->m_editor->m_currentUnit);
        prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);

        QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
        m_canvas->m_polyPoints.clear();
        if (visual) {
            m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
        }
    }
}

void DrawLineTool::onMouseMoved(QPointF pos) {
    if (m_canvas->m_polyPoints.isEmpty()) return;
    const QPen previewPen(Qt::cyan, 1, Qt::DashLine);
    QPointF start = m_canvas->m_polyPoints.first();
    QPointF end = pos;
    int tool = m_canvas->currentTool();
    if (tool == 1) { // Line
        m_canvas->m_previewItem = m_canvas->m_scene->addLine(QLineF(start, end), previewPen);
    } else if (tool == 2) { // Rect
        QRectF r = QRectF(start, end).normalized();
        m_canvas->m_previewItem = m_canvas->m_scene->addRect(r, previewPen);
    } else if (tool == 3) { // Circle
        qreal rad = QLineF(start, end).length();
        m_canvas->m_previewItem = m_canvas->m_scene->addEllipse(
            start.x()-rad, start.y()-rad, rad*2, rad*2, previewPen);
    } else if (tool == 4) { // Arc
        qreal rx = qAbs(end.x() - start.x());
        qreal ry = qAbs(end.y() - start.y());
        QRectF r(start.x()-rx, start.y()-ry, rx*2, ry*2);
        QPainterPath path;
        path.arcMoveTo(r, 0);
        path.arcTo(r, 0, 180);
        m_canvas->m_previewItem = m_canvas->m_scene->addPath(path, previewPen);
    }
}

// --- DrawShapeTool ---
void DrawShapeTool::onPointClicked(QPointF pos) {
    pos = m_canvas->snapToGrid(pos);
    int tool = m_canvas->currentTool();
    if (tool == 5) { // Text tool
        TextPropertiesDialog dlg(m_canvas->m_editor);
        if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
            SymbolPrimitive prim = SymbolPrimitive::createText(dlg.text(), pos, dlg.fontSize(), dlg.color());
            prim.setUnit(m_canvas->m_editor->m_currentUnit);
            prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);
            QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
            if (visual) {
                m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
            }
        }
    } else if (tool == 7) { // Polygon
        m_canvas->m_polyPoints.append(pos);
        if (m_canvas->m_polyPoints.size() > 2
            && QLineF(pos, m_canvas->m_polyPoints.first()).length() < 8.0) {
            m_canvas->m_polyPoints.removeLast();
            SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_canvas->m_polyPoints, false);
            prim.setUnit(m_canvas->m_editor->m_currentUnit);
            prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);
            QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
            m_canvas->m_polyPoints.clear();
            if (m_canvas->m_previewItem) {
                m_canvas->m_scene->removeItem(m_canvas->m_previewItem);
                delete m_canvas->m_previewItem;
                m_canvas->m_previewItem = nullptr;
            }
            if (visual) {
                m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
            }
        }
    } else if (tool == 11) { // Bezier
        m_canvas->m_polyPoints.append(pos);
        if (m_canvas->m_polyPoints.size() == 4) {
            SymbolPrimitive prim = SymbolPrimitive::createBezier(m_canvas->m_polyPoints[0], m_canvas->m_polyPoints[2], m_canvas->m_polyPoints[3], m_canvas->m_polyPoints[1]);
            prim.setUnit(m_canvas->m_editor->m_currentUnit);
            prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);
            QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
            m_canvas->m_polyPoints.clear();
            if (m_canvas->m_previewItem) {
                m_canvas->m_scene->removeItem(m_canvas->m_previewItem);
                delete m_canvas->m_previewItem;
                m_canvas->m_previewItem = nullptr;
            }
            if (visual) {
                m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
            }
        }
    } else if (tool == 10) { // Anchor
        SymbolDefinition oldDef = m_canvas->m_editor->symbolDefinition();
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
        m_canvas->m_editor->m_undoStack->push(new UpdateSymbolCommand(m_canvas->m_editor, oldDef, newDef, "Set Anchor"));
    }
}

void DrawShapeTool::onMouseMoved(QPointF pos) {
    int tool = m_canvas->currentTool();
    const QPen previewPen(Qt::cyan, 1, Qt::DashLine);
    if (tool == 11) { // Bezier
        if (m_canvas->m_polyPoints.isEmpty()) {
            m_canvas->m_previewItem = m_canvas->m_scene->addLine(QLineF(pos, pos), previewPen);
        } else if (m_canvas->m_polyPoints.size() == 1) {
            m_canvas->m_previewItem = m_canvas->m_scene->addLine(QLineF(m_canvas->m_polyPoints[0], pos), previewPen);
        } else if (m_canvas->m_polyPoints.size() == 2) {
            QPainterPath path;
            path.moveTo(m_canvas->m_polyPoints[0]);
            path.cubicTo(pos, pos, m_canvas->m_polyPoints[1]);
            m_canvas->m_previewItem = m_canvas->m_scene->addPath(path, previewPen);
        } else if (m_canvas->m_polyPoints.size() == 3) {
            QPainterPath path;
            path.moveTo(m_canvas->m_polyPoints[0]);
            path.cubicTo(m_canvas->m_polyPoints[2], pos, m_canvas->m_polyPoints[1]);
            m_canvas->m_previewItem = m_canvas->m_scene->addPath(path, previewPen);
        }
    } else if (tool == 7) { // Polygon
        if (m_canvas->m_polyPoints.isEmpty()) return;
        QPolygonF poly = m_canvas->m_polyPoints;
        poly.append(pos);
        m_canvas->m_previewItem = m_canvas->m_scene->addPolygon(poly, previewPen);
    } else if (tool == 9) { // ZoomArea
        QRectF r = QRectF(m_canvas->m_polyPoints.isEmpty() ? pos : m_canvas->m_polyPoints.first(), pos).normalized();
        m_canvas->m_previewItem = m_canvas->m_scene->addRect(r, previewPen);
    }
}

void DrawShapeTool::onDrawingFinished(QPointF start, QPointF end) {
    if (m_canvas->currentTool() == 9) { // ZoomArea
        QRectF r = QRectF(start, end).normalized();
        if (r.width() > 5 && r.height() > 5)
            m_canvas->fitInView(r, Qt::KeepAspectRatio);
    }
}

void DrawShapeTool::onPenPointAdded(QPointF pos) {
    if (m_canvas->currentTool() != 13) return; // Pen tool
    
    if (m_canvas->m_penPoints.size() > 2) {
        if (QLineF(pos, m_canvas->m_penPoints.first().pos).length() < 8.0) {
            onPenPathClosed();
            return;
        }
    }
    
    SymbolCanvas::PenPoint newPoint;
    newPoint.pos = pos;
    newPoint.handleIn = QPointF(0, 0);
    newPoint.handleOut = QPointF(0, 0);
    newPoint.smooth = false;
    newPoint.corner = false;
    m_canvas->m_penPoints.append(newPoint);
    m_canvas->updatePenPreview();
}

void DrawShapeTool::onPenHandleDragged(QPointF handlePos) {
    if (m_canvas->currentTool() != 13 || m_canvas->m_penPoints.isEmpty()) return; // Pen tool
    
    if (m_canvas->m_selectedPenMidpoint != -1) {
        int segIdx = m_canvas->m_selectedPenMidpoint;
        if (segIdx >= 0 && segIdx < m_canvas->m_penPoints.size()) {
            SymbolCanvas::PenPoint& p1 = m_canvas->m_penPoints[segIdx];
            SymbolCanvas::PenPoint& p2 = m_canvas->m_penPoints[(segIdx + 1) % m_canvas->m_penPoints.size()];
            
            QPointF midpoint = m_canvas->calculateBezierPoint(p1, p2, 0.5);
            qreal dragDist = QLineF(handlePos, midpoint).length();
            
            if (dragDist > 2.0) {
                SymbolCanvas::PenPoint newPoint;
                newPoint.pos = handlePos;
                newPoint.handleIn = QPointF(0, 0);
                newPoint.handleOut = QPointF(0, 0);
                newPoint.smooth = false;
                newPoint.corner = true;
                
                m_canvas->m_penPoints.insert(segIdx + 1, newPoint);
                m_canvas->m_selectedPenMidpoint = -1;
                m_canvas->updatePenPreview();
            }
        }
    } else if (m_canvas->m_selectedPenPoint == -1) {
        SymbolCanvas::PenPoint& lastPoint = m_canvas->m_penPoints.last();
        QPointF delta = handlePos - lastPoint.pos;
        lastPoint.handleOut = delta;
        if (lastPoint.smooth) {
            lastPoint.handleIn = -delta;
        }
    } else if (m_canvas->m_selectedPenPoint >= 0 && m_canvas->m_selectedPenPoint < m_canvas->m_penPoints.size()) {
        SymbolCanvas::PenPoint& selectedPoint = m_canvas->m_penPoints[m_canvas->m_selectedPenPoint];
        QPointF delta = handlePos - selectedPoint.pos;
        
        if (m_canvas->m_selectedPenHandle == 0) {
            selectedPoint.handleIn = delta;
            if (selectedPoint.smooth) {
                selectedPoint.handleOut = -delta;
            }
        } else if (m_canvas->m_selectedPenHandle == 1) {
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
    
    m_canvas->updatePenPreview();
}

void DrawShapeTool::onPenPointFinished() {
    if (m_canvas->currentTool() != 13) return; // Pen tool
    m_canvas->updatePenPreview();
}

void DrawShapeTool::onPenPathClosed() {
    if (m_canvas->currentTool() != 13 || m_canvas->m_penPoints.size() < 3) return; // Pen tool
    m_canvas->finalizePenPath();
}

void DrawShapeTool::onPenClicked(QPointF pos, int pointIndex, int handleIndex) {
    if (m_canvas->currentTool() != 13) return; // Pen tool
    
    const qreal HIT_RADIUS = 10.0;
    int closestPoint = -1;
    int closestHandle = -1;
    int closestMidpoint = -1;
    qreal closestDist = HIT_RADIUS + 1;
    
    for (int i = 0; i < m_canvas->m_penPoints.size(); ++i) {
        qreal distToPos = QLineF(pos, m_canvas->m_penPoints[i].pos).length();
        if (distToPos < closestDist) {
            closestDist = distToPos;
            closestPoint = i;
            closestHandle = -1;
            closestMidpoint = -1;
        }
        
        if (m_canvas->m_penPoints[i].handleIn != QPointF(0, 0)) {
            QPointF handlePos = m_canvas->m_penPoints[i].pos + m_canvas->m_penPoints[i].handleIn;
            qreal dist = QLineF(pos, handlePos).length();
            if (dist < closestDist) {
                closestDist = dist;
                closestPoint = i;
                closestHandle = 0;
                closestMidpoint = -1;
            }
        }
        
        if (m_canvas->m_penPoints[i].handleOut != QPointF(0, 0)) {
            QPointF handlePos = m_canvas->m_penPoints[i].pos + m_canvas->m_penPoints[i].handleOut;
            qreal dist = QLineF(pos, handlePos).length();
            if (dist < closestDist) {
                closestDist = dist;
                closestPoint = i;
                closestHandle = 1;
                closestMidpoint = -1;
            }
        }
    }
    
    if (m_canvas->m_penPoints.size() >= 2) {
        for (int i = 0; i < m_canvas->m_penPoints.size(); ++i) {
            SymbolCanvas::PenPoint& p1 = m_canvas->m_penPoints[i];
            SymbolCanvas::PenPoint& p2 = m_canvas->m_penPoints[(i + 1) % m_canvas->m_penPoints.size()];
            QPointF midpoint = m_canvas->calculateBezierPoint(p1, p2, 0.5);
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
        m_canvas->m_selectedPenMidpoint = closestMidpoint;
        m_canvas->updatePenPreview();
    } else if (closestPoint == -1) {
        if (m_canvas->m_penPoints.size() > 2) {
            if (QLineF(pos, m_canvas->m_penPoints.first().pos).length() < 8.0) {
                onPenPathClosed();
                return;
            }
        }
        
        SymbolCanvas::PenPoint newPoint;
        newPoint.pos = pos;
        newPoint.handleIn = QPointF(0, 0);
        newPoint.handleOut = QPointF(0, 0);
        newPoint.smooth = false;
        newPoint.corner = true;
        m_canvas->m_penPoints.append(newPoint);
        m_canvas->updatePenPreview();
    } else {
        bool isAltPressed = (handleIndex == 1);
        
        if (closestHandle == -1) {
            if (isAltPressed) {
                m_canvas->m_penPoints[closestPoint].smooth = !m_canvas->m_penPoints[closestPoint].smooth;
                m_canvas->m_penPoints[closestPoint].corner = !m_canvas->m_penPoints[closestPoint].corner;
                if (m_canvas->m_penPoints[closestPoint].smooth && 
                    m_canvas->m_penPoints[closestPoint].handleOut != QPointF(0, 0)) {
                    m_canvas->m_penPoints[closestPoint].handleIn = -m_canvas->m_penPoints[closestPoint].handleOut;
                }
            } else {
                if (m_canvas->m_selectedPenPoint == closestPoint) {
                    m_canvas->m_selectedPenPoint = -1;
                } else {
                    m_canvas->m_selectedPenPoint = closestPoint;
                    m_canvas->m_selectedPenHandle = -1;
                }
            }
        } else {
            m_canvas->m_selectedPenPoint = closestPoint;
            m_canvas->m_selectedPenHandle = closestHandle;
        }
        m_canvas->updatePenPreview();
    }
}

void DrawShapeTool::onPenDoubleClicked(QPointF pos, int pointIndex) {
    if (m_canvas->currentTool() != 13) return; // Pen tool
    
    const qreal HIT_RADIUS = 10.0;
    int closestPoint = -1;
    qreal closestDist = HIT_RADIUS + 1;
    
    for (int i = 0; i < m_canvas->m_penPoints.size(); ++i) {
        qreal dist = QLineF(pos, m_canvas->m_penPoints[i].pos).length();
        if (dist < closestDist) {
            closestDist = dist;
            closestPoint = i;
        }
    }
    
    if (closestPoint != -1 && m_canvas->m_penPoints.size() > 2) {
        m_canvas->m_penPoints.removeAt(closestPoint);
        if (m_canvas->m_selectedPenPoint == closestPoint) {
            m_canvas->m_selectedPenPoint = -1;
        }
        m_canvas->updatePenPreview();
    }
}

void DrawShapeTool::onRightClicked() {
    int tool = m_canvas->currentTool();
    if (tool == 7 && m_canvas->m_polyPoints.size() > 2) { // Polygon
        SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_canvas->m_polyPoints, false);
        prim.setUnit(m_canvas->m_editor->m_currentUnit);
        prim.setBodyStyle(m_canvas->m_editor->m_currentStyle);
        QGraphicsItem* visual = m_canvas->buildVisual(prim, m_canvas->m_editor->m_symbol.primitives().size());
        if (visual) {
            m_canvas->m_editor->m_undoStack->push(new AddPrimitiveCommand(m_canvas->m_editor, prim, visual));
        }
    } else if (tool == 13 && m_canvas->m_penPoints.size() >= 2) { // Pen
        m_canvas->finalizePenPath();
    }
}

// --- SymbolToolManager ---
SymbolToolManager::SymbolToolManager(SymbolCanvas* canvas, QObject* parent)
    : QObject(parent), m_canvas(canvas), m_activeTool(nullptr) {
    m_tools[0] = new SelectTool(canvas, this);
    
    auto* lineTool = new DrawLineTool(canvas, this);
    m_tools[1] = lineTool;
    m_tools[2] = lineTool;
    m_tools[3] = lineTool;
    m_tools[4] = lineTool;

    auto* shapeTool = new DrawShapeTool(canvas, this);
    m_tools[5] = shapeTool;
    m_tools[7] = shapeTool;
    m_tools[8] = shapeTool;
    m_tools[9] = shapeTool;
    m_tools[10] = shapeTool;
    m_tools[11] = shapeTool;
    m_tools[13] = shapeTool;

    m_tools[6] = new DrawPinTool(canvas, this);
}

SymbolToolManager::~SymbolToolManager() {
    QSet<SymbolTool*> uniqueTools;
    for (auto* tool : m_tools.values()) {
        if (tool) {
            uniqueTools.insert(tool);
        }
    }
    for (auto* tool : uniqueTools) {
        delete tool;
    }
    m_tools.clear();
}

void SymbolToolManager::setActiveTool(int toolType) {
    SymbolTool* newTool = m_tools.value(toolType, nullptr);
    if (m_activeTool != newTool) {
        if (m_activeTool) {
            m_activeTool->deactivate();
        }
        m_activeTool = newTool;
        if (m_activeTool) {
            m_activeTool->activate();
        }
    }
}
