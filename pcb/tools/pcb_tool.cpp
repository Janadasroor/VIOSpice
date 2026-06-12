/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_tool.h"
#include "pcb_view.h"

PCBTool::PCBTool(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
    , m_view(nullptr) {
}
