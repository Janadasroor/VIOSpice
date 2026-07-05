/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_ECO_RESOLVER_H
#define PCB_ECO_RESOLVER_H

#include "eco_types.h"

class QGraphicsScene;
class QStatusBar;
class QGraphicsView;

class PCBECOResolver {
public:
    static void applyECO(const ECOPackage& package, QGraphicsScene* scene, QStatusBar* statusBar, QGraphicsView* view);
};

#endif // PCB_ECO_RESOLVER_H
