/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SIMULATION_DESIGN_EXPLORER_PANEL_H
#define SIMULATION_DESIGN_EXPLORER_PANEL_H

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include "../../simulator/core/sim_results.h"

class QChart;

class SimulationDesignExplorerPanel : public QWidget {
    Q_OBJECT

public:
    explicit SimulationDesignExplorerPanel(QWidget* parent = nullptr);
    ~SimulationDesignExplorerPanel() = default;

    void setSpectrumChart(QChart* chart);
    void updateResults(const SimResults& results);
    void clearResults();

    QComboBox* steppedMeasSeriesCombo() const { return m_steppedMeasSeriesCombo; }
    QComboBox* steppedMeasAxisCombo() const { return m_steppedMeasAxisCombo; }

private Q_SLOTS:
    void onTableSelectionChanged();
    void onCopyButtonClicked();
    void onHeaderClicked(int index);
    void onSteppedComboChanged();

private:
    void setupUI();
    void refreshDesignExplorer(const SimResults& results);
    void refreshDesignExplorerSelection(const SimResults& results);
    void refreshSteppedMeasurementControls(const SimResults& results);
    void rebuildSteppedMeasurementPlot(const SimResults& results);

    QLabel* m_designExplorerSummaryLabel = nullptr;
    QTableWidget* m_designExplorerTable = nullptr;
    QLabel* m_designExplorerDetailLabel = nullptr;
    QPushButton* m_designExplorerCopyButton = nullptr;

    QComboBox* m_steppedMeasSeriesCombo = nullptr;
    QComboBox* m_steppedMeasAxisCombo = nullptr;

    QChart* m_spectrumChart = nullptr;
    SimResults m_lastResults;
    QString m_selectedSteppedMeasurement;
    QString m_selectedSteppedAxis;
};

#endif // SIMULATION_DESIGN_EXPLORER_PANEL_H
