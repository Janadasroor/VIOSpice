/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNC_ENGINE_H
#define SYNC_ENGINE_H

#include "eco_types.h"
#include "../schematic/models/schematic_page_model.h"
#include "../schematic/analysis/netlist_engine.h"
#include "../pcb/models/board_model.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Headless Engine for synchronizing Schematic and PCB models (ECO).
 * Generates an ECOPackage by comparing a Schematic Page with a Board Model.
 */
class SyncEngine {
public:
    /**
     * @brief Generate an ECO to push Schematic changes to the PCB.
     */
    static ECOPackage generateForwardECO(const Model::SchematicPageModel* schematic, const Model::BoardModel* board) {
        ECOPackage eco;

        // 1. Extract Netlist from Schematic
        auto schematicNetlist = NetlistEngine::generateNetlist(schematic);

        // 2. Identify new or modified components
        for (auto* sComp : schematic->components()) {
            bool foundInPcb = false;
            for (auto* pComp : board->components()) {
                if (pComp->name() == sComp->reference()) {
                    foundInPcb = true;
                    // Check if footprint changed
                    if (pComp->componentType() != sComp->footprint()) {
                        eco.components.append(convertComponent(sComp));
                    }
                    break;
                }
            }
            if (!foundInPcb) {
                eco.components.append(convertComponent(sComp));
            }
        }

        // 3. Map Schematic Netlist to ECO format
        for (auto it = schematicNetlist.begin(); it != schematicNetlist.end(); ++it) {
            ECONet ecoNet;
            ecoNet.name = it.key();
            for (const auto& conn : it.value().connections) {
                ecoNet.pins.append({conn.componentRef, conn.pinName});
            }
            eco.nets.append(ecoNet);
        }

        return eco;
    }

    /**
     * @brief Apply an ECOPackage to a BoardModel.
     * Pure headless logic - modifies the data models directly.
     */
    static void applyECOToBoard(const ECOPackage& eco, Model::BoardModel* board) {
        // Add/Update components
        for (const auto& ecoComp : eco.components) {
            Model::ComponentModel* target = nullptr;
            for (auto* pComp : board->components()) {
                if (pComp->name() == ecoComp.reference) {
                    target = pComp;
                    break;
                }
            }

            if (!target) {
                target = new Model::ComponentModel();
                target->setName(ecoComp.reference);
                board->addComponent(target);
            }

            target->setComponentType(ecoComp.footprint);
            target->setValue(ecoComp.value);
        }

        // Update Net names on PCB pads based on ECO nets
        for (const auto& ecoNet : eco.nets) {
            for (const auto& ecoPin : ecoNet.pins) {
                // Find component on board
                for (auto* pComp : board->components()) {
                    if (pComp->name() == ecoPin.componentRef) {
                        // Find pad in component (matching pin name/number)
                        for (auto* pad : pComp->pads()) {
                            // Assuming pad name matches pin name for now
                            if (pad->netName() != ecoNet.name) {
                                pad->setNetName(ecoNet.name);
                            }
                        }
                    }
                }
            }
        }
    }

    /**
     * @brief Generate a Reverse ECO to push PCB footprint/value edits back to the Schematic.
     */
    static ECOPackage generateBackwardECO(const Model::BoardModel* board, const Model::SchematicPageModel* schematic) {
        ECOPackage eco;
        if (!board || !schematic) return eco;

        for (const auto* pComp : board->components()) {
            for (const auto* sComp : schematic->components()) {
                if (pComp->name() == sComp->reference()) {
                    // If footprint or value was modified in PCB editor
                    if (pComp->componentType() != sComp->footprint() || pComp->value() != sComp->value()) {
                        ECOComponent e;
                        e.reference = pComp->name();
                        e.footprint = pComp->componentType();
                        e.value = pComp->value();
                        eco.components.append(e);
                    }
                    break;
                }
            }
        }
        return eco;
    }

    /**
     * @brief Apply a Reverse ECOPackage to a SchematicPageModel.
     */
    static void applyECOToSchematic(const ECOPackage& eco, Model::SchematicPageModel* schematic) {
        if (!schematic) return;

        for (const auto& ecoComp : eco.components) {
            for (auto* sComp : schematic->components()) {
                if (sComp->reference() == ecoComp.reference) {
                    if (!ecoComp.footprint.isEmpty()) {
                        sComp->setFootprint(ecoComp.footprint);
                    }
                    if (!ecoComp.value.isEmpty()) {
                        sComp->setValue(ecoComp.value);
                    }
                    break;
                }
            }
        }
    }

private:
    static ECOComponent convertComponent(const Model::SchematicComponentModel* sComp) {
        ECOComponent e;
        e.reference = sComp->reference();
        e.footprint = sComp->footprint();
        e.value = sComp->value();
        return e;
    }
};

} // namespace Analysis
} // namespace Flux

#endif // SYNC_ENGINE_H
