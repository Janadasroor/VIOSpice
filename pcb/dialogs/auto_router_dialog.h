/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUTO_ROUTER_DIALOG_H
#define AUTO_ROUTER_DIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QTabWidget>
#include <QMap>
#include "pcb_auto_router.h"

class QGraphicsScene;

/**
 * @brief Professional Dialog for configuring and running the PCB multi-layer auto-router.
 */
class AutoRouterDialog : public QDialog {
    Q_OBJECT

public:
    explicit AutoRouterDialog(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~AutoRouterDialog();

private slots:
    void onStartRouting();
    void onStopRouting();
    void onConnectionRouted(const QString& netName, int current, int total);
    void onConnectionFailed(const QString& netName, int current, int total);
    void onProgressChanged(double progress, const QString& status);
    void onRoutingFinished(const PCBAutoRouter::RouteStats& stats);

private:
    void setupUI();
    void setupOptionsTab();
    void setupProgressTab();
    void setupResultsTab();
    PCBAutoRouter::RouterConfig collectConfig();
    void displayResults(const PCBAutoRouter::RouteStats& stats);

    QGraphicsScene* m_scene;
    QTabWidget* m_tabs;

    // Execution & Net Filtering Options
    QCheckBox* m_ripUpCheck;
    QComboBox* m_netFilterCombo;

    // Grid & Geometry Options
    QDoubleSpinBox* m_gridSpacingSpin;
    QDoubleSpinBox* m_clearanceSpin;
    QDoubleSpinBox* m_traceWidthSpin;
    QSpinBox* m_maxIterSpin;
    QSpinBox* m_maxRipUpRoundsSpin;

    // Stackup Layer Controls
    QMap<int, QCheckBox*> m_layerChecks;
    QPushButton* m_selectAllLayersBtn;
    QPushButton* m_topBottomOnlyBtn;

    // Topology & Bias Controls
    QCheckBox* m_directionalBiasCheck;
    QDoubleSpinBox* m_biasPenaltySpin;
    QCheckBox* m_diagonalCheck;
    QCheckBox* m_optimizeLengthCheck;
    QPushButton* m_startBtn;

    // Progress Tab Controls
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_currentNetLabel;
    QTextEdit* m_logEdit;
    QPushButton* m_stopBtn;

    // Results Tab Controls
    QTextEdit* m_resultsEdit;
    QPushButton* m_closeBtn;

    PCBAutoRouter* m_router = nullptr;
    bool m_routingComplete = false;
};

#endif // AUTO_ROUTER_DIALOG_H
