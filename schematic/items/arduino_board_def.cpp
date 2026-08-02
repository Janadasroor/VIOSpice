/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arduino_board_def.h"
#include <QLibrary>
#include <QCoreApplication>
#include <cstring>

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

static QMap<QString, ArduinoBoardDef> buildFallbackDB() {
    QMap<QString, ArduinoBoardDef> db;
    auto p = [](int n, const QString& mcu, bool pwm, bool irq, int adc, const QString& l) -> ArduinoPinMapping {
        return {n, mcu, pwm, irq, adc, l};
    };

    // Uno / Nano / Pro Mini / Mini / Ethernet / Duemilanove / Fio / LilyPad / BT
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino Uno"; b.fqbn = "arduino:avr:uno"; b.mcuModel = "ATmega328P";
        b.defaultClock = 16000000; b.flashBytes = 32256; b.sramBytes = 2048;
        b.ledPin = 13; b.analogInputs = 6; b.hasUsbSerial = true; b.logicVoltage = 5.0;
        b.pins = {
            p(0,"PD0",false,true,-1,"RX"), p(1,"PD1",false,true,-1,"TX"),
            p(2,"PD2",false,true,-1,"INT0"), p(3,"PD3",true,true,-1,"INT1/OC2B"),
            p(4,"PD4",false,false,-1,""), p(5,"PD5",true,false,-1,"OC0B"),
            p(6,"PD6",true,false,-1,"OC0A"), p(7,"PD7",false,false,-1,""),
            p(8,"PB0",false,true,-1,"ICP1"), p(9,"PB1",true,false,-1,"OC1A"),
            p(10,"PB2",true,false,-1,"OC1B/SS"), p(11,"PB3",true,false,-1,"MOSI/OC2A"),
            p(12,"PB4",false,false,-1,"MISO"), p(13,"PB5",false,false,-1,"LED/SCK"),
            p(14,"PC0",false,false,0,"A0"), p(15,"PC1",false,false,1,"A1"),
            p(16,"PC2",false,false,2,"A2"), p(17,"PC3",false,false,3,"A3"),
            p(18,"PC4",false,false,4,"A4/SDA"), p(19,"PC5",false,false,5,"A5/SCL"),
        };
        db["Uno"] = b; db["Nano"] = b;
        ArduinoBoardDef pm = b; pm.boardName="Pro Mini 16MHz"; pm.fqbn="arduino:avr:pro:cpu=16MHz"; pm.hasUsbSerial=false; db["Pro Mini 16MHz"]=pm;
        ArduinoBoardDef pm8 = b; pm8.boardName="Pro Mini 8MHz"; pm8.defaultClock=8000000; pm8.hasUsbSerial=false; db["Pro Mini 8MHz"]=pm8;
    }
    // Mega
    {
        ArduinoBoardDef b;
        b.boardName="Arduino Mega 2560"; b.fqbn="arduino:avr:mega"; b.mcuModel="ATmega2560";
        b.defaultClock=16000000; b.flashBytes=262144; b.sramBytes=8192;
        b.ledPin=13; b.analogInputs=16; b.hasUsbSerial=true; b.logicVoltage=5.0;
        b.pins = {
            p(0,"PE0",false,true,-1,"RX0"), p(1,"PE1",false,true,-1,"TX0"),
            p(2,"PE4",false,true,-1,"INT4"), p(3,"PE5",false,true,-1,"INT5/OC3B"),
            p(4,"PG5",true,false,-1,"OC0B"), p(5,"PE3",true,false,-1,"OC3B"),
            p(6,"PH3",true,false,-1,"OC4A"), p(7,"PH4",true,false,-1,"OC4B"),
            p(8,"PH5",true,false,-1,"OC4C"), p(9,"PH6",true,false,-1,"OC2B"),
            p(10,"PB4",true,false,-1,"OC2A/SS"), p(11,"PB5",true,false,-1,"OC1A"),
            p(12,"PB6",true,false,-1,"OC1B"), p(13,"PB7",false,false,-1,"LED/OC0A"),
            p(14,"PJ1",false,true,-1,"TX3"), p(15,"PJ0",false,true,-1,"RX3"),
            p(16,"PH1",false,true,-1,"TX2"), p(17,"PH0",false,true,-1,"RX2"),
            p(18,"PD3",false,true,-1,"TX1/INT3"), p(19,"PD2",false,true,-1,"RX1/INT2"),
            p(20,"PD1",false,true,-1,"INT1/TXD0"), p(21,"PD0",false,true,-1,"INT0/RXD0"),
            p(22,"PA0",false,false,0,"A0"), p(23,"PA1",false,false,1,"A1"),
            p(24,"PA2",false,false,2,"A2"), p(25,"PA3",false,false,3,"A3"),
            p(26,"PA4",false,false,4,"A4"), p(27,"PA5",false,false,5,"A5"),
            p(28,"PA6",false,false,6,"A6"), p(29,"PA7",false,false,7,"A7"),
            p(30,"PC7",false,false,8,"A8"), p(31,"PC6",false,false,9,"A9"),
            p(32,"PC5",false,false,10,"A10/SDA"), p(33,"PC4",false,false,11,"A11/SCL"),
            p(34,"PC3",false,false,12,"A12"), p(35,"PC2",false,false,13,"A13"),
            p(36,"PC1",false,false,14,"A14"), p(37,"PC0",false,false,15,"A15"),
        };
        db["Mega 2560"] = b;
        ArduinoBoardDef madk = b; madk.boardName="Mega ADK"; madk.fqbn="arduino:avr:megaADK"; db["Mega ADK"]=madk;
    }
    // Leonardo / Micro / Esplora / Yun etc
    {
        ArduinoBoardDef b;
        b.boardName="Arduino Leonardo"; b.fqbn="arduino:avr:leonardo"; b.mcuModel="ATmega32U4";
        b.defaultClock=16000000; b.flashBytes=28672; b.sramBytes=2560;
        b.ledPin=13; b.analogInputs=12; b.hasUsbSerial=true; b.logicVoltage=5.0;
        b.pins = {
            p(0,"PD2",false,true,-1,"SCL/INT2"), p(1,"PD3",true,true,-1,"SDA/INT3"),
            p(2,"PD1",false,true,-1,"RX/INT1"), p(3,"PD0",false,true,-1,"TX/INT0"),
            p(4,"PD4",false,false,-1,""), p(5,"PC6",true,false,-1,"OC3A"),
            p(6,"PD7",true,false,-1,"OC0D"), p(7,"PE6",false,true,-1,"INT6/AIN0"),
            p(8,"PB4",false,false,-1,"SS"), p(9,"PB5",true,false,-1,"OC1A/OC4B"),
            p(10,"PB6",true,false,-1,"OC1B/OC4C"), p(11,"PB7",false,false,-1,"OC0A/OC1C"),
            p(12,"PD6",true,false,-1,"OC4D/AIN1"), p(13,"PC7",false,false,-1,"LED/OC4C"),
            p(14,"PB3",true,false,-1,"MOSI/OC2A"), p(15,"PB1",true,false,-1,"MISO/OC1A"),
            p(16,"PB2",true,false,-1,"SCK/OC1B"), p(17,"PB0",false,true,-1,"ICP1/CLKO"),
            p(18,"PF7",false,false,0,"A0"), p(19,"PF6",false,false,1,"A1"),
            p(20,"PF5",false,false,2,"A2"), p(21,"PF4",false,false,3,"A3"),
            p(22,"PF1",false,false,4,"A4"), p(23,"PF0",false,false,5,"A5"),
        };
        db["Leonardo"] = b;
        ArduinoBoardDef mi = b; mi.boardName="Micro"; mi.fqbn="arduino:avr:micro"; db["Micro"]=mi;
    }
    // Nano Every
    {
        ArduinoBoardDef b;
        b.boardName="Arduino Nano Every"; b.fqbn="arduino:megaavr:nona4809"; b.mcuModel="ATmega4809";
        b.defaultClock=20000000; b.flashBytes=49152; b.sramBytes=6144;
        b.ledPin=13; b.analogInputs=8; b.hasUsbSerial=true; b.logicVoltage=5.0;
        b.pins = {
            p(0,"PB0",false,true,-1,"RX"), p(1,"PB1",false,true,-1,"TX"),
            p(2,"PB2",false,true,-1,"INT0"), p(3,"PB3",false,true,-1,"INT1"),
            p(4,"PB4",false,false,-1,""), p(5,"PB5",false,false,-1,"LED"),
            p(8,"PC0",false,false,0,"A0"), p(9,"PC1",false,false,1,"A1"),
            p(10,"PC2",false,false,2,"A2"), p(11,"PC3",false,false,3,"A3"),
            p(12,"PC4",false,false,4,"A4/SDA"), p(13,"PC5",false,false,5,"A5/SCL"),
            p(14,"PD0",false,false,6,"A6"), p(15,"PD1",false,false,7,"A7"),
        };
        db["Nano Every"] = b;
    }
    return db;
}

static QMap<QString, ArduinoBoardDef> queryVioAVRBoards() {
    QMap<QString, ArduinoBoardDef> db;

    QLibrary lib("avr_cosim");
    if (!lib.load()) {
#ifdef Q_OS_WIN
        QString libName = "avr_cosim.dll";
#elif defined(Q_OS_MACOS)
        QString libName = "libavr_cosim.dylib";
#else
        QString libName = "libavr_cosim.so";
#endif
        lib.setFileName(QCoreApplication::applicationDirPath() + "/" + libName);
        if (!lib.load()) return buildFallbackDB();
    }

    auto countFn = reinterpret_cast<BoardCountFn>(lib.resolve("vioavr_board_count"));
    auto nameFn = reinterpret_cast<BoardNameFn>(lib.resolve("vioavr_board_name"));
    auto mcuFn = reinterpret_cast<BoardMcuFn>(lib.resolve("vioavr_board_mcu"));
    auto clockFn = reinterpret_cast<BoardClockFn>(lib.resolve("vioavr_board_clock"));
    auto fullPinCountFn = reinterpret_cast<BoardFullPinCountFn>(lib.resolve("vioavr_board_full_pin_count"));
    auto fullPinFn = reinterpret_cast<BoardFullPinFn>(lib.resolve("vioavr_board_full_pin"));
    auto fqbnFn = reinterpret_cast<BoardNameFn>(lib.resolve("vioavr_board_fqbn"));

    if (!countFn || !nameFn || !mcuFn || !clockFn || !fullPinCountFn || !fullPinFn)
        return buildFallbackDB();

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

        if (qMcu == "ATmega328P") { b.flashBytes = 32256; b.sramBytes = 2048; }
        else if (qMcu == "ATmega2560") { b.flashBytes = 262144; b.sramBytes = 8192; }
        else if (qMcu == "ATmega32U4") { b.flashBytes = 28672; b.sramBytes = 2560; }
        else if (qMcu == "ATmega4809") { b.flashBytes = 49152; b.sramBytes = 6144; }
        else { b.flashBytes = 32256; b.sramBytes = 2048; }
        b.logicVoltage = 5.0;

        b.hasUsbSerial = qName.contains("Leonardo") || qName.contains("Micro") || qName.contains("Yun") || qName.contains("Esplora");
        b.ledPin = (qName.contains("Uno") || qName.contains("Nano") || qName.contains("Mega") || qName.contains("Leonardo") || qName.contains("Micro")) ? 13 : -1;
        b.analogInputs = (qMcu == "ATmega32U4") ? 12 : (qMcu == "ATmega2560") ? 16 : (qMcu == "ATmega4809") ? 8 : 6;

        int pinCount = fullPinCountFn(i);
        for (int p = 0; p < pinCount; ++p) {
            CBoardPin cp = fullPinFn(i, p);
            ArduinoPinMapping pm;
            pm.digitalPin = cp.arduino_pin;
            if (cp.port >= 'A' && cp.port <= 'F')
                pm.mcuPin = QString("P%1%2").arg(cp.port).arg(cp.bit);
            else
                pm.mcuPin = QString("P%1%2").arg('D').arg(cp.bit);
            pm.label = QString::fromLatin1(cp.label);
            pm.supportsPWM = cp.supports_pwm;
            pm.supportsInterrupt = cp.supports_interrupt;
            pm.analogChannel = cp.analog_channel;
            b.pins.append(pm);
        }
        db[qName] = b;
    }

    if (db.isEmpty()) return buildFallbackDB();
    return db;
}

const QMap<QString, ArduinoBoardDef>& arduinoBoardDatabase() {
    static const auto db = queryVioAVRBoards();
    return db;
}
