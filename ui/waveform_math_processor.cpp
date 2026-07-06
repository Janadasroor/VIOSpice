// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#include "waveform_math_processor.h"
#include <QtGlobal>
#include <cmath>
#include <algorithm>

QVector<double> WaveformMathProcessor::computeDerivative(const QVector<double> &time, const QVector<double> &values) {
    QVector<double> derivative;
    int n = qMin(time.size(), values.size());
    if (n < 2) return derivative;
    
    derivative.resize(n);
    derivative[0] = 0.0;
    
    for (int i = 1; i < n; ++i) {
        double dt = time[i] - time[i-1];
        if (qFuzzyIsNull(dt)) {
            derivative[i] = 0.0;
        } else {
            derivative[i] = (values[i] - values[i-1]) / dt;
        }
    }
    return derivative;
}

QVector<double> WaveformMathProcessor::computeIntegral(const QVector<double> &time, const QVector<double> &values) {
    QVector<double> integral;
    int n = qMin(time.size(), values.size());
    if (n < 2) return integral;
    
    integral.resize(n);
    integral[0] = 0.0;
    
    for (int i = 1; i < n; ++i) {
        double dt = time[i] - time[i-1];
        double avgVal = (values[i] + values[i-1]) / 2.0;
        integral[i] = integral[i-1] + avgVal * dt;
    }
    return integral;
}

WaveformMathProcessor::EdgeTimes WaveformMathProcessor::computeEdgeTimes(const QVector<double>& time, const QVector<double>& values) {
    EdgeTimes result;
    if (time.size() < 3 || time.size() != values.size()) return result;

    double minVal = *std::min_element(values.begin(), values.end());
    double maxVal = *std::max_element(values.begin(), values.end());
    double range = maxVal - minVal;
    if (range < 1e-15) return result;

    double lo = minVal + range * 0.1;
    double hi = minVal + range * 0.9;

    QVector<double> riseTimes, fallTimes;

    auto interpTime = [&](int idx, double threshold) -> double {
        double y0 = values[idx], y1 = values[idx + 1];
        if (std::abs(y1 - y0) < 1e-15) return time[idx];
        double t = (threshold - y0) / (y1 - y0);
        return time[idx] + t * (time[idx + 1] - time[idx]);
    };

    for (int i = 0; i < values.size() - 1; ++i) {
        double y0 = values[i], y1 = values[i + 1];
        // Rising edge: crosses 10% going up
        if (y0 <= lo && y1 > lo) {
            double t10 = interpTime(i, lo);
            // Find next crossing of 90%
            for (int j = i + 1; j < values.size() - 1; ++j) {
                if (values[j] <= hi && values[j + 1] > hi) {
                    double t90 = interpTime(j, hi);
                    riseTimes.append(t90 - t10);
                    i = j;
                    break;
                }
                if (values[j + 1] < lo) break;
            }
        }
        // Falling edge: crosses 90% going down
        if (y0 >= hi && y1 < hi) {
            double t90 = interpTime(i, hi);
            for (int j = i + 1; j < values.size() - 1; ++j) {
                if (values[j] >= lo && values[j + 1] < lo) {
                    double t10 = interpTime(j, lo);
                    fallTimes.append(t10 - t90);
                    i = j;
                    break;
                }
                if (values[j + 1] > hi) break;
            }
        }
    }

    auto computeStats = [](const QVector<double>& v, double& mn, double& mx, double& avg) {
        if (v.isEmpty()) return;
        mn = *std::min_element(v.begin(), v.end());
        mx = *std::max_element(v.begin(), v.end());
        double sum = 0;
        for (double d : v) sum += d;
        avg = sum / v.size();
    };

    result.riseCount = riseTimes.size();
    result.fallCount = fallTimes.size();
    if (result.riseCount > 0) computeStats(riseTimes, result.riseMin, result.riseMax, result.riseAvg);
    if (result.fallCount > 0) computeStats(fallTimes, result.fallMin, result.fallMax, result.fallAvg);
    return result;
}
