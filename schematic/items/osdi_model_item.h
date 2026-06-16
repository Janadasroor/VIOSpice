/*
 * Copyright 2026 Janada Sroor
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSDI_MODEL_ITEM_H
#define OSDI_MODEL_ITEM_H

#include "schematic_item.h"
#include <QStringList>

/**
 * @brief Represents a compiled Verilog-A model loaded via OSDI.
 * In SPICE, these use the 'N' prefix and require a 'pre_osdi' library load.
 */
class OsdiModelItem : public SchematicItem {
    Q_OBJECT
public:
    OsdiModelItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "OsdiModel"; }
    ItemType itemType() const override { return SchematicItem::CustomType; }
    QString referencePrefix() const override { return "N"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    QList<PinElectricalType> pinElectricalTypes() const override;

    QString osdiPath() const { return m_osdiPath; }
    void setOsdiPath(const QString& path);

    QString modelName() const { return m_modelName; }
    void setModelName(const QString& name) { m_modelName = name; }

    QStringList pins() const { return m_pins; }
    void setPins(const QStringList& pins);

    void rebuildPrimitives() override;

private:
    void updateSize();

    QString m_osdiPath;
    QString m_modelName;
    QStringList m_pins;
    QSizeF m_size;
};

#endif // OSDI_MODEL_ITEM_H
