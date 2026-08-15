/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_layer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>

// ============================================================================
// PCBLayer Implementation
// ============================================================================

PCBLayer::PCBLayer(int id, const QString& name, LayerType type, Side side)
    : m_id(id)
    , m_name(name)
    , m_type(type)
    , m_side(side)
    , m_visible(true)
    , m_locked(false)
    , m_opacity(1.0)
{
    // Set KiCad 8 standard default colors based on layer type and side
    switch (type) {
        case Copper:
            m_color = (side == Top) ? PCBLayerManager::copperTopColor() 
                                    : PCBLayerManager::copperBottomColor();
            break;
        case Silkscreen:
            m_color = (side == Top) ? QColor(0, 200, 200) : QColor(0, 128, 128); // Cyan / Dark Cyan
            break;
        case Soldermask:
            m_color = (side == Top) ? QColor(200, 0, 200, 120) : QColor(128, 0, 128, 120); // Translucent Purple
            break;
        case Paste:
            m_color = (side == Top) ? QColor(128, 200, 255) : QColor(64, 128, 200); // Light Blue / Dark Blue Paste
            break;
        case Adhesive:
            m_color = (side == Top) ? QColor(200, 80, 200) : QColor(128, 40, 128); // Magenta Adhesive
            break;
        case Courtyard:
            m_color = (side == Top) ? QColor(200, 200, 160) : QColor(128, 128, 96); // Grey-Yellow Courtyard
            break;
        case Fabrication:
            m_color = (side == Top) ? QColor(144, 160, 176) : QColor(160, 144, 128); // Soft Grey-Blue / Brown Fab
            break;
        case UserDrawings:
            m_color = QColor(100, 200, 255); // Sky Blue
            break;
        case UserComments:
            m_color = QColor(200, 200, 100); // Soft Yellow
            break;
        case UserEco:
            m_color = QColor(255, 150, 50); // Orange
            break;
        case Margin:
            m_color = QColor(255, 0, 255); // Neon Magenta
            break;
        case EdgeCuts:
            m_color = PCBLayerManager::edgeCutsColor(); // Bright Yellow
            break;
        case Drill:
            m_color = PCBLayerManager::drillColor(); // Dark Grey
            break;
        default:
            m_color = QColor(128, 128, 128);
            break;
    }
}

QString PCBLayer::typeString() const {
    switch (m_type) {
        case Copper: return "Copper";
        case Silkscreen: return "Silkscreen";
        case Soldermask: return "Soldermask";
        case Paste: return "Paste";
        case Courtyard: return "Courtyard";
        case Fabrication: return "Fabrication";
        case EdgeCuts: return "Edge Cuts";
        case Drill: return "Drill";
        case Adhesive: return "Adhesive";
        case UserDrawings: return "User Drawings";
        case UserComments: return "User Comments";
        case UserEco: return "User ECO";
        case Margin: return "Margin";
        case UserDefined: return "User Defined";
        default: return "Unknown";
    }
}

QString PCBLayer::sideString() const {
    switch (m_side) {
        case Top: return "Top";
        case Bottom: return "Bottom";
        case Internal: return "Internal";
        case Both: return "Both";
        default: return "Unknown";
    }
}

// ============================================================================
// PCBLayerManager Implementation
// ============================================================================

PCBLayerManager::PCBLayerManager()
    : m_activeLayerId(TopCopper)
    , m_copperLayerCount(2)
{
    initializeStandardLayers();
    updateStackupFromLayerCount(2);
}

PCBLayerManager& PCBLayerManager::instance() {
    static PCBLayerManager instance;
    return instance;
}

void PCBLayerManager::initializeStandardLayers() {
    m_layers.clear();

    // Copper layers
    m_layers.append(PCBLayer(TopCopper, "F.Cu", PCBLayer::Copper, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomCopper, "B.Cu", PCBLayer::Copper, PCBLayer::Bottom));

    // Exact KiCad 8/9 Standard Copper Layer Color Palette for In1.Cu .. In30.Cu
    static const QList<QColor> kicadCopperColors = {
        QColor(200, 200, 50),   // In1.Cu: Yellow / Gold
        QColor(200, 50, 200),   // In2.Cu: Magenta
        QColor(50, 200, 200),   // In3.Cu: Cyan
        QColor(200, 100, 50),   // In4.Cu: Orange
        QColor(100, 200, 50),   // In5.Cu: Lime Green
        QColor(50, 200, 100),   // In6.Cu: Spring Green
        QColor(50, 100, 200),   // In7.Cu: Royal Blue
        QColor(100, 50, 200),   // In8.Cu: Purple
        QColor(200, 50, 100),   // In9.Cu: Crimson
        QColor(150, 150, 50),   // In10.Cu: Olive
        QColor(150, 50, 150),   // In11.Cu: Deep Purple
        QColor(50, 150, 150),   // In12.Cu: Teal
        QColor(150, 100, 50),   // In13.Cu: Brown
        QColor(100, 150, 50),   // In14.Cu: Forest Green
        QColor(50, 150, 100),   // In15.Cu: Sea Green
        QColor(50, 100, 150),   // In16.Cu: Slate Blue
        QColor(100, 50, 150),   // In17.Cu: Dark Violet
        QColor(150, 50, 100),   // In18.Cu: Rose
        QColor(200, 150, 50),   // In19.Cu: Amber
        QColor(150, 200, 50),   // In20.Cu: Yellow Green
        QColor(50, 200, 150),   // In21.Cu: Mint
        QColor(50, 150, 200),   // In22.Cu: Sky Blue
        QColor(150, 50, 200),   // In23.Cu: Violet
        QColor(200, 50, 150),   // In24.Cu: Hot Pink
        QColor(200, 100, 100),  // In25.Cu: Salmon
        QColor(100, 200, 100),  // In26.Cu: Pastel Green
        QColor(100, 100, 200),  // In27.Cu: Periwinkle
        QColor(200, 200, 100),  // In28.Cu: Soft Yellow
        QColor(200, 100, 200),  // In29.Cu: Soft Magenta
        QColor(100, 200, 200)   // In30.Cu: Soft Cyan
    };

    for (int i = 1; i <= 30; ++i) {
        int layerId = 100 + i; // 101..130 (Internal copper layers start at 101)
        PCBLayer inLayer(layerId, QString("In%1.Cu").arg(i), PCBLayer::Copper, PCBLayer::Internal);
        QColor color = kicadCopperColors[(i - 1) % kicadCopperColors.size()];
        inLayer.setColor(color);
        m_layers.append(inLayer);
    }

    // Silkscreen layers
    m_layers.append(PCBLayer(TopSilkscreen, "Top Silkscreen", PCBLayer::Silkscreen, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomSilkscreen, "Bottom Silkscreen", PCBLayer::Silkscreen, PCBLayer::Bottom));

    // Soldermask layers
    m_layers.append(PCBLayer(TopSoldermask, "Top Soldermask", PCBLayer::Soldermask, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomSoldermask, "Bottom Soldermask", PCBLayer::Soldermask, PCBLayer::Bottom));

    // Paste layers
    m_layers.append(PCBLayer(TopPaste, "Top Paste", PCBLayer::Paste, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomPaste, "Bottom Paste", PCBLayer::Paste, PCBLayer::Bottom));

    // Adhesive layers
    m_layers.append(PCBLayer(TopAdhesive, "Top Adhesive", PCBLayer::Adhesive, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomAdhesive, "Bottom Adhesive", PCBLayer::Adhesive, PCBLayer::Bottom));

    // Courtyard layers
    m_layers.append(PCBLayer(TopCourtyard, "Top Courtyard", PCBLayer::Courtyard, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomCourtyard, "Bottom Courtyard", PCBLayer::Courtyard, PCBLayer::Bottom));

    // Fabrication layers
    m_layers.append(PCBLayer(TopFabrication, "Top Fabrication", PCBLayer::Fabrication, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomFabrication, "Bottom Fabrication", PCBLayer::Fabrication, PCBLayer::Bottom));

    // Board outline
    m_layers.append(PCBLayer(EdgeCuts, "Edge.Cuts", PCBLayer::EdgeCuts, PCBLayer::Both));

    // Margin
    m_layers.append(PCBLayer(55, "Margin", PCBLayer::Margin, PCBLayer::Both));
}

PCBLayer* PCBLayerManager::layer(int id) {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id() == id) {
            return &m_layers[i];
        }
    }
    return nullptr;
}

PCBLayer* PCBLayerManager::layer(const QString& name) {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].name().compare(name, Qt::CaseInsensitive) == 0) {
            return &m_layers[i];
        }
    }
    return nullptr;
}

PCBLayer* PCBLayerManager::activeLayer() {
    return layer(m_activeLayerId);
}

void PCBLayerManager::setActiveLayer(int id) {
    if (layer(id) && m_activeLayerId != id) {
        m_activeLayerId = id;
        emit activeLayerChanged(id);
    }
}

void PCBLayerManager::setActiveLayer(const QString& name) {
    PCBLayer* l = layer(name);
    if (l) {
        setActiveLayer(l->id());
    }
}

void PCBLayerManager::setLayerVisible(int id, bool visible) {
    PCBLayer* l = layer(id);
    if (l && l->isVisible() != visible) {
        l->setVisible(visible);
        emit layerVisibilityChanged(id, visible);
    }
}

void PCBLayerManager::setLayerLocked(int id, bool locked) {
    PCBLayer* l = layer(id);
    if (l && l->isLocked() != locked) {
        l->setLocked(locked);
        emit layerLockedChanged(id, locked);
    }
}

void PCBLayerManager::toggleLayerVisibility(int id) {
    PCBLayer* l = layer(id);
    if (l) {
        setLayerVisible(id, !l->isVisible());
    }
}

QList<PCBLayer*> PCBLayerManager::copperLayers() {
    QList<PCBLayer*> result;
    if (auto* top = layer(TopCopper)) {
        result.append(top);
    }
    for (int i = 1; i <= m_copperLayerCount - 2; ++i) {
        if (auto* in = layer(100 + i)) {
            result.append(in);
        }
    }
    if (m_copperLayerCount >= 2) {
        if (auto* bot = layer(BottomCopper)) {
            result.append(bot);
        }
    }
    return result;
}

int PCBLayerManager::copperLayerCount() const {
    return m_copperLayerCount;
}

void PCBLayerManager::setCopperLayerCount(int count) {
    if (count != m_copperLayerCount && count >= 2 && count <= 32) {
        m_copperLayerCount = count;
        updateStackupFromLayerCount(count);
        emit layerListChanged();
    }
}

void PCBLayerManager::setStackup(const BoardStackup& stackup) {
    m_stackup = stackup;
    emit layerListChanged();
}

void PCBLayerManager::updateStackupFromLayerCount(int count) {
    m_stackup.stack.clear();
    m_stackup.finishThickness = 1.6; // Default standard thickness
    m_stackup.surfaceFinish = "ENIG";
    m_stackup.solderMaskExpansion = 0.05;
    m_stackup.pasteExpansion = 0.00;

    // Top Solder Mask
    m_stackup.stack.append({TopSoldermask, "Top Solder Mask", "Soldermask", 0.02, 3.5, "Epoxy", 0.0});
    
    // Top Copper
    m_stackup.stack.append({TopCopper, "Top Copper", "Copper", 0.035, 0, "Copper", 1.0});

    // Internal Layers
    if (count > 2) {
        double coreThickness = 1.6 / (count - 1);
        for (int i = 1; i < count - 1; ++i) {
            m_stackup.stack.append({-1, "Dielectric", "Prepreg", coreThickness, 4.2, "FR-4", 0.0});
            m_stackup.stack.append({100 + i, QString("In%1.Cu").arg(i), "Copper", 0.035, 0, "Copper", 1.0});
        }
        m_stackup.stack.append({-1, "Dielectric", "Core", coreThickness, 4.2, "FR-4", 0.0});
    } else {
        // Standard FR-4 Core
        m_stackup.stack.append({-1, "Dielectric", "Core", 1.5, 4.2, "FR-4", 0.0});
    }

    // Bottom Copper
    m_stackup.stack.append({BottomCopper, "Bottom Copper", "Copper", 0.035, 0, "Copper", 1.0});
    
    // Bottom Solder Mask
    m_stackup.stack.append({BottomSoldermask, "Bottom Solder Mask", "Soldermask", 0.02, 3.5, "Epoxy", 0.0});
}

QJsonObject PCBLayerManager::toJson() const {
    QJsonObject json;
    QJsonArray stackArray;
    for (const auto& layer : m_stackup.stack) {
        QJsonObject layerObj;
        layerObj["id"] = layer.layerId;
        layerObj["name"] = layer.name;
        layerObj["type"] = layer.type;
        layerObj["thickness"] = layer.thickness;
        layerObj["er"] = layer.dielectricConstant;
        layerObj["material"] = layer.material;
        layerObj["copperWeightOz"] = layer.copperWeightOz;
        stackArray.append(layerObj);
    }
    json["stackup"] = stackArray;
    json["finishThickness"] = m_stackup.finishThickness;
    json["surfaceFinish"] = m_stackup.surfaceFinish;
    json["solderMaskExpansion"] = m_stackup.solderMaskExpansion;
    json["pasteExpansion"] = m_stackup.pasteExpansion;
    json["copperLayerCount"] = m_copperLayerCount;
    return json;
}

void PCBLayerManager::fromJson(const QJsonObject& json) {
    if (json.contains("copperLayerCount")) {
        setCopperLayerCount(json["copperLayerCount"].toInt(2));
    }
    if (json.contains("stackup")) {
        m_stackup.stack.clear();
        QJsonArray stackArray = json["stackup"].toArray();
        for (const QJsonValue& val : stackArray) {
            QJsonObject obj = val.toObject();
            StackupLayer layer;
            layer.layerId = obj["id"].toInt();
            layer.name = obj["name"].toString();
            layer.type = obj["type"].toString();
            layer.thickness = obj["thickness"].toDouble();
            layer.dielectricConstant = obj["er"].toDouble();
            layer.material = obj["material"].toString();
            layer.copperWeightOz = obj["copperWeightOz"].toDouble(layer.type == "Copper" ? 1.0 : 0.0);
            m_stackup.stack.append(layer);
        }
        m_stackup.finishThickness = json["finishThickness"].toDouble(1.6);
        m_stackup.surfaceFinish = json["surfaceFinish"].toString("ENIG");
        m_stackup.solderMaskExpansion = json["solderMaskExpansion"].toDouble(0.05);
        m_stackup.pasteExpansion = json["pasteExpansion"].toDouble(0.0);
        emit layerListChanged();
    }
}
