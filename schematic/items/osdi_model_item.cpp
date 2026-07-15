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

#include "osdi_model_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyleOptionGraphicsItem>
#include <QFileInfo>
#include <algorithm>

OsdiModelItem::OsdiModelItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_size(100, 60) {
    setExcludeFromPcb(true);
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    m_pins << "P1" << "P2" << "P3";
    setReference("N1");
    setName("OSDI Verilog-A Model");
}

void OsdiModelItem::setOsdiPath(const QString& path) {
    m_osdiPath = path;
    setParamExpression("osdi_path", path);
    update();
}

void OsdiModelItem::setPins(const QStringList& pins) {
    m_pins = pins;
    updateSize();
    rebuildPrimitives();
    update();
}

void OsdiModelItem::updateSize() {
    qreal height = std::max(60.0, m_pins.size() * 20.0 + 20.0);
    const QSizeF newSize(100, height);
    if (m_size == newSize) return;
    prepareGeometryChange();
    m_size = newSize;
}

QRectF OsdiModelItem::boundingRect() const {
    return QRectF(-m_size.width() / 2 - 20, -m_size.height() / 2,
                  m_size.width() + 40, m_size.height());
}

void OsdiModelItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect(-m_size.width() / 2, -m_size.height() / 2,
                m_size.width(), m_size.height());

    QLinearGradient bgGrad(rect.topLeft(), rect.bottomLeft());
    bgGrad.setColorAt(0, QColor(20, 60, 60));
    bgGrad.setColorAt(1, QColor(10, 30, 30));
    
    if (isSelected()) {
        bgGrad.setColorAt(0, QColor(40, 100, 100));
        bgGrad.setColorAt(1, QColor(20, 60, 60));
    }
    
    painter->setBrush(bgGrad);
    painter->setPen(QPen(QColor(0, 200, 200), 1.5));
    painter->drawRoundedRect(rect, 4, 4);

    // Header
    painter->setBrush(QColor(0, 150, 150));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRectF(rect.left(), rect.top(), rect.width(), 12), 4, 4);
    painter->fillRect(QRectF(rect.left(), rect.top() + 8, rect.width(), 4), QColor(0, 150, 150));

    // Label
    QString displayName = m_modelName.isEmpty() ? "VERILOG-A" : m_modelName;
    painter->setPen(Qt::white);
    painter->setFont(QFont("Monospace", 7, QFont::Bold));
    painter->drawText(rect.adjusted(0, 15, 0, 0), Qt::AlignHCenter | Qt::AlignTop, displayName.toUpper());

    // Pins
    painter->setFont(QFont("Inter", 6));
    qreal startY = -m_size.height() / 2 + 25;
    for (int i = 0; i < m_pins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->setPen(QPen(QColor(0, 200, 200), 1));
        painter->drawLine(-m_size.width() / 2 - 20, y, -m_size.width() / 2, y);
        
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-m_size.width() / 2 + 4, y - 8, 80, 16),
                          Qt::AlignLeft | Qt::AlignVCenter, m_pins[i]);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> OsdiModelItem::connectionPoints() const {
    QList<QPointF> pts;
    qreal startY = -m_size.height() / 2 + 25;
    for (int i = 0; i < m_pins.size(); ++i)
        pts << QPointF(-m_size.width() / 2 - 20, startY + i * 20);
    return pts;
}

QString OsdiModelItem::pinName(int index) const {
    if (index >= 0 && index < m_pins.size()) return m_pins[index];
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> OsdiModelItem::pinElectricalTypes() const {
    QList<PinElectricalType> types;
    for (int i = 0; i < m_pins.size(); ++i) types << PassivePin;
    return types;
}

QJsonObject OsdiModelItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["osdiPath"] = m_osdiPath;
    j["modelName"] = m_modelName;

    QJsonArray pinsArr;
    for (const auto& p : m_pins) pinsArr.append(p);
    j["pins"] = pinsArr;

    return j;
}

bool OsdiModelItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_osdiPath = json.value("osdiPath").toString();
    m_modelName = json.value("modelName").toString();

    m_pins.clear();
    for (const auto& v : json.value("pins").toArray())
        m_pins << v.toString();

    if (m_pins.isEmpty()) m_pins << "P1" << "P2" << "P3";

    if (!m_osdiPath.isEmpty()) {
        setParamExpression("osdi_path", m_osdiPath);
    }

    updateSize();
    rebuildPrimitives();
    return true;
}

SchematicItem* OsdiModelItem::clone() const {
    auto* item = new OsdiModelItem(pos(), parentItem());
    item->m_osdiPath = m_osdiPath;
    item->m_modelName = m_modelName;
    item->m_pins = m_pins;
    if (!m_osdiPath.isEmpty()) {
        item->setParamExpression("osdi_path", m_osdiPath);
    }
    item->updateSize();
    return item;
}

void OsdiModelItem::rebuildPrimitives() {
    SchematicItem::rebuildPrimitives();
}
