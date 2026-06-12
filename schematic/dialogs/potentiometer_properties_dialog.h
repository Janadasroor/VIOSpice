/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef POTENTIOMETER_PROPERTIES_DIALOG_H
#define POTENTIOMETER_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>
#include <QMap>

class QLineEdit;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class SchematicItem;

class PotentiometerPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PotentiometerPropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

    QString reference() const;
    QString totalResistance() const;
    double wiperPosition() const;
    bool isLogarithmic() const;
    double logMultiplier() const;
    QMap<QString, QString> paramExpressions() const;

private Q_SLOTS:
    void onSliderChanged(int value);
    void onSpinBoxChanged(double value);
    void updateCommandPreview();
    void applyChanges();

private:
    void setupUI();
    void loadValues();

    QPointer<SchematicItem> m_item;

    QLineEdit* m_refEdit = nullptr;
    QLineEdit* m_resEdit = nullptr;
    QSlider* m_wiperSlider = nullptr;
    QDoubleSpinBox* m_wiperSpin = nullptr;
    QCheckBox* m_logCheck = nullptr;
    QDoubleSpinBox* m_logMultSpin = nullptr;
    QLineEdit* m_commandPreview = nullptr;
};

#endif // POTENTIOMETER_PROPERTIES_DIALOG_H
