/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COREDIALOG_H
#define COREDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QCheckBox;
class CoreItem;

class CoreDialog : public QDialog {
    Q_OBJECT
public:
    explicit CoreDialog(CoreItem* item, QWidget* parent = nullptr);

private Q_SLOTS:
    void onModeChanged(int idx);
    void onAccept();

private:
    void rebuildFields();

    CoreItem* m_item;
    QDoubleSpinBox* m_areaSpin;
    QDoubleSpinBox* m_lengthSpin;
    QComboBox* m_modeCombo;
    QLineEdit* m_hArrayEdit;
    QLineEdit* m_bArrayEdit;
    QDoubleSpinBox* m_inputDomainSpin;
    QCheckBox* m_fractionCheck;
    QDoubleSpinBox* m_inLowSpin;
    QDoubleSpinBox* m_inHighSpin;
    QDoubleSpinBox* m_hystSpin;
    QDoubleSpinBox* m_outLowerSpin;
    QDoubleSpinBox* m_outUpperSpin;
};

#endif
