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
class QTabWidget;
class QLabel;
class AvrMicrocontrollerItem;

class AvrMicrocontrollerDialog : public QDialog {
    Q_OBJECT
public:
    explicit AvrMicrocontrollerDialog(AvrMicrocontrollerItem* item, QWidget* parent = nullptr);

private slots:
    void onBrowseFirmware();
    void onAccept();
    void onChipSearchChanged(const QString& text);
    void onChipFilterChanged();
    void onChipItemClicked(QListWidgetItem* item);
    void onBoardSearchChanged(const QString& text);
    void onBoardItemClicked(QListWidgetItem* item);
    void onTabChanged(int index);

private:
    void populateDeviceList();
    void populateBoardList();
    void applyChipFilter();
    void applyBoardFilter();
    QString selectedMcu() const;
    QString selectedBoard() const;

    AvrMicrocontrollerItem* m_item;
    QTabWidget* m_tabWidget;

    // Chip mode
    QLineEdit* m_chipSearchEdit;
    QListWidget* m_chipDeviceList;
    QButtonGroup* m_chipFilterGroup;
    QStringList m_allDevices;

    // Board mode
    QLineEdit* m_boardSearchEdit;
    QListWidget* m_boardList;
    QLabel* m_boardInfoLabel;

    // Shared settings
    QLineEdit* m_firmwareEdit;
    QDoubleSpinBox* m_clockSpin;
    QCheckBox* m_jitCheck;
    QDoubleSpinBox* m_adcVoltageSpin;
};

#endif // AVR_MICROCONTROLLER_DIALOG_H
