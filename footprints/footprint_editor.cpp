/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "footprint_editor.h"
#include <QUndoStack>

using namespace Flux::Model;

FootprintEditor::FootprintEditor(QWidget* parent)
    : QDialog(parent), m_currentTool(Select), m_previewItem(nullptr), m_activeLayer(FootprintPrimitive::Top_Silkscreen)
{
    m_undoStack = new QUndoStack(this);
    setupUI();
}

FootprintEditor::FootprintEditor(const FootprintDefinition& footprint, QWidget* parent)
    : QDialog(parent), m_currentTool(Select), m_footprint(footprint), m_previewItem(nullptr), m_activeLayer(FootprintPrimitive::Top_Silkscreen)
{
    m_undoStack = new QUndoStack(this);
    setupUI();
    setFootprintDefinition(footprint);
    m_currentPadShape = "Rect"; // Default
}

FootprintEditor::~FootprintEditor() {}

FootprintDefinition FootprintEditor::footprintDefinition() const {
    return m_footprint;
}
