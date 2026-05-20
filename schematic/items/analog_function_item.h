#ifndef ANALOGFUNCTIONITEM_H
#define ANALOGFUNCTIONITEM_H

#include "schematic_item.h"
#include <QMap>

class AnalogFunctionItem : public SchematicItem {
    Q_OBJECT
public:
    AnalogFunctionItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "AnalogFunction"; }
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

    QString functionType() const { return m_functionType; }
    void setFunctionType(const QString& type);

    QMap<QString, double> parameters() const { return m_params; }
    double param(const QString& key, double def = 0.0) const { return m_params.value(key, def); }
    void setParam(const QString& key, double v) {
        m_params[key] = v;
        setParamExpression(key, QString::number(v));
    }

    static QStringList availableFunctions();

private:
    void updateSize();

    QString m_functionType = "gain";
    QMap<QString, double> m_params;
    QSizeF m_size{100, 50};
};

#endif
