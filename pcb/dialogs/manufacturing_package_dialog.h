/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MANUFACTURING_PACKAGE_DIALOG_H
#define MANUFACTURING_PACKAGE_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include "../manufacturing/manufacturing_exporter.h"

class ManufacturingPackageDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManufacturingPackageDialog(QWidget* parent = nullptr);

    ManufacturingExporter::ManufacturingPackageOptions options() const;
    QString outputPath() const;

private slots:
    void onBrowseOutput();

private:
    void setupUI();

    QComboBox* m_presetCombo;
    QCheckBox* m_bomCheck;
    QCheckBox* m_cplCheck;
    QCheckBox* m_gerberCheck;
    QCheckBox* m_drillCheck;
    QLineEdit* m_pathEdit;
    QPushButton* m_browseBtn;
};

#endif // MANUFACTURING_PACKAGE_DIALOG_H
