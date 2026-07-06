// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QVector>

class WaveformMathProcessor {
public:
    struct EdgeTimes {
        double riseMin = 0, riseMax = 0, riseAvg = 0;
        double fallMin = 0, fallMax = 0, fallAvg = 0;
        int riseCount = 0, fallCount = 0;
    };

    static QVector<double> computeDerivative(const QVector<double>& time, const QVector<double>& values);
    static QVector<double> computeIntegral(const QVector<double>& time, const QVector<double>& values);
    static EdgeTimes computeEdgeTimes(const QVector<double>& time, const QVector<double>& values);
};
