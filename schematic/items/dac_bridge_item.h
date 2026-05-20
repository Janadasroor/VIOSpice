#ifndef DACBRIDGEITEM_H
#define DACBRIDGEITEM_H

#include "schematic_item.h"

class DacBridgeItem : public SchematicItem {
    Q_OBJECT
public:
    DacBridgeItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "DacBridge"; }
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

    double outLow() const { return m_outLow; }
    double outHigh() const { return m_outHigh; }
    double outUndef() const { return m_outUndef; }
    double inputLoad() const { return m_inputLoad; }
    double tRise() const { return m_tRise; }
    double tFall() const { return m_tFall; }

    void setOutLow(double v) { m_outLow = v; setParamExpression("out_low", QString::number(v)); }
    void setOutHigh(double v) { m_outHigh = v; setParamExpression("out_high", QString::number(v)); }
    void setOutUndef(double v) { m_outUndef = v; setParamExpression("out_undef", QString::number(v)); }
    void setInputLoad(double v) { m_inputLoad = v; setParamExpression("input_load", QString::number(v)); }
    void setTRise(double v) { m_tRise = v; setParamExpression("t_rise", QString::number(v)); }
    void setTFall(double v) { m_tFall = v; setParamExpression("t_fall", QString::number(v)); }

private:
    double m_outLow = 0.0;
    double m_outHigh = 5.0;
    double m_outUndef = 2.5;
    double m_inputLoad = 1e-12;
    double m_tRise = 1e-9;
    double m_tFall = 1e-9;
};

#endif
