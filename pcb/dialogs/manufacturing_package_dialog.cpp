/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "manufacturing_package_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>

ManufacturingPackageDialog::ManufacturingPackageDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Generate One-Click Manufacturing Package");
    resize(500, 320);
    setupUI();
}

void ManufacturingPackageDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    QGroupBox* presetBox = new QGroupBox("Fabricator Preset", this);
    QFormLayout* presetLayout = new QFormLayout(presetBox);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem("JLCPCB (Gerber + NC Drill + BOM + CPL)", ManufacturingExporter::JLCPCB);
    m_presetCombo->addItem("PCBWay (Gerber + NC Drill + BOM + Centroid)", ManufacturingExporter::PCBWay);
    m_presetCombo->addItem("Eurocircuits (Gerber X2 + NC Drill)", ManufacturingExporter::Eurocircuits);
    m_presetCombo->addItem("Generic Gerber (RS-274X + NC Drill)", ManufacturingExporter::GenericGerber);

    presetLayout->addRow("Fabricator:", m_presetCombo);

    QGroupBox* contentsBox = new QGroupBox("Package Contents", this);
    QVBoxLayout* contentsLayout = new QVBoxLayout(contentsBox);

    m_gerberCheck = new QCheckBox("Include Gerber RS-274X / X2 Layers", this);
    m_gerberCheck->setChecked(true);
    m_drillCheck = new QCheckBox("Include NC Drill Files (.drl)", this);
    m_drillCheck->setChecked(true);
    m_bomCheck = new QCheckBox("Include Bill of Materials (BOM CSV)", this);
    m_bomCheck->setChecked(true);
    m_cplCheck = new QCheckBox("Include Component Placement List (CPL CSV)", this);
    m_cplCheck->setChecked(true);

    contentsLayout->addWidget(m_gerberCheck);
    contentsLayout->addWidget(m_drillCheck);
    contentsLayout->addWidget(m_bomCheck);
    contentsLayout->addWidget(m_cplCheck);

    QGroupBox* outputBox = new QGroupBox("Output Location", this);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputBox);

    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_pathEdit = new QLineEdit(QDir(defaultDir).filePath("jlcpcb_package.zip"), this);
    m_browseBtn = new QPushButton("Browse...", this);

    outputLayout->addWidget(m_pathEdit, 1);
    outputLayout->addWidget(m_browseBtn);

    connect(m_browseBtn, &QPushButton::clicked, this, &ManufacturingPackageDialog::onBrowseOutput);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* generateBtn = new QPushButton("Generate Package", this);
    generateBtn->setDefault(true);
    generateBtn->setStyleSheet("QPushButton { background-color: #2563eb; color: white; font-weight: bold; padding: 6px 16px; border-radius: 4px; }");

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(generateBtn);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(generateBtn, &QPushButton::clicked, this, &QDialog::accept);

    mainLayout->addWidget(presetBox);
    mainLayout->addWidget(contentsBox);
    mainLayout->addWidget(outputBox);
    mainLayout->addLayout(btnLayout);
}

void ManufacturingPackageDialog::onBrowseOutput() {
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Manufacturing Package", m_pathEdit->text(), "ZIP Archives (*.zip);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_pathEdit->setText(fileName);
    }
}

ManufacturingExporter::ManufacturingPackageOptions ManufacturingPackageDialog::options() const {
    ManufacturingExporter::ManufacturingPackageOptions opts;
    opts.preset = static_cast<ManufacturingExporter::FabricatorPreset>(m_presetCombo->currentData().toInt());
    opts.includeGerbers = m_gerberCheck->isChecked();
    opts.includeDrill = m_drillCheck->isChecked();
    opts.includeBOM = m_bomCheck->isChecked();
    opts.includeCPL = m_cplCheck->isChecked();
    opts.zipPackage = true;
    return opts;
}

QString ManufacturingPackageDialog::outputPath() const {
    return m_pathEdit->text().trimmed();
}
