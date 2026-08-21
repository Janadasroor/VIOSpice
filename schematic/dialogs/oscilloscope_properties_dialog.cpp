/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oscilloscope_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/core/sim_value_parser.h"

OscilloscopePropertiesDialog::OscilloscopePropertiesDialog(OscilloscopeItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle("Oscilloscope Configuration - " + item->reference());
    
    OscilloscopeItem::Config cfg = item->config();

    // General & Channel Setup Tab
    PropertyTab generalTab;
    generalTab.title = "Instrument Setup";
    
    PropertyField chCountField;
    chCountField.name = "channel_count";
    chCountField.label = "Number of Channels";
    chCountField.type = PropertyField::Choice;
    chCountField.choices = {"1", "2", "3", "4", "5", "6", "7", "8"};
    generalTab.fields.append(chCountField);

    generalTab.fields.append({"time_div", "Time/div (Horizontal)", PropertyField::EngineeringValue, "1ms", {}, "s"});
    
    PropertyField trigSource;
    trigSource.name = "trig_source";
    trigSource.label = "Trigger Source";
    trigSource.type = PropertyField::Choice;
    trigSource.choices = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8", "External"};
    generalTab.fields.append(trigSource);

    PropertyField trigLevel;
    trigLevel.name = "trig_level";
    trigLevel.label = "Trigger Level";
    trigLevel.type = PropertyField::Double;
    trigLevel.unit = "V";
    generalTab.fields.append(trigLevel);

    addTab(generalTab);

    // Channels Details Tab
    PropertyTab channelsTab;
    channelsTab.title = "Channels & Probes";
    
    for (int i = 1; i <= 8; ++i) {
        PropertyField chEnable;
        chEnable.name = QString("ch%1_enable").arg(i);
        chEnable.label = QString("CH%1 Active").arg(i);
        chEnable.type = PropertyField::Boolean;
        channelsTab.fields.append(chEnable);
        
        PropertyField chScale;
        chScale.name = QString("ch%1_scale").arg(i);
        chScale.label = QString("  CH%1 Scale (V/div)").arg(i);
        chScale.type = PropertyField::Double;
        channelsTab.fields.append(chScale);

        PropertyField chOffset;
        chOffset.name = QString("ch%1_offset").arg(i);
        chOffset.label = QString("  CH%1 Offset (V)").arg(i);
        chOffset.type = PropertyField::Double;
        channelsTab.fields.append(chOffset);

        PropertyField chFloating;
        chFloating.name = QString("ch%1_floating").arg(i);
        chFloating.label = QString("  CH%1 Floating Ref (CH- pin)").arg(i);
        chFloating.type = PropertyField::Boolean;
        channelsTab.fields.append(chFloating);
    }
    
    addTab(channelsTab);

    // Initialize values from current config
    setPropertyValue("channel_count", QString::number(cfg.channelCount));
    setPropertyValue("time_div", QString::number(cfg.timebase)); 
    setPropertyValue("trig_source", cfg.triggerSource);
    setPropertyValue("trig_level", cfg.triggerLevel);

    for (int i = 1; i <= 8; ++i) {
        if (i <= cfg.channels.size()) {
            const auto& ch = cfg.channels[i-1];
            setPropertyValue(QString("ch%1_enable").arg(i), ch.enabled);
            setPropertyValue(QString("ch%1_scale").arg(i), ch.scale);
            setPropertyValue(QString("ch%1_offset").arg(i), ch.offset);
            setPropertyValue(QString("ch%1_floating").arg(i), ch.floatingGround);
        } else {
            setPropertyValue(QString("ch%1_enable").arg(i), i <= cfg.channelCount);
            setPropertyValue(QString("ch%1_scale").arg(i), 1.0);
            setPropertyValue(QString("ch%1_offset").arg(i), 0.0);
            setPropertyValue(QString("ch%1_floating").arg(i), false);
        }
    }
}

void OscilloscopePropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack) return;

    OscilloscopeItem::Config newCfg = m_item->config();
    newCfg.channelCount = getPropertyValue("channel_count").toString().toInt();
    if (newCfg.channelCount < 1) newCfg.channelCount = 1;
    if (newCfg.channelCount > 8) newCfg.channelCount = 8;
    newCfg.channels.resize(newCfg.channelCount);

    for (int i = 1; i <= newCfg.channelCount; ++i) {
        newCfg.channels[i-1].enabled = getPropertyValue(QString("ch%1_enable").arg(i)).toBool();
        newCfg.channels[i-1].scale = getPropertyValue(QString("ch%1_scale").arg(i)).toDouble();
        newCfg.channels[i-1].offset = getPropertyValue(QString("ch%1_offset").arg(i)).toDouble();
        newCfg.channels[i-1].floatingGround = getPropertyValue(QString("ch%1_floating").arg(i)).toBool();
    }
    
    double tdiv = 0.001;
    SimValueParser::parseSpiceNumber(getPropertyValue("time_div").toString(), tdiv);
    newCfg.timebase = tdiv;
    
    newCfg.triggerSource = getPropertyValue("trig_source").toString();
    newCfg.triggerLevel = getPropertyValue("trig_level").toDouble();

    m_undoStack->push(new ChangeOscilloscopeConfigCommand(m_item, m_item->config(), newCfg));
}

void OscilloscopePropertiesDialog::applyPreview() {
    OscilloscopeItem::Config previewCfg = m_item->config();
    previewCfg.channelCount = getPropertyValue("channel_count").toString().toInt();
    if (previewCfg.channelCount < 1) previewCfg.channelCount = 1;
    if (previewCfg.channelCount > 8) previewCfg.channelCount = 8;
    previewCfg.channels.resize(previewCfg.channelCount);

    for (int i = 1; i <= previewCfg.channelCount; ++i) {
        previewCfg.channels[i-1].enabled = getPropertyValue(QString("ch%1_enable").arg(i)).toBool();
        previewCfg.channels[i-1].scale = getPropertyValue(QString("ch%1_scale").arg(i)).toDouble();
        previewCfg.channels[i-1].offset = getPropertyValue(QString("ch%1_offset").arg(i)).toDouble();
        previewCfg.channels[i-1].floatingGround = getPropertyValue(QString("ch%1_floating").arg(i)).toBool();
    }
    
    double tdiv = 0.001;
    SimValueParser::parseSpiceNumber(getPropertyValue("time_div").toString(), tdiv);
    previewCfg.timebase = tdiv;
    
    previewCfg.triggerSource = getPropertyValue("trig_source").toString();
    previewCfg.triggerLevel = getPropertyValue("trig_level").toDouble();

    m_item->setConfig(previewCfg);
}
