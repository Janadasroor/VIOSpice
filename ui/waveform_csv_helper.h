// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QString>
#include <QVector>
#include <QList>

struct CsvSignal {
    QString name;
    QVector<double> time;
    QVector<double> values;
};

class WaveformCsvHelper {
public:
    static bool loadCsv(const QString &filePath, QList<CsvSignal> &outSignals, QString &outError);
    static bool exportCsv(const QString &filePath, const QList<CsvSignal> &signalsList);
};
