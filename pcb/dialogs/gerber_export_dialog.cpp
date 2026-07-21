// ===== File: pcb/dialogs/gerber_export_dialog.cpp =====
/*
  * Copyright 2026 Janada Sroor
  * SPDX-License-Identifier: Apache-2.0
  */
#include "gerber_export_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QPageSize>

#include "../gerber/gerber_exporter.h"

GerberExportDialog::GerberExportDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    setupUI();
    populateLayers();
}

GerberExportDialog::~GerberExportDialog() = default;

void GerberExportDialog::setupUI()
{
    setWindowTitle(m_mode == Mode::Pdf
                       ? QStringLiteral("Export PDF Plots")
                       : QStringLiteral("Generate Gerber Files"));
    resize(520, 680);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // 1. Output Directory
    QGroupBox* dirGroup = new QGroupBox(QStringLiteral("Output Settings"), this);
    QHBoxLayout* dirLayout = new QHBoxLayout(dirGroup);

    m_dirEdit = new QLineEdit(dirGroup);

    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) {
        docs = QDir::homePath();
    }

    m_dirEdit->setText(docs +
                       (m_mode == Mode::Pdf
                            ? QStringLiteral("/PDF_Plots")
                            : QStringLiteral("/Gerbers")));

    QPushButton* browseBtn = new QPushButton(QStringLiteral("Browse..."), dirGroup);
    connect(browseBtn, &QPushButton::clicked, this, &GerberExportDialog::onBrowse);

    dirLayout->addWidget(m_dirEdit);
    dirLayout->addWidget(browseBtn);

    mainLayout->addWidget(dirGroup);

    // 2. Layer Selection
    QGroupBox* layerGroup = new QGroupBox(QStringLiteral("Select Layers to Plot"), this);
    QVBoxLayout* layerLayout = new QVBoxLayout(layerGroup);

    m_filterEdit = new QLineEdit(layerGroup);
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter layers..."));
    connect(m_filterEdit, &QLineEdit::textChanged, this, &GerberExportDialog::onFilterTextChanged);

    QHBoxLayout* selectLayout = new QHBoxLayout();
    QPushButton* selectAllBtn = new QPushButton(QStringLiteral("Select All"), layerGroup);
    QPushButton* selectNoneBtn = new QPushButton(QStringLiteral("Select None"), layerGroup);

    selectAllBtn->setMaximumWidth(110);
    selectNoneBtn->setMaximumWidth(110);

    connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
        setAllLayerCheckState(Qt::Checked);
    });

    connect(selectNoneBtn, &QPushButton::clicked, this, [this]() {
        setAllLayerCheckState(Qt::Unchecked);
    });

    selectLayout->addWidget(selectAllBtn);
    selectLayout->addWidget(selectNoneBtn);
    selectLayout->addStretch();

    m_layerList = new QListWidget(layerGroup);
    m_layerList->setSelectionMode(QAbstractItemView::NoSelection);

    layerLayout->addWidget(m_filterEdit);
    layerLayout->addLayout(selectLayout);
    layerLayout->addWidget(m_layerList);

    mainLayout->addWidget(layerGroup);

    // 3. Drill Options
    m_drillCheck = new QCheckBox(QStringLiteral("Generate Excellon Drill File (.drl)"), this);
    m_drillCheck->setChecked(true);

    if (m_mode == Mode::Gerber) {
        mainLayout->addWidget(m_drillCheck);
    }

    if (m_mode == Mode::Pdf) {
        QGroupBox* pdfGroup = new QGroupBox(QStringLiteral("PDF Plot Options"), this);
        QVBoxLayout* pdfLayout = new QVBoxLayout(pdfGroup);

        QHBoxLayout* scaleLayout = new QHBoxLayout();
        QLabel* scaleLabel = new QLabel(QStringLiteral("Scale:"), pdfGroup);
        m_pdfScaleMode = new QComboBox(pdfGroup);
        m_pdfScaleMode->addItem(QStringLiteral("Fit To Page"), QStringLiteral("fit"));
        m_pdfScaleMode->addItem(QStringLiteral("1:1"), QStringLiteral("1:1"));
        scaleLayout->addWidget(scaleLabel);
        scaleLayout->addWidget(m_pdfScaleMode, 1);
        pdfLayout->addLayout(scaleLayout);

        QHBoxLayout* pageLayout = new QHBoxLayout();
        QLabel* pageSizeLabel = new QLabel(QStringLiteral("Page size:"), pdfGroup);
        m_pdfPageSize = new QComboBox(pdfGroup);
        m_pdfPageSize->addItem(QStringLiteral("A4"), int(QPageSize::A4));
        m_pdfPageSize->addItem(QStringLiteral("A3"), int(QPageSize::A3));
        m_pdfPageSize->addItem(QStringLiteral("A2"), int(QPageSize::A2));
        m_pdfPageSize->addItem(QStringLiteral("Letter"), int(QPageSize::Letter));
        pageLayout->addWidget(pageSizeLabel);
        pageLayout->addWidget(m_pdfPageSize, 1);
        pdfLayout->addLayout(pageLayout);

        QHBoxLayout* orientLayout = new QHBoxLayout();
        QLabel* orientLabel = new QLabel(QStringLiteral("Orientation:"), pdfGroup);
        m_pdfOrientation = new QComboBox(pdfGroup);
        m_pdfOrientation->addItem(QStringLiteral("Auto"), 0);
        m_pdfOrientation->addItem(QStringLiteral("Portrait"), 1);
        m_pdfOrientation->addItem(QStringLiteral("Landscape"), 2);
        m_pdfOrientation->setCurrentIndex(0);
        orientLayout->addWidget(orientLabel);
        orientLayout->addWidget(m_pdfOrientation, 1);
        pdfLayout->addLayout(orientLayout);

        QHBoxLayout* marginLayout = new QHBoxLayout();
        QLabel* marginLabel = new QLabel(QStringLiteral("Margin:"), pdfGroup);
        m_pdfMargin = new QComboBox(pdfGroup);
        m_pdfMargin->addItem(QStringLiteral("0 mm"), 0.0);
        m_pdfMargin->addItem(QStringLiteral("5 mm"), 5.0);
        m_pdfMargin->addItem(QStringLiteral("10 mm"), 10.0);
        m_pdfMargin->addItem(QStringLiteral("15 mm"), 15.0);
        m_pdfMargin->addItem(QStringLiteral("20 mm"), 20.0);
        m_pdfMargin->setCurrentIndex(1);
        marginLayout->addWidget(marginLabel);
        marginLayout->addWidget(m_pdfMargin, 1);
        pdfLayout->addLayout(marginLayout);

        m_pdfBlackAndWhiteCheck = new QCheckBox(QStringLiteral("Black and white"), pdfGroup);
        m_pdfBlackAndWhiteCheck->setChecked(false);
        pdfLayout->addWidget(m_pdfBlackAndWhiteCheck);

        m_pdfTitleBlockCheck = new QCheckBox(QStringLiteral("Add title block"), pdfGroup);
        m_pdfTitleBlockCheck->setChecked(true);
        pdfLayout->addWidget(m_pdfTitleBlockCheck);

        mainLayout->addWidget(pdfGroup);

        m_combinedPdfCheck = new QCheckBox(QStringLiteral("Also export one combined board PDF"), this);
        m_combinedPdfCheck->setChecked(true);
        mainLayout->addWidget(m_combinedPdfCheck);

        m_pdfOpenAfterExportCheck = new QCheckBox(QStringLiteral("Open exported PDF in viewer"), this);
        m_pdfOpenAfterExportCheck->setChecked(true);
        mainLayout->addWidget(m_pdfOpenAfterExportCheck);
    }

    // 4. Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* exportBtn = new QPushButton(
        m_mode == Mode::Pdf ? QStringLiteral("Export PDFs") : QStringLiteral("Generate Files"),
        this);
    QPushButton* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);

    exportBtn->setStyleSheet(
        QStringLiteral("background-color: #007acc; color: white; font-weight: bold; padding: 8px;"));

    connect(exportBtn, &QPushButton::clicked, this, &GerberExportDialog::onExport);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(exportBtn);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);
}

void GerberExportDialog::populateLayers()
{
    for (const auto& layer : PCBLayerManager::instance().layers()) {
        if (layer.type() == PCBLayer::Copper ||
            layer.type() == PCBLayer::Silkscreen ||
            layer.type() == PCBLayer::Soldermask ||
            layer.type() == PCBLayer::Paste ||
            layer.type() == PCBLayer::Courtyard ||
            layer.type() == PCBLayer::Fabrication ||
            layer.type() == PCBLayer::EdgeCuts) {
            QListWidgetItem* item = new QListWidgetItem(layer.name());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            item->setData(Qt::UserRole, layer.id());
            item->setCheckState(Qt::Checked);
            m_layerList->addItem(item);
        }
    }

    refreshLayerVisibility();
}

void GerberExportDialog::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    QStringLiteral("Select Output Directory"),
                                                    m_dirEdit->text());
    if (!dir.isEmpty()) {
        m_dirEdit->setText(dir);
    }
}

void GerberExportDialog::onExport()
{
    QString dirPath = m_dirEdit->text().trimmed();

    if (dirPath.isEmpty()) {
        QMessageBox::warning(this,
                             windowTitle(),
                             QStringLiteral("Please choose an output directory."));
        return;
    }

    QDir dir;
    if (!dir.mkpath(dirPath)) {
        QMessageBox::warning(this,
                             windowTitle(),
                             QStringLiteral("Failed to create output directory:\n%1").arg(dirPath));
        return;
    }

    if (selectedLayers().isEmpty()) {
        QMessageBox::warning(this,
                             windowTitle(),
                             QStringLiteral("Select at least one layer to export."));
        return;
    }

    m_dirEdit->setText(dirPath);
    accept();
}

QList<int> GerberExportDialog::selectedLayers() const
{
    QList<int> ids;

    for (int i = 0; i < m_layerList->count(); ++i) {
        QListWidgetItem* item = m_layerList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            ids.append(item->data(Qt::UserRole).toInt());
        }
    }

    return ids;
}

QString GerberExportDialog::outputDirectory() const
{
    return m_dirEdit->text();
}

bool GerberExportDialog::generateDrillFile() const
{
    return m_drillCheck && m_drillCheck->isChecked();
}

bool GerberExportDialog::exportCombinedPdf() const
{
    return m_combinedPdfCheck && m_combinedPdfCheck->isChecked();
}

bool GerberExportDialog::pdfPlotOneToOne() const
{
    return m_pdfScaleMode &&
           m_pdfScaleMode->currentData().toString() == QStringLiteral("1:1");
}

bool GerberExportDialog::pdfBlackAndWhite() const
{
    return m_pdfBlackAndWhiteCheck && m_pdfBlackAndWhiteCheck->isChecked();
}

bool GerberExportDialog::pdfOpenAfterExport() const
{
    return m_pdfOpenAfterExportCheck && m_pdfOpenAfterExportCheck->isChecked();
}

QPageSize::PageSizeId GerberExportDialog::pdfPageSizeId() const
{
    if (!m_pdfPageSize) {
        return QPageSize::A4;
    }

    return static_cast<QPageSize::PageSizeId>(m_pdfPageSize->currentData().toInt());
}

int GerberExportDialog::pdfOrientationMode() const
{
    return m_pdfOrientation ? m_pdfOrientation->currentData().toInt() : 0;
}

qreal GerberExportDialog::pdfMarginMm() const
{
    return m_pdfMargin ? m_pdfMargin->currentData().toDouble() : 5.0;
}

bool GerberExportDialog::pdfTitleBlock() const
{
    return m_pdfTitleBlockCheck && m_pdfTitleBlockCheck->isChecked();
}

void GerberExportDialog::onFilterTextChanged(const QString& text)
{
    Q_UNUSED(text);
    refreshLayerVisibility();
}

void GerberExportDialog::setAllLayerCheckState(Qt::CheckState state)
{
    for (int i = 0; i < m_layerList->count(); ++i) {
        QListWidgetItem* item = m_layerList->item(i);
        if (!item || item->isHidden()) {
            continue;
        }
        item->setCheckState(state);
    }
}

void GerberExportDialog::refreshLayerVisibility()
{
    const QString filter = m_filterEdit ? m_filterEdit->text().trimmed() : QString();

    for (int i = 0; i < m_layerList->count(); ++i) {
        QListWidgetItem* item = m_layerList->item(i);
        if (!item) {
            continue;
        }

        const bool matches =
            filter.isEmpty() || item->text().contains(filter, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

