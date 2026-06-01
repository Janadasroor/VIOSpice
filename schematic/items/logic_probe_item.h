#ifndef LOGIC_PROBE_ITEM_H
#define LOGIC_PROBE_ITEM_H

#include "schematic_item.h"

/**
 * @brief A logic probe that monitors a net and displays 0 or 1.
 * It changes color (Red/Green) based on the voltage level (threshold at 2.5V).
 */
class LogicProbeItem : public SchematicItem {
    Q_OBJECT
public:
    explicit LogicProbeItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "LogicProbe"; }
    ItemType itemType() const override { return ItemType::CustomType; }
    QString referencePrefix() const override { return "LP"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    void updateFromSimulation(double voltage);

private:
    bool m_logicState = false; // Threshold-based state
    double m_lastVoltage = 0.0;
};

#endif // LOGIC_PROBE_ITEM_H
