/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSCILLOSCOPE_WINDOW_H
#define OSCILLOSCOPE_WINDOW_H

#include <QMainWindow>
#include <QUuid>
#include <QMap>
#include <QVector>
#include "mini_scope_widget.h"
#include "../items/oscilloscope_item.h"
#include "../../simulator/core/sim_results.h"

class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QGroupBox;
class QVBoxLayout;
class NetManager;

/**
 * @brief A standalone, hardware-realistic window for an Analog Oscilloscope instrument.
 */
class OscilloscopeWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit OscilloscopeWindow(const QUuid& itemId, const QString& itemName, QWidget* parent = nullptr);
    ~OscilloscopeWindow();

    QUuid itemId() const { return m_itemId; }

    OscilloscopeItem::Config config() const { return m_config; }
    void setConfig(const OscilloscopeItem::Config& cfg);

    /**
     * @brief Updates the window with new simulation results.
     * Filters for nets connected specifically to this instrument.
     */
    void updateResults(const SimResults& results, NetManager* netManager, class SchematicItem* item = nullptr);
    void updateRealTimeData(const std::vector<double>& times, const std::vector<std::vector<double>>& values, const QStringList& names, class SchematicItem* item = nullptr);

    /**
     * @brief Clear existing traces.
     */
    void clear();

    /**
     * @brief Render the current oscilloscope display to an image.
     */
    QImage renderToImage(const QSize& size = QSize(1000, 600));

    /**
     * @brief Automatically calculate V/Div for active channels so waveforms fit the screen.
     */
    void autoScaleChannels();
    void zoomToFit();
    void fitYAxis();

Q_SIGNALS:
    void windowClosing(const QUuid& id);
    void configChanged(const QUuid& id, const OscilloscopeItem::Config& cfg);
    void propertiesRequested(const QUuid& id);

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onChannelToggled(int ch, bool checked);
    void onFloatingToggled(int ch, bool checked);
    void onTimebaseChanged(double value);
    void onVoltsDivChanged(int ch, double value);
    void onOffsetChanged(int ch, double value);
    void onTriggerSourceChanged(int index);
    void onTriggerLevelChanged(double value);
    void onFreezeClicked();
    void onClearMemoriesClicked();

private:
    void setupUI();
    void rebuildChannelControls();

    QUuid m_itemId;
    QString m_itemName;
    OscilloscopeItem::Config m_config;

    // UI Components
    MiniScopeWidget* m_scopeDisplay;
    QVBoxLayout* m_channelsContainerLayout;
    
    // Channel Controls
    struct ChannelUI {
        QGroupBox* group = nullptr;
        QCheckBox* enabled = nullptr;
        QCheckBox* floating = nullptr;
        QDoubleSpinBox* voltsDiv = nullptr;
        QDoubleSpinBox* offset = nullptr;
    };
    QVector<ChannelUI> m_channelUIs;

    QDoubleSpinBox* m_timebaseSpin;
    QComboBox* m_triggerSourceCombo;
    QDoubleSpinBox* m_triggerLevelSpin;
    QPushButton* m_freezeBtn;
    QPushButton* m_clearMemBtn;

    // Cursors UI
    QComboBox* m_cursorModeCombo = nullptr;
    QLabel* m_cursorDeltaTimeLabel = nullptr;
    QLabel* m_cursorFreqLabel = nullptr;
    QLabel* m_cursorDeltaVoltLabel = nullptr;

    void reprocessTraces();

    NetManager* m_lastNetManager = nullptr;
    SimResults m_cachedResults;
    bool m_hasCachedResults = false;
    bool m_initialFitDone = false;
    class SchematicItem* m_cachedItem = nullptr;
};

#endif // OSCILLOSCOPE_WINDOW_H
