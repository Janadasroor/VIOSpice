/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_PROPERTIES_PANEL_H
#define SYMBOL_PROPERTIES_PANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

#include "../models/symbol_definition.h"
#include "../../ui/property_editor.h"
#include "../../python/cpp/gemini/gemini_panel.h"
#include "../ui/symbol_preview_widget.h"

using Flux::Model::SymbolDefinition;

class SymbolEditor;

class SymbolPropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit SymbolPropertiesPanel(SymbolEditor* editor, QWidget* parent = nullptr);
    ~SymbolPropertiesPanel() = default;

    // Interface methods
    void loadFromDefinition(const SymbolDefinition& def);
    void saveToDefinition(SymbolDefinition& def) const;
    void blockAllSignals(bool block);
    void applyTheme();
    void updateModelPathState();

    // Accessors
    QTabWidget* tabWidget() const { return m_propsTabWidget; }
    PropertyEditor* propertyEditor() const { return m_propertyEditor; }
    GeminiPanel* aiPanel() const { return m_aiPanel; }
    SymbolPreviewWidget* livePreview() const { return m_livePreview; }

    QLineEdit* nameEdit() const { return m_nameEdit; }
    QLineEdit* descriptionEdit() const { return m_descriptionEdit; }
    QComboBox* categoryCombo() const { return m_categoryCombo; }
    QLineEdit* prefixEdit() const { return m_prefixEdit; }
    QLineEdit* footprintEdit() const { return m_footprintEdit; }

    QComboBox* modelSourceCombo() const { return m_modelSourceCombo; }
    QLineEdit* modelPathEdit() const { return m_modelPathEdit; }
    QLineEdit* modelNameEdit() const { return m_modelNameEdit; }
    QPlainTextEdit* spiceSubcircuitEdit() const { return m_spiceSubcircuitEdit; }
    QWidget* embeddedCodeWidget() const { return m_embeddedCodeWidget; }

private:
    void setupUI();

    SymbolEditor* m_editor;

    // UI elements
    QTabWidget* m_propsTabWidget = nullptr;
    PropertyEditor* m_propertyEditor = nullptr;
    GeminiPanel* m_aiPanel = nullptr;
    SymbolPreviewWidget* m_livePreview = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_descriptionEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QLineEdit* m_prefixEdit = nullptr;
    QLineEdit* m_footprintEdit = nullptr;

    QComboBox* m_modelSourceCombo = nullptr;
    QLineEdit* m_modelPathEdit = nullptr;
    QLineEdit* m_modelNameEdit = nullptr;
    QPlainTextEdit* m_spiceSubcircuitEdit = nullptr;
    QWidget* m_embeddedCodeWidget = nullptr;

    QWidget* m_modelFileRow = nullptr;
    QWidget* m_modelNameRow = nullptr;
};

#endif // SYMBOL_PROPERTIES_PANEL_H
