/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYMBOL_WIZARD_PANEL_H
#define SYMBOL_WIZARD_PANEL_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include "../models/symbol_definition.h"

using Flux::Model::SymbolDefinition;
class SymbolCanvas;

/**
 * @brief Panel containing Wizard and Template options for schematic symbols.
 */
class SymbolWizardPanel : public QWidget {
    Q_OBJECT

public:
    explicit SymbolWizardPanel(SymbolCanvas* canvas, QWidget* parent = nullptr);
    ~SymbolWizardPanel() override = default;

    void setProjectKey(const QString& key);

public Q_SLOTS:
    void saveTemplate(const SymbolDefinition& def);

Q_SIGNALS:
    void symbolGenerated(const SymbolDefinition& def);
    void templateSaved(const QString& name, const SymbolDefinition& def);
    void templateApplied(const QString& defaultName, const QString& defaultPrefix, const QString& defaultCategory);
    void saveCurrentAsTemplateRequested();
    void importSpiceSubcircuitRequested();
    void importKicadSymbolRequested();
    void importLtspiceSymbolRequested();

private Q_SLOTS:
    void onWizardGenerate();
    void onWizardSaveTemplate();
    void onWizardTemplateSearchChanged(const QString& text);
    void onWizardApplyTemplate();

private:
    void setupUI();
    void refreshWizardTemplateList(const QString& query = QString());
    void updateWizardTemplatePreview();

    SymbolCanvas* m_canvas = nullptr;
    QString m_projectKey;

    // UI elements
    QLineEdit* m_wizardTemplateSearchEdit = nullptr;
    QComboBox* m_wizardTemplateCombo = nullptr;
    QLabel* m_wizardTemplateInfoLabel = nullptr;
    QLabel* m_wizardTemplateDescLabel = nullptr;
    QGraphicsView* m_wizardPreviewView = nullptr;
    QGraphicsScene* m_wizardPreviewScene = nullptr;

    QComboBox* m_wizardStyleCombo = nullptr;
    QSpinBox* m_pinCountSpin = nullptr;
    QDoubleSpinBox* m_pinSpacingSpin = nullptr;
    QDoubleSpinBox* m_bodyWidthSpin = nullptr;
};

#endif // SYMBOL_WIZARD_PANEL_H
