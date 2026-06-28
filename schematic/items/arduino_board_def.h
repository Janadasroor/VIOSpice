/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARDUINO_BOARD_DEF_H
#define ARDUINO_BOARD_DEF_H

#include <QString>
#include <QMap>
#include <QList>

struct ArduinoPinMapping {
    int digitalPin;
    QString mcuPin;
    bool supportsPWM = false;
    bool supportsInterrupt = false;
    int analogChannel = -1;
    QString label;
};

struct ArduinoBoardDef {
    QString boardName;
    QString fqbn;
    QString mcuModel;
    double defaultClock = 16000000;
    int flashBytes = 0;
    int sramBytes = 0;
    int ledPin = -1;
    int analogInputs = 0;
    QList<ArduinoPinMapping> pins;
    bool hasUsbSerial = false;
    double logicVoltage = 5.0;
};

const QMap<QString, ArduinoBoardDef>& arduinoBoardDatabase();

#endif // ARDUINO_BOARD_DEF_H
