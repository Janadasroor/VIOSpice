// ===== File: pcb/dialogs/gerber_export_dialog.h =====
/*
  * Copyright 2026 Janada Sroor
  * SPDX-License-Identifier: Apache-2.0
  */
#ifndef GERBER_EXPORT_DIALOG_H
#define GERBER_EXPORT_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPageSize>

#include "../layers/pcb_layer.h"

class GerberExportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        Gerber,
        Pdf
    };

    explicit GerberExportDialog(Mode mode = Mode::Gerber, QWidget* parent = nullptr);
    ~GerberExportDialog();

    QList<int> selectedLayers() const;
    QString outputDirectory() const;

    bool generateDrillFile() const;

    bool exportCombinedPdf() const;
    bool pdfPlotOneToOne() const;
    bool pdfBlackAndWhite() const;
    bool pdfOpenAfterExport() const;

    QPageSize::PageSizeId pdfPageSizeId() const;
    int pdfOrientationMode() const; // 0 = Auto, 1 = Portrait, 2 = Landscape
    qreal pdfMarginMm() const;
    bool pdfTitleBlock() const;
    bool pdfMirrorPlot() const;
    int pdfDrillMarksMode() const; // 0 = None, 1 = Small, 2 = Full Size

private slots:
    void onBrowse();
    void onExport();
    void onFilterTextChanged(const QString& text);

private:
    void setupUI();
    void populateLayers();
    void refreshLayerVisibility();
    void setAllLayerCheckState(Qt::CheckState state);

    QListWidget* m_layerList = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QCheckBox* m_drillCheck = nullptr;

    QCheckBox* m_combinedPdfCheck = nullptr;
    QCheckBox* m_pdfBlackAndWhiteCheck = nullptr;
    QCheckBox* m_pdfOpenAfterExportCheck = nullptr;
    QComboBox* m_pdfScaleMode = nullptr;

    QComboBox* m_pdfPageSize = nullptr;
    QComboBox* m_pdfOrientation = nullptr;
    QComboBox* m_pdfMargin = nullptr;
    QCheckBox* m_pdfTitleBlockCheck = nullptr;
    QCheckBox* m_pdfMirrorCheck = nullptr;
    QComboBox* m_pdfDrillMarksCombo = nullptr;

    Mode m_mode = Mode::Gerber;
};

#endif // GERBER_EXPORT_DIALOG_H

