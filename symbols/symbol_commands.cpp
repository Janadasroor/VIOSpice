/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_commands.h"
#include "symbol_editor.h"
#include "editor/symbol_canvas.h"
#include <QGraphicsScene>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

// ─────────────────────────────────────────────────────────────────────────────
//  AddPrimitiveCommand
// ─────────────────────────────────────────────────────────────────────────────

AddPrimitiveCommand::AddPrimitiveCommand(QPointer<SymbolEditor> editor,
                                        const SymbolPrimitive& prim,
                                        QGraphicsItem* visual)
    : m_editor(editor), m_prim(prim), m_visual(visual) {
    setText("Add Primitive");
}

AddPrimitiveCommand::~AddPrimitiveCommand() {
    if (m_visual && !m_visual->scene()) {
        delete m_visual;
        m_visual = nullptr;
    }
}

void AddPrimitiveCommand::redo() {
    if (!m_editor || !m_visual || !m_editor->m_canvas) return;
    m_insertedIndex = m_editor->m_symbol.primitives().size();
    
    m_editor->m_symbol.addPrimitive(m_prim);
    m_visual->setData(1, m_insertedIndex);
    m_editor->m_canvas->scene()->addItem(m_visual);
    m_editor->m_canvas->m_drawnItems.append(m_visual);
    m_editor->m_canvas->updateOverlayLabels();
    m_editor->updateCodePreview();
    m_editor->m_canvas->updateGuideAnchors();
}

void AddPrimitiveCommand::undo() {
    if (!m_editor || !m_visual || !m_editor->m_canvas) return;
    m_editor->m_canvas->scene()->removeItem(m_visual);
    int idx = m_editor->m_canvas->m_drawnItems.indexOf(m_visual);
    if (idx != -1) {
        m_editor->m_canvas->m_drawnItems.removeAt(idx);
        if (idx < m_editor->m_symbol.primitives().size())
            m_editor->m_symbol.removePrimitive(idx);
    }
    m_editor->m_canvas->updateOverlayLabels();
    m_editor->updateCodePreview();
    m_editor->m_canvas->updateGuideAnchors();
}

// ─────────────────────────────────────────────────────────────────────────────
//  RemovePrimitiveCommand
// ─────────────────────────────────────────────────────────────────────────────

RemovePrimitiveCommand::RemovePrimitiveCommand(QPointer<SymbolEditor> editor, int index)
    : m_editor(editor), m_index(index) {
    setText("Delete Item");
    if (editor && editor->m_canvas && index >= 0 && index < editor->m_canvas->m_drawnItems.size()) {
        m_visual = editor->m_canvas->m_drawnItems.at(index);
        m_prim   = editor->m_symbol.primitives().at(index);
    }
}

RemovePrimitiveCommand::~RemovePrimitiveCommand() {
    if (m_visual && !m_visual->scene()) {
        delete m_visual;
        m_visual = nullptr;
    }
}

void RemovePrimitiveCommand::redo() {
    if (!m_editor || !m_visual || !m_editor->m_canvas) return;
    m_editor->m_canvas->scene()->removeItem(m_visual);
    int idx = m_editor->m_canvas->m_drawnItems.indexOf(m_visual);
    if (idx != -1) {
        m_editor->m_canvas->m_drawnItems.removeAt(idx);
        m_editor->m_symbol.removePrimitive(idx);
        for (int i = idx; i < m_editor->m_canvas->m_drawnItems.size(); ++i)
            m_editor->m_canvas->m_drawnItems[i]->setData(1, i);
    }
    m_editor->m_canvas->updateOverlayLabels();
    m_editor->updateCodePreview();
}

void RemovePrimitiveCommand::undo() {
    if (!m_editor || !m_visual || !m_editor->m_canvas) return;
    int safeIdx = qBound(0, m_index, m_editor->m_canvas->m_drawnItems.size());
    m_editor->m_symbol.insertPrimitive(safeIdx, m_prim);
    m_editor->m_canvas->m_drawnItems.insert(safeIdx, m_visual);
    m_visual->setData(1, safeIdx);
    m_editor->m_canvas->scene()->addItem(m_visual);
    for (int i = safeIdx + 1; i < m_editor->m_canvas->m_drawnItems.size(); ++i)
        m_editor->m_canvas->m_drawnItems[i]->setData(1, i);
    m_editor->m_canvas->updateOverlayLabels();
    m_editor->updateCodePreview();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UpdateSymbolCommand
// ─────────────────────────────────────────────────────────────────────────────

UpdateSymbolCommand::UpdateSymbolCommand(QPointer<SymbolEditor> editor,
                                        const SymbolDefinition& oldDef,
                                        const SymbolDefinition& newDef,
                                        const QString& label)
    : m_editor(editor), m_oldDef(oldDef), m_newDef(newDef) {
    setText(label);
}

void UpdateSymbolCommand::undo() { if (m_editor) m_editor->applySymbolDefinition(m_oldDef); }
void UpdateSymbolCommand::redo() { if (m_editor) m_editor->applySymbolDefinition(m_newDef); }
