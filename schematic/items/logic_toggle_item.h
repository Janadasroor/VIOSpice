#ifndef LOGIC_TOGGLE_ITEM_H
#define LOGIC_TOGGLE_ITEM_H

#include "schematic_item.h"

/**
 * @brief An interactive logic toggle switch that injects 0V or 5V into the simulation.
 * When clicked in the GUI, it flips state and updates the simulation parameter in real-time.
 */
class LogicToggleItem : public SchematicItem {
    Q_OBJECT
public:
    explicit LogicToggleItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "LogicToggle"; }
    ItemType itemType() const override { return ItemType::CustomType; }
    QString referencePrefix() const override { return "LT"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    bool state() const { return m_state; }
    void setState(bool high);

    // Interactive support
    bool isInteractive() const override { return true; }
    void onInteractivePress(const QPointF& scenePos) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void updateSimulationValue();

    bool m_state = false; // false = 0V, true = 5V
    const double m_size = 50.0;
};

#endif // LOGIC_TOGGLE_ITEM_H
