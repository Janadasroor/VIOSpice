/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arduino_board_def.h"
#include <QLibrary>
#include <QCoreApplication>
#include <cstring>

// C API function pointer types matching VioAVR's c_api.h
struct CBoardPin {
    uint8_t arduino_pin;
    char port;
    uint8_t bit;
    char label[32];
    uint8_t supports_pwm;
    uint8_t supports_interrupt;
    int8_t analog_channel;
};

typedef int (*BoardCountFn)(void);
typedef const char* (*BoardNameFn)(int);
typedef const char* (*BoardMcuFn)(int);
typedef uint32_t (*BoardClockFn)(int);
typedef int (*BoardFullPinCountFn)(int);
typedef CBoardPin (*BoardFullPinFn)(int, int);

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
    auto fullPinCountFn = reinterpret_cast<BoardFullPinCountFn>(lib.resolve("vioavr_board_full_pin_count"));
    auto fullPinFn = reinterpret_cast<BoardFullPinFn>(lib.resolve("vioavr_board_full_pin"));
    auto fqbnFn = reinterpret_cast<BoardNameFn>(lib.resolve("vioavr_board_fqbn"));

    if (!countFn || !nameFn || !mcuFn || !clockFn || !fullPinCountFn || !fullPinFn) return db;

    int count = countFn();
    for (int i = 0; i < count; ++i) {
        const char* name = nameFn(i);
        const char* mcu = mcuFn(i);
        const char* fqbn = fqbnFn ? fqbnFn(i) : "";
        if (!name || !mcu) continue;

        QString qName = QString::fromLatin1(name);
        QString qMcu = QString::fromLatin1(mcu);

        ArduinoBoardDef b;
        b.boardName = qName;
        b.fqbn = fqbn ? QString::fromLatin1(fqbn) : "";
        b.mcuModel = qMcu;
        b.defaultClock = static_cast<double>(clockFn(i));

        // MCU-specific defaults
        if (qMcu == "ATmega328P") { b.flashBytes = 32256; b.sramBytes = 2048; }
        else if (qMcu == "ATmega2560") { b.flashBytes = 262144; b.sramBytes = 8192; }
        else if (qMcu == "ATmega32U4") { b.flashBytes = 28672; b.sramBytes = 2560; }
        else if (qMcu == "ATmega4809") { b.flashBytes = 49152; b.sramBytes = 6144; }
        else { b.flashBytes = 32256; b.sramBytes = 2048; }
        b.logicVoltage = 5.0;

        // USB detection
        b.hasUsbSerial = qName.contains("Leonardo") || qName.contains("Micro") ||
                          qName.contains("Yun") || qName.contains("Esplora") ||
                          qName.contains("Robot") || qName.contains("Gemma") ||
                          qName.contains("Circuit") || qName.contains("LilyPad USB");

        // LED pin
        b.ledPin = (qName.contains("Uno") || qName.contains("Nano") || qName.contains("Mega") ||
                     qName.contains("Leonardo") || qName.contains("Micro") || qName.contains("Pro Mini") ||
                     qName.contains("Ethernet") || qName.contains("BT") || qName.contains("WiFi")) ? 13 : -1;

        // Analog inputs
        b.analogInputs = (qMcu == "ATmega32U4") ? 12 : (qMcu == "ATmega2560") ? 16 : (qMcu == "ATmega4809") ? 8 : 6;

        // Build full pin list from VioAVR C API
        int pinCount = fullPinCountFn(i);
        for (int p = 0; p < pinCount; ++p) {
            CBoardPin cp = fullPinFn(i, p);
            ArduinoPinMapping pm;
            pm.digitalPin = cp.arduino_pin;
            // Reconstruct MCU pin name: port char + bit number
            char portChar = cp.port;
            // Fix: port field may be padded. Extract actual port letter.
            if (portChar >= 'A' && portChar <= 'F') {
                pm.mcuPin = QString("P%1%2").arg(portChar).arg(cp.bit);
            } else {
                pm.mcuPin = QString("PD%1").arg(cp.bit); // fallback
            }
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
