#ifndef LCOUPLEITEM_H
#define LCOUPLEITEM_H

#include "schematic_item.h"

class LcoupleItem : public SchematicItem {
    Q_OBJECT
public:
    LcoupleItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Lcouple"; }
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

    double numTurns() const { return m_numTurns; }
    void setNumTurns(double v) { m_numTurns = v; setParamExpression("num_turns", QString::number(v)); }

private:
    double m_numTurns = 100.0;
};

#endif
