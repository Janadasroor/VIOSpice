/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEMVERILOGBLOCKITEM_H
#define SYSTEMVERILOGBLOCKITEM_H

#include "schematic_item.h"
#include <QStringList>

class SystemVerilogBlockItem : public SchematicItem {
    Q_OBJECT
public:
    SystemVerilogBlockItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "SystemVerilogBlock"; }
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

    QString svFilePath() const { return m_svFilePath; }
    void setSvFilePath(const QString& path);

    QString moduleName() const { return m_moduleName; }
    void setModuleName(const QString& name) { m_moduleName = name; }

    QStringList inputPins() const { return m_inputPins; }
    QStringList outputPins() const { return m_outputPins; }

    void setPins(const QStringList& inputs, const QStringList& outputs);

    void rebuildPrimitives() override;

private:
    void updateSize();

    QString m_svFilePath;
    QString m_moduleName;
    QStringList m_inputPins;
    QStringList m_outputPins;
    QSizeF m_size;
};

#endif // SYSTEMVERILOGBLOCKITEM_H
