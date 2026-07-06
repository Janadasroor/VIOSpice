// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#include "waveform_csv_helper.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <limits>
#include <algorithm>

bool WaveformCsvHelper::loadCsv(const QString &filePath, QList<CsvSignal> &outSignals, QString &outError) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        outError = QString("Cannot open file: %1").arg(f.errorString());
        return false;
    }

    QVector<QVector<double>> columns;
    QStringList headers;
    QTextStream in(&f);
    bool firstRowIsHeader = false;
    int colCount = 0;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList parts;
        for (const QString& token : line.split(',')) {
            QString t = token.trimmed();
            if (!t.isEmpty()) parts.append(t);
        }
        if (parts.size() < 2) continue;

        if (columns.isEmpty()) {
            // Check if first row is header (non-numeric first token)
            bool isNumeric = false;
            parts[0].toDouble(&isNumeric);
            firstRowIsHeader = !isNumeric;

            if (firstRowIsHeader) {
                headers = parts.mid(1);
                colCount = headers.size();
                columns.resize(colCount);
                continue;
            }
        }

        if (columns.isEmpty()) {
            colCount = parts.size() - 1;
            columns.resize(colCount);
        }

        for (int c = 0; c < colCount && c + 1 < parts.size(); ++c) {
            bool ok = false;
            double val = parts[c + 1].toDouble(&ok);
            if (ok || parts[c + 1] == "0") {
                columns[c].append(val);
            } else {
                columns[c].append(std::numeric_limits<double>::quiet_NaN());
            }
        }
    }
    f.close();

    if (columns.isEmpty() || columns[0].isEmpty()) {
        outError = "No valid data found in CSV file.";
        return false;
    }

    // Generate time axis from index if not available
    QVector<double> time(columns[0].size());
    for (int i = 0; i < time.size(); ++i) time[i] = static_cast<double>(i);

    for (int c = 0; c < colCount; ++c) {
        CsvSignal sig;
        sig.name = (c < headers.size() && !headers[c].isEmpty()) ? headers[c] : QString("CSV_%1").arg(c + 1);
        sig.time = time;
        sig.values = columns[c];
        outSignals.append(sig);
    }

    return true;
}

bool WaveformCsvHelper::exportCsv(const QString &filePath, const QList<CsvSignal> &signalsList) {
    if (signalsList.isEmpty()) return false;
    
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QTextStream out(&f);

    out << "index";
    for (const auto& sig : signalsList) out << "," << sig.name << "_x," << sig.name << "_y";
    out << "\n";

    int maxSize = 0;
    for (const auto& sig : signalsList) {
        maxSize = std::max(maxSize, static_cast<int>(sig.time.size()));
    }

    for (int i = 0; i < maxSize; ++i) {
        out << i;
        for (const auto& sig : signalsList) {
            if (i < sig.time.size() && i < sig.values.size()) out << "," << sig.time[i] << "," << sig.values[i];
            else out << ",,";
        }
        out << "\n";
    }
    return true;
}
