/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef XSPICE_BLOCK_ITEM_H
#define XSPICE_BLOCK_ITEM_H

#include "schematic_item.h"
#include <QVector>
#include <QJsonObject>
#include <QVariant>

struct XspicePinDef {
    QString name;
    enum Type { VoltageIn, VoltageDiff, CurrentSense, Conductance, Digital, Real };
    Type type = VoltageIn;
    int minCount = 1;
    bool isVector = false;
};

struct XspiceParamDef {
    QString name;
    QVariant defaultValue;
    QString description;
    enum Widget { SpinboxDouble, SpinboxInt, LineEdit, Checkbox, FilePath, Combo };
    Widget widget = LineEdit;
    double min = -1e12;
    double max = 1e12;
    QStringList comboItems;
};

struct XspiceModelDef {
    QString name;
    QString category;
    QString spiceType;
    QString description;
    QVector<XspicePinDef> pins;
    QVector<XspiceParamDef> params;
    int inputPinCount = 1;
    int outputPinCount = 1;
};

class XspiceBlockItem : public SchematicItem {
    Q_OBJECT
public:
    explicit XspiceBlockItem(QGraphicsItem* parent = nullptr);
    explicit XspiceBlockItem(const QString& modelType, QGraphicsItem* parent = nullptr);
    ~XspiceBlockItem() override = default;

    static const QVector<XspiceModelDef>& modelDatabase();

    QString modelType() const { return m_modelType; }
    void setModelType(const QString& type);

    QJsonObject xspiceParams() const { return m_xspiceParams; }
    void setXspiceParams(const QJsonObject& params) { m_xspiceParams = params; update(); }

    // SchematicItem interface
    QString itemTypeName() const override { return "XspiceBlock"; }
    ItemType itemType() const override { return SchematicItem::CustomType; }
    QString referencePrefix() const override { return "U"; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void drawConnectionPointHighlights(QPainter*) const {}
    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    QList<PinElectricalType> pinElectricalTypes() const override;

    const XspiceModelDef* modelDef() const;
    QMap<QString, QString> paramExpressionsForNetlist() const;

private:
    void rebuildPins();

    QString m_modelType = "gain";
    QJsonObject m_xspiceParams;
    int m_inputPinCount = 1;
    int m_outputPinCount = 1;
};

#endif
