#ifndef COREITEM_H
#define COREITEM_H

#include "schematic_item.h"
#include <QStringList>

class CoreItem : public SchematicItem {
    Q_OBJECT
public:
    CoreItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "MagneticCore"; }
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

    double area() const { return m_area; }
    double length() const { return m_length; }
    int mode() const { return m_mode; }
    QString hArray() const { return m_hArray; }
    QString bArray() const { return m_bArray; }
    double inputDomain() const { return m_inputDomain; }
    bool fraction() const { return m_fraction; }
    double inLow() const { return m_inLow; }
    double inHigh() const { return m_inHigh; }
    double hyst() const { return m_hyst; }
    double outLowerLimit() const { return m_outLowerLimit; }
    double outUpperLimit() const { return m_outUpperLimit; }

    void setArea(double v) { m_area = v; setParamExpression("area", QString::number(v)); }
    void setLength(double v) { m_length = v; setParamExpression("length", QString::number(v)); }
    void setMode(int v) { m_mode = v; setParamExpression("mode", QString::number(v)); }
    void setHArray(const QString& v) { m_hArray = v; setParamExpression("H_array", v); }
    void setBArray(const QString& v) { m_bArray = v; setParamExpression("B_array", v); }
    void setInputDomain(double v) { m_inputDomain = v; setParamExpression("input_domain", QString::number(v)); }
    void setFraction(bool v) { m_fraction = v; setParamExpression("fraction", v ? "TRUE" : "FALSE"); }
    void setInLow(double v) { m_inLow = v; setParamExpression("in_low", QString::number(v)); }
    void setInHigh(double v) { m_inHigh = v; setParamExpression("in_high", QString::number(v)); }
    void setHyst(double v) { m_hyst = v; setParamExpression("hyst", QString::number(v)); }
    void setOutLowerLimit(double v) { m_outLowerLimit = v; setParamExpression("out_lower_limit", QString::number(v)); }
    void setOutUpperLimit(double v) { m_outUpperLimit = v; setParamExpression("out_upper_limit", QString::number(v)); }

private:
    double m_area = 1e-4;
    double m_length = 1e-2;
    int m_mode = 1;
    QString m_hArray = "-200 -100 100 200";
    QString m_bArray = "-1.26 -0.63 0.63 1.26";
    double m_inputDomain = 0.01;
    bool m_fraction = true;
    double m_inLow = -1.0;
    double m_inHigh = 1.0;
    double m_hyst = 0.1;
    double m_outLowerLimit = -1.0;
    double m_outUpperLimit = 1.0;
};

#endif
