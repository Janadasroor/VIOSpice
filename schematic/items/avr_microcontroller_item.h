/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AVR_MICROCONTROLLER_ITEM_H
#define AVR_MICROCONTROLLER_ITEM_H

#include "schematic_item.h"
#include <QStringList>
#include <QMap>

struct AvrPinDef {
    QString name;
    enum Direction { Input, Output, Bidirectional, Power, Ground, AnalogIn, AnalogInOut };
    Direction dir = Bidirectional;
};

struct AvrMcuDef {
    QString name;
    int pinCount;
    QStringList ports;       // {"PA", "PB", "PC", "PD", ...}
    int pinsPerPort;
    QList<AvrPinDef> pins;   // Full pin list in DIP order
    double defaultClock;     // Hz
};

class AvrMicrocontrollerItem : public SchematicItem {
    Q_OBJECT
public:
    explicit AvrMicrocontrollerItem(QGraphicsItem* parent = nullptr);
    explicit AvrMicrocontrollerItem(const QString& mcuModel, QGraphicsItem* parent = nullptr);
    ~AvrMicrocontrollerItem() override = default;

    static const QMap<QString, AvrMcuDef>& mcuDatabase();

    QString mcuModel() const { return m_mcuModel; }
    void setMcuModel(const QString& model);

    QString firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const QString& path);

    double clockFrequency() const { return m_clockFrequency; }
    void setClockFrequency(double hz) { m_clockFrequency = hz; setParamExpression("clockFrequency", QString::number(hz)); update(); }

    bool jitEnabled() const { return m_jitEnabled; }
    void setJitEnabled(bool enabled) { m_jitEnabled = enabled; setParamExpression("jitEnabled", enabled ? "1" : "0"); }

    double adcVoltage() const { return m_adcVoltage; }
    void setAdcVoltage(double v) { m_adcVoltage = v; setParamExpression("adcVoltage", QString::number(v, 'f', 1)); }

    QString boardType() const { return m_boardType; }
    void setBoardType(const QString& board);
    bool isArduinoMode() const { return m_isArduinoMode; }

    // SchematicItem interface
    QString itemTypeName() const override { return "AvrMicrocontroller"; }
    ItemType itemType() const override { return SchematicItem::CustomType; }
    QString referencePrefix() const override { return "UAVR"; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void drawConnectionPointHighlights(QPainter*) const {}
    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    QList<PinElectricalType> pinElectricalTypes() const override;
    void rebuildPrimitives() override;

private:
    void updateSize();
    void buildPinList();

    QString m_mcuModel = "ATmega328P";
    QString m_firmwarePath;
    double m_clockFrequency = 16000000;
    bool m_jitEnabled = true;
    double m_adcVoltage = 5.0;
    QString m_boardType;
    bool m_isArduinoMode = false;
    QList<AvrPinDef> m_pinList;
    QSizeF m_size;
};

#endif // AVR_MICROCONTROLLER_ITEM_H
