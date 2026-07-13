/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_editor.h"
#include "footprint_library.h"
#include "footprint_commands.h"
#include "kicad_footprint_importer.h"
#include "ui/footprint_wizard_dialog.h"
#include "ui/footprint_library_browser_panel.h"
#include "ui/footprint_wizard_panel.h"
#include "ui/footprint_model_3d_panel.h"
#include "../core/visuals/theme_manager.h"
#include "../core/project/config_manager.h"
#include "../pcb/ui/pcb_3d_window.h"
#include "../pcb/items/component_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QActionGroup>
#include <QHeaderView>
#include <QInputDialog>
#include <cmath>
#include <algorithm>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileDialog>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QCloseEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QCursor>
#include <QSignalBlocker>
#include "items/footprint_primitive_item.h"
#include "analysis/footprint_engine.h"

using namespace Flux::Model;
using namespace Flux::Item;
using namespace Flux::Analysis;

namespace {
FootprintPrimitive::Layer mirroredTopBottomLayer(FootprintPrimitive::Layer layer) {
    switch (layer) {
        case FootprintPrimitive::Top_Copper: return FootprintPrimitive::Bottom_Copper;
        case FootprintPrimitive::Bottom_Copper: return FootprintPrimitive::Top_Copper;
        case FootprintPrimitive::Top_Silkscreen: return FootprintPrimitive::Bottom_Silkscreen;
        case FootprintPrimitive::Bottom_Silkscreen: return FootprintPrimitive::Top_Silkscreen;
        case FootprintPrimitive::Top_Courtyard: return FootprintPrimitive::Bottom_Courtyard;
        case FootprintPrimitive::Bottom_Courtyard: return FootprintPrimitive::Top_Courtyard;
        case FootprintPrimitive::Top_Fabrication: return FootprintPrimitive::Bottom_Fabrication;
        case FootprintPrimitive::Bottom_Fabrication: return FootprintPrimitive::Top_Fabrication;
        case FootprintPrimitive::Top_SolderMask: return FootprintPrimitive::Bottom_SolderMask;
        case FootprintPrimitive::Bottom_SolderMask: return FootprintPrimitive::Top_SolderMask;
        case FootprintPrimitive::Top_SolderPaste: return FootprintPrimitive::Bottom_SolderPaste;
        case FootprintPrimitive::Bottom_SolderPaste: return FootprintPrimitive::Top_SolderPaste;
        case FootprintPrimitive::Top_Adhesive: return FootprintPrimitive::Bottom_Adhesive;
        case FootprintPrimitive::Bottom_Adhesive: return FootprintPrimitive::Top_Adhesive;
        default: return layer;
    }
}

QList<QPointF> primitiveToPadPolygonPoints(const FootprintPrimitive& prim) {
    QList<QPointF> points;
    if (prim.type == FootprintPrimitive::Rect) {
        QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                 prim.data["width"].toDouble(), prim.data["height"].toDouble());
        r = r.normalized();
        points << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    } else if (prim.type == FootprintPrimitive::Polygon) {
        const QJsonArray arr = prim.data["points"].toArray();
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            points << QPointF(o["x"].toDouble(), o["y"].toDouble());
        }
    }
    return points;
}

qreal cross2D(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

QPointF rotatePoint90CW(const QPointF& point, const QPointF& center) {
    const QPointF rel = point - center;
    return QPointF(center.x() + rel.y(), center.y() - rel.x());
}

void rotatePrimitive90CW(FootprintPrimitive& prim, const QPointF& center) {
    if (prim.type == FootprintPrimitive::Line || prim.type == FootprintPrimitive::Dimension) {
        const QPointF p1 = rotatePoint90CW(QPointF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble()), center);
        const QPointF p2 = rotatePoint90CW(QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()), center);
        prim.data["x1"] = p1.x();
        prim.data["y1"] = p1.y();
        prim.data["x2"] = p2.x();
        prim.data["y2"] = p2.y();
        return;
    }

    if (prim.type == FootprintPrimitive::Rect) {
        QRectF rect(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                    prim.data["width"].toDouble(), prim.data["height"].toDouble());
        rect = rect.normalized();
        const QPointF tl = rotatePoint90CW(rect.topLeft(), center);
        const QPointF tr = rotatePoint90CW(rect.topRight(), center);
        const QPointF br = rotatePoint90CW(rect.bottomRight(), center);
        const QPointF bl = rotatePoint90CW(rect.bottomLeft(), center);
        QRectF rotatedBounds(tl, QSizeF(0, 0));
        rotatedBounds = rotatedBounds.united(QRectF(tr, QSizeF(0, 0)));
        rotatedBounds = rotatedBounds.united(QRectF(br, QSizeF(0, 0)));
        rotatedBounds = rotatedBounds.united(QRectF(bl, QSizeF(0, 0)));
        prim.data["x"] = rotatedBounds.left();
        prim.data["y"] = rotatedBounds.top();
        prim.data["width"] = rotatedBounds.width();
        prim.data["height"] = rotatedBounds.height();
        return;
    }

    if (prim.type == FootprintPrimitive::Circle) {
        const QPointF c = rotatePoint90CW(QPointF(prim.data["cx"].toDouble(), prim.data["cy"].toDouble()), center);
        prim.data["cx"] = c.x();
        prim.data["cy"] = c.y();
        return;
    }

    if (prim.type == FootprintPrimitive::Arc) {
        const QPointF c = rotatePoint90CW(QPointF(prim.data["cx"].toDouble(), prim.data["cy"].toDouble()), center);
        prim.data["cx"] = c.x();
        prim.data["cy"] = c.y();
        prim.data["startAngle"] = prim.data["startAngle"].toDouble() - 90.0;
        return;
    }

    if (prim.type == FootprintPrimitive::Pad && prim.data["shape"].toString() == "Custom") {
        QJsonArray points = prim.data["points"].toArray();
        QJsonArray rotatedPoints;
        for (const QJsonValue& value : points) {
            const QJsonObject pointObj = value.toObject();
            const QPointF rotated = rotatePoint90CW(QPointF(pointObj["x"].toDouble(), pointObj["y"].toDouble()), center);
            QJsonObject newPoint;
            newPoint["x"] = rotated.x();
            newPoint["y"] = rotated.y();
            rotatedPoints.append(newPoint);
        }
        prim.data["points"] = rotatedPoints;
    }

    if (prim.data.contains("x") && prim.data.contains("y")) {
        const QPointF p = rotatePoint90CW(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()), center);
        prim.data["x"] = p.x();
        prim.data["y"] = p.y();
    }

    if (prim.data.contains("rotation")) {
        prim.data["rotation"] = prim.data["rotation"].toDouble() - 90.0;
    }
}

void mirrorPrimitiveInPlace(FootprintPrimitive& prim, qreal centerVal, bool mirrorHorizontally) {
    if (prim.type == FootprintPrimitive::Line || prim.type == FootprintPrimitive::Dimension) {
        if (mirrorHorizontally) {
            prim.data["x1"] = 2 * centerVal - prim.data["x1"].toDouble();
            prim.data["x2"] = 2 * centerVal - prim.data["x2"].toDouble();
        } else {
            prim.data["y1"] = 2 * centerVal - prim.data["y1"].toDouble();
            prim.data["y2"] = 2 * centerVal - prim.data["y2"].toDouble();
        }
    } else if (prim.type == FootprintPrimitive::Circle) {
        if (mirrorHorizontally) {
            prim.data["cx"] = 2 * centerVal - prim.data["cx"].toDouble();
        } else {
            prim.data["cy"] = 2 * centerVal - prim.data["cy"].toDouble();
        }
    } else if (prim.type == FootprintPrimitive::Arc) {
        if (mirrorHorizontally) {
            prim.data["cx"] = 2 * centerVal - prim.data["cx"].toDouble();
            qreal start = prim.data["startAngle"].toDouble();
            qreal span = prim.data["spanAngle"].toDouble();
            prim.data["startAngle"] = 180.0 - (start + span);
        } else {
            prim.data["cy"] = 2 * centerVal - prim.data["cy"].toDouble();
            qreal start = prim.data["startAngle"].toDouble();
            qreal span = prim.data["spanAngle"].toDouble();
            prim.data["startAngle"] = -(start + span);
        }
    } else if (prim.type == FootprintPrimitive::Rect) {
        if (mirrorHorizontally) {
            qreal oldX = prim.data["x"].toDouble();
            qreal w = prim.data["width"].toDouble();
            prim.data["x"] = 2 * centerVal - oldX - w;
        } else {
            qreal oldY = prim.data["y"].toDouble();
            qreal h = prim.data["height"].toDouble();
            prim.data["y"] = 2 * centerVal - oldY - h;
        }
    } else if (prim.type == FootprintPrimitive::Polygon) {
        QJsonArray pts = prim.data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            if (mirrorHorizontally) {
                o["x"] = 2 * centerVal - o["x"].toDouble();
            } else {
                o["y"] = 2 * centerVal - o["y"].toDouble();
            }
            newPts.append(o);
        }
        prim.data["points"] = newPts;
        if (prim.data.contains("x") && prim.data.contains("y")) {
            if (mirrorHorizontally) {
                prim.data["x"] = 2 * centerVal - prim.data["x"].toDouble();
            } else {
                prim.data["y"] = 2 * centerVal - prim.data["y"].toDouble();
            }
        }
    } else if (prim.type == FootprintPrimitive::Pad && prim.data["shape"].toString() == "Custom") {
        QJsonArray pts = prim.data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            if (mirrorHorizontally) {
                o["x"] = 2 * centerVal - o["x"].toDouble();
            } else {
                o["y"] = 2 * centerVal - o["y"].toDouble();
            }
            newPts.append(o);
        }
        prim.data["points"] = newPts;
        if (mirrorHorizontally) {
            prim.data["x"] = 2 * centerVal - prim.data["x"].toDouble();
        } else {
            prim.data["y"] = 2 * centerVal - prim.data["y"].toDouble();
        }
    } else {
        if (mirrorHorizontally) {
            prim.data["x"] = 2 * centerVal - prim.data["x"].toDouble();
        } else {
            prim.data["y"] = 2 * centerVal - prim.data["y"].toDouble();
        }
    }
}

QList<QPointF> convexHull2D(const QList<QPointF>& input) {
    if (input.size() < 3) return input;

    QVector<QPointF> pts = input.toVector();
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
        if (a.x() == b.x()) return a.y() < b.y();
        return a.x() < b.x();
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
        return std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9;
    }), pts.end());
    if (pts.size() < 3) return pts.toList();

    QVector<QPointF> lower, upper;
    for (const QPointF& p : pts) {
        while (lower.size() >= 2 && cross2D(lower[lower.size() - 2], lower[lower.size() - 1], p) <= 0.0) {
            lower.removeLast();
        }
        lower.push_back(p);
    }
    for (int i = pts.size() - 1; i >= 0; --i) {
        const QPointF& p = pts[i];
        while (upper.size() >= 2 && cross2D(upper[upper.size() - 2], upper[upper.size() - 1], p) <= 0.0) {
            upper.removeLast();
        }
        upper.push_back(p);
    }

    lower.removeLast();
    upper.removeLast();
    QVector<QPointF> hull = lower + upper;
    return hull.toList();
}

void updatePrimitivePos(FootprintPrimitive& prim, qreal dx, qreal dy) {
    prim.move(dx, dy);
}
}

void FootprintEditor::setPadShape(const QString& shape) {
    m_currentPadShape = shape;
    if (m_padShapeCombo && m_padShapeCombo->currentText() != shape) {
        const QSignalBlocker blocker(m_padShapeCombo);
        m_padShapeCombo->setCurrentText(shape);
    }
}

void FootprintEditor::applyPadToolbarDefaults(FootprintPrimitive& prim) const {
    if (prim.type != FootprintPrimitive::Pad) return;

    const QString shape = m_padShapeCombo ? m_padShapeCombo->currentText() : (m_currentPadShape.isEmpty() ? "Rect" : m_currentPadShape);
    prim.data["shape"] = shape;
    prim.data["width"] = m_padWidthSpin ? m_padWidthSpin->value() : prim.data["width"].toDouble(1.5);
    prim.data["height"] = m_padHeightSpin ? m_padHeightSpin->value() : prim.data["height"].toDouble(1.5);
    prim.data["drill_size"] = m_padDrillSpin ? m_padDrillSpin->value() : prim.data["drill_size"].toDouble();
    prim.data["pad_type"] = prim.data["drill_size"].toDouble() > 0.0 ? "Through-Hole" : "SMD";
    prim.data["rotation"] = m_padRotationDefault;
    if (shape == "Trapezoid") {
        prim.data["trapezoid_delta_x"] = qFuzzyIsNull(m_padTrapezoidDeltaX)
            ? prim.data["width"].toDouble() * 0.35
            : m_padTrapezoidDeltaX;
    }
}

void FootprintEditor::applyPadPresetFromDrill() {
    if (!m_padDrillSpin || !m_padWidthSpin || !m_padHeightSpin || !m_padShapeCombo) return;

    const bool throughHole = m_padDrillSpin->value() > 0.0;
    const QSignalBlocker widthBlocker(m_padWidthSpin);
    const QSignalBlocker heightBlocker(m_padHeightSpin);
    const QSignalBlocker shapeBlocker(m_padShapeCombo);

    if (throughHole) {
        m_padWidthSpin->setValue(1.8);
        m_padHeightSpin->setValue(1.8);
        if (m_padShapeCombo->currentText() == "Rect") {
            m_padShapeCombo->setCurrentText("Round");
        }
    } else {
        m_padWidthSpin->setValue(1.5);
        m_padHeightSpin->setValue(1.5);
        if (m_padShapeCombo->currentText() == "Round") {
            m_padShapeCombo->setCurrentText("Rect");
        }
    }

    setPadShape(m_padShapeCombo->currentText());
}

void FootprintEditor::applyPadToolbarToSelection() {
    if (!m_scene) return;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    bool changed = false;
    for (QGraphicsItem* item : selected) {
        const int index = m_drawnItems.indexOf(item);
        if (index < 0 || index >= newDef.primitives().size()) continue;
        FootprintPrimitive& prim = newDef.primitives()[index];
        if (prim.type != FootprintPrimitive::Pad) continue;
        applyPadToolbarDefaults(prim);
        prim.layer = m_activeLayer;
        changed = true;
    }
    if (changed) {
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Apply Pad Toolbar Settings"));
    }
}

void FootprintEditor::syncPadToolbarFromSelection() {
    if (!m_scene) return;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;
    const int index = m_drawnItems.indexOf(selected.first());
    if (index < 0 || index >= m_footprint.primitives().size()) return;
    const FootprintPrimitive& prim = m_footprint.primitives().at(index);
    if (prim.type != FootprintPrimitive::Pad) return;

    if (m_padShapeCombo) {
        const QSignalBlocker blocker(m_padShapeCombo);
        m_padShapeCombo->setCurrentText(prim.data.value("shape").toString("Rect"));
    }
    if (m_padWidthSpin) {
        const QSignalBlocker blocker(m_padWidthSpin);
        m_padWidthSpin->setValue(prim.data.value("width").toDouble(1.5));
    }
    if (m_padHeightSpin) {
        const QSignalBlocker blocker(m_padHeightSpin);
        m_padHeightSpin->setValue(prim.data.value("height").toDouble(1.5));
    }
    if (m_padDrillSpin) {
        const QSignalBlocker blocker(m_padDrillSpin);
        m_padDrillSpin->setValue(prim.data.value("drill_size").toDouble());
    }
    m_padRotationDefault = prim.data.value("rotation").toDouble();
    m_padTrapezoidDeltaX = prim.data.value("trapezoid_delta_x").toDouble();
}

void FootprintEditor::onToolSelected() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        m_polyPoints.clear();
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        m_currentTool = static_cast<Tool>(action->data().toInt());
        m_view->setCurrentTool(m_currentTool);
        updateResizeHandles();
    }
}

void FootprintEditor::onUndo() {
    if (m_undoStack) m_undoStack->undo();
}

void FootprintEditor::onRedo() {
    if (m_undoStack) m_undoStack->redo();
}

void FootprintEditor::onDelete() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    QList<int> indices;
    for (QGraphicsItem* item : selected) {
        int index = m_drawnItems.indexOf(item);
        if (index != -1) indices.append(index);
    }
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    
    for (int index : indices) {
        if (index < newDef.primitives().size()) {
            newDef.primitives().removeAt(index);
        }
    }
    
    if (indices.size() > 0) {
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, QString("Delete %1 Items").arg(indices.size())));
    }
}

void FootprintEditor::onSelectionChanged() {
    if (m_rightTabWidget && !m_scene->selectedItems().isEmpty() && m_rightTabWidget->currentIndex() != 0) {
        m_rightTabWidget->setCurrentIndex(0);
    }
    syncPadToolbarFromSelection();
    updateResizeHandles();
    updatePropertiesPanel();
}

void FootprintEditor::onZoomIn() { m_view->scale(1.2, 1.2); }
void FootprintEditor::onZoomOut() { m_view->scale(1/1.2, 1/1.2); }
void FootprintEditor::onZoomFit() { 
    if (m_scene->items().isEmpty()) m_view->fitInView(QRectF(-5, -5, 10, 10), Qt::KeepAspectRatio);
    else m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio); 
}

void FootprintEditor::onOpenWizard() {
    FootprintWizardDialog dlg(this, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_statusLabel) {
            m_statusLabel->setText("Footprint generated and saved to library.");
        }
    }
}

void FootprintEditor::onGridSizeChanged(const QString& size) {
    QString num = size.split(' ').first();
    m_view->setGridSize(num.toDouble());
}

void FootprintEditor::onMeasure(QPointF p1, QPointF p2) {
    if (QLineF(p1, p2).length() < 1e-6) return;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    FootprintPrimitive dim;
    dim.type = FootprintPrimitive::Dimension;
    dim.layer = FootprintPrimitive::Top_Fabrication;
    dim.data["x1"] = p1.x();
    dim.data["y1"] = p1.y();
    dim.data["x2"] = p2.x();
    dim.data["y2"] = p2.y();
    newDef.addPrimitive(dim);
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Add Dimension"));

    if (m_statusLabel) {
        const qreal dx = p2.x() - p1.x();
        const qreal dy = p2.y() - p1.y();
        const qreal d = QLineF(p1, p2).length();
        m_statusLabel->setText(QString("Dimension added | L: %1 mm  ΔX: %2  ΔY: %3")
                               .arg(d, 0, 'f', 2)
                               .arg(dx, 0, 'f', 2)
                               .arg(dy, 0, 'f', 2));
    }
}

void FootprintEditor::onWizardGenerate(const FootprintDefinition& def) {
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = def;
    
    newDef.setName(oldDef.name().trimmed().isEmpty() ? def.name() : oldDef.name());
    newDef.setDescription(oldDef.description());
    newDef.setCategory(oldDef.category());
    newDef.setClassification(oldDef.classification());
    newDef.setKeywords(oldDef.keywords());
    newDef.setExcludeFromBOM(oldDef.excludeFromBOM());
    newDef.setExcludeFromPosFiles(oldDef.excludeFromPosFiles());
    newDef.setDnp(oldDef.dnp());
    newDef.setIsNetTie(oldDef.isNetTie());

    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Generate Footprint"));
}

void FootprintEditor::onAlignLeft() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal minX = std::numeric_limits<qreal>::max();
    for (auto item : selected) minX = qMin(minX, item->sceneBoundingRect().left());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentLeft = item->sceneBoundingRect().left();
        updatePrimitivePos(prim, minX - currentLeft, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Left"));
}

void FootprintEditor::onAlignRight() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal maxX = std::numeric_limits<qreal>::lowest();
    for (auto item : selected) maxX = qMax(maxX, item->sceneBoundingRect().right());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentRight = item->sceneBoundingRect().right();
        updatePrimitivePos(prim, maxX - currentRight, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Right"));
}

void FootprintEditor::onAlignTop() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal minY = std::numeric_limits<qreal>::max();
    for (auto item : selected) minY = qMin(minY, item->sceneBoundingRect().top());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentTop = item->sceneBoundingRect().top();
        updatePrimitivePos(prim, 0, minY - currentTop);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Top"));
}

void FootprintEditor::onAlignBottom() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal maxY = std::numeric_limits<qreal>::lowest();
    for (auto item : selected) maxY = qMax(maxY, item->sceneBoundingRect().bottom());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentBottom = item->sceneBoundingRect().bottom();
        updatePrimitivePos(prim, 0, maxY - currentBottom);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Bottom"));
}

void FootprintEditor::onAlignCenterH() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal sumX = 0;
    for (auto item : selected) sumX += item->sceneBoundingRect().center().x();
    qreal centerX = sumX / selected.size();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentCenterX = item->sceneBoundingRect().center().x();
        updatePrimitivePos(prim, centerX - currentCenterX, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Center Horizontally"));
}

void FootprintEditor::onAlignCenterV() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal sumY = 0;
    for (auto item : selected) sumY += item->sceneBoundingRect().center().y();
    qreal centerY = sumY / selected.size();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentCenterY = item->sceneBoundingRect().center().y();
        updatePrimitivePos(prim, 0, centerY - currentCenterY);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Center Vertically"));
}

void FootprintEditor::onDistributeH() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    
    std::sort(selected.begin(), selected.end(), [](QGraphicsItem* a, QGraphicsItem* b) {
        return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x();
    });
    
    qreal startX = selected.first()->sceneBoundingRect().center().x();
    qreal endX = selected.last()->sceneBoundingRect().center().x();
    qreal step = (endX - startX) / (selected.size() - 1);
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal targetX = startX + i * step;
        qreal currentX = selected[i]->sceneBoundingRect().center().x();
        updatePrimitivePos(prim, targetX - currentX, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Distribute Horizontally"));
}

void FootprintEditor::onDistributeV() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    
    std::sort(selected.begin(), selected.end(), [](QGraphicsItem* a, QGraphicsItem* b) {
        return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y();
    });
    
    qreal startY = selected.first()->sceneBoundingRect().center().y();
    qreal endY = selected.last()->sceneBoundingRect().center().y();
    qreal step = (endY - startY) / (selected.size() - 1);
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal targetY = startY + i * step;
        qreal currentY = selected[i]->sceneBoundingRect().center().y();
        updatePrimitivePos(prim, 0, targetY - currentY);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Distribute Vertically"));
}

void FootprintEditor::onMatchSpacing() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;

    bool ok;
    double pitch = QInputDialog::getDouble(this, "Match Spacing", "Enter spacing (mm):", 
                                          m_view->gridSize(), 0.1, 100.0, 2, &ok);
    if (!ok) return;

    QRectF totalRect;
    for (auto* item : selected) totalRect = totalRect.united(item->sceneBoundingRect());
    bool horizontal = totalRect.width() > totalRect.height();

    if (horizontal) {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x(); 
        });
    } else {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y(); 
        });
    }

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    QPointF start = selected.first()->sceneBoundingRect().center();

    for (int i = 1; i < selected.size(); ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx != -1) {
            QPointF target;
            if (horizontal) target = start + QPointF(i * pitch, 0);
            else target = start + QPointF(0, i * pitch);

            QPointF delta = target - selected[i]->sceneBoundingRect().center();
            FootprintPrimitive& prim = newDef.primitives()[idx];
            updatePrimitivePos(prim, delta.x(), delta.y());
        }
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Match Spacing"));
}

void FootprintEditor::onMoveExactly() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    bool okX, okY;
    double dx = QInputDialog::getDouble(this, "Move Exactly", "Delta X (mm):", 0, -100, 100, 2, &okX);
    if (!okX) return;
    double dy = QInputDialog::getDouble(this, "Move Exactly", "Delta Y (mm):", 0, -100, 100, 2, &okY);
    if (!okY) return;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;

    for (auto* item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx != -1) {
            FootprintPrimitive& prim = newDef.primitives()[idx];
            updatePrimitivePos(prim, dx, dy);
        }
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Move Exactly"));
}

void FootprintEditor::onRotate() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QRectF bounds = selected.first()->sceneBoundingRect();
    for (auto* item : selected) bounds = bounds.united(item->sceneBoundingRect());
    const QPointF center = bounds.center();

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    bool changed = false;

    for (auto* item : selected) {
        const int index = m_drawnItems.indexOf(item);
        if (index < 0 || index >= newDef.primitives().size()) continue;
        rotatePrimitive90CW(newDef.primitives()[index], center);
        changed = true;
    }

    if (changed) {
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Rotate 90 CW"));
    }
}

void FootprintEditor::onFlipHorizontal() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    QRectF bounds = m_scene->selectionArea().boundingRect();
    if (bounds.isNull()) {
        bounds = selected.first()->sceneBoundingRect();
        for(auto item : selected) bounds = bounds.united(item->sceneBoundingRect());
    }
    qreal centerX = bounds.center().x();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        mirrorPrimitiveInPlace(newDef.primitives()[idx], centerX, true);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Flip Horizontally"));
}

void FootprintEditor::onFlipVertical() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    QRectF bounds = m_scene->selectionArea().boundingRect();
    if (bounds.isNull()) {
        bounds = selected.first()->sceneBoundingRect();
        for(auto item : selected) bounds = bounds.united(item->sceneBoundingRect());
    }
    qreal centerY = bounds.center().y();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        mirrorPrimitiveInPlace(newDef.primitives()[idx], centerY, false);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Flip Vertically"));
}

void FootprintEditor::onCreateMirroredPair() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Create Mirrored Pair");
    QFormLayout form(&dlg);

    QComboBox* axisCombo = new QComboBox(&dlg);
    axisCombo->addItems({"Mirror Left/Right", "Mirror Top/Bottom"});
    axisCombo->setCurrentIndex(0);
    form.addRow("Axis", axisCombo);

    QCheckBox* swapLayersCheck = new QCheckBox("Swap top/bottom layers on copy", &dlg);
    swapLayersCheck->setChecked(true);
    form.addRow("", swapLayersCheck);

    QPushButton* applyBtn = new QPushButton("Create Pair", &dlg);
    form.addRow(applyBtn);
    connect(applyBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return;

    QRectF bounds = selected.first()->sceneBoundingRect();
    for (auto* item : selected) bounds = bounds.united(item->sceneBoundingRect());
    const qreal centerX = bounds.center().x();
    const qreal centerY = bounds.center().y();
    const bool mirrorX = (axisCombo->currentIndex() == 0);
    const bool swapLayers = swapLayersCheck->isChecked();

    auto mirrorPrimitive = [&](const FootprintPrimitive& src) -> FootprintPrimitive {
        FootprintPrimitive dst = src;
        if (swapLayers) dst.layer = mirroredTopBottomLayer(dst.layer);
        mirrorPrimitiveInPlace(dst, mirrorX ? centerX : centerY, mirrorX);
        return dst;
    };

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    int nextPadNumber = getNextPadNumber().toInt();

    for (auto* item : selected) {
        const int idx = m_drawnItems.indexOf(item);
        if (idx < 0 || idx >= oldDef.primitives().size()) continue;
        FootprintPrimitive mirrored = mirrorPrimitive(oldDef.primitives()[idx]);

        if (mirrored.type == FootprintPrimitive::Pad) {
            mirrored.data["number"] = QString::number(nextPadNumber++);
        }
        newDef.addPrimitive(mirrored);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Create Mirrored Pair"));
}

void FootprintEditor::onArrayTool() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Array Tool", "Select an item (e.g. a Pad) to create an array.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Create Array");
    QFormLayout layout(&dlg);

    QComboBox* typeCombo = new QComboBox();
    typeCombo->addItems({"Linear", "Circular"});
    layout.addRow("Type:", typeCombo);

    QSpinBox* countSpin = new QSpinBox();
    countSpin->setRange(2, 100);
    countSpin->setValue(5);
    layout.addRow("Count:", countSpin);

    QWidget* linearWidget = new QWidget();
    QFormLayout* linearLayout = new QFormLayout(linearWidget);
    QDoubleSpinBox* stepX = new QDoubleSpinBox(); stepX->setRange(-100, 100); stepX->setDecimals(3); stepX->setValue(2.54);
    QDoubleSpinBox* stepY = new QDoubleSpinBox(); stepY->setRange(-100, 100); stepY->setDecimals(3); stepY->setValue(0.0);
    linearLayout->addRow("Step X (mm):", stepX);
    linearLayout->addRow("Step Y (mm):", stepY);
    layout.addRow(linearWidget);

    QWidget* circularWidget = new QWidget();
    QFormLayout* circularLayout = new QFormLayout(circularWidget);
    QDoubleSpinBox* centerX = new QDoubleSpinBox(); centerX->setRange(-500, 500); centerX->setValue(0.0);
    QDoubleSpinBox* centerY = new QDoubleSpinBox(); centerY->setRange(-500, 500); centerY->setValue(0.0);
    QDoubleSpinBox* totalAngle = new QDoubleSpinBox(); totalAngle->setRange(-360, 360); totalAngle->setValue(360.0);
    QCheckBox* rotateItems = new QCheckBox("Rotate Items to Center"); rotateItems->setChecked(true);
    circularLayout->addRow("Center X:", centerX);
    circularLayout->addRow("Center Y:", centerY);
    circularLayout->addRow("Total Angle:", totalAngle);
    circularLayout->addRow("", rotateItems);
    layout.addRow(circularWidget);
    circularWidget->hide();

    connect(typeCombo, &QComboBox::currentIndexChanged, this, [&dlg, linearWidget, circularWidget](int idx){
        linearWidget->setVisible(idx == 0);
        circularWidget->setVisible(idx == 1);
        dlg.adjustSize();
    });    
    QPushButton* okBtn = new QPushButton("Create");
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout.addRow(okBtn);

    if (dlg.exec() == QDialog::Accepted) {
        int count = countSpin->value();
        bool isLinear = (typeCombo->currentIndex() == 0);

        FootprintDefinition oldDef = m_footprint;
        FootprintDefinition newDef = oldDef;

        for (auto item : selected) {
            int idx = m_drawnItems.indexOf(item);
            if (idx == -1) continue;
            const FootprintPrimitive& basePrim = oldDef.primitives()[idx];
            
            for (int i = 1; i < count; ++i) {
                FootprintPrimitive newPrim = basePrim;
                
                if (isLinear) {
                    qreal dx = stepX->value();
                    qreal dy = stepY->value();
                    newPrim.move(dx * i, dy * i);
                } else {
                    qreal cx = centerX->value();
                    qreal cy = centerY->value();
                    
                    qreal startX = 0;
                    qreal startY = 0;
                    if (newPrim.type == FootprintPrimitive::Line || newPrim.type == FootprintPrimitive::Dimension) {
                        startX = newPrim.data["x1"].toDouble();
                        startY = newPrim.data["y1"].toDouble();
                    } else if (newPrim.type == FootprintPrimitive::Circle || newPrim.type == FootprintPrimitive::Arc) {
                        startX = newPrim.data["cx"].toDouble();
                        startY = newPrim.data["cy"].toDouble();
                    } else {
                        startX = newPrim.data["x"].toDouble();
                        startY = newPrim.data["y"].toDouble();
                    }

                    qreal relX = startX - cx;
                    qreal relY = startY - cy;
                    qreal radius = std::sqrt(relX*relX + relY*relY);
                    qreal startPhi = std::atan2(relY, relX);
                    qreal angleStep = (totalAngle->value() * M_PI / 180.0) / (totalAngle->value() == 360.0 ? count : count - 1);
                    
                    qreal currentPhi = startPhi + angleStep * i;
                    qreal newX = cx + radius * std::cos(currentPhi);
                    qreal newY = cy + radius * std::sin(currentPhi);

                    qreal dx = newX - startX;
                    qreal dy = newY - startY;
                    newPrim.move(dx, dy);

                    if (rotateItems->isChecked()) {
                        qreal rotDeg = (currentPhi - startPhi) * 180.0 / M_PI;
                        newPrim.data["rotation"] = newPrim.data["rotation"].toDouble() + rotDeg;
                    }
                }
                
                if (newPrim.type == FootprintPrimitive::Pad) {
                    QString oldNum = newPrim.data["number"].toString();
                    bool ok;
                    int n = oldNum.toInt(&ok);
                    if (ok) newPrim.data["number"] = QString::number(n + i);
                }
                
                newDef.addPrimitive(newPrim);
            }
        }
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Create Array"));
    }
}

void FootprintEditor::onPolarGridTool() {
    QDialog dlg(this);
    dlg.setWindowTitle("Polar Grid Generator");
    QFormLayout layout(&dlg);

    QDoubleSpinBox* centerX = new QDoubleSpinBox(&dlg);
    centerX->setRange(-500, 500);
    centerX->setDecimals(3);
    centerX->setValue(0.0);
    layout.addRow("Center X (mm):", centerX);

    QDoubleSpinBox* centerY = new QDoubleSpinBox(&dlg);
    centerY->setRange(-500, 500);
    centerY->setDecimals(3);
    centerY->setValue(0.0);
    layout.addRow("Center Y (mm):", centerY);

    QSpinBox* countSpin = new QSpinBox(&dlg);
    countSpin->setRange(2, 256);
    countSpin->setValue(8);
    layout.addRow("Count:", countSpin);

    QDoubleSpinBox* radiusSpin = new QDoubleSpinBox(&dlg);
    radiusSpin->setRange(0.1, 500);
    radiusSpin->setDecimals(3);
    radiusSpin->setValue(5.0);
    layout.addRow("Radius (mm):", radiusSpin);

    QDoubleSpinBox* startAngle = new QDoubleSpinBox(&dlg);
    startAngle->setRange(-360, 360);
    startAngle->setDecimals(2);
    startAngle->setValue(0.0);
    layout.addRow("Start Angle (deg):", startAngle);

    QComboBox* shapeCombo = new QComboBox(&dlg);
    shapeCombo->addItems({"Rect", "Round", "Oblong", "Trapezoid"});
    shapeCombo->setCurrentText(m_padShapeCombo ? m_padShapeCombo->currentText() : (m_currentPadShape.isEmpty() ? "Rect" : m_currentPadShape));
    layout.addRow("Pad Shape:", shapeCombo);

    QDoubleSpinBox* padW = new QDoubleSpinBox(&dlg);
    padW->setRange(0.1, 50);
    padW->setDecimals(3);
    padW->setValue(m_padWidthSpin ? m_padWidthSpin->value() : 1.5);
    layout.addRow("Pad Width (mm):", padW);

    QDoubleSpinBox* padH = new QDoubleSpinBox(&dlg);
    padH->setRange(0.1, 50);
    padH->setDecimals(3);
    padH->setValue(m_padHeightSpin ? m_padHeightSpin->value() : 1.5);
    layout.addRow("Pad Height (mm):", padH);

    QCheckBox* rotatePads = new QCheckBox("Rotate pads radially", &dlg);
    rotatePads->setChecked(true);
    layout.addRow("", rotatePads);

    QPushButton* createBtn = new QPushButton("Create", &dlg);
    connect(createBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout.addRow(createBtn);

    if (dlg.exec() != QDialog::Accepted) return;

    const int count = countSpin->value();
    const qreal cx = centerX->value();
    const qreal cy = centerY->value();
    const qreal radius = radiusSpin->value();
    const qreal start = startAngle->value();
    const QString shape = shapeCombo->currentText();
    const QSizeF size(padW->value(), padH->value());

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    int nextPadNumber = getNextPadNumber().toInt();

    for (int i = 0; i < count; ++i) {
        const qreal aDeg = start + (360.0 * i / count);
        const qreal aRad = aDeg * M_PI / 180.0;
        const QPointF pos(cx + radius * std::cos(aRad), cy + radius * std::sin(aRad));

        FootprintPrimitive prim = FootprintPrimitive::createPad(pos, QString::number(nextPadNumber++), shape, size);
        prim.layer = m_activeLayer;
        applyPadToolbarDefaults(prim);
        prim.data["shape"] = shape;
        prim.data["width"] = size.width();
        prim.data["height"] = size.height();
        if (rotatePads->isChecked()) prim.data["rotation"] = aDeg;
        newDef.addPrimitive(prim);
    }

    setPadShape(shape);
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Create Polar Grid"));
}

void FootprintEditor::onRunDRC() {
    QList<FootprintViolation> violations = FootprintEngine::checkFootprint(m_footprint);

    if (m_ruleList) {
        m_ruleList->clear();
        if (violations.isEmpty()) {
            QListWidgetItem* okItem = new QListWidgetItem("No issues found. Footprint passes the current rule checks.");
            okItem->setForeground(QBrush(QColor("#34d399")));
            m_ruleList->addItem(okItem);
        } else {
            for (const auto& v : violations) {
                const bool isError = (v.severity == FootprintViolation::Error || v.severity == FootprintViolation::Critical);
                QListWidgetItem* item = new QListWidgetItem(QString("%1 %2: %3")
                                                            .arg(isError ? "ERROR" : "WARN")
                                                            .arg(v.severityString(), v.message));
                item->setForeground(QBrush(QColor(isError ? "#f87171" : "#fbbf24")));
                m_ruleList->addItem(item);
            }
        }
    }
    if (m_bottomTabWidget && m_ruleList) {
        m_bottomTabWidget->setCurrentWidget(m_ruleList);
    }
    if (m_statusLabel) {
        m_statusLabel->setText(violations.isEmpty()
                               ? "Rule check passed with no issues."
                               : QString("Rule check found %1 issue(s).").arg(violations.size()));
    }

    if (violations.isEmpty()) {
        QMessageBox::information(this, "Footprint Rule Check", "✅ No issues found. Your footprint follows all design rules.");
        return;
    }

    QString report = "<h3>Footprint Design Rule Check Results</h3><ul>";
    int errorCount = 0;
    int warningCount = 0;

    for (const auto& v : violations) {
        QString icon = "⚠️ ";
        QString color = "#fbbf24";
        
        if (v.severity == FootprintViolation::Error || v.severity == FootprintViolation::Critical) {
            icon = "❌ ";
            color = "#f87171";
            errorCount++;
        } else {
            warningCount++;
        }

        report += QString("<li><span style='color:%1;'>%2 <b>%3:</b> %4</span></li>")
                    .arg(color, icon, v.severityString(), v.message);
    }
    report += "</ul>";
    report += QString("<p><b>Total: %1 Errors, %2 Warnings</b></p>").arg(errorCount).arg(warningCount);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Footprint Rule Check");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(report);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setIcon(errorCount > 0 ? QMessageBox::Warning : QMessageBox::Information);
    msgBox.exec();
}

void FootprintEditor::onSetAnchor(QPointF pos) {
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    for (int i = 0; i < newDef.primitives().size(); ++i) {
        FootprintPrimitive& prim = newDef.primitives()[i];
        updatePrimitivePos(prim, -pos.x(), -pos.y());
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Set Anchor"));
    m_statusLabel->setText(QString("Anchor set at (%1, %2)").arg(pos.x()).arg(pos.y()));
}

void FootprintEditor::onContextMenu(QPoint pos) {
    QGraphicsItem* item = m_view->itemAt(pos);
    QMenu menu(this);
    
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    bool hasConvertiblePrimitives = false;
    for (auto* it : selected) {
        const int idx = m_drawnItems.indexOf(it);
        if (idx < 0 || idx >= m_footprint.primitives().size()) continue;
        const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
        if (prim.type == FootprintPrimitive::Polygon || prim.type == FootprintPrimitive::Rect) {
            hasConvertiblePrimitives = true;
            break;
        }
    }

    if (hasConvertiblePrimitives) {
        menu.addAction(QIcon(":/icons/tool_pad.svg"), "Convert to Custom Pad", this, &FootprintEditor::onConvertToPad);
        menu.addSeparator();
    }

    if (!selected.isEmpty()) {
        menu.addAction(QIcon(":/icons/tool_rect.svg"), "Generate Fab From Selection", this, [this]() {
            generateOutlineFromSelection(FootprintPrimitive::Top_Fabrication, 0.25, "Generate Fab Outline");
        });
        menu.addAction(QIcon(":/icons/tool_rect.svg"), "Generate Courtyard From Selection", this, [this]() {
            generateOutlineFromSelection(FootprintPrimitive::Top_Courtyard, 0.5, "Generate Courtyard");
        });

        QMenu* renumberMenu = menu.addMenu("Renumber Pads");
        renumberMenu->addAction("Left to Right", this, [this]() { renumberPads("left-right"); });
        renumberMenu->addAction("Top to Bottom", this, [this]() { renumberPads("top-bottom"); });
        renumberMenu->addAction("Clockwise", this, [this]() { renumberPads("clockwise"); });
        menu.addSeparator();
    }

    if (item) {
        menu.addAction(QIcon(":/icons/tool_delete.svg"), "Delete", this, &FootprintEditor::onDelete);
    }

    menu.exec(m_view->mapToGlobal(pos));
}

void FootprintEditor::onRectResizeStarted(const QString& corner, QPointF scenePos) {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = m_drawnItems.indexOf(selected.first());
    if (idx < 0 || idx >= m_footprint.primitives().size()) return;

    const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
    m_rectResizeSessionActive = true;
    m_rectResizePrimIdx = idx;
    m_rectResizeCorner = corner;
    m_rectResizeOldDef = m_footprint;
    m_rectResizeAnchor = QPointF();
    m_resizeLineOtherEnd = QPointF();
    m_resizeCircleCenter = QPointF();

    if (prim.type == FootprintPrimitive::Rect) {
        QRectF rect(prim.data.value("x").toDouble(), prim.data.value("y").toDouble(),
                    prim.data.value("width").toDouble(), prim.data.value("height").toDouble());
        rect = rect.normalized();
        if (rect.isNull()) {
            m_rectResizeSessionActive = false;
            return;
        }
        if (corner == "tl") m_rectResizeAnchor = rect.bottomRight();
        else if (corner == "tr") m_rectResizeAnchor = rect.bottomLeft();
        else if (corner == "bl") m_rectResizeAnchor = rect.topRight();
        else m_rectResizeAnchor = rect.topLeft();
    } else if (prim.type == FootprintPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        m_resizeLineOtherEnd = (corner == "p1") ? p2 : p1;
    } else if (prim.type == FootprintPrimitive::Circle) {
        m_resizeCircleCenter = QPointF(prim.data.value("cx").toDouble(), prim.data.value("cy").toDouble());
    } else {
        m_rectResizeSessionActive = false;
        return;
    }

    onRectResizeUpdated(scenePos);
}

void FootprintEditor::onRectResizeUpdated(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;
    if (m_rectResizePrimIdx < 0 || m_rectResizePrimIdx >= m_footprint.primitives().size()) return;

    FootprintPrimitive& prim = m_footprint.primitives()[m_rectResizePrimIdx];
    if (prim.type == FootprintPrimitive::Rect) {
        QRectF rect(m_rectResizeAnchor, scenePos);
        rect = rect.normalized();
        const qreal minSize = qMax<qreal>(0.25, m_view ? m_view->gridSize() * 0.5 : 0.25);
        if (rect.width() < minSize) rect.setWidth(minSize);
        if (rect.height() < minSize) rect.setHeight(minSize);
        prim.data["x"] = rect.left();
        prim.data["y"] = rect.top();
        prim.data["width"] = rect.width();
        prim.data["height"] = rect.height();
    } else if (prim.type == FootprintPrimitive::Line) {
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
    } else if (prim.type == FootprintPrimitive::Circle) {
        qreal radius = 0.25;
        if (m_rectResizeCorner == "east" || m_rectResizeCorner == "west") {
            radius = qAbs(scenePos.x() - m_resizeCircleCenter.x());
        } else if (m_rectResizeCorner == "north" || m_rectResizeCorner == "south") {
            radius = qAbs(scenePos.y() - m_resizeCircleCenter.y());
        }
        const qreal minRadius = qMax<qreal>(0.25, m_view ? m_view->gridSize() * 0.25 : 0.25);
        prim.data["cx"] = m_resizeCircleCenter.x();
        prim.data["cy"] = m_resizeCircleCenter.y();
        prim.data["radius"] = qMax(radius, minRadius);
    } else {
        return;
    }

    updateSceneFromDefinition();
    if (m_rectResizePrimIdx >= 0 && m_rectResizePrimIdx < m_drawnItems.size()) {
        m_drawnItems[m_rectResizePrimIdx]->setSelected(true);
    }
}

void FootprintEditor::onRectResizeFinished(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;

    onRectResizeUpdated(scenePos);
    const FootprintDefinition newDef = m_footprint;
    const bool changed = (QJsonDocument(newDef.toJson()).toJson(QJsonDocument::Compact) !=
                          QJsonDocument(m_rectResizeOldDef.toJson()).toJson(QJsonDocument::Compact));

    if (changed) {
        m_undoStack->push(new UpdateFootprintCommand(this, m_rectResizeOldDef, newDef, "Resize Shape"));
    } else {
        m_footprint = m_rectResizeOldDef;
        updateSceneFromDefinition();
    }

    m_rectResizeSessionActive = false;
    m_rectResizePrimIdx = -1;
    m_rectResizeCorner.clear();
}

void FootprintEditor::generateOutlineFromSelection(FootprintPrimitive::Layer layer, qreal margin, const QString& commandText) {
    const QList<QGraphicsItem*> selected = m_scene ? m_scene->selectedItems() : QList<QGraphicsItem*>();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Generate Outline", "Select one or more primitives first.");
        return;
    }

    QRectF bounds = selected.first()->sceneBoundingRect();
    for (QGraphicsItem* item : selected) {
        if (!item) continue;
        bounds = bounds.united(item->sceneBoundingRect());
    }
    if (!bounds.isValid() || bounds.isNull()) return;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    FootprintPrimitive outline = FootprintPrimitive::createRect(bounds.adjusted(-margin, -margin, margin, margin).normalized(), false, 0.1);
    outline.layer = layer;
    newDef.addPrimitive(outline);
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, commandText));
}

void FootprintEditor::renumberPads(const QString& pattern) {
    QList<int> padIndices;
    const QList<QGraphicsItem*> selected = m_scene ? m_scene->selectedItems() : QList<QGraphicsItem*>();
    if (!selected.isEmpty()) {
        for (QGraphicsItem* item : selected) {
            const int index = m_drawnItems.indexOf(item);
            if (index >= 0 && index < m_footprint.primitives().size() &&
                m_footprint.primitives().at(index).type == FootprintPrimitive::Pad) {
                padIndices.append(index);
            }
        }
    } else {
        for (int i = 0; i < m_footprint.primitives().size(); ++i) {
            if (m_footprint.primitives().at(i).type == FootprintPrimitive::Pad) padIndices.append(i);
        }
    }

    std::sort(padIndices.begin(), padIndices.end());
    padIndices.erase(std::unique(padIndices.begin(), padIndices.end()), padIndices.end());
    if (padIndices.isEmpty()) {
        QMessageBox::information(this, "Renumber Pads", "Select pads or create some pads first.");
        return;
    }

    bool ok = false;
    const int start = QInputDialog::getInt(this, "Renumber Pads", "Start number:", 1, 1, 9999, 1, &ok);
    if (!ok) return;

    auto padCenter = [this](int index) {
        const FootprintPrimitive& prim = m_footprint.primitives().at(index);
        if (prim.data.contains("x") && prim.data.contains("y")) {
            return QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble());
        }
        if (prim.data.contains("cx") && prim.data.contains("cy")) {
            return QPointF(prim.data["cx"].toDouble(), prim.data["cy"].toDouble());
        }
        return QPointF();
    };

    if (pattern == "left-right") {
        std::sort(padIndices.begin(), padIndices.end(), [&](int a, int b) {
            const QPointF pa = padCenter(a);
            const QPointF pb = padCenter(b);
            if (!qFuzzyCompare(pa.x(), pb.x())) return pa.x() < pb.x();
            return pa.y() < pb.y();
        });
    } else if (pattern == "top-bottom") {
        std::sort(padIndices.begin(), padIndices.end(), [&](int a, int b) {
            const QPointF pa = padCenter(a);
            const QPointF pb = padCenter(b);
            if (!qFuzzyCompare(pa.y(), pb.y())) return pa.y() < pb.y();
            return pa.x() < pb.x();
        });
    } else if (pattern == "clockwise") {
        QPointF center;
        for (int index : padIndices) center += padCenter(index);
        center /= double(padIndices.size());
        std::sort(padIndices.begin(), padIndices.end(), [&](int a, int b) {
            const QPointF pa = padCenter(a) - center;
            const QPointF pb = padCenter(b) - center;
            const double aa = std::atan2(pa.x(), -pa.y());
            const double ab = std::atan2(pb.x(), -pb.y());
            if (!qFuzzyCompare(aa, ab)) return aa < ab;
            return QLineF(QPointF(), pa).length() < QLineF(QPointF(), pb).length();
        });
    }

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    int value = start;
    for (int index : padIndices) {
        newDef.primitives()[index].data["number"] = QString::number(value++);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Renumber Pads"));
}

QString FootprintEditor::getNextPadNumber() const {
    int maxNum = 0;
    for (const auto& prim : m_footprint.primitives()) {
        if (prim.type == FootprintPrimitive::Pad) {
            bool ok;
            int n = prim.data["number"].toString().toInt(&ok);
            if (ok && n > maxNum) maxNum = n;
        }
    }
    const int step = m_padNumberStepSpin ? qMax(1, m_padNumberStepSpin->value()) : 1;
    return QString::number(maxNum == 0 ? 1 : (maxNum + step));
}

void FootprintEditor::clearResizeHandles() {
    for (QGraphicsItem* handle : m_resizeHandles) {
        if (!handle) continue;
        if (m_scene) m_scene->removeItem(handle);
        delete handle;
    }
    m_resizeHandles.clear();
}

void FootprintEditor::updateResizeHandles() {
    clearResizeHandles();
    if (!m_scene) return;

    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = m_drawnItems.indexOf(selected.first());
    if (idx < 0 || idx >= m_footprint.primitives().size()) return;

    const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
    QList<QPair<QString, QPointF>> handles;
    const qreal viewScale = (m_view && !qFuzzyIsNull(m_view->transform().m11()))
        ? qAbs(m_view->transform().m11()) : 1.0;
    const qreal minSceneHandle = qBound<qreal>(0.8, 8.0 / viewScale, 2.4);
    qreal handleSize = minSceneHandle;

    if (prim.type == FootprintPrimitive::Rect) {
        QRectF rect(prim.data.value("x").toDouble(), prim.data.value("y").toDouble(),
                    prim.data.value("width").toDouble(), prim.data.value("height").toDouble());
        rect = rect.normalized();
        if (rect.isNull()) return;
        const qreal minDim = qMin(rect.width(), rect.height());
        handleSize = qMax(minSceneHandle, qBound<qreal>(0.8, minDim * 0.10, 2.4));
        const qreal edgeOffset = (minDim < 16.0) ? qBound<qreal>(0.2, handleSize * 0.35, 0.9) : 0.0;
        handles = {
            {"tl", rect.topLeft() + QPointF(-edgeOffset, -edgeOffset)},
            {"tr", rect.topRight() + QPointF(edgeOffset, -edgeOffset)},
            {"br", rect.bottomRight() + QPointF(edgeOffset, edgeOffset)},
            {"bl", rect.bottomLeft() + QPointF(-edgeOffset, edgeOffset)}
        };
    } else if (prim.type == FootprintPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        const qreal len = QLineF(p1, p2).length();
        handleSize = qMax(minSceneHandle, qBound<qreal>(0.8, len * 0.05, 2.2));
        handles = {{"p1", p1}, {"p2", p2}};
    } else if (prim.type == FootprintPrimitive::Circle) {
        const qreal cx = prim.data.value("cx").toDouble();
        const qreal cy = prim.data.value("cy").toDouble();
        const qreal r = prim.data.value("radius").toDouble();
        if (r <= 0.0) return;
        const qreal diameter = r * 2.0;
        handleSize = qMax(minSceneHandle, qBound<qreal>(0.8, diameter * 0.10, 2.2));
        const qreal radialOffset = (r < 10.0) ? qBound<qreal>(0.2, handleSize * 0.35, 0.9) : 0.0;
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

    for (const auto& handleData : handles) {
        auto* handle = new QGraphicsEllipseItem(handleData.second.x() - handleSize / 2.0,
                                                handleData.second.y() - handleSize / 2.0,
                                                handleSize, handleSize);
        handle->setBrush(QColor(56, 189, 248, 240));
        handle->setPen(QPen(QColor(255, 255, 255, 230), 0.0));
        handle->setZValue(3000);
        handle->setData(0, "resize_handle");
        handle->setData(1, handleData.first);
        handle->setData(2, idx);
        handle->setFlag(QGraphicsItem::ItemIsSelectable, false);
        handle->setFlag(QGraphicsItem::ItemIsMovable, false);
        m_scene->addItem(handle);
        m_resizeHandles.append(handle);
    }
    m_scene->update();
    if (m_view) m_view->viewport()->update();
}

void FootprintEditor::populatePropertiesFor(int index) {
    if (!m_propertyEditor) return;
    m_propertyEditor->clear();
    if (index < 0 || index >= m_footprint.primitives().size()) return;

    const FootprintPrimitive& prim = m_footprint.primitives().at(index);

    auto layerName = [](FootprintPrimitive::Layer layer) -> QString {
        switch (layer) {
            case FootprintPrimitive::Top_Silkscreen: return "Top Silkscreen";
            case FootprintPrimitive::Top_Courtyard: return "Top Courtyard";
            case FootprintPrimitive::Top_Fabrication: return "Top Fabrication";
            case FootprintPrimitive::Top_Copper: return "Top Copper";
            case FootprintPrimitive::Bottom_Copper: return "Bottom Copper";
            case FootprintPrimitive::Bottom_Silkscreen: return "Bottom Silkscreen";
            case FootprintPrimitive::Top_SolderMask: return "Top Solder Mask";
            case FootprintPrimitive::Bottom_SolderMask: return "Bottom Solder Mask";
            case FootprintPrimitive::Top_SolderPaste: return "Top Solder Paste";
            case FootprintPrimitive::Bottom_SolderPaste: return "Bottom Solder Paste";
            case FootprintPrimitive::Top_Adhesive: return "Top Adhesive";
            case FootprintPrimitive::Bottom_Adhesive: return "Bottom Adhesive";
            case FootprintPrimitive::Bottom_Courtyard: return "Bottom Courtyard";
            case FootprintPrimitive::Bottom_Fabrication: return "Bottom Fabrication";
            case FootprintPrimitive::Inner_Copper_1: return "Inner Copper 1";
            case FootprintPrimitive::Inner_Copper_2: return "Inner Copper 2";
            case FootprintPrimitive::Inner_Copper_3: return "Inner Copper 3";
            case FootprintPrimitive::Inner_Copper_4: return "Inner Copper 4";
            default: return "Top Silkscreen";
        }
    };

    const QString layerEnum = "enum|Top Silkscreen,Top Courtyard,Top Fabrication,Top Copper,Bottom Copper,Bottom Silkscreen,Top Solder Mask,Bottom Solder Mask,Top Solder Paste,Bottom Solder Paste,Top Adhesive,Bottom Adhesive,Bottom Courtyard,Bottom Fabrication,Inner Copper 1,Inner Copper 2,Inner Copper 3,Inner Copper 4";
    m_propertyEditor->addSectionHeader("Primitive");
    m_propertyEditor->addProperty("Layer", layerName(prim.layer), layerEnum);

    if (prim.type == FootprintPrimitive::Pad) {
        m_propertyEditor->addProperty("Number", prim.data.value("number").toString());
        m_propertyEditor->addProperty("Pad Type", prim.data.value("pad_type").toString("SMD"), "enum|SMD,Through-Hole,Connector");
        m_propertyEditor->addProperty("Shape", prim.data.value("shape").toString(), "enum|Rect,Round,Oblong,Trapezoid,RoundedRect,Custom");
        m_propertyEditor->addProperty("Width", prim.data.value("width").toDouble());
        m_propertyEditor->addProperty("Height", prim.data.value("height").toDouble());
        m_propertyEditor->addProperty("X", prim.data.value("x").toDouble());
        m_propertyEditor->addProperty("Y", prim.data.value("y").toDouble());
        m_propertyEditor->addProperty("Rotation", prim.data.value("rotation").toDouble());
        m_propertyEditor->addProperty("Corner Radius", prim.data.value("corner_radius").toDouble());
        m_propertyEditor->addProperty("Trapezoid Delta X", prim.data.value("trapezoid_delta_x").toDouble());

        m_propertyEditor->addSectionHeader("Pad Rules");
        m_propertyEditor->addProperty("Drill Size", prim.data.value("drill_size").toDouble());
        m_propertyEditor->addProperty("Clearance Override", prim.data.value("net_clearance_override_enabled").toBool());
        m_propertyEditor->addProperty("Net Clearance", prim.data.value("net_clearance").toDouble());
        m_propertyEditor->addProperty("Thermal Relief", prim.data.value("thermal_relief_enabled").toBool(true));
        m_propertyEditor->addProperty("Thermal Spoke Width", prim.data.value("thermal_spoke_width").toDouble(0.3));
        m_propertyEditor->addProperty("Thermal Relief Gap", prim.data.value("thermal_relief_gap").toDouble(0.25));
        m_propertyEditor->addProperty("Thermal Spoke Count", prim.data.value("thermal_spoke_count").toInt(4), "enum|1,2,3,4,5,6,7,8");
        m_propertyEditor->addProperty("Thermal Spoke Angle", prim.data.value("thermal_spoke_angle_deg").toDouble(0.0));
        m_propertyEditor->addProperty("Jumper Group", prim.data.value("jumper_group").toInt(0));
        m_propertyEditor->addProperty("Net Tie Group", prim.data.value("net_tie_group").toInt(0));
        m_propertyEditor->addProperty("Solder Mask Exp", prim.data.value("solder_mask_expansion").toDouble());
        m_propertyEditor->addProperty("Paste Mask Exp", prim.data.value("paste_mask_expansion").toDouble());
        m_propertyEditor->addProperty("Plated", prim.data.value("plated").toBool());
    } else if (prim.type == FootprintPrimitive::Text) {
        m_propertyEditor->addProperty("Text", prim.data.value("text").toString());
        m_propertyEditor->addProperty("Height", prim.data.value("height").toDouble());
        m_propertyEditor->addProperty("X", prim.data.value("x").toDouble());
        m_propertyEditor->addProperty("Y", prim.data.value("y").toDouble());
        m_propertyEditor->addProperty("Rotation", prim.data.value("rotation").toDouble());
    } else if (prim.type == FootprintPrimitive::Line || prim.type == FootprintPrimitive::Dimension) {
        m_propertyEditor->addProperty("X1", prim.data.value("x1").toDouble());
        m_propertyEditor->addProperty("Y1", prim.data.value("y1").toDouble());
        m_propertyEditor->addProperty("X2", prim.data.value("x2").toDouble());
        m_propertyEditor->addProperty("Y2", prim.data.value("y2").toDouble());
    } else if (prim.type == FootprintPrimitive::Rect) {
        m_propertyEditor->addProperty("X", prim.data.value("x").toDouble());
        m_propertyEditor->addProperty("Y", prim.data.value("y").toDouble());
        m_propertyEditor->addProperty("Width", prim.data.value("width").toDouble());
        m_propertyEditor->addProperty("Height", prim.data.value("height").toDouble());
        m_propertyEditor->addProperty("Filled", prim.data.value("filled").toBool());
        m_propertyEditor->addProperty("Line Width", prim.data.value("lineWidth").toDouble(0.1));
    } else if (prim.type == FootprintPrimitive::Circle) {
        m_propertyEditor->addProperty("Center X", prim.data.value("cx").toDouble());
        m_propertyEditor->addProperty("Center Y", prim.data.value("cy").toDouble());
        m_propertyEditor->addProperty("Radius", prim.data.value("radius").toDouble());
        m_propertyEditor->addProperty("Filled", prim.data.value("filled").toBool());
        m_propertyEditor->addProperty("Line Width", prim.data.value("lineWidth").toDouble(0.1));
    } else if (prim.type == FootprintPrimitive::Arc) {
        m_propertyEditor->addProperty("Center X", prim.data.value("cx").toDouble());
        m_propertyEditor->addProperty("Center Y", prim.data.value("cy").toDouble());
        m_propertyEditor->addProperty("Radius", prim.data.value("radius").toDouble());
        m_propertyEditor->addProperty("Start Angle", prim.data.value("startAngle").toDouble());
        m_propertyEditor->addProperty("Span Angle", prim.data.value("spanAngle").toDouble());
        m_propertyEditor->addProperty("Line Width", prim.data.value("width").toDouble(0.1));
    }
}

void FootprintEditor::onConvertToPad() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    bool anyConverted = false;
    QList<int> convertibleIndices;
    QList<QPointF> mergedPoints;
    for (auto* item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        if (idx < 0 || idx >= m_footprint.primitives().size()) continue;
        const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
        const QList<QPointF> points = primitiveToPadPolygonPoints(prim);
        if (points.size() >= 3) {
            convertibleIndices.append(idx);
            for (const QPointF& p : points) mergedPoints.append(p);
        }
    }

    if (convertibleIndices.isEmpty()) return;

    std::sort(convertibleIndices.begin(), convertibleIndices.end());
    convertibleIndices.erase(std::unique(convertibleIndices.begin(), convertibleIndices.end()), convertibleIndices.end());

    QList<QPointF> finalPoints;
    if (convertibleIndices.size() == 1) {
        finalPoints = primitiveToPadPolygonPoints(m_footprint.primitives().at(convertibleIndices.first()));
    } else {
        finalPoints = convexHull2D(mergedPoints);
    }
    if (finalPoints.size() < 3) return;

    const QString nextNum = getNextPadNumber();
    FootprintPrimitive pad = FootprintPrimitive::createPolygonPad(finalPoints, nextNum);
    pad.layer = FootprintPrimitive::Top_Copper;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    const int keepIdx = convertibleIndices.first();
    newDef.primitives()[keepIdx] = pad;
    for (int i = convertibleIndices.size() - 1; i >= 0; --i) {
        const int idx = convertibleIndices.at(i);
        if (idx != keepIdx) newDef.removePrimitive(idx);
    }
    anyConverted = true;

    if (anyConverted) {
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Convert To Custom Pad"));
        if (convertibleIndices.size() > 1) {
            m_statusLabel->setText("Converted selected shapes to one combined custom pad.");
        } else {
            m_statusLabel->setText("Converted selection to custom pad.");
        }
    }
}

void FootprintEditor::openPadSettingsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Pad Settings");
    dlg.resize(360, 0);

    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    auto* shapeCombo = new QComboBox(&dlg);
    shapeCombo->addItems({"Rect", "Round", "Oblong", "RoundedRect", "Trapezoid"});
    shapeCombo->setCurrentText(m_padShapeCombo->currentText());
    form->addRow("Shape", shapeCombo);

    auto* padTypeCombo = new QComboBox(&dlg);
    padTypeCombo->addItems({"SMD", "Through-Hole"});
    padTypeCombo->setCurrentText(m_padDrillSpin->value() > 0.0 ? "Through-Hole" : "SMD");
    form->addRow("Pad Type", padTypeCombo);

    auto* widthSpin = new QDoubleSpinBox(&dlg);
    widthSpin->setDecimals(3);
    widthSpin->setRange(0.1, 50.0);
    widthSpin->setSingleStep(0.1);
    widthSpin->setSuffix(" mm");
    widthSpin->setValue(m_padWidthSpin->value());
    form->addRow("Width", widthSpin);

    auto* heightSpin = new QDoubleSpinBox(&dlg);
    heightSpin->setDecimals(3);
    heightSpin->setRange(0.1, 50.0);
    heightSpin->setSingleStep(0.1);
    heightSpin->setSuffix(" mm");
    heightSpin->setValue(m_padHeightSpin->value());
    form->addRow("Height", heightSpin);

    auto* drillSpin = new QDoubleSpinBox(&dlg);
    drillSpin->setDecimals(3);
    drillSpin->setRange(0.0, 20.0);
    drillSpin->setSingleStep(0.05);
    drillSpin->setSuffix(" mm");
    drillSpin->setValue(m_padDrillSpin->value());
    form->addRow("Drill", drillSpin);

    auto* layerCombo = new QComboBox(&dlg);
    if (m_layerCombo) {
        for (int i = 0; i < m_layerCombo->count(); ++i) {
            layerCombo->addItem(m_layerCombo->itemText(i), m_layerCombo->itemData(i));
        }
        const int idx = layerCombo->findData(static_cast<int>(m_activeLayer));
        if (idx >= 0) layerCombo->setCurrentIndex(idx);
    }
    form->addRow("Layer", layerCombo);

    auto* rotationSpin = new QDoubleSpinBox(&dlg);
    rotationSpin->setDecimals(1);
    rotationSpin->setRange(-360.0, 360.0);
    rotationSpin->setSingleStep(15.0);
    rotationSpin->setSuffix(" deg");
    rotationSpin->setValue(m_padRotationDefault);
    form->addRow("Rotation", rotationSpin);

    auto* trapezoidDeltaSpin = new QDoubleSpinBox(&dlg);
    trapezoidDeltaSpin->setDecimals(3);
    trapezoidDeltaSpin->setRange(-50.0, 50.0);
    trapezoidDeltaSpin->setSingleStep(0.1);
    trapezoidDeltaSpin->setSuffix(" mm");
    trapezoidDeltaSpin->setValue(m_padTrapezoidDeltaX);
    form->addRow("Trapezoid Delta", trapezoidDeltaSpin);

    auto* stepSpin = new QSpinBox(&dlg);
    stepSpin->setRange(1, 64);
    stepSpin->setValue(m_padNumberStepSpin->value());
    form->addRow("Number Step", stepSpin);

    auto* applyToSelection = new QCheckBox("Apply to selected pads", &dlg);
    applyToSelection->setChecked(!m_scene->selectedItems().isEmpty());
    layout->addWidget(applyToSelection);

    auto* snapToGrid = new QCheckBox("Snap to grid", &dlg);
    snapToGrid->setChecked(m_view && m_view->snapToGridEnabled());
    layout->addWidget(snapToGrid);

    auto* showCrosshair = new QCheckBox("Show crosshair", &dlg);
    showCrosshair->setChecked(m_view && m_view->isCrosshairEnabled());
    layout->addWidget(showCrosshair);

    auto updateDerivedState = [&]() {
        const bool throughHole = padTypeCombo->currentText() == "Through-Hole";
        const QSignalBlocker drillBlocker(drillSpin);
        const QSignalBlocker widthBlocker(widthSpin);
        const QSignalBlocker heightBlocker(heightSpin);
        const QSignalBlocker shapeBlocker(shapeCombo);

        if (throughHole) {
            if (drillSpin->value() <= 0.0) drillSpin->setValue(0.8);
            if (widthSpin->value() < 1.8) widthSpin->setValue(1.8);
            if (heightSpin->value() < 1.8) heightSpin->setValue(1.8);
            if (shapeCombo->currentText() == "Rect") shapeCombo->setCurrentText("Round");
        } else {
            drillSpin->setValue(0.0);
            if (shapeCombo->currentText() == "Round") shapeCombo->setCurrentText("Rect");
        }

        const bool trapezoid = shapeCombo->currentText() == "Trapezoid";
        trapezoidDeltaSpin->setEnabled(trapezoid);
        if (trapezoid && qFuzzyIsNull(trapezoidDeltaSpin->value())) {
            trapezoidDeltaSpin->setValue(widthSpin->value() * 0.35);
        }
    };

    QObject::connect(padTypeCombo, &QComboBox::currentTextChanged, &dlg, [&](const QString&) { updateDerivedState(); });
    QObject::connect(shapeCombo, &QComboBox::currentTextChanged, &dlg, [&](const QString&) { updateDerivedState(); });
    QObject::connect(drillSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dlg, [&](double value) {
        const QSignalBlocker typeBlocker(padTypeCombo);
        padTypeCombo->setCurrentText(value > 0.0 ? "Through-Hole" : "SMD");
        updateDerivedState();
    });
    QObject::connect(widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dlg, [&](double) {
        if (shapeCombo->currentText() == "Trapezoid" && qFuzzyIsNull(trapezoidDeltaSpin->value())) {
            trapezoidDeltaSpin->setValue(widthSpin->value() * 0.35);
        }
    });
    updateDerivedState();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto* applyButton = buttons->addButton("Apply", QDialogButtonBox::ApplyRole);
    layout->addWidget(buttons);

    auto applyValues = [this, shapeCombo, widthSpin, heightSpin, drillSpin, layerCombo, rotationSpin, trapezoidDeltaSpin, stepSpin, applyToSelection, snapToGrid, showCrosshair]() {
        {
            const QSignalBlocker shapeBlocker(m_padShapeCombo);
            m_padShapeCombo->setCurrentText(shapeCombo->currentText());
        }
        {
            const QSignalBlocker widthBlocker(m_padWidthSpin);
            m_padWidthSpin->setValue(widthSpin->value());
        }
        {
            const QSignalBlocker heightBlocker(m_padHeightSpin);
            m_padHeightSpin->setValue(heightSpin->value());
        }
        {
            const QSignalBlocker drillBlocker(m_padDrillSpin);
            m_padDrillSpin->setValue(drillSpin->value());
        }
        {
            const QSignalBlocker stepBlocker(m_padNumberStepSpin);
            m_padNumberStepSpin->setValue(stepSpin->value());
        }

        m_padRotationDefault = rotationSpin->value();
        m_padTrapezoidDeltaX = trapezoidDeltaSpin->value();
        setPadShape(shapeCombo->currentText());

        if (m_layerCombo && layerCombo->count() > 0) {
            const int idx = m_layerCombo->findData(layerCombo->currentData());
            if (idx >= 0) {
                const QSignalBlocker layerBlocker(m_layerCombo);
                m_layerCombo->setCurrentIndex(idx);
            }
            m_activeLayer = static_cast<FootprintPrimitive::Layer>(layerCombo->currentData().toInt());
            refreshLayerChipStates();
        }

        if (m_view) {
            m_view->setSnapToGrid(snapToGrid->isChecked());
            m_view->setCrosshairEnabled(showCrosshair->isChecked());
        }
        ConfigManager::instance().setToolProperty("FootprintEditor", "showCrosshair", showCrosshair->isChecked());
        if (applyToSelection->isChecked()) {
            applyPadToolbarToSelection();
        }
        if (m_statusLabel) {
            m_statusLabel->setText(QString("Pad defaults: %1 %2 x %3 mm, drill %4 mm, rot %5 deg, step %6")
                .arg(shapeCombo->currentText())
                .arg(widthSpin->value(), 0, 'f', 3)
                .arg(heightSpin->value(), 0, 'f', 3)
                .arg(drillSpin->value(), 0, 'f', 3)
                .arg(rotationSpin->value(), 0, 'f', 1)
                .arg(stepSpin->value()));
        }
    };

    QObject::connect(applyButton, &QPushButton::clicked, &dlg, applyValues);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        applyValues();
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

void FootprintEditor::onOpen3DPreview() {
    if (m_model3DPanel) {
        m_model3DPanel->onOpen3DPreview();
    }
}

void FootprintEditor::onAddPrimitiveExact() {
    QDialog dialog(this);
    dialog.setWindowTitle("Add Primitive (Exact Dimensions)");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QFormLayout* form = new QFormLayout();
    QComboBox* typeCombo = new QComboBox();
    typeCombo->addItems({"Pad", "Line", "Rect", "Circle", "Text"});
    form->addRow("Type:", typeCombo);

    QDoubleSpinBox* x1 = new QDoubleSpinBox(); x1->setRange(-1000, 1000); x1->setSuffix(" mm");
    QDoubleSpinBox* y1 = new QDoubleSpinBox(); y1->setRange(-1000, 1000); y1->setSuffix(" mm");
    QDoubleSpinBox* x2 = new QDoubleSpinBox(); x2->setRange(-1000, 1000); x2->setSuffix(" mm");
    QDoubleSpinBox* y2 = new QDoubleSpinBox(); y2->setRange(-1000, 1000); y2->setSuffix(" mm");
    QLineEdit* textEdit = new QLineEdit("1");
    QComboBox* shapeCombo = new QComboBox(); shapeCombo->addItems({"Rect", "Round", "Oblong", "Trapezoid"});

    form->addRow("X / CX / X1:", x1);
    form->addRow("Y / CY / Y1:", y1);
    form->addRow("W / Radius / X2:", x2);
    form->addRow("H / Y2:", y2);
    form->addRow("Text / Pad Num:", textEdit);
    form->addRow("Pad Shape:", shapeCombo);

    auto updateFields = [&]() {
        QString type = typeCombo->currentText();
        x2->setEnabled(type != "Text");
        y2->setEnabled(type == "Pad" || type == "Line" || type == "Rect");
        textEdit->setEnabled(type == "Text" || type == "Pad");
        shapeCombo->setEnabled(type == "Pad");
        if (type == "Pad") {
            x1->setValue(0); y1->setValue(0); x2->setValue(1.5); y2->setValue(1.5);
        }
    };
    connect(typeCombo, &QComboBox::currentTextChanged, updateFields);
    updateFields();

    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        FootprintPrimitive prim;
        QString type = typeCombo->currentText();
        if (type == "Pad") {
            prim = FootprintPrimitive::createPad(QPointF(x1->value(), y1->value()), textEdit->text(), shapeCombo->currentText(), QSizeF(x2->value(), y2->value()));
            prim.layer = m_activeLayer;
            prim.data["drill_size"] = m_padDrillSpin ? m_padDrillSpin->value() : 0.0;
            prim.data["pad_type"] = prim.data["drill_size"].toDouble() > 0.0 ? "Through-Hole" : "SMD";
        } else if (type == "Line") {
            prim = FootprintPrimitive::createLine(QPointF(x1->value(), y1->value()), QPointF(x2->value(), y2->value()));
            prim.layer = m_activeLayer;
        } else if (type == "Rect") {
            prim = FootprintPrimitive::createRect(QRectF(x1->value(), y1->value(), x2->value(), y2->value()));
            prim.layer = m_activeLayer;
        } else if (type == "Circle") {
            prim = FootprintPrimitive::createCircle(QPointF(x1->value(), y1->value()), x2->value());
            prim.layer = m_activeLayer;
        } else if (type == "Text") {
            prim = FootprintPrimitive::createText(textEdit->text(), QPointF(x1->value(), y1->value()));
            prim.layer = m_activeLayer;
        }
        
        m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
    }
}
