/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arduino_board_def.h"

static QMap<QString, ArduinoBoardDef> buildBoardDB() {
    QMap<QString, ArduinoBoardDef> db;

    auto pin = [](int num, const QString& mcu, bool pwm, bool irq, int adc, const QString& lbl) -> ArduinoPinMapping {
        return {num, mcu, pwm, irq, adc, lbl};
    };

    // ── Arduino Uno ──────────────────────────────────────────────────────
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino Uno";
        b.fqbn = "arduino:avr:uno";
        b.mcuModel = "ATmega328P";
        b.defaultClock = 16000000;
        b.flashBytes = 32256;
        b.sramBytes = 2048;
        b.ledPin = 13;
        b.analogInputs = 6;
        b.hasUsbSerial = true;
        b.logicVoltage = 5.0;
        b.pins = {
            pin(0, "PD0", false, true, -1, "RX"),
            pin(1, "PD1", false, true, -1, "TX"),
            pin(2, "PD2", false, true, -1, "INT0"),
            pin(3, "PD3", true, true, -1, "INT1/OC2B"),
            pin(4, "PD4", false, false, -1, ""),
            pin(5, "PD5", true, false, -1, "OC0B"),
            pin(6, "PD6", true, false, -1, "OC0A"),
            pin(7, "PD7", false, false, -1, ""),
            pin(8, "PB0", false, true, -1, "ICP1"),
            pin(9, "PB1", true, false, -1, "OC1A"),
            pin(10, "PB2", true, false, -1, "OC1B/SS"),
            pin(11, "PB3", true, false, -1, "MOSI/OC2A"),
            pin(12, "PB4", false, false, -1, "MISO"),
            pin(13, "PB5", false, false, -1, "LED/SCK"),
            pin(14, "PC0", false, false, 0, "A0"),
            pin(15, "PC1", false, false, 1, "A1"),
            pin(16, "PC2", false, false, 2, "A2"),
            pin(17, "PC3", false, false, 3, "A3"),
            pin(18, "PC4", false, false, 4, "A4/SDA"),
            pin(19, "PC5", false, false, 5, "A5/SCL"),
        };
        db["Arduino Uno"] = b;
    }

    // ── Arduino Nano ─────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Nano";
        b.fqbn = "arduino:avr:nano";
        b.pins[12] = pin(13, "PB5", false, false, -1, "LED");
        db["Arduino Nano"] = b;
    }

    // ── Arduino Mega 2560 ───────────────────────────────────────────────
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino Mega 2560";
        b.fqbn = "arduino:avr:mega";
        b.mcuModel = "ATmega2560";
        b.defaultClock = 16000000;
        b.flashBytes = 262144;
        b.sramBytes = 8192;
        b.ledPin = 13;
        b.analogInputs = 16;
        b.hasUsbSerial = true;
        b.logicVoltage = 5.0;
        b.pins = {
            pin(0, "PE0", false, true, -1, "RX0"),
            pin(1, "PE1", false, true, -1, "TX0"),
            pin(2, "PE4", false, true, -1, "INT4"),
            pin(3, "PE5", false, true, -1, "INT5/OC3B"),
            pin(4, "PG5", true, false, -1, "OC0B"),
            pin(5, "PE3", true, false, -1, "OC3B"),
            pin(6, "PH3", true, false, -1, "OC4A"),
            pin(7, "PH4", true, false, -1, "OC4B"),
            pin(8, "PH5", true, false, -1, "OC4C"),
            pin(9, "PH6", true, false, -1, "OC2B"),
            pin(10, "PB4", true, false, -1, "OC2A/SS"),
            pin(11, "PB5", true, false, -1, "OC1A"),
            pin(12, "PB6", true, false, -1, "OC1B"),
            pin(13, "PB7", false, false, -1, "LED/OC0A/CLKO"),
            pin(14, "PJ1", false, true, -1, "TXD3"),
            pin(15, "PJ0", false, true, -1, "RXD3"),
            pin(16, "PH1", false, true, -1, "TXD2"),
            pin(17, "PH0", false, true, -1, "RXD2"),
            pin(18, "PD3", false, true, -1, "TXD1/INT3"),
            pin(19, "PD2", false, true, -1, "RXD1/INT2"),
            pin(20, "PD1", false, true, -1, "INT1/TXD0"),
            pin(21, "PD0", false, true, -1, "INT0/RXD0"),
            pin(22, "PA0", false, false, 0, "A0"),
            pin(23, "PA1", false, false, 1, "A1"),
            pin(24, "PA2", false, false, 2, "A2"),
            pin(25, "PA3", false, false, 3, "A3"),
            pin(26, "PA4", false, false, 4, "A4"),
            pin(27, "PA5", false, false, 5, "A5"),
            pin(28, "PA6", false, false, 6, "A6"),
            pin(29, "PA7", false, false, 7, "A7"),
            pin(30, "PC7", false, false, 8, "A8"),
            pin(31, "PC6", false, false, 9, "A9"),
            pin(32, "PC5", false, false, 10, "A10/SDA"),
            pin(33, "PC4", false, false, 11, "A11/SCL"),
            pin(34, "PC3", false, false, 12, "A12"),
            pin(35, "PC2", false, false, 13, "A13"),
            pin(36, "PC1", false, false, 14, "A14"),
            pin(37, "PC0", false, false, 15, "A15"),
        };
        db["Arduino Mega 2560"] = b;
    }

    // ── Arduino Mega ADK ────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Mega 2560"];
        b.boardName = "Arduino Mega ADK";
        b.fqbn = "arduino:avr:megaADK";
        db["Arduino Mega ADK"] = b;
    }

    // ── Arduino Leonardo ────────────────────────────────────────────────
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino Leonardo";
        b.fqbn = "arduino:avr:leonardo";
        b.mcuModel = "ATmega32U4";
        b.defaultClock = 16000000;
        b.flashBytes = 28672;
        b.sramBytes = 2560;
        b.ledPin = 13;
        b.analogInputs = 12;
        b.hasUsbSerial = true;
        b.logicVoltage = 5.0;
        b.pins = {
            pin(0, "PD2", false, true, -1, "RX/INT2"),
            pin(1, "PD3", true, true, -1, "TX/OC0B/INT3"),
            pin(2, "PD1", false, true, -1, "SDA/INT1"),
            pin(3, "PD0", false, true, -1, "SCL/INT0"),
            pin(4, "PD4", false, false, -1, ""),
            pin(5, "PC6", true, false, -1, "OC3A/OC4A"),
            pin(6, "PD7", true, false, -1, "OC0D"),
            pin(7, "PE6", false, true, -1, "INT6/AIN0"),
            pin(8, "PB4", false, false, -1, "SS"),
            pin(9, "PB5", true, false, -1, "OC1A/OC4B"),
            pin(10, "PB6", true, false, -1, "OC1B/OC4C"),
            pin(11, "PB7", false, false, -1, "OC0A/OC1C/RTS"),
            pin(12, "PD6", true, false, -1, "OC4D/AIN1"),
            pin(13, "PC7", false, false, -1, "LED/OC4C"),
            pin(14, "PB3", true, false, -1, "MOSI/OC2A"),
            pin(15, "PB1", true, false, -1, "MISO/OC1A"),
            pin(16, "PB2", true, false, -1, "SCK/OC1B"),
            pin(17, "PB0", false, true, -1, "ICP1/CLKO"),
            pin(18, "PF7", false, false, 0, "A0"),
            pin(19, "PF6", false, false, 1, "A1"),
            pin(20, "PF5", false, false, 2, "A2"),
            pin(21, "PF4", false, false, 3, "A3"),
            pin(22, "PF1", false, false, 4, "A4"),
            pin(23, "PF0", false, false, 5, "A5"),
        };
        db["Arduino Leonardo"] = b;
    }

    // ── Arduino Micro ────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Micro";
        b.fqbn = "arduino:avr:micro";
        db["Arduino Micro"] = b;
    }

    // ── Arduino Nano Every ──────────────────────────────────────────────
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino Nano Every";
        b.fqbn = "arduino:megaavr:nona4809";
        b.mcuModel = "ATmega4809";
        b.defaultClock = 20000000;
        b.flashBytes = 49152;
        b.sramBytes = 6144;
        b.ledPin = 13;
        b.analogInputs = 8;
        b.hasUsbSerial = true;
        b.logicVoltage = 5.0;
        b.pins = {
            pin(0, "PB0", false, true, -1, "RX"),
            pin(1, "PB1", false, true, -1, "TX"),
            pin(2, "PB2", false, true, -1, "INT0"),
            pin(3, "PB3", false, true, -1, "INT1"),
            pin(4, "PB4", false, false, -1, ""),
            pin(5, "PB5", false, false, -1, "LED"),
            pin(6, "PB6", false, false, -1, ""),
            pin(7, "PB7", false, false, -1, ""),
            pin(8, "PC0", false, false, 0, "A0"),
            pin(9, "PC1", false, false, 1, "A1"),
            pin(10, "PC2", false, false, 2, "A2"),
            pin(11, "PC3", false, false, 3, "A3"),
            pin(12, "PC4", false, false, 4, "A4/SDA"),
            pin(13, "PC5", false, false, 5, "A5/SCL"),
            pin(14, "PD0", false, false, 6, "A6"),
            pin(15, "PD1", false, false, 7, "A7"),
            pin(16, "PD2", false, false, -1, ""),
            pin(17, "PD3", false, false, -1, ""),
            pin(18, "PD4", false, false, -1, ""),
            pin(19, "PD5", false, false, -1, ""),
        };
        db["Arduino Nano Every"] = b;
    }

    // ── Arduino Pro Mini 16MHz ──────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Pro Mini 16MHz";
        b.fqbn = "arduino:avr:pro:cpu=16MHz";
        b.hasUsbSerial = false;
        db["Arduino Pro Mini 16MHz"] = b;
    }

    // ── Arduino Pro Mini 8MHz ───────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Pro Mini 8MHz";
        b.fqbn = "arduino:avr:pro:cpu=8MHz";
        b.defaultClock = 8000000;
        b.hasUsbSerial = false;
        db["Arduino Pro Mini 8MHz"] = b;
    }

    // ── Arduino Mini ────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Mini";
        b.fqbn = "arduino:avr:mini";
        b.hasUsbSerial = false;
        db["Arduino Mini"] = b;
    }

    // ── Arduino Ethernet ────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Ethernet";
        b.fqbn = "arduino:avr:ethernet";
        db["Arduino Ethernet"] = b;
    }

    // ── Arduino Duemilanove ─────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Duemilanove";
        b.fqbn = "arduino:avr:diecimila";
        db["Arduino Duemilanove"] = b;
    }

    // ── Arduino Fio ─────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Fio";
        b.fqbn = "arduino:avr:fio";
        b.defaultClock = 8000000;
        b.hasUsbSerial = false;
        db["Arduino Fio"] = b;
    }

    // ── Arduino LilyPad ─────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino LilyPad";
        b.fqbn = "arduino:avr:lilypad";
        b.defaultClock = 8000000;
        b.hasUsbSerial = false;
        db["Arduino LilyPad"] = b;
    }

    // ── Arduino LilyPad USB ─────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino LilyPad USB";
        b.fqbn = "arduino:avr:LilyPadUSB";
        b.defaultClock = 8000000;
        db["Arduino LilyPad USB"] = b;
    }

    // ── Arduino Gemma ───────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino Gemma";
        b.fqbn = "arduino:avr:gemma";
        b.defaultClock = 8000000;
        b.hasUsbSerial = false;
        db["Arduino Gemma"] = b;
    }

    // ── Arduino Esplora ─────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Esplora";
        b.fqbn = "arduino:avr:esplora";
        db["Arduino Esplora"] = b;
    }

    // ── Arduino Yun ─────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Yun";
        b.fqbn = "arduino:avr:yun";
        db["Arduino Yun"] = b;
    }

    // ── Arduino BT ──────────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Uno"];
        b.boardName = "Arduino BT";
        b.fqbn = "arduino:avr:bt";
        db["Arduino BT"] = b;
    }

    // ── Arduino Industrial 101 ──────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Industrial 101";
        b.fqbn = "arduino:avr:chiwawa";
        db["Arduino Industrial 101"] = b;
    }

    // ── Arduino Leonardo ETH ────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Leonardo ETH";
        b.fqbn = "arduino:avr:leonardoeth";
        db["Arduino Leonardo ETH"] = b;
    }

    // ── Arduino Robot Control ───────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Robot Control";
        b.fqbn = "arduino:avr:robotControl";
        db["Arduino Robot Control"] = b;
    }

    // ── Arduino Robot Motor ─────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Robot Motor";
        b.fqbn = "arduino:avr:robotMotor";
        db["Arduino Robot Motor"] = b;
    }

    // ── Arduino Yun Mini ────────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Yun Mini";
        b.fqbn = "arduino:avr:yunmini";
        db["Arduino Yun Mini"] = b;
    }

    // ── Arduino Linino One ──────────────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Linino One";
        b.fqbn = "arduino:avr:one";
        db["Arduino Linino One"] = b;
    }

    // ── Arduino Circuit Playground ──────────────────────────────────────
    {
        ArduinoBoardDef b = db["Arduino Leonardo"];
        b.boardName = "Arduino Circuit Playground";
        b.fqbn = "arduino:avr:circuitplay32u4cat";
        b.defaultClock = 8000000;
        db["Arduino Circuit Playground"] = b;
    }

    // ── Arduino UNO WiFi Rev2 ───────────────────────────────────────────
    {
        ArduinoBoardDef b;
        b.boardName = "Arduino UNO WiFi Rev2";
        b.fqbn = "arduino:megaavr:uno2018";
        b.mcuModel = "ATmega4809";
        b.defaultClock = 16000000;
        b.flashBytes = 49152;
        b.sramBytes = 6144;
        b.ledPin = 13;
        b.analogInputs = 6;
        b.hasUsbSerial = true;
        b.logicVoltage = 5.0;
        b.pins = {
            pin(0, "PB0", false, true, -1, "RX"),
            pin(1, "PB1", false, true, -1, "TX"),
            pin(2, "PD2", false, true, -1, "INT0"),
            pin(3, "PD3", false, true, -1, "INT1"),
            pin(4, "PD4", false, false, -1, ""),
            pin(5, "PD5", false, false, -1, "LED"),
            pin(6, "PD6", false, false, -1, ""),
            pin(7, "PD7", false, false, -1, ""),
            pin(8, "PB2", false, false, 0, "A0"),
            pin(9, "PB3", false, false, 1, "A1"),
            pin(10, "PB4", false, false, 2, "A2"),
            pin(11, "PB5", false, false, 3, "A3/SDA"),
            pin(12, "PB6", false, false, 4, "A4/SCL"),
            pin(13, "PB7", false, false, 5, "A5"),
        };
        db["Arduino UNO WiFi Rev2"] = b;
    }

    return db;
}

const QMap<QString, ArduinoBoardDef>& arduinoBoardDatabase() {
    static const auto db = buildBoardDB();
    return db;
}
