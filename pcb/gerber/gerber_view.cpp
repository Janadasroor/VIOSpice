/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gerber_view.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPolygonItem>

namespace {
QString normalizedLayerName(const QString& name) {
    return name.toLower();
}
}

GerberView::GerberView(QWidget* parent)
    : QGraphicsView(parent), m_backgroundColor(Qt::black), m_isPanning(false) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(m_backgroundColor);
    
    // Improved zoom level
    scale(10.0, -10.0); // Y-axis is inverted in Gerber (up is positive)
}

void GerberView::setBackgroundColor(const QColor& color) {
    if (!color.isValid() || color == m_backgroundColor) {
        return;
    }

    m_backgroundColor = color;
    setBackgroundBrush(m_backgroundColor);
    viewport()->update();
}

void GerberView::setMonochrome(bool enabled) {
    if (m_monochrome == enabled) return;
    m_monochrome = enabled;
    rebuildScene();
}

void GerberView::setWireframeMode(bool enabled) {
    if (m_wireframeMode == enabled) return;
    m_wireframeMode = enabled;
    rebuildScene();
}

void GerberView::setShowDCodes(bool enabled) {
    if (m_showDCodes == enabled) return;
    m_showDCodes = enabled;
    rebuildScene();
}

void GerberView::setMeasureMode(bool enabled) {
    m_measureMode = enabled;
    m_measureHasFirst = false;
    m_measureHasSecond = false;
    viewport()->update();
}

QRectF GerberView::plotBounds() const {
    const QPainterPath outline = boardOutlinePath();
    const QRectF outlineBounds = outline.boundingRect();
    if (outlineBounds.isValid() && !outlineBounds.isEmpty()) {
        return outlineBounds;
    }
    return m_scene ? m_scene->itemsBoundingRect() : QRectF();
}

void GerberView::addLayer(GerberLayer* layer) {
    m_layers.append(layer);
    rebuildScene();
}

void GerberView::setLayers(const QList<GerberLayer*>& layers) {
    m_layers = layers;
    rebuildScene();
}

void GerberView::clear() {
    m_scene->clear();
    for (auto* layer : m_layers) delete layer;
    m_layers.clear();
}

bool GerberView::isEdgeLayer(const GerberLayer* layer) const {
    const QString name = normalizedLayerName(layer ? layer->name() : QString());
    return name.contains("edge") || name.contains("outline") ||
           name.contains(".gm1") || name.contains(".gko");
}

bool GerberView::isDrillLayer(const GerberLayer* layer) const {
    const QString name = normalizedLayerName(layer ? layer->name() : QString());
    return name.contains("drill") || name.contains(".drl") || name.contains(".drll") ||
           name.contains("excellon") || name.contains(".xln") || name.contains(".txt");
}

QColor GerberView::displayColorForLayer(const GerberLayer* layer) const {
    if (m_monochrome) {
        return Qt::black;
    }

    const QString name = normalizedLayerName(layer ? layer->name() : QString());
    if (isDrillLayer(layer)) {
        return QColor(35, 35, 35);
    }
    if (isEdgeLayer(layer)) {
        return QColor(255, 208, 92); // Gold Yellow
    }
    if (name.contains("f_cu") || name.contains("gtl") || (name.contains("top") && name.contains("cu"))) {
        return QColor(220, 40, 40); // KiCad Professional Red
    }
    if (name.contains("b_cu") || name.contains("gbl") || (name.contains("bottom") && name.contains("cu"))) {
        return QColor(59, 130, 246); // KiCad Tech Blue
    }
    if (name.contains("in1_cu") || name.contains(".g1")) {
        return QColor(16, 185, 129); // Inner 1 Green
    }
    if (name.contains("in2_cu") || name.contains(".g2")) {
        return QColor(139, 92, 246); // Inner 2 Purple
    }
    if (name.contains("f_mask") || name.contains("gts") || (name.contains("mask") && name.contains("top"))) {
        return QColor(35, 111, 62, 160); // Translucent Dark Green
    }
    if (name.contains("b_mask") || name.contains("gbs") || (name.contains("mask") && name.contains("bottom"))) {
        return QColor(27, 83, 49, 160); // Translucent Dark Green
    }
    if (name.contains("f_silkscreen") || name.contains("f_silks") || name.contains("gto") || name.contains("top_silk")) {
        return QColor(240, 240, 240); // Pure White
    }
    if (name.contains("b_silkscreen") || name.contains("b_silks") || name.contains("gbo") || name.contains("bottom_silk")) {
        return QColor(234, 179, 8); // Yellow
    }
    if (name.contains("paste")) {
        return QColor(180, 180, 180, 180);
    }
    if (name.contains("courtyard")) {
        return QColor(125, 180, 255);
    }
    if (name.contains("fabrication")) {
        return QColor(150, 150, 180);
    }
    return layer ? layer->color() : QColor(Qt::white);
}

QPainterPath GerberView::boardOutlinePath() const {
    QPainterPath outline;
    for (GerberLayer* layer : m_layers) {
        if (!layer || !layer->isVisible() || !isEdgeLayer(layer)) {
            continue;
        }
        for (const auto& prim : layer->primitives()) {
            outline.addPath(prim.path);
        }
    }

    if (!outline.isEmpty()) {
        return outline.simplified();
    }

    QRectF bounds;
    bool first = true;
    for (GerberLayer* layer : m_layers) {
        if (!layer || !layer->isVisible() || isDrillLayer(layer)) {
            continue;
        }
        for (const auto& prim : layer->primitives()) {
            QRectF r = prim.type == GerberPrimitive::Flash
                ? QRectF(prim.center.x() - 0.5, prim.center.y() - 0.5, 1.0, 1.0)
                : prim.path.boundingRect();
            if (first) {
                bounds = r;
                first = false;
            } else {
                bounds = bounds.united(r);
            }
        }
    }

    if (first) {
        bounds = QRectF(-50, -50, 100, 100);
    }

    QPainterPath fallback;
    fallback.addRect(bounds.adjusted(-2.0, -2.0, 2.0, 2.0));
    return fallback;
}

void GerberView::rebuildScene() {
    m_scene->clear();

    const QPainterPath boardPath = boardOutlinePath();
    if (!boardPath.isEmpty()) {
        QGraphicsPathItem* boardItem = m_scene->addPath(boardPath);
        if (m_monochrome) {
            boardItem->setBrush(Qt::white);
            boardItem->setPen(QPen(Qt::black, 0.18));
        } else {
            boardItem->setBrush(QColor(140, 168, 108));
            boardItem->setPen(QPen(QColor(108, 134, 82), 0.18));
        }
        boardItem->setZValue(-100.0);
    }

    for (GerberLayer* layer : m_layers) {
        renderLayer(layer);
    }
}

void GerberView::renderLayer(GerberLayer* layer) {
    if (!layer->isVisible()) return;

    const bool drillLayer = isDrillLayer(layer);
    const QColor layerColor = displayColorForLayer(layer);
    const bool edgeLayer = isEdgeLayer(layer);

    for (const auto& prim : layer->primitives()) {
        GerberAperture ap = layer->getAperture(prim.apertureId);
        
        QColor fillColor = layerColor;
        QColor penColor = layerColor;
        bool isClear = (prim.polarity == GerberPrimitive::Clear);

        if (isClear) {
            fillColor = m_monochrome ? Qt::white : m_backgroundColor;
            penColor = fillColor;
        }

        if (prim.type == GerberPrimitive::Line) {
            double width = ap.params.isEmpty() ? 0.2 : ap.params[0];
            QGraphicsPathItem* item = m_scene->addPath(prim.path);
            if (m_wireframeMode) {
                item->setPen(QPen(penColor, 0.05, Qt::SolidLine));
                item->setBrush(Qt::NoBrush);
            } else {
                item->setPen(QPen(penColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                item->setBrush(Qt::NoBrush);
            }
            item->setZValue(edgeLayer ? 50.0 : (isClear ? 15.0 : 10.0));
            item->setVisible(true);
        } else if (prim.type == GerberPrimitive::Region) {
            QGraphicsPathItem* item = m_scene->addPath(prim.path);
            if (m_wireframeMode) {
                item->setPen(QPen(penColor, 0.05));
                item->setBrush(Qt::NoBrush);
            } else {
                item->setPen(Qt::NoPen);
                item->setBrush(fillColor);
            }
            item->setZValue(edgeLayer ? 50.0 : (isClear ? 16.0 : 11.0));
            item->setVisible(true);
        } else if (prim.type == GerberPrimitive::Flash) {
            double d = ap.params.isEmpty() ? 0.5 : ap.params[0];
            const QColor flashFill = (drillLayer || isClear) ? (m_monochrome ? Qt::white : m_backgroundColor) : layerColor;
            const QColor flashStroke = drillLayer ? (m_monochrome ? QColor(Qt::black) : QColor(190, 190, 190, 80)) : 
                                       (isClear ? flashFill : layerColor);
            
            QPainterPath path;
            if (ap.type == GerberAperture::Circle) {
                path.addEllipse(prim.center, d/2, d/2);
                if (ap.params.size() > 1 && ap.params[1] > 0.001) {
                    double holeD = ap.params[1];
                    path.addEllipse(prim.center, holeD/2, holeD/2);
                    path.setFillRule(Qt::OddEvenFill);
                }
            } else if (ap.type == GerberAperture::Rectangle || ap.type == GerberAperture::Obround) {
                double h = ap.params.size() > 1 ? ap.params[1] : d;
                QRectF rect(prim.center.x() - d/2, prim.center.y() - h/2, d, h);
                if (ap.type == GerberAperture::Obround) {
                    qreal r = std::min(d, h) / 2.0;
                    path.addRoundedRect(rect, r, r);
                } else {
                    path.addRect(rect);
                }
                
                // Handle hole in Rect/Obround (3rd param in template)
                if (ap.params.size() > 2 && ap.params[2] > 0.001) {
                    double holeD = ap.params[2];
                    path.addEllipse(prim.center, holeD/2, holeD/2);
                    path.setFillRule(Qt::OddEvenFill);
                }
            }

            if (!path.isEmpty()) {
                QGraphicsPathItem* item = m_scene->addPath(path);
                if (m_wireframeMode) {
                    item->setBrush(Qt::NoBrush);
                    item->setPen(QPen(flashStroke, 0.05));
                } else {
                    item->setBrush(flashFill);
                    item->setPen(drillLayer ? QPen(flashStroke, 0.08) : Qt::NoPen);
                }
                item->setZValue(drillLayer ? 100.0 : (isClear ? 17.0 : 12.0));
                item->setVisible(true);

                if (m_showDCodes && prim.apertureId > 0) {
                    QGraphicsSimpleTextItem* txt = m_scene->addSimpleText(QString("D%1").arg(prim.apertureId));
                    txt->setPos(prim.center);
                    txt->setBrush(Qt::yellow);
                    txt->setZValue(200.0);
                    txt->setScale(0.08);
                }
            }
        }
    }
}

void GerberView::zoomFit() {
    if (!m_scene->items().isEmpty()) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
}

void GerberView::wheelEvent(QWheelEvent* event) {
    const double factor = 1.15;
    if (event->angleDelta().y() > 0) scale(factor, factor);
    else scale(1.0 / factor, 1.0 / factor);
}

void GerberView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_M) {
        setMeasureMode(!m_measureMode);
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GerberView::mousePressEvent(QMouseEvent* event) {
    if (m_measureMode && event->button() == Qt::LeftButton) {
        const QPointF sp = mapToScene(event->pos());
        if (!m_measureHasFirst || m_measureHasSecond) {
            m_measureP1 = sp;
            m_measureHasFirst = true;
            m_measureHasSecond = false;
        } else {
            m_measureP2 = sp;
            m_measureHasSecond = true;
        }
        viewport()->update();
        return;
    }
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GerberView::mouseMoveEvent(QMouseEvent* event) {
    m_currentMousePos = mapToScene(event->pos());
    if (m_measureMode && m_measureHasFirst) {
        viewport()->update();
    }
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPanPoint = event->pos();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GerberView::mouseReleaseEvent(QMouseEvent* event) {
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsView::mouseReleaseEvent(event);
}

void GerberView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, m_backgroundColor);
}

void GerberView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);
    if (!m_measureMode || !m_measureHasFirst) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QPointF p2 = m_measureHasSecond ? m_measureP2 : m_currentMousePos;
    const double dist = QLineF(m_measureP1, p2).length();
    const double dx = std::abs(p2.x() - m_measureP1.x());
    const double dy = std::abs(p2.y() - m_measureP1.y());

    QPen pen(QColor(255, 212, 59), 0.2, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(255, 212, 59, 140)));

    painter->drawEllipse(m_measureP1, 0.4, 0.4);
    painter->drawEllipse(p2, 0.4, 0.4);
    painter->drawLine(m_measureP1, p2);

    const QString text = QString("%1 mm (dX: %2, dY: %3)")
        .arg(dist, 0, 'f', 3).arg(dx, 0, 'f', 3).arg(dy, 0, 'f', 3);

    const QPointF mid = (m_measureP1 + p2) * 0.5;
    painter->save();
    painter->translate(mid);
    painter->scale(0.12, -0.12);
    QRectF fontRect = painter->fontMetrics().boundingRect(text).adjusted(-6, -3, 6, 3);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(15, 20, 26, 210));
    painter->drawRoundedRect(fontRect, 4, 4);
    painter->setPen(QColor(255, 232, 140));
    painter->drawText(fontRect, Qt::AlignCenter, text);
    painter->restore();

    painter->restore();
}
