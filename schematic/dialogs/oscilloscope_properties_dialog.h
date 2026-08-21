/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSCILLOSCOPE_PROPERTIES_DIALOG_H
#define OSCILLOSCOPE_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QUndoStack>
#include <QGraphicsScene>
#include <QVector>
#include "../items/oscilloscope_item.h"

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QGroupBox;
class QVBoxLayout;

class OscilloscopePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    OscilloscopePropertiesDialog(OscilloscopeItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
private Q_SLOTS:
    void onApply();
    void onChannelCountChanged(int count);
    void onChooseColor(int ch);

private:
    void setupUI();
    void loadFromConfig();
    void rebuildChannelRows();

    OscilloscopeItem* m_item;
    QUndoStack* m_undoStack;
    QGraphicsScene* m_scene;
    OscilloscopeItem::Config m_config;

    // Controls
    QLineEdit* m_refEdit;
    QSpinBox* m_channelCountSpin;
    QDoubleSpinBox* m_timebaseSpin;
    QComboBox* m_trigSourceCombo;
    QDoubleSpinBox* m_trigLevelSpin;

    QVBoxLayout* m_channelsContainerLayout;

    struct ChannelRow {
        QGroupBox* group;
        QCheckBox* enabled;
        QCheckBox* floating;
        QDoubleSpinBox* scaleSpin;
        QDoubleSpinBox* offsetSpin;
        QPushButton* colorBtn;
        QColor color;
    };
    QVector<ChannelRow> m_channelRows;
};

#endif // OSCILLOSCOPE_PROPERTIES_DIALOG_H
