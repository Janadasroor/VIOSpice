/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ADCBRIDGEITEM_H
#define ADCBRIDGEITEM_H

#include "schematic_item.h"

class AdcBridgeItem : public SchematicItem {
    Q_OBJECT
public:
    AdcBridgeItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "AdcBridge"; }
    ItemType itemType() const override { return SchematicItem::CustomType; }
    QString referencePrefix() const override { return "U"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    QList<PinElectricalType> pinElectricalTypes() const override;

    double inLow() const { return m_inLow; }
    double inHigh() const { return m_inHigh; }
    double riseDelay() const { return m_riseDelay; }
    double fallDelay() const { return m_fallDelay; }

    void setInLow(double v) { m_inLow = v; setParamExpression("in_low", QString::number(v)); }
    void setInHigh(double v) { m_inHigh = v; setParamExpression("in_high", QString::number(v)); }
    void setRiseDelay(double v) { m_riseDelay = v; setParamExpression("rise_delay", QString::number(v)); }
    void setFallDelay(double v) { m_fallDelay = v; setParamExpression("fall_delay", QString::number(v)); }

private:
    double m_inLow = 0.1;
    double m_inHigh = 0.9;
    double m_riseDelay = 1e-9;
    double m_fallDelay = 1e-9;
};

#endif
