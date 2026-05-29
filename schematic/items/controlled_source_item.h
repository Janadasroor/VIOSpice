#ifndef CONTROLLED_SOURCE_ITEM_H
#define CONTROLLED_SOURCE_ITEM_H

#include "schematic_item.h"

class ControlledSourceItem : public SchematicItem {
    Q_OBJECT
public:
    enum Type {
        VCVS, // E
        VCCS, // G
        CCCS, // F
        CCVS  // H
    };

    ControlledSourceItem(Type type, QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override;
    QString referencePrefix() const override;
    ItemType itemType() const override { return CustomType; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    QList<PinElectricalType> pinElectricalTypes() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    Type sourceType() const { return m_type; }
    void setSourceType(Type type) { m_type = type; update(); }

private:
    Type m_type;
};

#endif // CONTROLLED_SOURCE_ITEM_H
