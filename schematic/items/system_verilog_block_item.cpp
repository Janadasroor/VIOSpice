/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "system_verilog_block_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyleOptionGraphicsItem>
#include <algorithm>

SystemVerilogBlockItem::SystemVerilogBlockItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_size(120, 60) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    m_inputPins << "In1";
    m_outputPins << "Out1";
    setReference("U1");
    setName("SystemVerilog Block");
}

void SystemVerilogBlockItem::setSvFilePath(const QString& path) {
    m_svFilePath = path;
    SchematicItem::setValue(path);
    setParamExpression("systemVerilogFile", path);
    setParamExpression("systemVerilogModule", m_moduleName);
    update();
}

void SystemVerilogBlockItem::setPins(const QStringList& inputs, const QStringList& outputs) {
    m_inputPins = inputs;
    m_outputPins = outputs;
    updateSize();
    rebuildPrimitives();
    update();
}

void SystemVerilogBlockItem::updateSize() {
    int maxPins = std::max(m_inputPins.size(), m_outputPins.size());
    qreal height = std::max(80.0, maxPins * 20.0 + 30.0); // Increased min height and padding
    const QSizeF newSize(120, height);
    if (m_size == newSize) return;
    prepareGeometryChange();
    m_size = newSize;
}

QRectF SystemVerilogBlockItem::boundingRect() const {
    return QRectF(-m_size.width() / 2 - 20, -m_size.height() / 2,
                  m_size.width() + 40, m_size.height());
}

void SystemVerilogBlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect(-m_size.width() / 2, -m_size.height() / 2,
                m_size.width(), m_size.height());

    constexpr qreal PIN_TAIL = 20;

    // Background Gradient (Industrial Slate)
    QLinearGradient bgGrad(rect.topLeft(), rect.bottomLeft());
    bgGrad.setColorAt(0, QColor(45, 45, 50));
    bgGrad.setColorAt(1, QColor(30, 30, 35));
    
    if (isSelected()) {
        bgGrad.setColorAt(0, QColor(90, 70, 120)); // Purple-tinted selection
        bgGrad.setColorAt(1, QColor(40, 30, 70));
    }
    
    painter->setBrush(bgGrad);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->drawRoundedRect(rect, 4, 4);

    // Header Accent (Purple - distinguishes SV from Blue XSPICE)
    painter->setBrush(QColor(139, 92, 246));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRectF(rect.left(), rect.top(), rect.width(), 12), 4, 4);
    painter->fillRect(QRectF(rect.left(), rect.top() + 8, rect.width(), 4), QColor(139, 92, 246));

    // Center "OLED" Display Area
    QRectF displayRect = QRectF(-m_size.width()/2 + 10, -12, m_size.width() - 20, 24);
    painter->setBrush(QColor(12, 10, 15));
    painter->setPen(QPen(QColor(167, 139, 250, 80), 1)); // Faint purple border
    painter->drawRect(displayRect);

    // Subtle Glow
    QRadialGradient glow(0, 0, 40);
    glow.setColorAt(0, QColor(167, 139, 250, 30));
    glow.setColorAt(1, Qt::transparent);
    painter->setBrush(glow);
    painter->setPen(Qt::NoPen);
    painter->drawRect(displayRect);

    // Module name in Display
    QString displayName = m_moduleName.isEmpty() ? "SYSTEMVERILOG" : m_moduleName;
    painter->setPen(QColor(167, 139, 250));
    QFont f("Monospace", 8, QFont::Bold);
    painter->setFont(f);
    painter->drawText(displayRect, Qt::AlignCenter, displayName.toUpper());

    // Pins and Labels
    QFont pinFont("Inter", 6);
    painter->setFont(pinFont);
    
    qreal startY = -m_size.height() / 2 + 25;
    for (int i = 0; i < m_inputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->setPen(QPen(QColor(100, 100, 105), 1));
        painter->drawLine(-m_size.width() / 2 - PIN_TAIL, y, -m_size.width() / 2, y);
        
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-m_size.width() / 2 + 4, y - 8, 50, 16),
                          Qt::AlignLeft | Qt::AlignVCenter, m_inputPins[i]);
    }

    for (int i = 0; i < m_outputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->setPen(QPen(QColor(100, 100, 105), 1));
        painter->drawLine(m_size.width() / 2, y, m_size.width() / 2 + PIN_TAIL, y);
        
        painter->setPen(Qt::white);
        painter->drawText(QRectF(m_size.width() / 2 - 54, y - 8, 50, 16),
                          Qt::AlignRight | Qt::AlignVCenter, m_outputPins[i]);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> SystemVerilogBlockItem::connectionPoints() const {
    QList<QPointF> pts;
    qreal startY = -m_size.height() / 2 + 25;
    for (int i = 0; i < m_inputPins.size(); ++i)
        pts << QPointF(-m_size.width() / 2 - 20, startY + i * 20);
    for (int i = 0; i < m_outputPins.size(); ++i)
        pts << QPointF(m_size.width() / 2 + 20, startY + i * 20);
    return pts;
}

QString SystemVerilogBlockItem::pinName(int index) const {
    if (index < 0) return QString();
    if (index < m_inputPins.size()) return m_inputPins[index];
    int outIdx = index - m_inputPins.size();
    if (outIdx < m_outputPins.size()) return m_outputPins[outIdx];
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> SystemVerilogBlockItem::pinElectricalTypes() const {
    QList<PinElectricalType> types;
    for (int i = 0; i < m_inputPins.size(); ++i) types << InputPin;
    for (int i = 0; i < m_outputPins.size(); ++i) types << OutputPin;
    return types;
}

QJsonObject SystemVerilogBlockItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["svFilePath"] = m_svFilePath;
    j["moduleName"] = m_moduleName;

    QJsonArray inArr;
    for (const auto& p : m_inputPins) inArr.append(p);
    j["inputs"] = inArr;

    QJsonArray outArr;
    for (const auto& p : m_outputPins) outArr.append(p);
    j["outputs"] = outArr;

    return j;
}

bool SystemVerilogBlockItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_svFilePath = json.value("svFilePath").toString();
    m_moduleName = json.value("moduleName").toString();

    m_inputPins.clear();
    for (const auto& v : json.value("inputs").toArray())
        m_inputPins << v.toString();

    m_outputPins.clear();
    for (const auto& v : json.value("outputs").toArray())
        m_outputPins << v.toString();

    if (m_inputPins.isEmpty()) m_inputPins << "In1";
    if (m_outputPins.isEmpty()) m_outputPins << "Out1";

    // Restore value() and paramExpressions for simulation bridge detection
    if (!m_svFilePath.isEmpty()) {
        SchematicItem::setValue(m_svFilePath);
        setParamExpression("systemVerilogFile", m_svFilePath);
        setParamExpression("systemVerilogModule", m_moduleName);
        setProperty("systemVerilogFile", m_svFilePath);
        setProperty("systemVerilogModule", m_moduleName);
    }

    updateSize();
    rebuildPrimitives();
    return true;
}

SchematicItem* SystemVerilogBlockItem::clone() const {
    auto* item = new SystemVerilogBlockItem(pos(), parentItem());
    item->m_svFilePath = m_svFilePath;
    item->m_moduleName = m_moduleName;
    item->m_inputPins = m_inputPins;
    item->m_outputPins = m_outputPins;
    if (!m_svFilePath.isEmpty()) {
        item->SchematicItem::setValue(m_svFilePath);
        item->setParamExpression("systemVerilogFile", m_svFilePath);
        item->setParamExpression("systemVerilogModule", m_moduleName);
        item->setProperty("systemVerilogFile", m_svFilePath);
        item->setProperty("systemVerilogModule", m_moduleName);
    }
    item->updateSize();
    return item;
}

void SystemVerilogBlockItem::rebuildPrimitives() {
    SchematicItem::rebuildPrimitives();
}
