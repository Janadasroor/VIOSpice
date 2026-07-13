/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATIC_COMPONENT_MODEL_H
#define SCHEMATIC_COMPONENT_MODEL_H

#include "schematic_item_model.h"
#include <QList>
#include <QJsonArray>

namespace Flux {
namespace Model {

/**
 * @brief Represents a single Pin in the Schematic Component.
 */
struct PinModel {
    QUuid id;
    QString name;
    QString number;
    QPointF pos; // Local relative to component center
    double length = 5.0; // mm (EDA standard grid)
    double angle = 0.0; // Degrees
    QString netName;

    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = id.toString();
        json["name"] = name;
        json["number"] = number;
        json["x"] = pos.x();
        json["y"] = pos.y();
        json["length"] = length;
        json["angle"] = angle;
        json["netName"] = netName;
        return json;
    }

    void fromJson(const QJsonObject& json) {
        id = QUuid(json["id"].toString());
        name = json["name"].toString();
        number = json["number"].toString();
        pos = QPointF(json["x"].toDouble(), json["y"].toDouble());
        length = json["length"].toDouble(5.0);
        angle = json["angle"].toDouble();
        netName = json["netName"].toString();
    }
};

/**
 * @brief Data model for a Schematic Component (e.g., Resistor, IC).
 */
class SchematicComponentModel : public SchematicItemModel {
public:
    SchematicComponentModel() : m_footprint("") {}

    ~SchematicComponentModel() {
        qDeleteAll(m_pins);
    }

    QList<PinModel*> pins() const { return m_pins; }
    void addPin(PinModel* p) { m_pins.append(p); }
    void clearPins() { qDeleteAll(m_pins); m_pins.clear(); }

    QString footprint() const { return m_footprint; }
    void setFootprint(const QString& f) { m_footprint = f; }

    // Serialization
    QJsonObject toJson() const override {
        QJsonObject json = SchematicItemModel::toJson();
        json["footprint"] = m_footprint;
        QJsonArray pinsArray;
        for (const auto* p : m_pins) {
            pinsArray.append(p->toJson());
        }
        json["pins"] = pinsArray;
        return json;
    }

    void fromJson(const QJsonObject& json) override {
        SchematicItemModel::fromJson(json);
        m_footprint = json["footprint"].toString();
        clearPins();
        QJsonArray pinsArray = json["pins"].toArray();
        for (const QJsonValue& val : pinsArray) {
            PinModel* p = new PinModel();
            p->fromJson(val.toObject());
            m_pins.append(p);
        }
    }

    SchematicComponentModel* clone() const {
        SchematicComponentModel* copy = new SchematicComponentModel();
        copy->setId(id());
        copy->setPos(pos());
        copy->setRotation(rotation());
        copy->setMirroredX(isMirroredX());
        copy->setMirroredY(isMirroredY());
        copy->setUnit(unit());
        copy->setName(name());
        copy->setValue(value());
        copy->setReference(reference());
        copy->setFootprint(m_footprint);
        for (const PinModel* p : m_pins) {
            PinModel* pinCopy = new PinModel();
            pinCopy->id = p->id;
            pinCopy->name = p->name;
            pinCopy->number = p->number;
            pinCopy->pos = p->pos;
            pinCopy->length = p->length;
            pinCopy->angle = p->angle;
            pinCopy->netName = p->netName;
            copy->addPin(pinCopy);
        }
        return copy;
    }

private:
    QList<PinModel*> m_pins;
    QString m_footprint;
};

} // namespace Model
} // namespace Flux

#endif // SCHEMATIC_COMPONENT_MODEL_H
