/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSCILLOSCOPEITEM_H
#define OSCILLOSCOPEITEM_H

#include "schematic_item.h"
#include <QPainter>
#include <QVector>

/**
 * @brief Schematic symbol for a multi-channel oscilloscope instrument with floating ground per channel.
 */
class OscilloscopeItem : public SchematicItem {
public:
    OscilloscopeItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "OscilloscopeInstrument"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "OSC"; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    struct ChannelConfig {
        bool enabled = true;
        double scale = 1.0;
        double offset = 0.0;
        QColor color = Qt::yellow;
        bool floatingGround = false; // When true, probe measures (CH+ - CH-) rather than GND
    };

    struct Config {
        int channelCount = 4; // 1 to 8 channels
        QVector<ChannelConfig> channels;
        double timebase = 1e-3;
        QString triggerSource = "CH1";
        double triggerLevel = 0.0;

        Config() {
            channelCount = 4;
            channels.resize(4);
            static const QColor defaultColors[8] = {
                Qt::yellow, Qt::cyan, Qt::magenta, QColor(0, 255, 100),
                QColor(255, 165, 0), QColor(147, 112, 219), QColor(255, 105, 180), QColor(0, 191, 255)
            };
            for (int i = 0; i < 4; ++i) {
                channels[i].enabled = true;
                channels[i].scale = 1.0;
                channels[i].offset = 0.0;
                channels[i].color = defaultColors[i % 8];
                channels[i].floatingGround = false;
            }
        }
    };

    Config config() const { return m_config; }
    void setConfig(const Config& cfg);

    int channelCount() const { return m_config.channelCount; }
    void setChannelCount(int count);

    QString channelNet(int chIdx) const;
    QString channelNegNet(int chIdx) const;

Q_SIGNALS:
    void configChanged();

private:
    Config m_config;
};

#endif // OSCILLOSCOPEITEM_H
