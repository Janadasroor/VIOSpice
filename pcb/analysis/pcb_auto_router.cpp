/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_auto_router.h"
#include "net_class.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/component_item.h"
#include "../items/copper_pour_item.h"
#include "../items/pcb_item.h"
#include "../layers/pcb_layer.h"
#include "../models/trace_model.h"
#include "../models/via_model.h"
#include "../analysis/pcb_ratsnest_manager.h"

#include <QGraphicsScene>
#include <QtMath>
#include <QVector>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <queue>
#include <iostream>

// ============================================================================
// Construction
// ============================================================================

PCBAutoRouter::PCBAutoRouter(QGraphicsScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene)
{
}

PCBAutoRouter::~PCBAutoRouter() = default;

double PCBAutoRouter::progress() const {
    if (m_totalConnections == 0) return 1.0;
    return static_cast<double>(m_currentConnection) / m_totalConnections;
}

// ============================================================================
// Main routing entry point
// ============================================================================

PCBAutoRouter::RouteStats PCBAutoRouter::routeAll(const RouterConfig& config) {
    if (!m_scene) return m_stats;

    m_config = config;
    m_stats = RouteStats();
    m_running = true;
    m_stopRequested = false;
    m_blockingNets.clear();

    QElapsedTimer timer;
    timer.start();

    // Step 1: Build routing grid
    buildGrid();
    markObstacles();
    rebuildCommittedEdgeSet();

    int blockedCountL0 = 0;
    int blockedCountL1 = 0;
    for (int y = 0; y < m_gridHeight; ++y) {
        for (int x = 0; x < m_gridWidth; ++x) {
            if (cellAt(x, y, 0) && cellAt(x, y, 0)->blocked) blockedCountL0++;
            if (cellAt(x, y, 1) && cellAt(x, y, 1)->blocked) blockedCountL1++;
        }
    }
    std::cerr << "Grid size: " << m_gridWidth << "x" << m_gridHeight << " Blocked cells L0: " << blockedCountL0 << " L1: " << blockedCountL1 << std::endl;

    // Step 2: Discover unrouted connections
    QList<UnroutedConnection> connections = findUnroutedConnections();
    m_totalConnections = connections.size();
    m_stats.totalConnections = connections.size();

    if (connections.isEmpty()) {
        m_running = false;
        emit routingFinished(m_stats);
        return m_stats;
    }

    emit progressChanged(0.0, QString("Starting auto-router: %1 connections to route").arg(connections.size()));

    // Step 3: Route each connection
    for (int passIdx = 0; passIdx <= static_cast<int>(RoutingPass::Final); ++passIdx) {
        if (m_stopRequested) break;

        RoutingPass pass = static_cast<RoutingPass>(passIdx);
        QString passName;
        switch (pass) {
            case RoutingPass::Initial: passName = "Initial pass"; break;
            case RoutingPass::RipUpRetry: passName = "Rip-up and retry"; break;
            case RoutingPass::Final: passName = "Final aggressive pass"; break;
        }
        emit progressChanged(progress(), QString("Pass: %1").arg(passName));

        // For rip-up passes, remove some existing traces
        if (pass == RoutingPass::RipUpRetry && m_stats.failedConnections > 0) {
            int ripCount = qMin(m_config.maxRipUpCount, m_stats.failedConnections * 2);
            QSet<QString> failedNetSet(m_stats.failedNets.begin(), m_stats.failedNets.end());
            int actuallyRipped = ripUpWorstTraces(ripCount, failedNetSet);
            m_stats.ripUpCount += actuallyRipped;

            // Rebuild grid after rip-up
            buildGrid();
            markObstacles();
            rebuildCommittedEdgeSet();

            // Re-find connections (some may now be routed)
            connections = findUnroutedConnections();
        }

        m_currentConnection = 0;
        RouterConfig passConfig = config;
        if (pass == RoutingPass::Final) {
            passConfig.maxIterations *= 2;
            passConfig.allowDiagonals = true;
        }
        m_config = passConfig;

        QList<UnroutedConnection> stillFailed;

        for (int i = 0; i < connections.size(); ++i) {
            if (m_stopRequested) break;

            m_currentConnection = i + 1;
            const auto& conn = connections[i];

            QVector<AStarNode> path;
            bool ok = findPath(conn, path);

            if (ok && !path.isEmpty()) {
                std::cerr << "Routed net: " << conn.netName.toStdString() << " Path size: " << path.size() << std::endl;
                for (const auto& node : path) {
                    QPointF p = gridToScene(node.x, node.y);
                    std::cerr << "  Node (" << node.x << ", " << node.y << ") -> Scene (" << p.x() << ", " << p.y() << ") layer " << node.layer << (node.isVia ? " (Via)" : "") << std::endl;
                }
                convertPathToTraces(path, conn);
                m_stats.routedConnections++;
                emit connectionRouted(conn.netName, m_currentConnection, m_totalConnections);

                // Mark the new path as occupied
                markObstacles();
            } else {
                stillFailed.append(conn);
                m_stats.failedConnections++;
                m_stats.failedNets.append(conn.netName);
                emit connectionFailed(conn.netName, m_currentConnection, m_totalConnections);
            }

            // Progress update
            if (config.reportProgress && i % config.progressInterval == 0) {
                emit progressChanged(progress(),
                    QString("Routed %1/%2").arg(m_stats.routedConnections).arg(m_totalConnections));
            }
        }

        connections = stillFailed;
        if (connections.isEmpty()) break;
    }

    updateStats();
    m_running = false;

    emit progressChanged(1.0, QString("Routing complete: %1/%2 routed")
        .arg(m_stats.routedConnections).arg(m_stats.totalConnections));
    emit routingFinished(m_stats);

    return m_stats;
}

bool PCBAutoRouter::routeNet(const QString& netName, const RouterConfig& config) {
    m_config = config;
    buildGrid();
    markObstacles();

    auto connections = findUnroutedConnections();
    auto netConns = std::move(connections);
    netConns.erase(std::remove_if(netConns.begin(), netConns.end(),
        [&netName](const UnroutedConnection& c) { return c.netName != netName; }),
        netConns.end());

    bool allOk = true;
    for (const auto& conn : netConns) {
        QVector<AStarNode> path;
        if (findPath(conn, path) && !path.isEmpty()) {
            convertPathToTraces(path, conn);
            markObstacles();
        } else {
            allOk = false;
        }
    }
    return allOk;
}

// ============================================================================
// Grid management
// ============================================================================

void PCBAutoRouter::buildGrid() {
    // Determine board bounding box
    double minX = 0, minY = 0, maxX = 100, maxY = 100;
    bool first = true;

    for (auto* item : m_scene->items()) {
        auto* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) continue;

        QRectF rect = item->sceneBoundingRect();
        if (first) {
            minX = rect.left(); minY = rect.top();
            maxX = rect.right(); maxY = rect.bottom();
            first = false;
        } else {
            minX = qMin(minX, rect.left());
            minY = qMin(minY, rect.top());
            maxX = qMax(maxX, rect.right());
            maxY = qMax(maxY, rect.bottom());
        }
    }

    // Add margin for clearance
    double margin = qMax(m_config.clearance, m_config.viaClearance) * 3;
    minX -= margin; minY -= margin;
    maxX += margin; maxY += margin;

    m_gridOriginX = minX;
    m_gridOriginY = minY;

    m_gridWidth = qMin(static_cast<int>((maxX - minX) / m_config.gridSpacing) + 2, m_config.maxGridWidth);
    m_gridHeight = qMin(static_cast<int>((maxY - minY) / m_config.gridSpacing) + 2, m_config.maxGridHeight);
    m_activeLayers.clear();
    const auto copperLayers = PCBLayerManager::instance().copperLayers();
    for (const PCBLayer* layer : copperLayers) {
        if (!layer) continue;
        if (!m_config.enabledLayerIds.isEmpty()) {
            if (m_config.enabledLayerIds.contains(layer->id())) {
                m_activeLayers.append(layer->id());
            }
        } else {
            if (layer->id() == PCBLayerManager::TopCopper && !m_config.preferTopLayer) continue;
            if (layer->id() == PCBLayerManager::BottomCopper && !m_config.preferBottomLayer) continue;
            m_activeLayers.append(layer->id());
        }
    }
    if (m_activeLayers.isEmpty()) {
        m_activeLayers.append(PCBLayerManager::TopCopper);
        m_activeLayers.append(PCBLayerManager::BottomCopper);
    }
    m_gridLayers = m_activeLayers.size();

    m_grid.resize(m_gridWidth * m_gridHeight * m_gridLayers);

    // Initialize grid
    for (int l = 0; l < m_gridLayers; ++l) {
        for (int y = 0; y < m_gridHeight; ++y) {
            for (int x = 0; x < m_gridWidth; ++x) {
                GridCell* cell = cellAt(x, y, l);
                cell->blocked = false;
                cell->traceOccupied = false;
                cell->occupancyCost = 0.0;
                cell->layer = l;
                cell->netName = "";
            }
        }
    }
}

void PCBAutoRouter::markObstacles() {
    for (auto* item : m_scene->items()) {
        // Pads are obstacles on their layer (or all layers if through-hole)
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            // Mark pad cells at actual pad extent only.
            // Clearance enforcement is handled dynamically by isCellPassable().
            // Do NOT inflate here — double-inflation blocks valid routing channels
            // between closely-spaced components on different nets.
            QRectF sceneRect = pad->sceneBoundingRect();

            QPoint gMin = sceneToGrid(sceneRect.topLeft());
            QPoint gMax = sceneToGrid(sceneRect.bottomRight());

            int xMin = qBound(0, qMin(gMin.x(), gMax.x()), m_gridWidth - 1);
            int xMax = qBound(0, qMax(gMin.x(), gMax.x()), m_gridWidth - 1);
            int yMin = qBound(0, qMin(gMin.y(), gMax.y()), m_gridHeight - 1);
            int yMax = qBound(0, qMax(gMin.y(), gMax.y()), m_gridHeight - 1);

            bool isTH = (pad->drillSize() > 0.001);
            for (int l = 0; l < m_gridLayers; ++l) {
                if (!isTH && l != gridLayerIndex(pad->layer())) {
                    continue; // Skip if SMD pad and not on this grid layer
                }
                for (int y = yMin; y <= yMax; ++y) {
                    for (int x = xMin; x <= xMax; ++x) {
                        markCellOccupied(x, y, l, pad->netName());
                    }
                }
            }
        }

        // Copper pours are hard obstacles
        if (auto* pour = dynamic_cast<CopperPourItem*>(item)) {
            if (pour->itemType() == PCBItem::CopperPourType) {
                QRectF rect = item->sceneBoundingRect();
                QPoint g1 = sceneToGrid(rect.topLeft());
                QPoint g2 = sceneToGrid(rect.bottomRight());

                int layerIdx = gridLayerIndex(pour->layer());
                if (layerIdx < 0) continue;

                for (int y = qMax(0, g1.y()); y < qMin(m_gridHeight, g2.y()); ++y) {
                    for (int x = qMax(0, g1.x()); x < qMin(m_gridWidth, g2.x()); ++x) {
                        markCellOccupied(x, y, layerIdx, pour->netName());
                    }
                }
            }
        }

        // Vias are obstacles on the layers they span
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            int startL = via->startLayer();
            int endL = via->endLayer();
            int minL = qMin(startL, endL);
            int maxL = qMax(startL, endL);

            double cl = NetClassManager::instance().getClassForNet(via->netName()).clearance;
            QRectF sceneRect = via->sceneBoundingRect();
            sceneRect.adjust(-cl, -cl, cl, cl);

            QPoint gMin = sceneToGrid(sceneRect.topLeft());
            QPoint gMax = sceneToGrid(sceneRect.bottomRight());

            int xMin = qBound(0, qMin(gMin.x(), gMax.x()), m_gridWidth - 1);
            int xMax = qBound(0, qMax(gMin.x(), gMax.x()), m_gridWidth - 1);
            int yMin = qBound(0, qMin(gMin.y(), gMax.y()), m_gridHeight - 1);
            int yMax = qBound(0, qMax(gMin.y(), gMax.y()), m_gridHeight - 1);

            for (int l = minL; l <= maxL; ++l) {
                int layerIdx = gridLayerIndex(l);
                if (layerIdx < 0) continue;
                for (int y = yMin; y <= yMax; ++y) {
                    for (int x = xMin; x <= xMax; ++x) {
                        markCellOccupied(x, y, layerIdx, via->netName());
                    }
                }
            }
        }
    }

    // Always mark existing traces as occupied/obstacles
    markExistingTraces();
}

void PCBAutoRouter::markExistingTraces() {
    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            QPoint gStart = sceneToGrid(trace->startPoint());
            QPoint gEnd = sceneToGrid(trace->endPoint());

            int layerIdx = gridLayerIndex(trace->layer());
            if (layerIdx < 0) continue;

            // Bresenham-like line drawing to mark trace cells
            int dx = qAbs(gEnd.x() - gStart.x());
            int dy = qAbs(gEnd.y() - gStart.y());
            int sx = gStart.x() < gEnd.x() ? 1 : -1;
            int sy = gStart.y() < gEnd.y() ? 1 : -1;
            int err = dx - dy;
            int x = gStart.x(), y = gStart.y();

            while (true) {
                markCellOccupied(x, y, layerIdx, trace->netName());
                if (isValidCell(x, y, layerIdx)) {
                    cellAt(x, y, layerIdx)->traceOccupied = true;
                    cellAt(x, y, layerIdx)->occupancyCost += 5.0; // Prefer not routing over existing traces
                }
                
                // Mark clearance cells around trace path point dynamically
                double cl = NetClassManager::instance().getClassForNet(trace->netName()).clearance;
                int numCells = qMax(1, static_cast<int>(qCeil(cl / m_config.gridSpacing)));
                for (int dy2 = -numCells; dy2 <= numCells; ++dy2) {
                    for (int dx2 = -numCells; dx2 <= numCells; ++dx2) {
                        if (dx2 == 0 && dy2 == 0) continue;
                        markCellOccupied(x + dx2, y + dy2, layerIdx, trace->netName());
                    }
                }

                if (x == gEnd.x() && y == gEnd.y()) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
            }
        }
    }
}

PCBAutoRouter::GridCell* PCBAutoRouter::cellAt(int x, int y, int layer) {
    if (!isValidCell(x, y, layer)) return nullptr;
    return &m_grid[(layer * m_gridHeight + y) * m_gridWidth + x];
}

const PCBAutoRouter::GridCell* PCBAutoRouter::cellAt(int x, int y, int layer) const {
    if (!isValidCell(x, y, layer)) return nullptr;
    return &m_grid[(layer * m_gridHeight + y) * m_gridWidth + x];
}

bool PCBAutoRouter::isValidCell(int x, int y, int layer) const {
    return x >= 0 && x < m_gridWidth && y >= 0 && y < m_gridHeight && layer >= 0 && layer < m_gridLayers;
}

void PCBAutoRouter::markCellOccupied(int x, int y, int layer, const QString& netName) {
    if (!isValidCell(x, y, layer)) return;
    GridCell* cell = cellAt(x, y, layer);
    if (!cell) return;

    if (netName.isEmpty()) {
        cell->blocked = true;
        cell->netName = "";
        return;
    }

    if (cell->blocked && cell->netName.isEmpty()) {
        return;
    }

    if (cell->netName.isEmpty()) {
        cell->netName = netName;
        cell->blocked = true;
    } else if (cell->netName != netName) {
        cell->netName = "";
        cell->blocked = true;
    }
}

QPointF PCBAutoRouter::gridToScene(int gx, int gy) const {
    return QPointF(m_gridOriginX + gx * m_config.gridSpacing,
                   m_gridOriginY + gy * m_config.gridSpacing);
}

QPoint PCBAutoRouter::sceneToGrid(QPointF scenePos) const {
    int gx = qRound((scenePos.x() - m_gridOriginX) / m_config.gridSpacing);
    int gy = qRound((scenePos.y() - m_gridOriginY) / m_config.gridSpacing);
    return QPoint(gx, gy);
}

// ============================================================================
// Connection discovery
// ============================================================================

QMultiMap<QString, QPointF> PCBAutoRouter::groupPadsByNet() {
    QMultiMap<QString, QPointF> netPads;

    for (auto* item : m_scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            QString net = pad->netName();
            if (!net.isEmpty()) {
                netPads.insert(net, pad->scenePos());
            }
        }
    }

    return netPads;
}

bool PCBAutoRouter::arePadsConnected(QPointF p1, QPointF p2, const QString& netName) const {
    // Check if there's an existing trace path between two pads on the same net
    // Simple approach: check if any trace connects near both points
    bool nearStart = false, nearEnd = false;

    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (trace->netName() != netName) continue;

            double tolerance = m_config.gridSpacing * 1.5;
            if (QLineF(trace->startPoint(), p1).length() < tolerance ||
                QLineF(trace->endPoint(), p1).length() < tolerance) {
                nearStart = true;
            }
            if (QLineF(trace->startPoint(), p2).length() < tolerance ||
                QLineF(trace->endPoint(), p2).length() < tolerance) {
                nearEnd = true;
            }
        }
    }

    return nearStart && nearEnd;
}

QList<PCBAutoRouter::UnroutedConnection> PCBAutoRouter::findUnroutedConnections() {
    QList<UnroutedConnection> connections;
    auto netPads = groupPadsByNet();

    // Get unique nets
    QSet<QString> seenNets;
    for (auto it = netPads.begin(); it != netPads.end(); ++it) {
        seenNets.insert(it.key());
    }

    for (const QString& netName : seenNets) {
        auto pads = netPads.values(netName);
        if (pads.size() < 2) continue; // Need at least 2 pads for a connection

        // Simple approach: connect pads in sequence (chain topology)
        // A better approach would use MST, but chain is simpler and works for most cases
        for (int i = 0; i < pads.size() - 1; ++i) {
            QPointF p1 = pads[i];
            QPointF p2 = pads[i + 1];

            if (arePadsConnected(p1, p2, netName)) continue;

            UnroutedConnection conn;
            conn.netName = netName;
            conn.start = p1;
            conn.end = p2;
            NetClass netClass = NetClassManager::instance().getClassForNet(netName);
            conn.traceWidth = netClass.traceWidth;
            conn.startClearance = netClass.clearance;
            connections.append(conn);
        }
    }

    return connections;
}

// ============================================================================
// A* Pathfinding
// ============================================================================

bool PCBAutoRouter::findPath(const UnroutedConnection& conn, QVector<AStarNode>& outPath) {
    QPoint gStart = sceneToGrid(conn.start);
    QPoint gEnd = sceneToGrid(conn.end);

    // Clamp to grid bounds
    gStart.setX(qBound(0, gStart.x(), m_gridWidth - 1));
    gStart.setY(qBound(0, gStart.y(), m_gridHeight - 1));
    gEnd.setX(qBound(0, gEnd.x(), m_gridWidth - 1));
    gEnd.setY(qBound(0, gEnd.y(), m_gridHeight - 1));

    auto getGridLayer = [&](int itemLayer) -> int {
        int idx = gridLayerIndex(itemLayer);
        return (idx >= 0) ? idx : 0;
    };

    int startLayer = 0;
    int endLayer = 0;
    for (auto* item : m_scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->netName() == conn.netName) {
                if (QLineF(pad->scenePos(), conn.start).length() < 0.1) {
                    startLayer = getGridLayer(pad->layer());
                }
                if (QLineF(pad->scenePos(), conn.end).length() < 0.1) {
                    endLayer = getGridLayer(pad->layer());
                }
            }
        }
    }

    // A* structures with 0 heap string allocations (O(1) flat grid indexing)
    const int totalCells = m_gridWidth * m_gridHeight * m_gridLayers;
    std::vector<int> openMap(totalCells, -1);
    std::vector<uint8_t> closedSet(totalCells, 0);
    QVector<AStarNode> nodes;
    std::priority_queue<AStarOpenEntry, QVector<AStarOpenEntry>, std::greater<AStarOpenEntry>> openQueue;

    auto flatIndex = [this, totalCells](int x, int y, int layer) -> int {
        if (x < 0 || x >= m_gridWidth || y < 0 || y >= m_gridHeight || layer < 0 || layer >= m_gridLayers) return -1;
        int idx = (layer * m_gridHeight + y) * m_gridWidth + x;
        return (idx >= 0 && idx < totalCells) ? idx : -1;
    };

    // Check if start pad is through-hole
    bool startIsTH = false;
    bool endIsTH = false;
    for (auto* item : m_scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->netName() == conn.netName) {
                if (QLineF(pad->scenePos(), conn.start).length() < 0.1) {
                    if (pad->drillSize() > 0.001) {
                        startIsTH = true;
                    }
                }
                if (QLineF(pad->scenePos(), conn.end).length() < 0.1) {
                    if (pad->drillSize() > 0.001) {
                        endIsTH = true;
                    }
                }
            }
        }
    }

    if (startIsTH && m_gridLayers > 1) {
        for (int l = 0; l < m_gridLayers; ++l) {
            AStarNode startNode;
            startNode.x = gStart.x(); startNode.y = gStart.y(); startNode.layer = l;
            startNode.gCost = 0;
            startNode.hCost = heuristic(gStart.x(), gStart.y(), gEnd.x(), gEnd.y());
            int nodeIdx = nodes.size();
            nodes.append(startNode);
            int idxKey = flatIndex(startNode.x, startNode.y, startNode.layer);
            if (idxKey >= 0) openMap[idxKey] = nodeIdx;
            openQueue.push({startNode.fCost(), startNode.x, startNode.y, startNode.layer});
        }
    } else {
        AStarNode startNode;
        startNode.x = gStart.x(); startNode.y = gStart.y(); startNode.layer = startLayer;
        startNode.gCost = 0;
        startNode.hCost = heuristic(gStart.x(), gStart.y(), gEnd.x(), gEnd.y());
        nodes.append(startNode);
        int idxKey = flatIndex(startNode.x, startNode.y, startNode.layer);
        if (idxKey >= 0) openMap[idxKey] = 0;
        openQueue.push({startNode.fCost(), startNode.x, startNode.y, startNode.layer});
    }

    int marginCells = std::max(15, static_cast<int>(20.0 / m_config.gridSpacing));
    int bMinX = std::max(0, std::min(gStart.x(), gEnd.x()) - marginCells);
    int bMaxX = std::min(m_gridWidth - 1, std::max(gStart.x(), gEnd.x()) + marginCells);
    int bMinY = std::max(0, std::min(gStart.y(), gEnd.y()) - marginCells);
    int bMaxY = std::min(m_gridHeight - 1, std::max(gStart.y(), gEnd.y()) + marginCells);

    int iterations = 0;
    int goalNodeIdx = -1;

    while (!openQueue.empty() && iterations < m_config.maxIterations) {
        iterations++;
        m_stats.iterations++;

        // Get lowest f-cost node
        AStarOpenEntry currentEntry = openQueue.top();
        openQueue.pop();

        int cKey = flatIndex(currentEntry.x, currentEntry.y, currentEntry.layer);
        if (cKey < 0 || closedSet[cKey]) continue; // Already processed

        int currentIdx = openMap[cKey];
        if (currentIdx < 0 || currentIdx >= nodes.size()) continue;

        const AStarNode& current = nodes[currentIdx];

        // Check if we reached the goal
        if (current.x == gEnd.x() && current.y == gEnd.y()) {
            if (endIsTH || current.layer == endLayer) {
                goalNodeIdx = currentIdx;
                break;
            }
        }

        closedSet[cKey] = 1;

        // Explore neighbors
        auto neighbors = getNeighbors(current, conn.netName);
        for (const AStarNode& neighbor : neighbors) {
            if (neighbor.x < bMinX || neighbor.x > bMaxX || neighbor.y < bMinY || neighbor.y > bMaxY) continue;

            int nKey = flatIndex(neighbor.x, neighbor.y, neighbor.layer);
            if (nKey < 0 || closedSet[nKey]) continue;

            if (!isCellPassable(neighbor.x, neighbor.y, neighbor.layer, conn.netName)) {
                const GridCell* cell = cellAt(neighbor.x, neighbor.y, neighbor.layer);
                if (cell && !cell->netName.isEmpty() && cell->netName != conn.netName) {
                    m_blockingNets[conn.netName].insert(cell->netName);
                }
                continue;
            }

            double moveCost = m_config.gridSpacing;
            if (neighbor.x != current.x && neighbor.y != current.y) {
                moveCost *= 1.41421356237; // Diagonal move cost correction
            }
            if (neighbor.isVia) {
                moveCost = neighbor.extraCost;
            } else if (m_config.enableDirectionalBias) {
                int layerId = (neighbor.layer >= 0 && neighbor.layer < m_activeLayers.size()) ? m_activeLayers[neighbor.layer] : -1;
                bool isHorizontalMove = (neighbor.x != current.x);
                bool isVerticalMove = (neighbor.y != current.y);

                bool preferHorizontal = false;
                if (layerId == PCBLayerManager::TopCopper) {
                    preferHorizontal = true;
                } else if (layerId == PCBLayerManager::BottomCopper) {
                    preferHorizontal = false;
                } else {
                    preferHorizontal = (neighbor.layer % 2 == 0);
                }

                if ((preferHorizontal && isVerticalMove) || (!preferHorizontal && isHorizontalMove)) {
                    moveCost *= m_config.directionalBiasPenalty;
                }
            }

            // Turn penalty: discourage unnecessary bends and zig-zags
            if (current.parentX != -1 && current.parentY != -1 && !neighbor.isVia) {
                int prevDx = current.x - current.parentX;
                int prevDy = current.y - current.parentY;
                int newDx = neighbor.x - current.x;
                int newDy = neighbor.y - current.y;
                
                int prevDirX = (prevDx > 0) ? 1 : ((prevDx < 0) ? -1 : 0);
                int prevDirY = (prevDy > 0) ? 1 : ((prevDy < 0) ? -1 : 0);
                int newDirX = (newDx > 0) ? 1 : ((newDx < 0) ? -1 : 0);
                int newDirY = (newDy > 0) ? 1 : ((newDy < 0) ? -1 : 0);

                if (prevDirX != newDirX || prevDirY != newDirY) {
                    moveCost += m_config.gridSpacing * 2.0; // Significant penalty for turns
                }
            }

            // Add occupancy cost from obstacles
            GridCell* cell = cellAt(neighbor.x, neighbor.y, neighbor.layer);
            if (cell) moveCost += cell->occupancyCost;

            double tentativeG = current.gCost + moveCost;

            int existingIdx = openMap[nKey];
            if (existingIdx < 0 || tentativeG < nodes[existingIdx].gCost) {
                AStarNode newNode = neighbor;
                newNode.gCost = tentativeG;
                newNode.hCost = heuristic(neighbor.x, neighbor.y, gEnd.x(), gEnd.y());
                newNode.parentX = current.x;
                newNode.parentY = current.y;
                newNode.parentLayer = current.layer;
                newNode.parentIdx = currentIdx;

                int nodeIdx = nodes.size();
                nodes.append(newNode);
                openMap[nKey] = nodeIdx;
                openQueue.push({newNode.fCost(), newNode.x, newNode.y, newNode.layer});
            }
        }
    }

    // Reconstruct path in O(1) time
    if (goalNodeIdx < 0) return false;

    outPath.clear();
    int idx = goalNodeIdx;
    while (idx >= 0 && idx < nodes.size()) {
        outPath.prepend(nodes[idx]);
        if (nodes[idx].parentIdx == idx) break; // Root guard
        idx = nodes[idx].parentIdx;
    }

    return !outPath.isEmpty();
}

double PCBAutoRouter::heuristic(int x1, int y1, int x2, int y2) const {
    if (m_config.optimizeTraceLength) {
        // Euclidean distance for optimal path
        return m_config.gridSpacing * qSqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    } else {
        // Manhattan distance
        return m_config.gridSpacing * (qAbs(x2 - x1) + qAbs(y2 - y1));
    }
}

int PCBAutoRouter::gridLayerIndex(int physicalLayer) const {
    for (int i = 0; i < m_activeLayers.size(); ++i) {
        if (m_activeLayers[i] == physicalLayer) {
            return i;
        }
    }
    return -1;
}

bool PCBAutoRouter::isCellPassable(int x, int y, int layer, const QString& netName) const {
    if (!isValidCell(x, y, layer)) return false;

    const GridCell* cell = cellAt(x, y, layer);
    if (!cell) return false;

    if (cell->blocked && cell->netName.isEmpty()) return false;
    if (!cell->netName.isEmpty() && cell->netName != netName) return false;

    // Dynamic symmetric clearance check against neighbors
    double clA = NetClassManager::instance().getClassForNet(netName).clearance;
    double wA = NetClassManager::instance().getClassForNet(netName).traceWidth;
    double totalClA = clA + wA / 2.0;
    int k = static_cast<int>(qCeil(totalClA / m_config.gridSpacing));

    for (int dy = -k; dy <= k; ++dy) {
        for (int dx = -k; dx <= k; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (!isValidCell(nx, ny, layer)) continue;

            const GridCell* neighbor = cellAt(nx, ny, layer);
            if (neighbor && !neighbor->netName.isEmpty() && neighbor->netName != netName) {
                double dist = m_config.gridSpacing * qSqrt(dx * dx + dy * dy);
                double clNeighbor = NetClassManager::instance().getClassForNet(neighbor->netName).clearance;
                double wNeighbor = NetClassManager::instance().getClassForNet(neighbor->netName).traceWidth;
                double reqDist = qMax(clA, clNeighbor) + wA / 2.0 + wNeighbor / 2.0;
                if (dist < reqDist) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool PCBAutoRouter::isViaPassable(int cx, int cy, const QString& netName, double* penaltyOut) const {
    if (penaltyOut) {
        *penaltyOut = 0.0;
    }

    if (!isValidCell(cx, cy, 0)) {
        return false;
    }

    const QPointF pos = gridToScene(cx, cy);

    // 1. Scene-based drill/pad/via clearance check
    const double searchRadius =
        m_config.viaRadius +
        m_config.clearance +
        m_config.viaPreferredClearanceMargin +
        0.25;

    const QRectF searchRect(
        pos.x() - searchRadius,
        pos.y() - searchRadius,
        searchRadius * 2.0,
        searchRadius * 2.0);

    const QList<QGraphicsItem*> localItems = m_scene->items(searchRect);

    for (QGraphicsItem* item : localItems) {
        // Via-to-via spacing.
        if (auto* otherVia = dynamic_cast<ViaItem*>(item)) {
            const double d = QLineF(otherVia->scenePos(), pos).length();

            // Hard spacing violation.
            if (d < m_config.viaToViaSpacing) {
                return false;
            }

            // Preferred spacing penalty.
            const double preferred =
                m_config.viaToViaSpacing +
                m_config.viaPreferredClearanceMargin;

            if (d < preferred && penaltyOut) {
                const double deficit = preferred - d;
                *penaltyOut += m_config.viaProximityPenalty *
                               (deficit * deficit) /
                               (preferred * preferred);
            }
        }

        // Pad/drill keepout.
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            QRectF padRect = pad->sceneBoundingRect();
            double keepoutMargin = m_config.viaRadius + m_config.clearance + m_config.viaDrillKeepoutRadius;

            // Hard via-in-pad and near-drill keepout.
            padRect.adjust(
                -keepoutMargin,
                -keepoutMargin,
                 keepoutMargin,
                 keepoutMargin);

            if (padRect.contains(pos)) {
                return false;
            }
        }
    }

    // 2. Copper clearance check
    const double hardMinimum =
        m_config.viaRadius + m_config.clearance;

    const double preferred =
        hardMinimum + m_config.viaPreferredClearanceMargin;

    const int cellRadius =
        qMax(1, static_cast<int>(qCeil(preferred / m_config.gridSpacing)));

    for (int l = 0; l < m_gridLayers; ++l) {
        for (int dy = -cellRadius; dy <= cellRadius; ++dy) {
            for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
                // Circular window.
                if ((dx * dx + dy * dy) > (cellRadius * cellRadius)) {
                    continue;
                }

                const int nx = cx + dx;
                const int ny = cy + dy;

                if (!isValidCell(nx, ny, l)) {
                    continue;
                }

                const GridCell* cell = cellAt(nx, ny, l);
                if (!cell) {
                    continue;
                }

                const double dist =
                    m_config.gridSpacing *
                    qSqrt(double(dx * dx + dy * dy));

                // Anonymous keepout/block.
                if (cell->blocked && cell->netName.isEmpty()) {
                    if (dist < hardMinimum) {
                        return false;
                    }
                }

                // Foreign net copper.
                if (!cell->netName.isEmpty() && cell->netName != netName) {
                    if (dist < hardMinimum) {
                        return false;
                    }

                    if (dist < preferred && penaltyOut) {
                        const double deficit = preferred - dist;
                        *penaltyOut += m_config.viaProximityPenalty *
                                       (deficit * deficit) /
                                       (preferred * preferred);
                    }
                }
            }
        }
    }

    return true;
}

QList<PCBAutoRouter::AStarNode> PCBAutoRouter::getNeighbors(const AStarNode& node, const QString& netName) const {
    QList<AStarNode> neighbors;

    // 4-directional (or 8 if diagonals allowed)
    static const int dx4[] = {0, 0, 1, -1};
    static const int dy4[] = {1, -1, 0, 0};
    static const int dx8[] = {0, 0, 1, -1, 1, 1, -1, -1};
    static const int dy8[] = {1, -1, 0, 0, 1, -1, 1, -1};

    const int* dx = m_config.allowDiagonals ? dx8 : dx4;
    const int* dy = m_config.allowDiagonals ? dy8 : dy4;
    int count = m_config.allowDiagonals ? 8 : 4;

    for (int i = 0; i < count; ++i) {
        int nx = node.x + dx[i];
        int ny = node.y + dy[i];

        if (!isValidCell(nx, ny, node.layer)) continue;
        if (!isCellPassable(nx, ny, node.layer, netName)) continue;

        // Block diagonal moves that slice through corners of another net's pads/traces
        if (dx[i] != 0 && dy[i] != 0) {
            if (!isCellPassable(node.x + dx[i], node.y, node.layer, netName) ||
                !isCellPassable(node.x, node.y + dy[i], node.layer, netName)) {
                continue;
            }
        }

        AStarNode n;
        n.x = nx; n.y = ny; n.layer = node.layer;
        n.isVia = false;
        neighbors.append(n);
    }

    // Via transitions (layer changes)
    if (m_gridLayers > 1) {
        for (int l = 0; l < m_gridLayers; ++l) {
            if (l == node.layer) continue;
            double viaPenalty = 0.0;
            if (!isViaPassable(node.x, node.y, netName, &viaPenalty)) continue;

            AStarNode n;
            n.x = node.x; n.y = node.y; n.layer = l;
            n.isVia = true;
            n.parentX = node.x; n.parentY = node.y; n.parentLayer = node.layer;
            n.extraCost = m_config.viaBasePenaltyMm + viaPenalty;
            neighbors.append(n);
        }
    }

    return neighbors;
}

// ============================================================================
// Path to trace conversion
// ============================================================================

static double traceAngleDeg(const QPointF& a, const QPointF& b, const QPointF& c) {
    const QPointF u = b - a;
    const QPointF v = c - b;

    const double lu = QLineF(QPointF(0, 0), u).length();
    const double lv = QLineF(QPointF(0, 0), v).length();

    if (lu < 1e-6 || lv < 1e-6) {
        return 180.0;
    }

    double dot = QPointF::dotProduct(u, v) / (lu * lv);
    dot = qBound(-1.0, dot, 1.0);

    const double vectorAngle = qRadiansToDegrees(qAcos(dot));

    if (vectorAngle < 0.001) {
        return 180.0;
    }

    if (vectorAngle > 179.999) {
        return 0.0;
    }

    return qMin(vectorAngle, 180.0 - vectorAngle);
}

bool PCBAutoRouter::stubAnglesLegal(const QVector<QPointF>& points) const {
    for (int i = 1; i + 1 < points.size(); ++i) {
        const double angle = traceAngleDeg(points[i - 1], points[i], points[i + 1]);
        if (angle < m_config.minStubAngleDeg) {
            return false;
        }
    }
    return true;
}

QVector<QPointF> PCBAutoRouter::makeLegalStub(
    const QPointF& pad,
    const QPointF& gridNode,
    const QPointF& neighborGridPoint,
    bool isStart) const
{
    auto appendPoint = [](QVector<QPointF>& pts, const QPointF& p) {
        if (pts.isEmpty() || QLineF(pts.last(), p).length() > 0.01) {
            pts.append(p);
        }
    };

    auto pathLength = [](const QVector<QPointF>& pts) {
        double len = 0.0;
        for (int i = 1; i < pts.size(); ++i) {
            len += QLineF(pts[i - 1], pts[i]).length();
        }
        return len;
    };

    auto fullStubWithNeighbor = [&](const QVector<QPointF>& stub) {
        QVector<QPointF> full;
        if (!isStart && !neighborGridPoint.isNull()) {
            full.append(neighborGridPoint);
        }
        full.append(stub);
        if (isStart && !neighborGridPoint.isNull()) {
            full.append(neighborGridPoint);
        }
        return full;
    };

    auto isLegal = [&](const QVector<QPointF>& stub) {
        return stubAnglesLegal(fullStubWithNeighbor(stub));
    };

    auto minAngle = [&](const QVector<QPointF>& stub) {
        const QVector<QPointF> full = fullStubWithNeighbor(stub);
        double worst = 180.0;
        for (int i = 1; i + 1 < full.size(); ++i) {
            worst = qMin(worst, traceAngleDeg(full[i - 1], full[i], full[i + 1]));
        }
        return worst;
    };

    // Candidate 1: direct connection
    QVector<QPointF> direct;
    appendPoint(direct, pad);
    appendPoint(direct, gridNode);

    if (isLegal(direct)) {
        return direct;
    }

    // Candidate 2: horizontal then vertical
    QVector<QPointF> hv;
    appendPoint(hv, pad);
    appendPoint(hv, QPointF(gridNode.x(), pad.y()));
    appendPoint(hv, gridNode);

    // Candidate 3: vertical then horizontal
    QVector<QPointF> vh;
    appendPoint(vh, pad);
    appendPoint(vh, QPointF(pad.x(), gridNode.y()));
    appendPoint(vh, gridNode);

    const bool hvLegal = isLegal(hv);
    const bool vhLegal = isLegal(vh);

    if (hvLegal && vhLegal) {
        return (pathLength(hv) <= pathLength(vh)) ? hv : vh;
    }
    if (hvLegal) return hv;
    if (vhLegal) return vh;

    const double hvAngle = minAngle(hv);
    const double vhAngle = minAngle(vh);

    if (hvAngle > vhAngle) return hv;
    if (vhAngle > hvAngle) return vh;

    return (pathLength(hv) <= pathLength(vh)) ? hv : vh;
}

EdgeKey PCBAutoRouter::makeEdgeKey(const QPointF& a, const QPointF& b, int layer) const {
    auto quantize = [](double v) -> qint64 {
        return qint64(qRound(v * 100.0)); // 0.01mm resolution
    };

    qint64 x1 = quantize(a.x());
    qint64 y1 = quantize(a.y());
    qint64 x2 = quantize(b.x());
    qint64 y2 = quantize(b.y());

    if (x1 > x2 || (x1 == x2 && y1 > y2)) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    EdgeKey key;
    key.x1 = x1;
    key.y1 = y1;
    key.x2 = x2;
    key.y2 = y2;
    key.layer = layer;
    return key;
}

void PCBAutoRouter::rebuildCommittedEdgeSet() {
    m_committedEdges.clear();
    if (!m_scene) return;

    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            EdgeKey key = makeEdgeKey(trace->startPoint(), trace->endPoint(), trace->layer());
            m_committedEdges.insert(key);
        }
    }
}

void PCBAutoRouter::convertPathToTraces(const QVector<AStarNode>& path, const UnroutedConnection& conn) {
    if (path.size() < 2) return;

    // Start connection using legal-angle escape stubs
    QPointF firstGridPos = gridToScene(path[0].x, path[0].y);
    int startLayer = m_activeLayers[path[0].layer];

    QPointF startNeighbor;
    if (path.size() >= 2) {
        startNeighbor = gridToScene(path[1].x, path[1].y);
    }

    QVector<QPointF> startStub;
    if (m_config.useLegalAngleEscapeStubs) {
        startStub = makeLegalStub(conn.start, firstGridPos, startNeighbor, true);
    } else {
        startStub = {conn.start, firstGridPos};
    }

    for (int i = 1; i < startStub.size(); ++i) {
        TraceItem* t = createTraceSegment(startStub[i - 1], startStub[i], startLayer, conn.netName, conn.traceWidth);
        if (t) {
            m_stats.traceSegmentCount++;
            m_stats.totalTraceLength += QLineF(startStub[i - 1], startStub[i]).length();
        }
    }

    QVector<AStarNode> segmentPoints;
    segmentPoints.append(path[0]);

    for (int i = 1; i < path.size(); ++i) {
        const auto& curr = path[i];
        
        if (curr.isVia) {
            // Finalize current segment trace if we have accumulated points
            if (segmentPoints.size() >= 2) {
                createTraceFromPoints(segmentPoints, conn);
            }
            segmentPoints.clear();
            
            // Create via
            int viaStartLayer = m_activeLayers[curr.parentLayer];
            int viaEndLayer = m_activeLayers[curr.layer];
            QPointF viaPos = gridToScene(curr.x, curr.y);
            ViaItem* via = createVia(viaPos, viaStartLayer, viaEndLayer, conn.netName);
            if (via) m_stats.viaCount++;

            // Start new segment from the via position
            segmentPoints.append(curr);
        } else {
            if (segmentPoints.isEmpty()) {
                segmentPoints.append(curr);
            } else {
                // Check collinearity with the last segment direction
                if (segmentPoints.size() < 2) {
                    segmentPoints.append(curr);
                } else {
                    const auto& prev = segmentPoints[segmentPoints.size() - 2];
                    const auto& last = segmentPoints.last();
                    
                    int dx1 = last.x - prev.x;
                    int dy1 = last.y - prev.y;
                    int dx2 = curr.x - last.x;
                    int dy2 = curr.y - last.y;
                    
                    bool sameLayer = (curr.layer == last.layer && last.layer == prev.layer);
                    bool collinear = (dx1 * dy2 == dx2 * dy1);
                    bool sameDirection = ((dx1 >= 0 && dx2 >= 0) || (dx1 <= 0 && dx2 <= 0)) &&
                                         ((dy1 >= 0 && dy2 >= 0) || (dy1 <= 0 && dy2 <= 0));
                                         
                    if (sameLayer && collinear && sameDirection) {
                        // Replace last with curr to extend the segment
                        segmentPoints.last() = curr;
                    } else {
                        segmentPoints.append(curr);
                    }
                }
            }
        }
    }

    if (segmentPoints.size() >= 2) {
        createTraceFromPoints(segmentPoints, conn);
    }

    // Final connection using legal-angle escape stubs
    QPointF lastGridPos = gridToScene(path.last().x, path.last().y);
    int finalLayer = m_activeLayers[path.last().layer];

    QPointF endNeighbor;
    if (path.size() >= 2) {
        endNeighbor = gridToScene(path[path.size() - 2].x, path[path.size() - 2].y);
    }

    QVector<QPointF> endStub;
    if (m_config.useLegalAngleEscapeStubs) {
        endStub = makeLegalStub(conn.end, lastGridPos, endNeighbor, false);
    } else {
        endStub = {conn.end, lastGridPos};
    }

    std::reverse(endStub.begin(), endStub.end());

    for (int i = 1; i < endStub.size(); ++i) {
        TraceItem* t = createTraceSegment(endStub[i - 1], endStub[i], finalLayer, conn.netName, conn.traceWidth);
        if (t) {
            m_stats.traceSegmentCount++;
            m_stats.totalTraceLength += QLineF(endStub[i - 1], endStub[i]).length();
        }
    }
}

void PCBAutoRouter::createTraceFromPoints(const QVector<AStarNode>& points, const UnroutedConnection& conn) {
    for (int i = 0; i < points.size() - 1; ++i) {
        QPointF start = gridToScene(points[i].x, points[i].y);
        QPointF end = gridToScene(points[i+1].x, points[i+1].y);
        int layer = m_activeLayers[points[i+1].layer];
        
        TraceItem* trace = createTraceSegment(start, end, layer, conn.netName, conn.traceWidth);
        if (trace) {
            m_stats.traceSegmentCount++;
            m_stats.totalTraceLength += QLineF(start, end).length();
        }
    }
}

TraceItem* PCBAutoRouter::createTraceSegment(QPointF start, QPointF end, int layer,
                                              const QString& netName, double width) {
    if (!m_scene) return nullptr;

    // Don't create zero-length traces
    if (QLineF(start, end).length() < 0.01) return nullptr;

    // Fast duplicate check via EdgeKey set
    EdgeKey key = makeEdgeKey(start, end, layer);
    if (m_committedEdges.contains(key)) {
        return nullptr;
    }
    m_committedEdges.insert(key);

    TraceItem* trace = new TraceItem(start, end);
    trace->setLayer(layer);
    trace->setWidth(width);
    trace->setNetName(netName);
    trace->updateConnectivity();

    m_scene->addItem(trace);
    return trace;
}

ViaItem* PCBAutoRouter::createVia(QPointF pos, int startLayer, int endLayer, const QString& netName) {
    if (!m_scene) return nullptr;

    ViaItem* via = new ViaItem(pos);
    via->setStartLayer(startLayer);
    via->setEndLayer(endLayer);
    via->setNetName(netName);

    NetClass nc = NetClassManager::instance().getClassForNet(netName);
    if (via->model()) {
        if (nc.viaDiameter > 0.001) via->model()->setDiameter(nc.viaDiameter);
        if (nc.viaDrill > 0.001) via->model()->setDrillSize(nc.viaDrill);
    }

    m_scene->addItem(via);
    return via;
}

// ============================================================================
// Rip-up and retry
// ============================================================================

int PCBAutoRouter::ripUpWorstTraces(int count, const QSet<QString>& failedNets) {
    int removed = 0;

    // Collect all nets that are blocking the failed nets
    QSet<QString> blockerNets;
    for (const QString& failedNet : failedNets) {
        blockerNets.unite(m_blockingNets.value(failedNet));
    }

    // Collect all trace items
    QList<TraceItem*> blockerTraces;
    QList<TraceItem*> failedNetTraces;
    QList<TraceItem*> otherTraces;

    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (blockerNets.contains(trace->netName())) {
                blockerTraces.append(trace);
            } else if (failedNets.contains(trace->netName())) {
                failedNetTraces.append(trace);
            } else {
                otherTraces.append(trace);
            }
        }
    }

    // Prioritize: blocker traces first, then failed net traces, then other traces
    QList<TraceItem*> tracesToRip = blockerTraces;
    tracesToRip.append(failedNetTraces);
    tracesToRip.append(otherTraces);

    // Remove up to 'count' traces
    for (int i = 0; i < qMin(count, tracesToRip.size()); ++i) {
        m_scene->removeItem(tracesToRip[i]);
        delete tracesToRip[i];
        removed++;
    }

    // Also remove orphan vias (vias no longer connected to any trace)
    QList<ViaItem*> orphanVias;
    for (auto* item : m_scene->items()) {
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            bool connected = false;
            for (auto* other : m_scene->items()) {
                if (auto* trace = dynamic_cast<TraceItem*>(other)) {
                    if (trace->netName() == via->netName()) {
                        if (QLineF(trace->startPoint(), via->pos()).length() < 0.5 ||
                            QLineF(trace->endPoint(), via->pos()).length() < 0.5) {
                            connected = true;
                            break;
                        }
                    }
                }
            }
            if (!connected) orphanVias.append(via);
        }
    }

    for (auto* via : orphanVias) {
        m_scene->removeItem(via);
        delete via;
        removed++;
    }

    return removed;
}

// ============================================================================
// Statistics
// ============================================================================

void PCBAutoRouter::updateStats() {
    m_stats.viaCount = 0;
    m_stats.traceSegmentCount = 0;
    m_stats.totalTraceLength = 0.0;

    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            m_stats.traceSegmentCount++;
            m_stats.totalTraceLength += QLineF(trace->startPoint(), trace->endPoint()).length();
        }
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            m_stats.viaCount++;
        }
    }
}
