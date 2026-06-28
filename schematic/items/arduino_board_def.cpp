/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arduino_board_def.h"
#include <QLibrary>
#include <QCoreApplication>
#include <cstring>

typedef int (*BoardCountFn)(void);
typedef const char* (*BoardNameFn)(int);
typedef const char* (*BoardMcuFn)(int);
typedef uint32_t (*BoardClockFn)(int);
typedef int (*BoardPinCountFn)(int);

struct CBoardPin {
    uint8_t arduino_pin;
    char port;
    uint8_t bit;
    char label[32];
    uint8_t supports_pwm;
    uint8_t supports_interrupt;
    int8_t analog_channel;
};

typedef CBoardPin (*BoardPinFn)(int, int);

static QMap<QString, ArduinoBoardDef> queryVioAVRBoards() {
    QMap<QString, ArduinoBoardDef> db;

    QLibrary lib("avr_cosim");
    if (!lib.load()) {
        lib.setFileName(QCoreApplication::applicationDirPath() + "/../build/libavr_cosim");
        if (!lib.load()) return db;
    }

    auto countFn = reinterpret_cast<BoardCountFn>(lib.resolve("vioavr_board_count"));
    auto nameFn = reinterpret_cast<BoardNameFn>(lib.resolve("vioavr_board_name"));
    auto mcuFn = reinterpret_cast<BoardMcuFn>(lib.resolve("vioavr_board_mcu"));
    auto clockFn = reinterpret_cast<BoardClockFn>(lib.resolve("vioavr_board_clock"));
    auto pinCountFn = reinterpret_cast<BoardPinCountFn>(lib.resolve("vioavr_board_pin_count"));
    auto pinFn = reinterpret_cast<BoardPinFn>(lib.resolve("vioavr_board_pin"));
    auto fqbnFn = reinterpret_cast<BoardNameFn>(lib.resolve("vioavr_board_fqbn"));

    if (!countFn || !nameFn || !mcuFn || !clockFn || !pinCountFn || !pinFn) return db;

    int count = countFn();
    for (int i = 0; i < count; ++i) {
        const char* name = nameFn(i);
        const char* mcu = mcuFn(i);
        const char* fqbn = fqbnFn ? fqbnFn(i) : "";
        if (!name || !mcu) continue;

        QString qName = QString::fromLatin1(name);
        QString qMcu = QString::fromLatin1(mcu);
        QString qFqbn = fqbn ? QString::fromLatin1(fqbn) : "";

        ArduinoBoardDef b;
        b.boardName = qName;
        b.fqbn = qFqbn;
        b.mcuModel = qMcu;
        b.defaultClock = static_cast<double>(clockFn(i));

        // Determine MCU-specific defaults
        if (qMcu == "ATmega328P") { b.flashBytes = 32256; b.sramBytes = 2048; b.logicVoltage = 5.0; }
        else if (qMcu == "ATmega2560") { b.flashBytes = 262144; b.sramBytes = 8192; b.logicVoltage = 5.0; }
        else if (qMcu == "ATmega32U4") { b.flashBytes = 28672; b.sramBytes = 2560; b.logicVoltage = 5.0; }
        else if (qMcu == "ATmega4809") { b.flashBytes = 49152; b.sramBytes = 6144; b.logicVoltage = 5.0; }
        else if (qMcu == "ATmega8") { b.flashBytes = 8192; b.sramBytes = 1024; b.logicVoltage = 5.0; }
        else if (qMcu == "ATmega168") { b.flashBytes = 16384; b.sramBytes = 1024; b.logicVoltage = 5.0; }
        else { b.flashBytes = 32256; b.sramBytes = 2048; b.logicVoltage = 5.0; }

        // USB detection from board name
        if (qName.contains("Leonardo") || qName.contains("Micro") || qName.contains("Yun") ||
            qName.contains("Esplora") || qName.contains("Robot") || qName.contains("Gemma") ||
            qName.contains("Circuit") || qName.contains("LilyPad USB") || qMcu.contains("32U4")) {
            b.hasUsbSerial = true;
        } else {
            b.hasUsbSerial = false;
        }

        // LED pin defaults
        if (qName.contains("Uno") || qName.contains("Nano") || qName.contains("Mega") ||
            qName.contains("Leonardo") || qName.contains("Micro") || qName.contains("Pro Mini") ||
            qName.contains("Ethernet") || qName.contains("BT") || qName.contains("WiFi")) {
            b.ledPin = 13;
        } else {
            b.ledPin = -1;
        }
        b.analogInputs = (qMcu == "ATmega32U4") ? 12 : (qMcu == "ATmega2560") ? 16 : (qMcu == "ATmega4809") ? 8 : 6;

        // Build pin list from VioAVR
        int pinCount = pinCountFn(i);
        for (int p = 0; p < pinCount; ++p) {
            CBoardPin cp = pinFn(i, p);
            ArduinoPinMapping pm;
            pm.digitalPin = cp.arduino_pin;
            pm.mcuPin = QString("%1%2").arg(QChar(cp.port)).arg(cp.bit);
            pm.label = QString::fromLatin1(cp.label);
            pm.supportsPWM = cp.supports_pwm;
            pm.supportsInterrupt = cp.supports_interrupt;
            pm.analogChannel = cp.analog_channel;
            b.pins.append(pm);
        }

        db[qName] = b;
    }
    return db;
}

const QMap<QString, ArduinoBoardDef>& arduinoBoardDatabase() {
    static const auto db = queryVioAVRBoards();
    return db;
}
