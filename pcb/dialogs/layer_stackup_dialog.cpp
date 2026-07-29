/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "layer_stackup_dialog.h"
#include <QHeaderView>
#include <QFormLayout>
#include <QTabWidget>
#include <QDebug>
#include <cmath>
#include <numeric>

// ============================================================================
// LayerStackupGraphicWidget Implementation
// ============================================================================

LayerStackupGraphicWidget::LayerStackupGraphicWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(240);
    setStyleSheet("background-color: #121214; border: 1px solid #27272a; border-radius: 6px;");
}

void LayerStackupGraphicWidget::setLayers(const QVector<StackupLayer>& layers) {
    m_layers = layers;
    update();
}

void LayerStackupGraphicWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_layers.isEmpty()) {
        p.setPen(QColor("#71717a"));
        p.drawText(rect(), Qt::AlignCenter, "No layers defined");
        return;
    }

    double totalThickness = 0.0;
    for (const auto& l : m_layers) {
        totalThickness += std::max(0.01, l.thicknessMm);
    }

    const int marginX = 20;
    const int marginY = 25;
    const int drawWidth = width() - (2 * marginX) - 160;
    const int drawHeight = height() - (2 * marginY);

    if (drawWidth <= 10 || drawHeight <= 10) return;

    int currentY = marginY;

    for (int i = 0; i < m_layers.size(); ++i) {
        const auto& layer = m_layers[i];
        double fraction = std::max(0.01, layer.thicknessMm) / totalThickness;
        int layerH = std::max(12, static_cast<int>(fraction * drawHeight));

        QRect layerRect(marginX, currentY, drawWidth, layerH);

        // Fill background
        p.setPen(QPen(layer.color.darker(140), 1.0));
        p.setBrush(QBrush(layer.color));
        p.drawRect(layerRect);

        // Pattern overlay for dielectric core
        if (layer.type == "Dielectric") {
            p.setPen(QPen(QColor(255, 255, 255, 30), 1.0, Qt::DotLine));
            for (int lx = layerRect.left() + 10; lx < layerRect.right(); lx += 20) {
                p.drawLine(lx, layerRect.top(), lx + 10, layerRect.bottom());
            }
        }

        // Layer Name Label on Graphic
        p.setPen(QPen(layer.isCopper ? Qt::black : Qt::white));
        QFont font = p.font();
        font.setPointSize(9);
        font.setBold(true);
        p.setFont(font);
        p.drawText(layerRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, layer.name);

        // Thickness & Info Callout on Right Side
        int calloutX = marginX + drawWidth + 15;
        int calloutY = currentY + (layerH / 2);

        p.setPen(QPen(QColor("#a1a1aa"), 1.0, Qt::DashLine));
        p.drawLine(marginX + drawWidth, calloutY, calloutX - 5, calloutY);

        p.setPen(QPen(QColor("#e4e4e7")));
        font.setBold(false);
        font.setPointSize(8);
        p.setFont(font);

        QString infoStr = QString("%1 (%2 mm)")
            .arg(layer.type)
            .arg(layer.thicknessMm, 0, 'f', 3);
        if (layer.type == "Dielectric") {
            infoStr += QString(" εr=%1").arg(layer.dielectricEr, 0, 'f', 1);
        } else if (layer.isCopper) {
            infoStr += QString(" [%1]").arg(layer.material);
        }

        p.drawText(calloutX, calloutY - 6, 140, 16, Qt::AlignVCenter | Qt::AlignLeft, infoStr);

        currentY += layerH + 2;
    }
}

// ============================================================================
// LayerStackupDialog Implementation
// ============================================================================

LayerStackupDialog::LayerStackupDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("PCB Board Layer Stackup & High-Speed Impedance Calculator");
    resize(960, 680);
    setStyleSheet("QDialog { background-color: #18181b; color: #f4f4f5; }");
    setupUI();
    loadDefaultStackup(4);
}

void LayerStackupDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // ── Header Preset Bar ──────────────────────────────────────────────────
    QHBoxLayout* presetLayout = new QHBoxLayout();
    presetLayout->addWidget(new QLabel("Board Preset:"));

    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItems({
        "Standard 4-Layer FR-4 (1.6mm JLC/PCBWay)",
        "Standard 2-Layer FR-4 (1.6mm)",
        "High-Speed 6-Layer Stackup (1.6mm)",
        "High-Speed 8-Layer Stackup (1.6mm)",
        "Flexible 2-Layer Polyimide (0.2mm)"
    });
    m_presetCombo->setStyleSheet("QComboBox { background: #27272a; color: white; padding: 4px 10px; border: 1px solid #3f3f46; border-radius: 4px; }");
    connect(m_presetCombo, &QComboBox::currentTextChanged, this, &LayerStackupDialog::onPresetSelected);
    presetLayout->addWidget(m_presetCombo, 1);

    presetLayout->addWidget(new QLabel("Copper Layers:"));
    m_layerCountCombo = new QComboBox(this);
    m_layerCountCombo->addItems({"2 Layers", "4 Layers", "6 Layers", "8 Layers", "10 Layers", "16 Layers", "32 Layers"});
    m_layerCountCombo->setCurrentIndex(1); // 4 layers
    m_layerCountCombo->setStyleSheet("QComboBox { background: #27272a; color: white; padding: 4px 10px; border: 1px solid #3f3f46; border-radius: 4px; }");
    connect(m_layerCountCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        int counts[] = {2, 4, 6, 8, 10, 16, 32};
        onLayerCountChanged(counts[idx]);
    });
    presetLayout->addWidget(m_layerCountCombo);

    mainLayout->addLayout(presetLayout);

    // ── Main Content Split (Tabs: Table & Graphic & Calculator) ────────────
    QTabWidget* tabs = new QTabWidget(this);
    tabs->setStyleSheet(R"(
        QTabWidget::pane { border: 1px solid #27272a; background: #18181b; }
        QTabBar::tab { background: #27272a; color: #a1a1aa; padding: 8px 16px; border-top-left-radius: 4px; border-top-right-radius: 4px; }
        QTabBar::tab:selected { background: #3b82f6; color: white; font-weight: bold; }
    )");

    // Tab 1: Layer Table & Graphic Diagram
    QWidget* tab1 = new QWidget(tabs);
    QHBoxLayout* tab1Layout = new QHBoxLayout(tab1);
    tab1Layout->setContentsMargins(8, 8, 8, 8);
    tab1Layout->setSpacing(12);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({"Layer Name", "Type", "Material", "Thickness (mm)", "Copper Wt", "εr (Er)", "Loss Tan"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setStyleSheet(R"(
        QTableWidget { background-color: #121214; gridline-color: #27272a; color: #f4f4f5; border: 1px solid #27272a; }
        QHeaderView::section { background-color: #27272a; color: #d4d4d8; font-weight: bold; padding: 6px; border: none; }
    )");
    connect(m_table, &QTableWidget::itemChanged, this, &LayerStackupDialog::onTableDataChanged);

    tab1Layout->addWidget(m_table, 3);

    m_graphicWidget = new LayerStackupGraphicWidget(this);
    tab1Layout->addWidget(m_graphicWidget, 2);

    tabs->addTab(tab1, "📚 Stackup Layers & Geometry");

    // Tab 2: High-Speed Controlled Impedance Calculator
    QWidget* tab2 = new QWidget(tabs);
    QVBoxLayout* tab2Layout = new QVBoxLayout(tab2);
    tab2Layout->setContentsMargins(16, 16, 16, 16);

    QGroupBox* impBox = new QGroupBox("High-Speed Microstrip & Differential Pair Impedance Solver", tab2);
    impBox->setStyleSheet("QGroupBox { font-weight: bold; color: #3b82f6; border: 1px solid #27272a; border-radius: 6px; margin-top: 10px; padding-top: 15px; }");

    QFormLayout* form = new QFormLayout(impBox);
    form->setSpacing(12);

    m_targetImpedanceSpin = new QDoubleSpinBox(this);
    m_targetImpedanceSpin->setRange(20.0, 200.0);
    m_targetImpedanceSpin->setValue(50.0);
    m_targetImpedanceSpin->setSuffix(" Ω");
    m_targetImpedanceSpin->setStyleSheet("QDoubleSpinBox { background: #27272a; color: white; padding: 4px; }");
    connect(m_targetImpedanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LayerStackupDialog::calculateImpedance);

    m_traceWidthSpin = new QDoubleSpinBox(this);
    m_traceWidthSpin->setRange(0.05, 5.0);
    m_traceWidthSpin->setSingleStep(0.05);
    m_traceWidthSpin->setValue(0.25);
    m_traceWidthSpin->setSuffix(" mm");
    m_traceWidthSpin->setStyleSheet("QDoubleSpinBox { background: #27272a; color: white; padding: 4px; }");
    connect(m_traceWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LayerStackupDialog::calculateImpedance);

    m_diffGapSpin = new QDoubleSpinBox(this);
    m_diffGapSpin->setRange(0.05, 5.0);
    m_diffGapSpin->setSingleStep(0.05);
    m_diffGapSpin->setValue(0.18);
    m_diffGapSpin->setSuffix(" mm");
    m_diffGapSpin->setStyleSheet("QDoubleSpinBox { background: #27272a; color: white; padding: 4px; }");
    connect(m_diffGapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LayerStackupDialog::calculateImpedance);

    m_calcResultLabel = new QLabel("Single-Ended Impedance (Z₀): 50.2 Ω", this);
    m_calcResultLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #10b981; padding: 6px;");

    m_calcDiffResultLabel = new QLabel("Differential Pair Impedance (Zdiff): 98.6 Ω", this);
    m_calcDiffResultLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #3b82f6; padding: 6px;");

    form->addRow("Target Impedance:", m_targetImpedanceSpin);
    form->addRow("Trace Width (w):", m_traceWidthSpin);
    form->addRow("Diff Pair Gap (s):", m_diffGapSpin);
    form->addRow("Single-Ended Result:", m_calcResultLabel);
    form->addRow("Differential Result:", m_calcDiffResultLabel);

    tab2Layout->addWidget(impBox);
    tab2Layout->addStretch();

    tabs->addTab(tab2, "⚡ High-Speed Impedance Solver");

    mainLayout->addWidget(tabs);

    // ── Bottom Summary & Buttons ───────────────────────────────────────────
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    m_totalThicknessLabel = new QLabel("Total Board Thickness: 1.60 mm", this);
    m_totalThicknessLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #3b82f6;");
    bottomLayout->addWidget(m_totalThicknessLabel);
    bottomLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setStyleSheet("QPushButton { background: #27272a; color: white; padding: 6px 16px; border-radius: 4px; } QPushButton:hover { background: #3f3f46; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton* okBtn = new QPushButton("Apply Stackup", this);
    okBtn->setStyleSheet("QPushButton { background: #3b82f6; color: white; font-weight: bold; padding: 6px 20px; border-radius: 4px; } QPushButton:hover { background: #2563eb; }");
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomLayout->addWidget(cancelBtn);
    bottomLayout->addWidget(okBtn);

    mainLayout->addLayout(bottomLayout);
}

void LayerStackupDialog::loadDefaultStackup(int layerCount) {
    m_layers.clear();

    // Top Silkscreen & Mask
    m_layers.append({"Top Silkscreen", "Silkscreen", "White Ink", 0.010, 3.3, 0.02, QColor("#f4f4f5"), false});
    m_layers.append({"Top Solder Mask", "SolderMask", "Taiyo PSR-4000", 0.015, 3.8, 0.02, QColor("#15803d"), false});

    // Top Copper
    m_layers.append({"F.Cu (Top)", "Copper", "1 oz Cu", 0.035, 1.0, 0.0, QColor("#ef4444"), true});

    if (layerCount == 2) {
        m_layers.append({"Dielectric Core", "Dielectric", "FR-4 Standard", 1.500, 4.5, 0.02, QColor("#854d0e"), false});
    } else if (layerCount == 4) {
        m_layers.append({"Prepreg (1080)", "Dielectric", "FR-4 Prepreg", 0.200, 4.3, 0.02, QColor("#a16207"), false});
        m_layers.append({"In1.Cu (GND)", "Copper", "1 oz Cu", 0.035, 1.0, 0.0, QColor("#3b82f6"), true});
        m_layers.append({"Dielectric Core", "Dielectric", "FR-4 Core", 1.000, 4.5, 0.02, QColor("#854d0e"), false});
        m_layers.append({"In2.Cu (PWR)", "Copper", "1 oz Cu", 0.035, 1.0, 0.0, QColor("#eab308"), true});
        m_layers.append({"Prepreg (1080)", "Dielectric", "FR-4 Prepreg", 0.200, 4.3, 0.02, QColor("#a16207"), false});
    } else {
        // 6+ Layers auto-generate
        double innerCoreThickness = 1.2 / (layerCount - 1);
        for (int i = 1; i < layerCount; ++i) {
            m_layers.append({QString("Prepreg %1").arg(i), "Dielectric", "FR-4 Prepreg", innerCoreThickness, 4.3, 0.02, QColor("#a16207"), false});
            m_layers.append({QString("In%1.Cu").arg(i), "Copper", "1 oz Cu", 0.035, 1.0, 0.0, QColor("#a855f7"), true});
        }
        m_layers.append({"Prepreg Final", "Dielectric", "FR-4 Prepreg", 0.150, 4.3, 0.02, QColor("#a16207"), false});
    }

    // Bottom Copper, Mask & Silkscreen
    m_layers.append({"B.Cu (Bottom)", "Copper", "1 oz Cu", 0.035, 1.0, 0.0, QColor("#3b82f6"), true});
    m_layers.append({"Bottom Solder Mask", "SolderMask", "Taiyo PSR-4000", 0.015, 3.8, 0.02, QColor("#15803d"), false});
    m_layers.append({"Bottom Silkscreen", "Silkscreen", "White Ink", 0.010, 3.3, 0.02, QColor("#f4f4f5"), false});

    // Populate table
    m_table->blockSignals(true);
    m_table->setRowCount(m_layers.size());
    for (int r = 0; r < m_layers.size(); ++r) {
        const auto& layer = m_layers[r];
        m_table->setItem(r, 0, new QTableWidgetItem(layer.name));
        m_table->setItem(r, 1, new QTableWidgetItem(layer.type));
        m_table->setItem(r, 2, new QTableWidgetItem(layer.material));
        m_table->setItem(r, 3, new QTableWidgetItem(QString::number(layer.thicknessMm, 'f', 3)));
        m_table->setItem(r, 4, new QTableWidgetItem(layer.isCopper ? "1 oz" : "N/A"));
        m_table->setItem(r, 5, new QTableWidgetItem(QString::number(layer.dielectricEr, 'f', 1)));
        m_table->setItem(r, 6, new QTableWidgetItem(QString::number(layer.lossTangent, 'f', 3)));
    }
    m_table->blockSignals(false);

    updateGraphicPreview();
    calculateImpedance();
}

void LayerStackupDialog::onLayerCountChanged(int count) {
    loadDefaultStackup(count);
}

void LayerStackupDialog::onPresetSelected(const QString& presetName) {
    if (presetName.contains("2-Layer")) {
        m_layerCountCombo->setCurrentIndex(0);
    } else if (presetName.contains("4-Layer")) {
        m_layerCountCombo->setCurrentIndex(1);
    } else if (presetName.contains("6-Layer")) {
        m_layerCountCombo->setCurrentIndex(2);
    } else if (presetName.contains("8-Layer")) {
        m_layerCountCombo->setCurrentIndex(3);
    }
}

void LayerStackupDialog::onTableDataChanged() {
    for (int r = 0; r < m_table->rowCount() && r < m_layers.size(); ++r) {
        if (auto* item = m_table->item(r, 3)) {
            m_layers[r].thicknessMm = item->text().toDouble();
        }
        if (auto* item = m_table->item(r, 5)) {
            m_layers[r].dielectricEr = item->text().toDouble();
        }
    }
    updateGraphicPreview();
    calculateImpedance();
}

double LayerStackupDialog::totalBoardThicknessMm() const {
    double total = 0.0;
    for (const auto& l : m_layers) {
        total += l.thicknessMm;
    }
    return total;
}

void LayerStackupDialog::updateGraphicPreview() {
    if (m_graphicWidget) {
        m_graphicWidget->setLayers(m_layers);
    }
    if (m_totalThicknessLabel) {
        m_totalThicknessLabel->setText(QString("Total Board Thickness: %1 mm")
            .arg(totalBoardThicknessMm(), 0, 'f', 2));
    }
}

void LayerStackupDialog::calculateImpedance() {
    double w = m_traceWidthSpin ? m_traceWidthSpin->value() : 0.25;
    double s = m_diffGapSpin ? m_diffGapSpin->value() : 0.18;

    // Find first dielectric layer thickness (h) and Er
    double h = 0.200; // default prepreg thickness in mm
    double er = 4.3;  // default FR-4 Er

    for (const auto& l : m_layers) {
        if (l.type == "Dielectric") {
            h = l.thicknessMm;
            er = l.dielectricEr;
            break;
        }
    }

    double t = 0.035; // 1oz copper thickness

    // Microstrip Impedance Formula: Z0 = (87 / sqrt(Er + 1.41)) * ln(5.98*h / (0.8*w + t))
    double z0 = (87.0 / std::sqrt(er + 1.41)) * std::log((5.98 * h) / (0.8 * w + t));
    if (z0 < 10.0) z0 = 10.0;
    if (z0 > 300.0) z0 = 300.0;

    // Differential Pair Impedance Formula: Zdiff = 2 * Z0 * (1 - 0.48 * exp(-0.96 * s / h))
    double zdiff = 2.0 * z0 * (1.0 - 0.48 * std::exp(-0.96 * (s / h)));

    if (m_calcResultLabel) {
        m_calcResultLabel->setText(QString("Single-Ended Impedance (Z₀): %1 Ω (h=%2mm, w=%3mm)")
            .arg(z0, 0, 'f', 1)
            .arg(h, 0, 'f', 2)
            .arg(w, 0, 'f', 2));
    }

    if (m_calcDiffResultLabel) {
        m_calcDiffResultLabel->setText(QString("Differential Pair Impedance (Zdiff): %1 Ω (s=%2mm)")
            .arg(zdiff, 0, 'f', 1)
            .arg(s, 0, 'f', 2));
    }
}
