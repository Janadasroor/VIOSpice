// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

class WaveformExpressionParser {
public:
    struct SignalData {
        QVector<double> time;
        QVector<double> values;
    };

    static bool parseExpression(const QString &expression, const QStringList &availableSignalKeys, QStringList &signalNames, QString &error);
    static bool evaluateExpression(const QString &expression, const QStringList &signalNames, const QMap<QString, SignalData> &allSignals, QVector<double> &time, QVector<double> &values);

private:
    static double evaluateSimpleMath(const QString &expr, bool &ok);
};
