#include "system_verilog_block_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
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
    qreal height = std::max(60.0, maxPins * 20.0 + 20.0);
    const QSizeF newSize(120, height);
    if (m_size == newSize) return;
    prepareGeometryChange();
    m_size = newSize;
}

QRectF SystemVerilogBlockItem::boundingRect() const {
    return QRectF(-m_size.width() / 2 - 10, -m_size.height() / 2,
                  m_size.width() + 20, m_size.height());
}

void SystemVerilogBlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect(-m_size.width() / 2, -m_size.height() / 2,
                m_size.width(), m_size.height());

    QPen bodyPen(Qt::white, 2);
    if (isSelected()) bodyPen.setColor(QColor(99, 102, 241));
    painter->setPen(bodyPen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRoundedRect(rect, 4, 4);

    painter->setPen(QColor(16, 185, 129));
    QFont titleFont("Inter", 7, QFont::Bold);
    painter->setFont(titleFont);
    QString label = "SV";
    if (!m_moduleName.isEmpty()) label += ": " + m_moduleName;
    painter->drawText(rect.adjusted(5, 5, -5, -m_size.height() + 15),
                      Qt::AlignCenter, label);

    QFont pinFont("Inter", 7);
    painter->setFont(pinFont);
    painter->setPen(QPen(Qt::white, 1.5));

    qreal startY = -m_size.height() / 2 + 20;
    for (int i = 0; i < m_inputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->drawLine(-m_size.width() / 2 - 10, y, -m_size.width() / 2, y);
        painter->drawText(QRectF(-m_size.width() / 2 + 5, y - 8, 50, 16),
                          Qt::AlignLeft | Qt::AlignVCenter, m_inputPins[i]);
    }

    for (int i = 0; i < m_outputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->drawLine(m_size.width() / 2, y, m_size.width() / 2 + 10, y);
        painter->drawText(QRectF(m_size.width() / 2 - 55, y - 8, 50, 16),
                          Qt::AlignRight | Qt::AlignVCenter, m_outputPins[i]);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> SystemVerilogBlockItem::connectionPoints() const {
    QList<QPointF> pts;
    qreal startY = -m_size.height() / 2 + 20;
    for (int i = 0; i < m_inputPins.size(); ++i)
        pts << QPointF(-m_size.width() / 2 - 10, startY + i * 20);
    for (int i = 0; i < m_outputPins.size(); ++i)
        pts << QPointF(m_size.width() / 2 + 10, startY + i * 20);
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
