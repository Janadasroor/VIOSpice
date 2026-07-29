/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LAYER_STACKUP_DIALOG_H
#define LAYER_STACKUP_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QWidget>
#include <QPainter>
#include <QVector>
#include <QString>

struct StackupLayer {
    QString name;
    QString type;        // "Silkscreen", "SolderMask", "Copper", "Dielectric"
    QString material;    // "FR-4", "Rogers 4003C", "Polyimide", "Solder Resist", "Epoxy"
    double thicknessMm;  // Thickness in mm (e.g. 0.035mm for 1oz Cu, 1.4mm for core)
    double dielectricEr; // Dielectric constant (e.g. 4.5 for FR-4)
    double lossTangent;  // Loss tangent (e.g. 0.02)
    QColor color;
    bool isCopper;
};

class LayerStackupGraphicWidget : public QWidget {
    Q_OBJECT
public:
    explicit LayerStackupGraphicWidget(QWidget* parent = nullptr);
    void setLayers(const QVector<StackupLayer>& layers);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<StackupLayer> m_layers;
};

class LayerStackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit LayerStackupDialog(QWidget* parent = nullptr);
    ~LayerStackupDialog() override = default;

    QVector<StackupLayer> stackupLayers() const { return m_layers; }
    double totalBoardThicknessMm() const;

private slots:
    void onLayerCountChanged(int count);
    void onPresetSelected(const QString& presetName);
    void onTableDataChanged();
    void calculateImpedance();

private:
    void setupUI();
    void loadDefaultStackup(int layerCount);
    void updateGraphicPreview();

    QComboBox* m_presetCombo;
    QComboBox* m_layerCountCombo;
    QTableWidget* m_table;
    LayerStackupGraphicWidget* m_graphicWidget;
    QLabel* m_totalThicknessLabel;

    // High Speed Impedance Calculator Widgets
    QDoubleSpinBox* m_targetImpedanceSpin;
    QDoubleSpinBox* m_traceWidthSpin;
    QDoubleSpinBox* m_diffGapSpin;
    QLabel* m_calcResultLabel;
    QLabel* m_calcDiffResultLabel;

    QVector<StackupLayer> m_layers;
};

#endif // LAYER_STACKUP_DIALOG_H
