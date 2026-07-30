/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "auto_router_dialog.h"
#include "../layers/pcb_layer.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QGraphicsScene>
#include <QSet>
#include <QDateTime>

AutoRouterDialog::AutoRouterDialog(QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_scene(scene)
{
    setWindowTitle("Multi-Layer PCB Auto-Router");
    resize(640, 560);
    setStyleSheet(
        "QDialog {"
        "  background-color: #1e1e24;"
        "  color: #e0e0e0;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #33333d;"
        "  background: #1e1e24;"
        "  border-radius: 4px;"
        "}"
        "QTabBar::tab {"
        "  background: #252530;"
        "  color: #aaa;"
        "  padding: 8px 16px;"
        "  border: 1px solid #33333d;"
        "  border-top-left-radius: 4px;"
        "  border-top-right-radius: 4px;"
        "  margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #094771;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "}"
        "QGroupBox {"
        "  font-weight: bold;"
        "  border: 1px solid #33333d;"
        "  border-radius: 6px;"
        "  margin-top: 10px;"
        "  padding-top: 12px;"
        "  background-color: #252530;"
        "  color: #4daafc;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  padding: 2px 8px;"
        "  background-color: #1e1e24;"
        "  border-radius: 3px;"
        "}"
        "QLabel, QCheckBox {"
        "  color: #d0d0d0;"
        "}"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
        "  background-color: #16161a;"
        "  color: #ffffff;"
        "  border: 1px solid #444452;"
        "  border-radius: 3px;"
        "  padding: 4px;"
        "}"
        "QPushButton {"
        "  background-color: #094771;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #0c598d;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #33333d;"
        "  color: #666;"
        "}"
        "QProgressBar {"
        "  border: 1px solid #444452;"
        "  border-radius: 4px;"
        "  text-align: center;"
        "  background-color: #16161a;"
        "  color: #fff;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #094771;"
        "  border-radius: 3px;"
        "}"
    );

    setupUI();
}

AutoRouterDialog::~AutoRouterDialog() = default;

void AutoRouterDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    m_tabs = new QTabWidget();
    setupOptionsTab();
    setupProgressTab();
    setupResultsTab();
    mainLayout->addWidget(m_tabs);
}

void AutoRouterDialog::setupOptionsTab() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    // 1. Execution & Rip-Up Card
    QGroupBox* execGroup = new QGroupBox("Execution & Net Scope");
    QFormLayout* execLayout = new QFormLayout(execGroup);

    m_ripUpCheck = new QCheckBox("Rip up all existing traces and vias before routing (--ripup)");
    m_ripUpCheck->setChecked(false);
    m_ripUpCheck->setToolTip("Deletes existing trace segments to re-route the layout cleanly from scratch.");
    execLayout->addRow(m_ripUpCheck);

    m_netFilterCombo = new QComboBox();
    m_netFilterCombo->addItems({"All Unrouted Nets", "Signal Nets Only", "Power & Ground Nets Only"});
    execLayout->addRow("Net Target Scope:", m_netFilterCombo);

    layout->addWidget(execGroup);

    // 2. Grid & Clearance Geometry Card
    QGroupBox* gridGroup = new QGroupBox("Grid Resolution & Clearance");
    QFormLayout* gridLayout = new QFormLayout(gridGroup);

    QHBoxLayout* gridSpacingLayout = new QHBoxLayout();
    m_gridSpacingSpin = new QDoubleSpinBox();
    m_gridSpacingSpin->setRange(0.05, 5.0);
    m_gridSpacingSpin->setSingleStep(0.1);
    m_gridSpacingSpin->setValue(0.5);
    m_gridSpacingSpin->setSuffix(" mm");
    gridSpacingLayout->addWidget(m_gridSpacingSpin);

    // Preset buttons
    auto addPreset = [&](const QString& label, double val) {
        QPushButton* btn = new QPushButton(label, this);
        btn->setStyleSheet("padding: 2px 8px; font-size: 11px; background-color: #2b2b36;");
        connect(btn, &QPushButton::clicked, this, [this, val]() {
            m_gridSpacingSpin->setValue(val);
        });
        gridSpacingLayout->addWidget(btn);
    };
    addPreset("1.0mm", 1.0);
    addPreset("0.5mm", 0.5);
    addPreset("0.25mm", 0.25);
    addPreset("0.1mm", 0.1);

    gridLayout->addRow("Grid Resolution:", gridSpacingLayout);

    m_clearanceSpin = new QDoubleSpinBox();
    m_clearanceSpin->setRange(0.05, 2.0);
    m_clearanceSpin->setSingleStep(0.05);
    m_clearanceSpin->setValue(0.2);
    m_clearanceSpin->setSuffix(" mm");
    gridLayout->addRow("Clearance Spacing:", m_clearanceSpin);

    m_maxIterSpin = new QSpinBox();
    m_maxIterSpin->setRange(1000, 1000000);
    m_maxIterSpin->setSingleStep(10000);
    m_maxIterSpin->setValue(50000);
    gridLayout->addRow("Max A* Iterations:", m_maxIterSpin);

    m_maxRipUpRoundsSpin = new QSpinBox();
    m_maxRipUpRoundsSpin->setRange(0, 20);
    m_maxRipUpRoundsSpin->setValue(3);
    gridLayout->addRow("Max Rip-Up Retries:", m_maxRipUpRoundsSpin);

    layout->addWidget(gridGroup);

    // 3. Stackup Copper Layers Card
    QGroupBox* layerGroup = new QGroupBox("Stackup Routing Layers");
    QVBoxLayout* layerMainLayout = new QVBoxLayout(layerGroup);
    QHBoxLayout* layerLayout = new QHBoxLayout();
    
    m_layerChecks.clear();
    const auto copperLayers = PCBLayerManager::instance().copperLayers();
    for (const PCBLayer* layer : copperLayers) {
        if (!layer) continue;
        QCheckBox* chk = new QCheckBox(layer->name(), this);
        chk->setChecked(true);
        m_layerChecks[layer->id()] = chk;
        layerLayout->addWidget(chk);
    }
    layerLayout->addStretch();
    layerMainLayout->addLayout(layerLayout);

    QHBoxLayout* layerBtnLayout = new QHBoxLayout();
    m_selectAllLayersBtn = new QPushButton("Select All Layers", this);
    m_selectAllLayersBtn->setStyleSheet("padding: 2px 8px; font-size: 11px; background-color: #2b2b36;");
    connect(m_selectAllLayersBtn, &QPushButton::clicked, this, [this]() {
        for (auto* chk : m_layerChecks.values()) chk->setChecked(true);
    });
    layerBtnLayout->addWidget(m_selectAllLayersBtn);

    m_topBottomOnlyBtn = new QPushButton("Top & Bottom Only", this);
    m_topBottomOnlyBtn->setStyleSheet("padding: 2px 8px; font-size: 11px; background-color: #2b2b36;");
    connect(m_topBottomOnlyBtn, &QPushButton::clicked, this, [this]() {
        for (auto it = m_layerChecks.begin(); it != m_layerChecks.end(); ++it) {
            bool isTopOrBottom = (it.key() == PCBLayerManager::TopCopper || it.key() == PCBLayerManager::BottomCopper);
            it.value()->setChecked(isTopOrBottom);
        }
    });
    layerBtnLayout->addWidget(m_topBottomOnlyBtn);
    layerBtnLayout->addStretch();
    layerMainLayout->addLayout(layerBtnLayout);

    layout->addWidget(layerGroup);

    // 4. Advanced Topology Options
    QGroupBox* advGroup = new QGroupBox("Multi-Layer Topology & Directional Bias");
    QVBoxLayout* advLayout = new QVBoxLayout(advGroup);

    QHBoxLayout* biasLayout = new QHBoxLayout();
    m_directionalBiasCheck = new QCheckBox("Orthogonal Manhattan layer direction bias (H/V alternating)", this);
    m_directionalBiasCheck->setChecked(true);
    biasLayout->addWidget(m_directionalBiasCheck);

    m_biasPenaltySpin = new QDoubleSpinBox();
    m_biasPenaltySpin->setRange(1.0, 10.0);
    m_biasPenaltySpin->setSingleStep(0.2);
    m_biasPenaltySpin->setValue(1.6);
    m_biasPenaltySpin->setPrefix("Penalty: ");
    biasLayout->addWidget(m_biasPenaltySpin);
    advLayout->addLayout(biasLayout);

    m_diagonalCheck = new QCheckBox("Allow 45° diagonal moves (faster but less clean)");
    advLayout->addWidget(m_diagonalCheck);

    m_optimizeLengthCheck = new QCheckBox("Optimize for shortest trace length (Euclidean heuristic)");
    m_optimizeLengthCheck->setChecked(true);
    advLayout->addWidget(m_optimizeLengthCheck);

    layout->addWidget(advGroup);

    // Start Routing Button
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_startBtn = new QPushButton("🚀 Start Auto-Routing");
    m_startBtn->setMinimumWidth(200);
    m_startBtn->setFixedHeight(36);
    connect(m_startBtn, &QPushButton::clicked, this, &AutoRouterDialog::onStartRouting);
    btnLayout->addWidget(m_startBtn);
    layout->addLayout(btnLayout);

    m_tabs->addTab(page, "Routing Options");
}

void AutoRouterDialog::setupProgressTab() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    m_statusLabel = new QLabel("Ready to start auto-routing.");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #4daafc;");
    layout->addWidget(m_statusLabel);

    m_currentNetLabel = new QLabel("Current net: -");
    layout->addWidget(m_currentNetLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedHeight(24);
    layout->addWidget(m_progressBar);

    m_logEdit = new QTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet("font-family: monospace; background-color: #121215; color: #e0e0e0;");
    layout->addWidget(m_logEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_stopBtn = new QPushButton("Stop Routing");
    m_stopBtn->setStyleSheet("background-color: #a83232; color: white;");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &AutoRouterDialog::onStopRouting);
    btnLayout->addWidget(m_stopBtn);
    layout->addLayout(btnLayout);

    m_tabs->addTab(page, "Progress & Logs");
}

void AutoRouterDialog::setupResultsTab() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    m_resultsEdit = new QTextEdit();
    m_resultsEdit->setReadOnly(true);
    m_resultsEdit->setStyleSheet("font-family: monospace; background-color: #121215; color: #e0e0e0;");
    layout->addWidget(m_resultsEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_closeBtn = new QPushButton("Close");
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_closeBtn);
    layout->addLayout(btnLayout);

    m_tabs->addTab(page, "Results");
}

PCBAutoRouter::RouterConfig AutoRouterDialog::collectConfig() {
    PCBAutoRouter::RouterConfig config;
    config.gridSpacing = m_gridSpacingSpin->value();
    config.clearance = m_clearanceSpin->value();
    config.maxIterations = m_maxIterSpin->value();
    config.maxRipUpRounds = m_maxRipUpRoundsSpin->value();
    config.allowDiagonals = m_diagonalCheck->isChecked();
    config.optimizeTraceLength = m_optimizeLengthCheck->isChecked();
    config.enableDirectionalBias = m_directionalBiasCheck->isChecked();
    config.directionalBiasPenalty = m_biasPenaltySpin->value();

    config.enabledLayerIds.clear();
    for (auto it = m_layerChecks.cbegin(); it != m_layerChecks.cend(); ++it) {
        if (it.value()->isChecked()) {
            config.enabledLayerIds.insert(it.key());
        }
    }

    return config;
}

void AutoRouterDialog::onStartRouting() {
    if (!m_scene) {
        QMessageBox::warning(this, "Error", "No PCB scene available.");
        return;
    }

    // Handle --ripup option
    if (m_ripUpCheck->isChecked()) {
        QList<QGraphicsItem*> toDelete;
        for (auto* item : m_scene->items()) {
            if (dynamic_cast<TraceItem*>(item) || dynamic_cast<ViaItem*>(item)) {
                toDelete.append(item);
            }
        }
        for (auto* item : toDelete) {
            m_scene->removeItem(item);
            delete item;
        }
        m_logEdit->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] Ripped up all existing traces and vias.");
    }

    m_tabs->setCurrentIndex(1); // Switch to progress tab
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_logEdit->clear();
    m_statusLabel->setText("Routing in progress...");

    m_router = new PCBAutoRouter(m_scene, this);

    connect(m_router, &PCBAutoRouter::connectionRouted, this, &AutoRouterDialog::onConnectionRouted);
    connect(m_router, &PCBAutoRouter::connectionFailed, this, &AutoRouterDialog::onConnectionFailed);
    connect(m_router, &PCBAutoRouter::progressChanged, this, &AutoRouterDialog::onProgressChanged);
    connect(m_router, &PCBAutoRouter::routingFinished, this, &AutoRouterDialog::onRoutingFinished);

    PCBAutoRouter::RouterConfig config = collectConfig();
    m_logEdit->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] Starting multi-layer auto-router...");
    m_logEdit->append(QString("Grid: %1 mm, Clearance: %2 mm, Max Iterations: %3")
                      .arg(config.gridSpacing).arg(config.clearance).arg(config.maxIterations));

    m_router->routeAll(config);
}

void AutoRouterDialog::onStopRouting() {
    m_logEdit->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] Auto-routing cancelled by user.");
    m_statusLabel->setText("Routing cancelled.");
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void AutoRouterDialog::onConnectionRouted(const QString& netName, int current, int total) {
    m_currentNetLabel->setText(QString("Routing net: %1 (%2/%3)").arg(netName).arg(current).arg(total));
    m_logEdit->append(QString("[%1] Routed net: %2 (%3/%4)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(netName).arg(current).arg(total));
}

void AutoRouterDialog::onConnectionFailed(const QString& netName, int current, int total) {
    m_logEdit->append(QString("[%1] ❌ Failed to route net: %2 (%3/%4)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(netName).arg(current).arg(total));
}

void AutoRouterDialog::onProgressChanged(double progress, const QString& status) {
    m_progressBar->setValue(qRound(progress * 100.0));
    m_statusLabel->setText(status);
}

void AutoRouterDialog::onRoutingFinished(const PCBAutoRouter::RouteStats& stats) {
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_progressBar->setValue(100);
    m_statusLabel->setText("Auto-routing complete!");

    m_logEdit->append(QString("[%1] Auto-routing completed. %2/%3 connections routed.")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(stats.routedConnections).arg(stats.totalConnections));

    displayResults(stats);
    m_tabs->setCurrentIndex(2); // Switch to results tab
}

void AutoRouterDialog::displayResults(const PCBAutoRouter::RouteStats& stats) {
    QString html = "<h2>Auto-Router Results</h2>";
    html += "<table border='1' cellspacing='0' cellpadding='6' style='border-color: #33333d;'>";
    html += QString("<tr><td><b>Total Connections</b></td><td>%1</td></tr>").arg(stats.totalConnections);
    html += QString("<tr><td><b>Successfully Routed</b></td><td style='color: #4daafc;'><b>%1</b></td></tr>").arg(stats.routedConnections);
    html += QString("<tr><td><b>Failed Connections</b></td><td style='color: %1;'><b>%2</b></td></tr>")
            .arg(stats.failedConnections > 0 ? "#ff5555" : "#55ff55")
            .arg(stats.failedConnections);
    html += QString("<tr><td><b>Total Trace Length</b></td><td>%1 mm</td></tr>").arg(stats.totalTraceLength, 0, 'f', 2);
    html += QString("<tr><td><b>A* Iterations</b></td><td>%1</td></tr>").arg(stats.iterations);
    html += "</table>";

    if (stats.failedConnections == 0) {
        html += "<p style='color: #55ff55;'><b>🎉 All net connections were routed successfully with 0 DRC conflicts!</b></p>";
    } else {
        html += "<p style='color: #ffaa00;'><b>⚠️ Some dense connections could not be routed. Try reducing grid resolution or enabling rip-up.</b></p>";
    }

    m_resultsEdit->setHtml(html);
}
