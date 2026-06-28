/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "avr_microcontroller_item.h"
#include "arduino_board_def.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyleOptionGraphicsItem>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <QLibrary>
#include <QCoreApplication>

// ─── MCU Database ────────────────────────────────────────────────────────────

typedef int (*DeviceCountFn)(void);
typedef const char* (*DeviceNameFn)(int);

static void queryVioAVRDevices(QMap<QString, AvrMcuDef>& db) {
    QLibrary lib("avr_cosim");
    if (!lib.load()) {
        lib.setFileName(QCoreApplication::applicationDirPath() + "/../build/libavr_cosim");
        if (!lib.load()) return;
    }

    auto countFn = reinterpret_cast<DeviceCountFn>(lib.resolve("vioavr_device_count"));
    auto nameFn = reinterpret_cast<DeviceNameFn>(lib.resolve("vioavr_device_name"));
    if (!countFn || !nameFn) return;

    int count = countFn();
    auto makePin = [](const QString& name, AvrPinDef::Direction dir) -> AvrPinDef {
        return {name, dir};
    };

    for (int i = 0; i < count; ++i) {
        const char* name = nameFn(i);
        if (!name) continue;
        QString qName = QString::fromLatin1(name);
        if (db.contains(qName)) continue;

        // Generic pin layout for unknown MCUs — 32 GPIO pins
        AvrMcuDef m;
        m.name = qName;
        m.pinCount = 32;
        m.ports = {"PA", "PB", "PC", "PD"};
        m.pinsPerPort = 8;
        m.defaultClock = 16000000;
        m.pins = {
            makePin("VCC",  AvrPinDef::Power),
            makePin("GND",  AvrPinDef::Ground),
        };
        for (int port = 0; port < 4; ++port) {
            for (int bit = 0; bit < 8; ++bit) {
                m.pins.append(makePin(
                    QString("%1%2").arg(QChar('A' + port)).arg(bit),
                    AvrPinDef::Bidirectional));
            }
        }
        m.pins.append(makePin("AVCC", AvrPinDef::Power));
        m.pins.append(makePin("AREF", AvrPinDef::Input));
        db[qName] = m;
    }
}

// ─── MCU Database ────────────────────────────────────────────────────────────

static QMap<QString, AvrMcuDef> buildMcuDB() {
    QMap<QString, AvrMcuDef> db;

    auto makePin = [](const QString& name, AvrPinDef::Direction dir) -> AvrPinDef {
        return {name, dir};
    };

    // ATmega328P — 28-pin DIP
    {
        AvrMcuDef m;
        m.name = "ATmega328P";
        m.pinCount = 28;
        m.ports = {"PC", "PD", "PB", "PC"};
        m.pinsPerPort = 8;
        m.defaultClock = 16000000;
        // DIP-28 pinout (physical pin order)
        m.pins = {
            makePin("VCC",  AvrPinDef::Power),
            makePin("GND",  AvrPinDef::Ground),
            makePin("PC6",  AvrPinDef::Bidirectional),  // RESET
            makePin("PD0",  AvrPinDef::Bidirectional),  // RXD
            makePin("PD1",  AvrPinDef::Bidirectional),  // TXD
            makePin("PD2",  AvrPinDef::Bidirectional),  // INT0
            makePin("PD3",  AvrPinDef::Bidirectional),  // INT1/OC2B
            makePin("PD4",  AvrPinDef::Bidirectional),
            makePin("PB6",  AvrPinDef::Input),           // XTAL1
            makePin("PB7",  AvrPinDef::Input),           // XTAL2
            makePin("PD5",  AvrPinDef::Bidirectional),  // OC0B
            makePin("PD6",  AvrPinDef::Bidirectional),  // AIN0
            makePin("PD7",  AvrPinDef::Bidirectional),  // AIN1
            makePin("PB0",  AvrPinDef::Bidirectional),  // ICP1/CLKO
            makePin("PB1",  AvrPinDef::Bidirectional),  // OC1A
            makePin("PB2",  AvrPinDef::Bidirectional),  // SS/OC1B
            makePin("PB3",  AvrPinDef::Bidirectional),  // MOSI/OC2A
            makePin("PB4",  AvrPinDef::Bidirectional),  // MISO
            makePin("PB5",  AvrPinDef::Bidirectional),  // SCK
            makePin("AVCC", AvrPinDef::Power),
            makePin("AREF", AvrPinDef::Input),
            makePin("GND",  AvrPinDef::Ground),
            makePin("PC0",  AvrPinDef::AnalogInOut),    // ADC0
            makePin("PC1",  AvrPinDef::AnalogInOut),    // ADC1
            makePin("PC2",  AvrPinDef::AnalogInOut),    // ADC2
            makePin("PC3",  AvrPinDef::AnalogInOut),    // ADC3
            makePin("PC4",  AvrPinDef::AnalogInOut),    // ADC4/SDA
            makePin("PC5",  AvrPinDef::AnalogInOut),    // ADC5/SCL
        };
        db["ATmega328P"] = m;
    }

    // ATmega2560 — 100-pin TQFP (simplified, key pins)
    {
        AvrMcuDef m;
        m.name = "ATmega2560";
        m.pinCount = 100;
        m.ports = {"PE", "PB", "PG", "PD", "PH", "PB", "PG", "PD", "PJ", "PD", "PB", "PH", "PG", "PB", "PJ", "PD", "PE", "PC", "PH", "PE"};
        m.pinsPerPort = 8;
        m.defaultClock = 16000000;
        // Simplified pin list — just power, ground, and port groups
        m.pins = {
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PE0",  AvrPinDef::Bidirectional), makePin("PE1",  AvrPinDef::Bidirectional),
            makePin("PE2",  AvrPinDef::Bidirectional), makePin("PE3",  AvrPinDef::Bidirectional),
            makePin("PE4",  AvrPinDef::Bidirectional), makePin("PE5",  AvrPinDef::Bidirectional),
            makePin("PE6",  AvrPinDef::Bidirectional), makePin("PE7",  AvrPinDef::Bidirectional),
            makePin("PB0",  AvrPinDef::Bidirectional), makePin("PB1",  AvrPinDef::Bidirectional),
            makePin("PB2",  AvrPinDef::Bidirectional), makePin("PB3",  AvrPinDef::Bidirectional),
            makePin("PB4",  AvrPinDef::Bidirectional), makePin("PB5",  AvrPinDef::Bidirectional),
            makePin("PB6",  AvrPinDef::Bidirectional), makePin("PB7",  AvrPinDef::Bidirectional),
            makePin("PG0",  AvrPinDef::Bidirectional), makePin("PG1",  AvrPinDef::Bidirectional),
            makePin("PD0",  AvrPinDef::Bidirectional), makePin("PD1",  AvrPinDef::Bidirectional),
            makePin("PD2",  AvrPinDef::Bidirectional), makePin("PD3",  AvrPinDef::Bidirectional),
            makePin("PD4",  AvrPinDef::Bidirectional), makePin("PD5",  AvrPinDef::Bidirectional),
            makePin("PD6",  AvrPinDef::Bidirectional), makePin("PD7",  AvrPinDef::Bidirectional),
            makePin("PH0",  AvrPinDef::Bidirectional), makePin("PH1",  AvrPinDef::Bidirectional),
            makePin("PH2",  AvrPinDef::Bidirectional), makePin("PH3",  AvrPinDef::Bidirectional),
            makePin("PH4",  AvrPinDef::Bidirectional), makePin("PH5",  AvrPinDef::Bidirectional),
            makePin("PH6",  AvrPinDef::Bidirectional), makePin("PH7",  AvrPinDef::Bidirectional),
            makePin("PB0",  AvrPinDef::Bidirectional), makePin("PB1",  AvrPinDef::Bidirectional),
            makePin("PG2",  AvrPinDef::Bidirectional), makePin("PD7",  AvrPinDef::Bidirectional),
            makePin("PG3",  AvrPinDef::Bidirectional), makePin("PG4",  AvrPinDef::Bidirectional),
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PH5",  AvrPinDef::Bidirectional), makePin("PB6",  AvrPinDef::Bidirectional),
            makePin("PB7",  AvrPinDef::Bidirectional), makePin("PG5",  AvrPinDef::Bidirectional),
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PJ0",  AvrPinDef::Bidirectional), makePin("PJ1",  AvrPinDef::Bidirectional),
            makePin("PJ2",  AvrPinDef::Bidirectional), makePin("PJ3",  AvrPinDef::Bidirectional),
            makePin("PJ4",  AvrPinDef::Bidirectional), makePin("PJ5",  AvrPinDef::Bidirectional),
            makePin("PJ6",  AvrPinDef::Bidirectional), makePin("PJ7",  AvrPinDef::Bidirectional),
            makePin("PD0",  AvrPinDef::Bidirectional), makePin("PD1",  AvrPinDef::Bidirectional),
            makePin("PD2",  AvrPinDef::Bidirectional), makePin("PD3",  AvrPinDef::Bidirectional),
            makePin("PD4",  AvrPinDef::Bidirectional), makePin("PD5",  AvrPinDef::Bidirectional),
            makePin("PD6",  AvrPinDef::Bidirectional), makePin("PD7",  AvrPinDef::Bidirectional),
            makePin("PE0",  AvrPinDef::Bidirectional), makePin("PE1",  AvrPinDef::Bidirectional),
            makePin("PE2",  AvrPinDef::Bidirectional), makePin("PE3",  AvrPinDef::Bidirectional),
            makePin("PE4",  AvrPinDef::Bidirectional), makePin("PE5",  AvrPinDef::Bidirectional),
            makePin("PE6",  AvrPinDef::Bidirectional), makePin("PE7",  AvrPinDef::Bidirectional),
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PC0",  AvrPinDef::AnalogInOut), makePin("PC1",  AvrPinDef::AnalogInOut),
            makePin("PC2",  AvrPinDef::AnalogInOut), makePin("PC3",  AvrPinDef::AnalogInOut),
            makePin("PC4",  AvrPinDef::AnalogInOut), makePin("PC5",  AvrPinDef::AnalogInOut),
            makePin("PC6",  AvrPinDef::AnalogInOut), makePin("PC7",  AvrPinDef::AnalogInOut),
            makePin("PA0",  AvrPinDef::AnalogInOut), makePin("PA1",  AvrPinDef::AnalogInOut),
            makePin("PA2",  AvrPinDef::AnalogInOut), makePin("PA3",  AvrPinDef::AnalogInOut),
            makePin("PA4",  AvrPinDef::AnalogInOut), makePin("PA5",  AvrPinDef::AnalogInOut),
            makePin("PA6",  AvrPinDef::AnalogInOut), makePin("PA7",  AvrPinDef::AnalogInOut),
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("AREF", AvrPinDef::Input),
        };
        db["ATmega2560"] = m;
    }

    // ATtiny85 — 8-pin DIP
    {
        AvrMcuDef m;
        m.name = "ATtiny85";
        m.pinCount = 8;
        m.ports = {"PB"};
        m.pinsPerPort = 8;
        m.defaultClock = 8000000;
        m.pins = {
            makePin("PB5",  AvrPinDef::Bidirectional),  // RESET/ADC0
            makePin("PB3",  AvrPinDef::Bidirectional),  // ADC3/OC1B
            makePin("PB4",  AvrPinDef::Bidirectional),  // ADC2/OC1B
            makePin("GND",  AvrPinDef::Ground),
            makePin("PB0",  AvrPinDef::Bidirectional),  // MOSI/OC0A/AIN0
            makePin("PB1",  AvrPinDef::Bidirectional),  // MISO/OC0B/AIN1/OC1A
            makePin("PB2",  AvrPinDef::Bidirectional),  // SCK/ADC1/T0/INT0
            makePin("VCC",  AvrPinDef::Power),
        };
        db["ATtiny85"] = m;
    }

    // ATmega4809 — 48-pin (megaAVR-0 series, Arduino Nano Every)
    {
        AvrMcuDef m;
        m.name = "ATmega4809";
        m.pinCount = 48;
        m.ports = {"PA", "PB", "PC", "PD", "PE", "PF"};
        m.pinsPerPort = 8;
        m.defaultClock = 20000000;
        m.pins = {
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PA0",  AvrPinDef::AnalogInOut), makePin("PA1",  AvrPinDef::AnalogInOut),
            makePin("PA2",  AvrPinDef::AnalogInOut), makePin("PA3",  AvrPinDef::AnalogInOut),
            makePin("PA4",  AvrPinDef::AnalogInOut), makePin("PA5",  AvrPinDef::AnalogInOut),
            makePin("PA6",  AvrPinDef::AnalogInOut), makePin("PA7",  AvrPinDef::AnalogInOut),
            makePin("PB0",  AvrPinDef::Bidirectional), makePin("PB1",  AvrPinDef::Bidirectional),
            makePin("PB2",  AvrPinDef::Bidirectional), makePin("PB3",  AvrPinDef::Bidirectional),
            makePin("PB4",  AvrPinDef::Bidirectional), makePin("PB5",  AvrPinDef::Bidirectional),
            makePin("PB6",  AvrPinDef::Bidirectional), makePin("PB7",  AvrPinDef::Bidirectional),
            makePin("PC0",  AvrPinDef::AnalogInOut), makePin("PC1",  AvrPinDef::AnalogInOut),
            makePin("PC2",  AvrPinDef::AnalogInOut), makePin("PC3",  AvrPinDef::AnalogInOut),
            makePin("PC4",  AvrPinDef::AnalogInOut), makePin("PC5",  AvrPinDef::AnalogInOut),
            makePin("PC6",  AvrPinDef::Bidirectional), makePin("PC7",  AvrPinDef::Bidirectional),
            makePin("PD0",  AvrPinDef::Bidirectional), makePin("PD1",  AvrPinDef::Bidirectional),
            makePin("PD2",  AvrPinDef::Bidirectional), makePin("PD3",  AvrPinDef::Bidirectional),
            makePin("PD4",  AvrPinDef::Bidirectional), makePin("PD5",  AvrPinDef::Bidirectional),
            makePin("PD6",  AvrPinDef::Bidirectional), makePin("PD7",  AvrPinDef::Bidirectional),
            makePin("PE0",  AvrPinDef::Bidirectional), makePin("PE1",  AvrPinDef::Bidirectional),
            makePin("PE2",  AvrPinDef::Bidirectional), makePin("PE3",  AvrPinDef::Bidirectional),
            makePin("PE4",  AvrPinDef::Bidirectional), makePin("PE5",  AvrPinDef::Bidirectional),
            makePin("VCCIO",AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
            makePin("PF0",  AvrPinDef::AnalogInOut), makePin("PF1",  AvrPinDef::AnalogInOut),
            makePin("PF2",  AvrPinDef::AnalogInOut), makePin("PF3",  AvrPinDef::AnalogInOut),
            makePin("PF4",  AvrPinDef::AnalogInOut), makePin("PF5",  AvrPinDef::AnalogInOut),
            makePin("VCC",  AvrPinDef::Power), makePin("GND",  AvrPinDef::Ground),
        };
        db["ATmega4809"] = m;
    }

    return db;
}

const QMap<QString, AvrMcuDef>& AvrMicrocontrollerItem::mcuDatabase() {
    static auto db = buildMcuDB();
    static bool queried = false;
    if (!queried) {
        queried = true;
        queryVioAVRDevices(db);
    }
    return db;
}

// ─── Constructor ─────────────────────────────────────────────────────────────

AvrMicrocontrollerItem::AvrMicrocontrollerItem(QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_size(140, 80) {
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    buildPinList();
    setReference("UAVR1");
    setName("AVR Microcontroller");
    setParamExpression("avrModel", m_mcuModel);
    setParamExpression("clockFrequency", QString::number(m_clockFrequency));
    setParamExpression("jitEnabled", "1");
    setParamExpression("adcVoltage", "5.0");
}

AvrMicrocontrollerItem::AvrMicrocontrollerItem(const QString& mcuModel, QGraphicsItem* parent)
    : AvrMicrocontrollerItem(parent) {
    setMcuModel(mcuModel);
}

// ─── MCU Model ───────────────────────────────────────────────────────────────

void AvrMicrocontrollerItem::setMcuModel(const QString& model) {
    if (!mcuDatabase().contains(model)) return;
    m_mcuModel = model;
    setParamExpression("avrModel", model);
    buildPinList();
    updateSize();
    update();
}

void AvrMicrocontrollerItem::buildPinList() {
    auto it = mcuDatabase().find(m_mcuModel);
    if (it != mcuDatabase().end()) {
        m_pinList = it->pins;
    } else {
        m_pinList.clear();
    }
}

void AvrMicrocontrollerItem::updateSize() {
    int pinCount = m_pinList.size();
    int halfPins = (pinCount + 1) / 2;
    qreal height = std::max(80.0, halfPins * 16.0 + 40.0);
    const QSizeF newSize(140, height);
    if (m_size == newSize) return;
    prepareGeometryChange();
    m_size = newSize;
}

void AvrMicrocontrollerItem::setFirmwarePath(const QString& path) {
    m_firmwarePath = path;
    SchematicItem::setValue(path);
    setParamExpression("firmwarePath", path);
    update();
}

void AvrMicrocontrollerItem::setBoardType(const QString& board) {
    const auto& db = arduinoBoardDatabase();
    if (!db.contains(board)) {
        m_boardType.clear();
        m_isArduinoMode = false;
        return;
    }
    m_boardType = board;
    m_isArduinoMode = true;
    const auto& def = db[board];

    setMcuModel(def.mcuModel);
    setClockFrequency(def.defaultClock);

    // Build lookup: ext_id -> Arduino alias
    QMap<int, QString> aliasMap;
    QMap<int, int> adcMap;
    for (const auto& p : def.pins) {
        if (p.mcuPin.length() >= 3) {
            QChar portLetter = p.mcuPin[1];
            int portIdx = portLetter.toUpper().toLatin1() - 'A';
            if (portIdx >= 0 && portIdx < 4) {
                bool ok;
                int bitNum = p.mcuPin.mid(2).toInt(&ok);
                if (ok) {
                    int extId = portIdx * 8 + bitNum;
                    aliasMap[extId] = p.label.isEmpty() ? QString("D%1").arg(p.digitalPin) : p.label;
                    if (p.analogChannel >= 0) adcMap[extId] = p.analogChannel;
                }
            }
        }
    }

    // Build full 32-pin list: Arduino pins first, then remaining MCU pins
    static const QStringList portNames = {"PA", "PB", "PC", "PD"};
    m_pinList.clear();
    QMap<int, QString> usedExtIds;

    // Arduino pins in order (D0, D1, D2, ...)
    for (int i = 0; i < 40; ++i) {
        bool found = false;
        for (const auto& p : def.pins) {
            if (p.digitalPin == i) {
                AvrPinDef pin;
                pin.name = p.label.isEmpty() ? QString("D%1").arg(i) : p.label;
                pin.dir = (p.analogChannel >= 0) ? AvrPinDef::AnalogInOut : AvrPinDef::Bidirectional;
                m_pinList.append(pin);
                if (p.mcuPin.length() >= 3) {
                    int portIdx = p.mcuPin[1].toUpper().toLatin1() - 'A';
                    if (portIdx >= 0 && portIdx < 4) {
                        int bitNum = p.mcuPin.mid(2).toInt();
                        usedExtIds[portIdx * 8 + bitNum] = pin.name;
                    }
                }
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    // Remaining MCU pins
    for (int port = 0; port < 4; ++port) {
        for (int bit = 0; bit < 8; ++bit) {
            int extId = port * 8 + bit;
            if (!usedExtIds.contains(extId)) {
                AvrPinDef pin;
                pin.name = QString("%1%2").arg(portNames[port]).arg(bit);
                pin.dir = AvrPinDef::Bidirectional;
                m_pinList.append(pin);
            }
        }
    }

    updateSize();
    setParamExpression("boardType", board);
    update();
}

// ─── Geometry ────────────────────────────────────────────────────────────────

QRectF AvrMicrocontrollerItem::boundingRect() const {
    return QRectF(-m_size.width() / 2 - 20, -m_size.height() / 2,
                  m_size.width() + 40, m_size.height());
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void AvrMicrocontrollerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    const QColor textColor = theme ? theme->textColor() : QColor(244, 244, 245);
    const QColor lineColor = theme ? theme->schematicLine() : QColor(200, 200, 210);
    const bool isDark = theme ? theme->type() == PCBTheme::Dark : true;

    QRectF rect(-m_size.width() / 2, -m_size.height() / 2,
                m_size.width(), m_size.height());

    constexpr qreal PIN_TAIL = 20;

    // Background Gradient (adapts to theme)
    QLinearGradient bgGrad(rect.topLeft(), rect.bottomLeft());
    if (isDark) {
        bgGrad.setColorAt(0, QColor(45, 45, 50));
        bgGrad.setColorAt(1, QColor(30, 30, 35));
    } else {
        bgGrad.setColorAt(0, QColor(240, 240, 242));
        bgGrad.setColorAt(1, QColor(225, 225, 228));
    }

    if (isSelected()) {
        if (isDark) {
            bgGrad.setColorAt(0, QColor(50, 90, 60));
            bgGrad.setColorAt(1, QColor(30, 50, 35));
        } else {
            bgGrad.setColorAt(0, QColor(180, 220, 190));
            bgGrad.setColorAt(1, QColor(150, 200, 160));
        }
    }

    painter->setBrush(bgGrad);
    painter->setPen(QPen(lineColor, 1.5));
    painter->drawRoundedRect(rect, 6, 6);

    // Header Accent (Green — distinguishes AVR from Blue XSPICE, Purple SV)
    painter->setBrush(QColor(34, 197, 94));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRectF(rect.left(), rect.top(), rect.width(), 14), 6, 6);
    painter->fillRect(QRectF(rect.left(), rect.top() + 10, rect.width(), 4), QColor(34, 197, 94));

    // Center "OLED" Display Area
    QRectF displayRect = QRectF(-m_size.width()/2 + 10, -14, m_size.width() - 20, 28);
    painter->setBrush(QColor(10, 14, 10));
    painter->setPen(QPen(QColor(34, 197, 94, 80), 1));
    painter->drawRect(displayRect);

    // Subtle Glow
    QRadialGradient glow(0, 0, 40);
    glow.setColorAt(0, QColor(34, 197, 94, 30));
    glow.setColorAt(1, Qt::transparent);
    painter->setBrush(glow);
    painter->setPen(Qt::NoPen);
    painter->drawRect(displayRect);

    // MCU model name in Display
    QString displayName;
    if (m_isArduinoMode && !m_boardType.isEmpty()) {
        displayName = m_boardType;
    } else {
        displayName = m_mcuModel;
    }
    if (displayName.isEmpty()) displayName = "AVR MCU";
    painter->setPen(QColor(34, 197, 94));
    QFont f("Monospace", 9, QFont::Bold);
    painter->setFont(f);
    painter->drawText(displayRect, Qt::AlignCenter, displayName.toUpper());

    // Firmware name below model (if set)
    if (!m_firmwarePath.isEmpty()) {
        QFont smallFont("Inter", 6);
        painter->setFont(smallFont);
        painter->setPen(QColor(150, 170, 150));
        QString fwName = m_firmwarePath;
        if (fwName.contains('/')) fwName = fwName.section('/', -1);
        if (fwName.length() > 20) fwName = fwName.left(17) + "...";
        QRectF fwRect = QRectF(-m_size.width()/2 + 10, displayRect.bottom() + 2,
                               m_size.width() - 20, 14);
        painter->drawText(fwRect, Qt::AlignCenter, fwName);
    }

    // Pins — split left (power/control) and right (GPIO/analog)
    QFont pinFont("Inter", 5);
    painter->setFont(pinFont);

    int leftCount = 0, rightCount = 0;
    for (const auto& pin : m_pinList) {
        if (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground) leftCount++;
        else rightCount++;
    }
    if (leftCount == 0) leftCount = 1;
    if (rightCount == 0) rightCount = 1;

    qreal leftStartY = -m_size.height() / 2 + 30;
    qreal rightStartY = -m_size.height() / 2 + 30;
    qreal leftSpacing = std::min(18.0, (m_size.height() - 40.0) / leftCount);
    qreal rightSpacing = std::min(16.0, (m_size.height() - 40.0) / rightCount);
    int leftIdx = 0, rightIdx = 0;

    for (const auto& pin : m_pinList) {
        bool isLeft = (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground);
        qreal x = isLeft ? -m_size.width() / 2 : m_size.width() / 2;
        qreal tailX = isLeft ? x - PIN_TAIL : x + PIN_TAIL;
        qreal y;
        if (isLeft) {
            y = leftStartY + leftIdx * leftSpacing;
            leftIdx++;
        } else {
            y = rightStartY + rightIdx * rightSpacing;
            rightIdx++;
        }

        // Pin line
        QColor pinColor = (pin.dir == AvrPinDef::Power)   ? QColor(255, 80, 80) :
                          (pin.dir == AvrPinDef::Ground)  ? QColor(100, 100, 105) :
                          (pin.dir == AvrPinDef::AnalogInOut) ? QColor(100, 180, 255) :
                                                                QColor(100, 100, 105);
        painter->setPen(QPen(pinColor, 1));
        painter->drawLine(QPointF(tailX, y), QPointF(x, y));

        // Label
        painter->setPen(textColor);
        QFont labelFont("Inter", 5);
        painter->setFont(labelFont);
        if (isLeft) {
            painter->drawText(QRectF(-m_size.width() / 2 - PIN_TAIL - 36, y - 7, 34, 14),
                              Qt::AlignRight | Qt::AlignVCenter, pin.name);
        } else {
            painter->drawText(QRectF(m_size.width() / 2 + PIN_TAIL + 2, y - 7, 34, 14),
                              Qt::AlignLeft | Qt::AlignVCenter, pin.name);
        }
    }

    // Chip notch (top center)
    painter->setPen(QPen(QColor(80, 80, 85), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(QRectF(-6, rect.top() - 3, 12, 12), 0, 180 * 16);

    drawConnectionPointHighlights(painter);
}

// ─── Pins ────────────────────────────────────────────────────────────────────

QList<QPointF> AvrMicrocontrollerItem::connectionPoints() const {
    QList<QPointF> pts;
    int leftCount = 0, rightCount = 0;
    for (const auto& pin : m_pinList) {
        if (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground) leftCount++;
        else rightCount++;
    }
    if (leftCount == 0) leftCount = 1;
    if (rightCount == 0) rightCount = 1;

    qreal leftStartY = -m_size.height() / 2 + 30;
    qreal rightStartY = -m_size.height() / 2 + 30;
    qreal leftSpacing = std::min(18.0, (m_size.height() - 40.0) / leftCount);
    qreal rightSpacing = std::min(16.0, (m_size.height() - 40.0) / rightCount);
    int leftIdx = 0, rightIdx = 0;

    for (const auto& pin : m_pinList) {
        bool isLeft = (pin.dir == AvrPinDef::Power || pin.dir == AvrPinDef::Ground);
        qreal y;
        if (isLeft) {
            y = leftStartY + leftIdx * leftSpacing;
            leftIdx++;
            pts << QPointF(-m_size.width() / 2 - 20, y);
        } else {
            y = rightStartY + rightIdx * rightSpacing;
            rightIdx++;
            pts << QPointF(m_size.width() / 2 + 20, y);
        }
    }
    return pts;
}

QString AvrMicrocontrollerItem::pinName(int index) const {
    if (index < 0 || index >= m_pinList.size()) return QString();
    return m_pinList[index].name;
}

QList<SchematicItem::PinElectricalType> AvrMicrocontrollerItem::pinElectricalTypes() const {
    QList<PinElectricalType> types;
    for (const auto& pin : m_pinList) {
        switch (pin.dir) {
            case AvrPinDef::Power:      types << PowerInputPin; break;
            case AvrPinDef::Ground:     types << PowerInputPin; break;
            case AvrPinDef::Input:      types << InputPin; break;
            case AvrPinDef::Output:     types << OutputPin; break;
            case AvrPinDef::AnalogIn:   types << InputPin; break;
            case AvrPinDef::AnalogInOut:types << BidirectionalPin; break;
            default:                    types << BidirectionalPin; break;
        }
    }
    return types;
}

// ─── Serialization ───────────────────────────────────────────────────────────

QJsonObject AvrMicrocontrollerItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["avrModel"] = m_mcuModel;
    j["firmwarePath"] = m_firmwarePath;
    j["clockFrequency"] = m_clockFrequency;
    j["jitEnabled"] = m_jitEnabled;
    j["adcVoltage"] = m_adcVoltage;
    if (!m_boardType.isEmpty()) j["boardType"] = m_boardType;
    return j;
}

bool AvrMicrocontrollerItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_mcuModel = json.value("avrModel").toString("ATmega328P");
    m_firmwarePath = json.value("firmwarePath").toString();
    m_clockFrequency = json.value("clockFrequency").toDouble(16000000);
    m_jitEnabled = json.value("jitEnabled").toBool(true);
    m_adcVoltage = json.value("adcVoltage").toDouble(5.0);
    QString boardType = json.value("boardType").toString();

    if (!boardType.isEmpty()) {
        setBoardType(boardType);
    } else {
        setParamExpression("avrModel", m_mcuModel);
        setParamExpression("firmwarePath", m_firmwarePath);
        setParamExpression("clockFrequency", QString::number(m_clockFrequency));
        setParamExpression("jitEnabled", m_jitEnabled ? "1" : "0");
        setParamExpression("adcVoltage", QString::number(m_adcVoltage, 'f', 1));
    }

    if (!m_firmwarePath.isEmpty()) {
        SchematicItem::setValue(m_firmwarePath);
        setProperty("firmwarePath", m_firmwarePath);
    }

    buildPinList();
    updateSize();
    rebuildPrimitives();
    return true;
}

SchematicItem* AvrMicrocontrollerItem::clone() const {
    auto* item = new AvrMicrocontrollerItem(parentItem());
    item->setPos(pos());
    item->m_mcuModel = m_mcuModel;
    item->m_firmwarePath = m_firmwarePath;
    item->m_clockFrequency = m_clockFrequency;
    item->m_jitEnabled = m_jitEnabled;
    item->m_adcVoltage = m_adcVoltage;
    item->m_boardType = m_boardType;
    item->m_isArduinoMode = m_isArduinoMode;
    if (!m_boardType.isEmpty()) {
        item->setBoardType(m_boardType);
    } else {
        item->setParamExpression("avrModel", m_mcuModel);
        item->setParamExpression("firmwarePath", m_firmwarePath);
        item->setParamExpression("clockFrequency", QString::number(m_clockFrequency));
        item->setParamExpression("jitEnabled", m_jitEnabled ? "1" : "0");
        item->setParamExpression("adcVoltage", QString::number(m_adcVoltage, 'f', 1));
    }
    if (!m_firmwarePath.isEmpty()) {
        item->SchematicItem::setValue(m_firmwarePath);
        item->setProperty("firmwarePath", m_firmwarePath);
    }
    item->buildPinList();
    item->updateSize();
    return item;
}

void AvrMicrocontrollerItem::rebuildPrimitives() {
    SchematicItem::rebuildPrimitives();
}
