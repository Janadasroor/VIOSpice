/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOOTPRINT_WIZARD_PANEL_H
#define FOOTPRINT_WIZARD_PANEL_H

#include <QWidget>
#include "../models/footprint_definition.h"

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;

class FootprintWizardPanel : public QWidget {
    Q_OBJECT

public:
    explicit FootprintWizardPanel(QWidget* parent = nullptr);
    ~FootprintWizardPanel() override = default;

signals:
    void footprintGenerated(const FootprintDefinition& def);
    void importKicadFootprintRequested();

private slots:
    void onGenerate();

private:
    void setupUI();

    QComboBox* m_wizType;
    QSpinBox* m_wizPins;
    QDoubleSpinBox* m_wizPitch;
    QDoubleSpinBox* m_wizSpan;
    QDoubleSpinBox* m_wizPadW;
    QDoubleSpinBox* m_wizPadH;
};

#endif // FOOTPRINT_WIZARD_PANEL_H
