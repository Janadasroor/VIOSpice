/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOOTPRINT_MODEL_3D_PANEL_H
#define FOOTPRINT_MODEL_3D_PANEL_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPointer>

class FootprintEditor;
class QGraphicsScene;
class PCB3DWindow;

class FootprintModel3DPanel : public QWidget {
    Q_OBJECT
public:
    explicit FootprintModel3DPanel(FootprintEditor* editor, QWidget* parent = nullptr);
    ~FootprintModel3DPanel() override = default;

    void updatePanelFromModels();
    void syncCurrentModelFromFields();
    void refreshModelSelector();
    void loadModelToFields(int index);

public slots:
    void onOpen3DPreview();

private:
    void setupUI();

    FootprintEditor* m_editor;

    // 3D Model Settings (moved from FootprintEditor)
    QComboBox* m_modelSelector = nullptr;
    QPushButton* m_addModelButton = nullptr;
    QPushButton* m_removeModelButton = nullptr;
    QLineEdit* m_modelFileEdit = nullptr;
    QLineEdit* m_modelOffsetX = nullptr;
    QLineEdit* m_modelOffsetY = nullptr;
    QLineEdit* m_modelOffsetZ = nullptr;
    QLineEdit* m_modelRotX = nullptr;
    QLineEdit* m_modelRotY = nullptr;
    QLineEdit* m_modelRotZ = nullptr;
    QLineEdit* m_modelScaleX = nullptr;
    QLineEdit* m_modelScaleY = nullptr;
    QLineEdit* m_modelScaleZ = nullptr;
    QDoubleSpinBox* m_modelOpacitySpin = nullptr;
    QCheckBox* m_modelVisibleCheck = nullptr;
    QCheckBox* m_previewBottomCopperCheck = nullptr;
};

#endif // FOOTPRINT_MODEL_3D_PANEL_H
