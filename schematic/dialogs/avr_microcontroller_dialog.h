/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AVR_MICROCONTROLLER_DIALOG_H
#define AVR_MICROCONTROLLER_DIALOG_H

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QDoubleSpinBox;
class QCheckBox;
class QButtonGroup;
class AvrMicrocontrollerItem;

class AvrMicrocontrollerDialog : public QDialog {
    Q_OBJECT
public:
    explicit AvrMicrocontrollerDialog(AvrMicrocontrollerItem* item, QWidget* parent = nullptr);

private slots:
    void onBrowseFirmware();
    void onAccept();
    void onSearchChanged(const QString& text);
    void onFilterChanged();
    void onItemClicked(QListWidgetItem* item);

private:
    void populateDeviceList();
    void applyFilter();
    QString selectedMcu() const;

    AvrMicrocontrollerItem* m_item;
    QLineEdit* m_searchEdit;
    QListWidget* m_deviceList;
    QDoubleSpinBox* m_clockSpin;
    QCheckBox* m_jitCheck;
    QDoubleSpinBox* m_adcVoltageSpin;
    QStringList m_allDevices;
    QButtonGroup* m_filterGroup;
};

#endif // AVR_MICROCONTROLLER_DIALOG_H
