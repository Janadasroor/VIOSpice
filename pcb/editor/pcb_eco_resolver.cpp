/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_eco_resolver.h"
#include "component_item.h"
#include "pad_item.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include "../../footprints/footprint_library.h"
#include <QGraphicsScene>
#include <QStatusBar>
#include <QGraphicsView>
#include <QDebug>
#include <QMap>
#include <QSet>
#include <algorithm>

void PCBECOResolver::applyECO(const ECOPackage& package, QGraphicsScene* scene, QStatusBar* statusBar, QGraphicsView* view) {
    if (!scene) { qWarning() << "[PCB applyECO] scene is null!"; return; }
    qDebug() << "[PCB applyECO] Starting, package:" << package.components.size() << "components," << package.nets.size() << "nets";

    if (statusBar) {
        statusBar->showMessage("🔄 Applying ECO from Schematic...", 3000);
    }
    auto& lib = FootprintLibraryManager::instance();
    auto resolveFootprintName = [&lib](const QString& rawName) -> QString {
        const QString trimmed = rawName.trimmed();
        if (trimmed.isEmpty()) return QString();
        if (lib.hasFootprint(trimmed)) return trimmed;
        if (trimmed.contains(':')) {
            const QString tail = trimmed.section(':', -1).trimmed();
            if (!tail.isEmpty() && lib.hasFootprint(tail)) return tail;
        }
        if (trimmed.contains('/')) {
            const QString tail = trimmed.section('/', -1).trimmed();
            if (!tail.isEmpty() && lib.hasFootprint(tail)) return tail;
        }
        return trimmed;
    };
    
    QMap<QString, ComponentItem*> existingComps;
    for (auto* item : scene->items()) {
        if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
            existingComps[comp->name()] = comp;
        }
    }

    int newCount = 0;
    QPointF gridStart(0, 0);
    int row = 0, col = 0;
    QList<ComponentItem*> newItems;
    QSet<QString> unresolvedFootprints;
    QMap<QString, QMap<QString, QString>> componentPinPadMappings;

    for (const auto& ecoComp : package.components) {
        componentPinPadMappings[ecoComp.reference] = ecoComp.pinPadMapping;
    }

    for (const auto& ecoComp : package.components) {
        if (existingComps.contains(ecoComp.reference)) {
            ComponentItem* existingComp = existingComps.value(ecoComp.reference);
            const QString newFootprint = resolveFootprintName(ecoComp.footprint);
            if (!newFootprint.isEmpty() && !lib.hasFootprint(newFootprint)) {
                unresolvedFootprints.insert(newFootprint);
            }
            if (!newFootprint.isEmpty() && existingComp->componentType() != newFootprint) {
                existingComp->setComponentType(newFootprint);
            }
            existingComp->setName(ecoComp.reference);
            existingComp->setValue(ecoComp.value);
        } else {
            // Space items by 20mm instead of 150mm
            QPointF pos = gridStart + QPointF(col * 20, row * 20);
            const QString newFootprint = resolveFootprintName(ecoComp.footprint);
            if (!newFootprint.isEmpty() && !lib.hasFootprint(newFootprint)) {
                unresolvedFootprints.insert(newFootprint);
            }
            ComponentItem* newComp = new ComponentItem(pos, newFootprint);
            newComp->setName(ecoComp.reference);
            newComp->setValue(ecoComp.value);
            scene->addItem(newComp);
            existingComps[ecoComp.reference] = newComp;
            newItems.append(newComp);
            newCount++;
            col++;
            if (col > 5) { col = 0; row++; }
        }
    }
    
    // Cache pads for deterministic pin->pad mapping and clear stale net assignments.
    QMap<ComponentItem*, QList<PadItem*>> componentPads;
    for (auto it = existingComps.begin(); it != existingComps.end(); ++it) {
        ComponentItem* c = it.value();
        QList<PadItem*> pads;
        for (QGraphicsItem* child : c->childItems()) {
            if (PadItem* p = dynamic_cast<PadItem*>(child)) {
                p->setNetName(QString());
                pads.append(p);
            }
        }
        std::sort(pads.begin(), pads.end(), [](PadItem* a, PadItem* b) {
            const bool aNamed = !a->name().trimmed().isEmpty();
            const bool bNamed = !b->name().trimmed().isEmpty();
            if (aNamed != bNamed) return aNamed; // Prefer named pads first

            bool aNumOk = false, bNumOk = false;
            int aNum = a->name().toInt(&aNumOk);
            int bNum = b->name().toInt(&bNumOk);
            if (aNumOk && bNumOk && aNum != bNum) return aNum < bNum;
            if (aNumOk != bNumOk) return aNumOk;

            if (!qFuzzyCompare(a->pos().x() + 1.0, b->pos().x() + 1.0)) {
                return a->pos().x() < b->pos().x();
            }
            return a->pos().y() < b->pos().y();
        });
        componentPads[c] = pads;
    }

    int netCount = 0;
    for (const auto& net : package.nets) {
        for (const auto& pin : net.pins) {
            if (!existingComps.contains(pin.componentRef)) continue;
            ComponentItem* c = existingComps[pin.componentRef];
            const QMap<QString, QString> pinPadMap = componentPinPadMappings.value(pin.componentRef);

            PadItem* targetPad = nullptr;
            const QList<PadItem*>& pads = componentPads[c];
            QString requestedPadName = pin.pinName.trimmed();
            if (pinPadMap.contains(pin.pinName) && !pinPadMap.value(pin.pinName).trimmed().isEmpty()) {
                requestedPadName = pinPadMap.value(pin.pinName).trimmed();
            }

            // 1) Strict named match first
            for (PadItem* p : pads) {
                if (p->name().trimmed() == requestedPadName) {
                    targetPad = p;
                    break;
                }
            }

            // 2) Fallback for unnamed footprints: numeric pin maps to sorted unnamed pad index
            if (!targetPad) {
                bool pinNumOk = false;
                int pinNum = requestedPadName.toInt(&pinNumOk);
                if (pinNumOk && pinNum > 0) {
                    QList<PadItem*> unnamedPads;
                    for (PadItem* p : pads) {
                        if (p->name().trimmed().isEmpty()) unnamedPads.append(p);
                    }
                    if (pinNum <= unnamedPads.size()) {
                        targetPad = unnamedPads[pinNum - 1];
                    }
                }
            }

            if (targetPad) {
                targetPad->setNetName(net.name);
            }
        }
        netCount++;
    }

    if (statusBar) {
        statusBar->showMessage(QString("✅ ECO Applied: %1 new parts, %2 active nets").arg(newCount).arg(netCount), 5000);
    }
    qDebug() << "[PCB applyECO] Done. newCount:" << newCount << "netCount:" << netCount << "total items in scene:" << scene->items().size();
    if (!unresolvedFootprints.isEmpty() && statusBar) {
        QStringList unresolvedList;
        for (const QString& fp : unresolvedFootprints) unresolvedList.append(fp);
        unresolvedList.sort();
        statusBar->showMessage(
            QString("ECO applied, but %1 footprint(s) were not found in PCB libraries: %2")
                .arg(unresolvedFootprints.size())
                .arg(unresolvedList.join(", ")),
            8000
        );
    }
    
    // Zoom to fit new items if any
    if (!newItems.isEmpty() && view) {
        view->centerOn(newItems.first());
    }

    PCBRatsnestManager::instance().update();
}
