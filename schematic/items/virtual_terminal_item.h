#ifndef VIRTUAL_TERMINAL_ITEM_H
#define VIRTUAL_TERMINAL_ITEM_H

#include "schematic_item.h"

class VirtualTerminalItem : public SchematicItem {
public:
    explicit VirtualTerminalItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "VirtualTerminalInstrument"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "TERM"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    enum class BaudRate {
        B9600 = 9600,
        B19200 = 19200,
        B38400 = 38400,
        B57600 = 57600,
        B115200 = 115200
    };

    struct Config {
        int baudRate = 9600;
        int dataBits = 8;
        QString parity = "None"; // None, Even, Odd
        double stopBits = 1.0;
        bool hexMode = false;
        bool autoScroll = true;
    };

    Config config() const { return m_config; }
    void setConfig(const Config& cfg);

    int baudRate() const { return m_config.baudRate; }
    void setBaudRate(int baud) { m_config.baudRate = baud; update(); }

    void setPendingTxWaveform(const QVector<QPair<double, double>>& waveform) { m_pendingTxWaveform = waveform; }
    QVector<QPair<double, double>> pendingTxWaveform() const { return m_pendingTxWaveform; }
    void clearPendingTxWaveform() { m_pendingTxWaveform.clear(); }
    bool hasPendingTxData() const { return !m_pendingTxWaveform.isEmpty(); }

private:
    Config m_config;
    QVector<QPair<double, double>> m_pendingTxWaveform;
};

#endif // VIRTUAL_TERMINAL_ITEM_H
