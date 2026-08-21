/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mini_scope_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QClipboard>
#include <QRubberBand>
#include <algorithm>
#include <cmath>
#include <QRegularExpression>

namespace {
QString formatValueSI(double val, const QString& unit) {
    const double absVal = std::abs(val);
    if (absVal < 1e-18) return "0" + unit;

    static const struct { double mult; const char* sym; } suffixes[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "Meg"}, {1e3, "k"},
        {1.0, ""},
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}
    };

    for (const auto& s : suffixes) {
        if (absVal >= s.mult * 0.999) {
            QString num = QString::number(val / s.mult, 'f', 2).remove(QRegularExpression("\\.?0+$"));
            return num + s.sym + unit;
        }
    }
    return QString::number(val, 'g', 4) + unit;
}

QString unitForTrace(const QString& name) {
    const QString n = name.trimmed();
    if (n.startsWith("I(", Qt::CaseInsensitive)) return "A";
    if (n.startsWith("V(", Qt::CaseInsensitive)) return "V";
    if (n.startsWith("I", Qt::CaseInsensitive) && n.contains("(")) return "A";
    if (n.startsWith("V", Qt::CaseInsensitive) && n.contains("(")) return "V";
    return "V";
}
}

MiniScopeWidget::MiniScopeWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("MiniScopeWidget");
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumSize(400, 250);
    setMouseTracking(true);
    setStyleSheet("background-color: #0a0a0a; border: 2px solid #333; border-radius: 2px;");

    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    m_rubberBand->setStyleSheet("border: 1px dashed #00f0ff; background-color: rgba(0, 240, 255, 45);");
}

void MiniScopeWidget::setCursorMode(CursorMode mode) {
    m_cursorMode = mode;
    if (m_cursorMode != CursorNone && !m_cursorsInitialized) {
        // Initialize cursors at 25% and 75% of current screen
        double timeSpan = std::max(1e-9, m_maxX - m_minX);
        m_timeCursorA = m_minX + 0.25 * timeSpan;
        m_timeCursorB = m_minX + 0.75 * timeSpan;

        double voltSpan = std::max(1e-9, m_globalMaxY - m_globalMinY);
        m_voltCursorA = m_globalMinY + 0.75 * voltSpan;
        m_voltCursorB = m_globalMinY + 0.25 * voltSpan;
        m_cursorsInitialized = true;
    }
    update();
}

void MiniScopeWidget::setTimebase(double t) {
    if (t > 1e-12) {
        m_timebase = t;
        m_useTimebaseWindow = true;

        double minXData = 0.0;
        bool hasX = false;
        for (const auto& tr : m_traces) {
            if (!tr.points.isEmpty()) {
                if (!hasX) {
                    minXData = tr.points.first().x();
                    hasX = true;
                } else {
                    minXData = std::min(minXData, tr.points.first().x());
                }
            }
        }

        if (hasX) {
            double windowSpan = m_timebase * 10.0;
            m_minX = minXData;
            m_maxX = minXData + windowSpan;
        }
        update();
    }
}

static const QColor s_scopeDefaultColors[8] = {
    Qt::yellow, Qt::cyan, Qt::magenta, QColor(0, 255, 100),
    QColor(255, 165, 0), QColor(147, 112, 219), QColor(255, 105, 180), QColor(0, 191, 255)
};

MiniScopeWidget::TraceStats MiniScopeWidget::getTraceStats(const QString& channelName) const {
    TraceStats stats;
    if (m_traces.contains(channelName)) {
        const auto& tr = m_traces[channelName];
        if (!tr.points.isEmpty()) {
            stats.minV = tr.minV;
            stats.maxV = tr.maxV;
            stats.minT = tr.points.first().x();
            stats.maxT = tr.points.last().x();
            stats.hasData = true;
        }
    }
    return stats;
}

MiniScopeWidget::TraceStats MiniScopeWidget::getOverallTraceStats() const {
    TraceStats stats;
    bool first = true;
    for (const auto& tr : m_traces) {
        if (tr.points.isEmpty()) continue;
        if (first) {
            stats.minV = tr.minV;
            stats.maxV = tr.maxV;
            stats.minT = tr.points.first().x();
            stats.maxT = tr.points.last().x();
            stats.hasData = true;
            first = false;
        } else {
            stats.minV = std::min(stats.minV, tr.minV);
            stats.maxV = std::max(stats.maxV, tr.maxV);
            stats.minT = std::min(stats.minT, tr.points.first().x());
            stats.maxT = std::max(stats.maxT, tr.points.last().x());
        }
    }
    return stats;
}

void MiniScopeWidget::appendMultiTraceData(const QMap<QString, QVector<QPointF>>& traces, const QMap<QString, QColor>& colors) {
    for (auto it = traces.begin(); it != traces.end(); ++it) {
        if (it.value().isEmpty()) continue;

        if (!m_traces.contains(it.key())) {
            TraceData data;
            if (colors.contains(it.key())) {
                data.color = colors[it.key()];
            } else {
                int chNum = 1;
                if (it.key().startsWith("CH", Qt::CaseInsensitive)) {
                    chNum = it.key().mid(2).toInt();
                    if (chNum < 1) chNum = 1;
                }
                data.color = s_scopeDefaultColors[(chNum - 1) % 8];
            }
            m_traces[it.key()] = data;
        } else if (colors.contains(it.key())) {
            m_traces[it.key()].color = colors[it.key()];
        }

        auto& target = m_traces[it.key()];
        const auto& newPts = it.value();

        // 1. If new simulation started at t=0, reset the buffer
        if (!target.points.isEmpty() && !newPts.isEmpty()) {
            if (newPts.first().x() < 1e-12 && target.points.last().x() > 1e-9) {
                target.points.clear();
            } else {
                // Overwrite any speculative points from solver step retries
                while (!target.points.isEmpty() && target.points.last().x() >= newPts.first().x()) {
                    target.points.removeLast();
                }
            }
        }

        // 2. Append new points, ensuring strictly non-decreasing time
        for (const auto& pt : newPts) {
            if (target.points.isEmpty() || pt.x() >= target.points.last().x()) {
                target.points.append(pt);
            }
        }

        // 3. Keep ring buffer bounded
        const int maxMiniPoints = 25000;
        if (target.points.size() > maxMiniPoints) {
            target.points.remove(0, target.points.size() - 15000);
        }

        calculateMeasurements(it.key(), target.points);
    }

    // Determine latest and earliest times in live stream
    double latestTime = 0.0;
    double earliestTime = 1e30;
    bool hasPoints = false;
    for (const auto& trace : m_traces) {
        if (!trace.points.isEmpty()) {
            latestTime = std::max(latestTime, trace.points.last().x());
            earliestTime = std::min(earliestTime, trace.points.first().x());
            hasPoints = true;
        }
    }

    if (hasPoints) {
        double windowSpan = m_timebase * 10.0; // 10 horizontal divisions
        if (windowSpan < 1e-9) windowSpan = 1e-3;
        
        // While simulation is filling the initial screen window, start at earliestTime and draw left-to-right.
        // Once data extends past the screen, smoothly slide the window with latestTime.
        if (latestTime <= earliestTime + windowSpan) {
            m_minX = earliestTime;
            m_maxX = earliestTime + windowSpan;
        } else {
            m_maxX = latestTime;
            m_minX = std::max(earliestTime, latestTime - windowSpan);
        }
    }

    update();
}

void MiniScopeWidget::setMultiTraceData(const QMap<QString, QVector<QPointF>>& traces, const QMap<QString, QColor>& colors) {
    m_traces.clear();

    m_globalMinY = 0;
    m_globalMaxY = 0;
    bool first = true;

    // First consider memories in boundaries
    for (const auto& mem : m_memories) {
        for (const auto& data : mem) {
            if (data.points.isEmpty()) continue;
            if (first) {
                m_globalMinY = data.minV;
                m_globalMaxY = data.maxV;
                m_minX = data.points.first().x();
                m_maxX = data.points.last().x();
                first = false;
            } else {
                m_globalMinY = std::min(m_globalMinY, data.minV);
                m_globalMaxY = std::max(m_globalMaxY, data.maxV);
                m_minX = std::min(m_minX, data.points.first().x());
                m_maxX = std::max(m_maxX, data.points.last().x());
            }
        }
    }

    double minXData = 0.0;
    bool hasX = false;

    for (auto it = traces.begin(); it != traces.end(); ++it) {
        if (it.value().isEmpty()) continue;

        TraceData data;
        data.points = it.value();
        
        if (!hasX) {
            minXData = data.points.first().x();
            hasX = true;
        } else {
            minXData = std::min(minXData, data.points.first().x());
        }

        if (colors.contains(it.key())) {
            data.color = colors[it.key()];
        } else {
            int chNum = 1;
            if (it.key().startsWith("CH", Qt::CaseInsensitive)) {
                chNum = it.key().mid(2).toInt();
                if (chNum < 1) chNum = 1;
            }
            data.color = s_scopeDefaultColors[(chNum - 1) % 8];
        }

        calculateMeasurements(it.key(), data.points);

        if (first) {
            m_globalMinY = data.minV;
            m_globalMaxY = data.maxV;
            m_minX = data.points.first().x();
            m_maxX = data.points.last().x();
            first = false;
        } else {
            m_globalMinY = std::min(m_globalMinY, data.minV);
            m_globalMaxY = std::max(m_globalMaxY, data.maxV);
            m_minX = std::min(m_minX, data.points.first().x());
            m_maxX = std::max(m_maxX, data.points.last().x());
        }

        m_traces[it.key()] = data;
    }

    if (hasX) {
        double windowSpan = m_timebase * 10.0;
        if (windowSpan < 1e-9) windowSpan = 1e-3;
        m_minX = minXData;
        m_maxX = minXData + windowSpan;

        if (!m_cursorsInitialized) {
            double timeSpan = std::max(1e-9, m_maxX - m_minX);
            m_timeCursorA = m_minX + 0.25 * timeSpan;
            m_timeCursorB = m_minX + 0.75 * timeSpan;
            m_voltCursorA = 1.5;
            m_voltCursorB = -1.5;
            m_cursorsInitialized = true;
        }
    }

    update();
}

void MiniScopeWidget::setData(const QVector<QPointF>& points) {
    QMap<QString, QVector<QPointF>> traces;
    traces["CH1"] = points;
    setMultiTraceData(traces);
}

void MiniScopeWidget::calculateMeasurements(const QString& name, const QVector<QPointF>& points) {
    if (points.isEmpty()) return;

    double sumSq = 0;
    double minV = points[0].y();
    double maxV = points[0].y();
    
    for (const auto& p : points) {
        sumSq += p.y() * p.y();
        minV = std::min(minV, p.y());
        maxV = std::max(maxV, p.y());
    }

    m_traces[name].minV = minV;
    m_traces[name].maxV = maxV;
    m_traces[name].rms = std::sqrt(sumSq / points.size());

    // Basic Frequency Detection (Zero-crossing)
    int crossings = 0;
    double avg = (maxV + minV) / 2.0;
    for (int i = 1; i < points.size(); ++i) {
        if ((points[i-1].y() < avg && points[i].y() >= avg) ||
            (points[i-1].y() > avg && points[i].y() <= avg)) {
            crossings++;
        }
    }
    double duration = points.last().x() - points.first().x();
    if (duration > 0 && crossings > 1) {
        m_traces[name].freq = (crossings / 2.0) / duration;
    } else {
        m_traces[name].freq = 0;
    }
}

void MiniScopeWidget::freezeCurrentTraces() {
    if (m_traces.isEmpty()) return;
    
    // Store deep copy of current traces as a new memory layer
    m_memories.append(m_traces);
    
    // Cap memory layers (max 5)
    while (m_memories.size() > 5) {
        m_memories.removeFirst();
    }
    
    update();
}

void MiniScopeWidget::clearMemories() {
    m_memories.clear();
    update();
}

void MiniScopeWidget::clear() {
    m_traces.clear();
    update();
}

void MiniScopeWidget::exportToCsv(const QString& filePath) const {
    if (filePath.isEmpty() || m_traces.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    QStringList header;
    header << "Time (s)";
    for (auto it = m_traces.begin(); it != m_traces.end(); ++it) {
        header << it.key();
    }
    out << header.join(",") << "\n";

    if (!m_traces.isEmpty()) {
        const auto& firstPts = m_traces.begin().value().points;
        for (int i = 0; i < firstPts.size(); ++i) {
            QStringList row;
            row << QString::number(firstPts[i].x(), 'g', 10);
            for (auto it = m_traces.begin(); it != m_traces.end(); ++it) {
                if (i < it.value().points.size()) {
                    row << QString::number(it.value().points[i].y(), 'g', 8);
                } else {
                    row << "0";
                }
            }
            out << row.join(",") << "\n";
        }
    }
}

void MiniScopeWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    renderToPainter(painter, size());
}

QImage MiniScopeWidget::renderToImage(const QSize& size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(10, 10, 10));
    QPainter painter(&image);
    renderToPainter(painter, size);
    return image;
}

void MiniScopeWidget::renderToPainter(QPainter& painter, const QSize& targetSize) {
    painter.setRenderHint(QPainter::Antialiasing);

    int w = targetSize.width();
    int h = targetSize.height();
    int graphW = w - 120; // Space for measurement legend

    // Background
    painter.fillRect(0, 0, w, h, QColor(10, 10, 10));

    // Draw Grid (Professional Scope Style)
    painter.setPen(QPen(QColor(35, 45, 35), 1));
    for (int i = 0; i <= 10; ++i) {
        int x = i * graphW / 10;
        painter.drawLine(x, 0, x, h);
    }
    for (int i = 0; i <= 8; ++i) {
        int y = i * h / 8;
        painter.drawLine(0, y, graphW, y);
    }

    // Centered crosshairs (brighter dotted)
    painter.setPen(QPen(QColor(55, 75, 55), 1, Qt::DashLine));
    painter.drawLine(0, h / 2, graphW, h / 2);
    painter.drawLine(graphW / 2, 0, graphW / 2, h);
    
    painter.setPen(QPen(QColor(70, 70, 70), 2));
    painter.drawLine(graphW, 0, graphW, h);

    if (m_traces.isEmpty()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(QRect(0, 0, graphW, h), Qt::AlignCenter, "No Signal Connected");
        return;
    }

    auto mapX = [&](double x) {
        double range = std::max(1e-9, m_maxX - m_minX);
        return (x - m_minX) / range * graphW;
    };

    // Standard 8-division scope graticule (-4 to +4 divisions around center line)
    auto mapY = [&](double divY) {
        double divHeight = (double)h / 8.0;
        return (h / 2.0) - (divY * divHeight);
    };

    // Set clipping for CRT graticule area so waveforms don't spill into right legends
    painter.save();
    painter.setClipRect(0, 0, graphW, h);

    // Draw Memories (Ghost traces: dashed, transparent)
    for (const auto& mem : m_memories) {
        for (const auto& data : mem) {
            if (data.points.isEmpty()) continue;
            QPainterPath path;
            bool started = false;
            double lastX = -1e30;
            for (int i = 0; i < data.points.size(); ++i) {
                const QPointF& pt = data.points[i];
                if (!started || pt.x() < lastX) {
                    path.moveTo(mapX(pt.x()), mapY(pt.y()));
                    started = true;
                } else {
                    path.lineTo(mapX(pt.x()), mapY(pt.y()));
                }
                lastX = pt.x();
            }
            
            QColor ghostColor = data.color;
            ghostColor.setAlpha(100);
            painter.setPen(QPen(ghostColor, 1.0, Qt::DashLine));
            painter.drawPath(path);
        }
    }

    // Draw Live Traces
    for (auto it = m_traces.begin(); it != m_traces.end(); ++it) {
        const auto& data = it.value();
        if (data.points.isEmpty()) continue;

        QPainterPath path;
        bool started = false;
        double lastX = -1e30;
        for (int i = 0; i < data.points.size(); ++i) {
            const QPointF& pt = data.points[i];
            if (!started || pt.x() < lastX) {
                path.moveTo(mapX(pt.x()), mapY(pt.y()));
                started = true;
            } else {
                path.lineTo(mapX(pt.x()), mapY(pt.y()));
            }
            lastX = pt.x();
        }

        // Phosphor glow effect
        QColor glowColor = data.color;
        glowColor.setAlpha(55);
        painter.setPen(QPen(glowColor, 4.0));
        painter.drawPath(path);
        
        painter.setPen(QPen(data.color, 1.8));
        painter.drawPath(path);
    }
    painter.restore();

    // Draw Legend & Measurements (outside graticule clipping)
    int legendY = 18;
    for (auto it = m_traces.begin(); it != m_traces.end(); ++it) {
        const auto& data = it.value();
        if (data.points.isEmpty()) continue;

        painter.setBrush(data.color);
        painter.setPen(QPen(data.color.lighter(130), 1));
        painter.drawRoundedRect(graphW + 8, legendY - 8, 8, 8, 2, 2);
        
        painter.setPen(data.color);
        QFont f = painter.font();
        f.setPointSize(8);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(graphW + 22, legendY, it.key().toUpper());
        
        f.setBold(false);
        f.setPointSize(7);
        painter.setFont(f);
        painter.setPen(QColor(180, 180, 180));
        const QString unit = unitForTrace(it.key());
        painter.drawText(graphW + 10, legendY + 15, QString("Vpp:  %1").arg(formatValueSI(data.maxV - data.minV, unit)));
        painter.drawText(graphW + 10, legendY + 28, QString("RMS:  %1").arg(formatValueSI(data.rms, unit)));
        if (data.freq > 0) {
            QString freqStr = formatValueSI(data.freq, "Hz");
            painter.drawText(graphW + 10, legendY + 41, QString("Freq: %1").arg(freqStr));
        }
        
        legendY += 58;
    }

    // --- Draw Interactive Cursors ---
    if (m_cursorMode == CursorTime || m_cursorMode == CursorBoth) {
        double pxA = mapX(m_timeCursorA);
        double pxB = mapX(m_timeCursorB);

        // Cursor A (Cyan dashed)
        painter.setPen(QPen(QColor(0, 220, 255), 1.5, Qt::DashLine));
        painter.drawLine(pxA, 0, pxA, h);
        painter.drawText(pxA + 3, 14, "X1");

        // Cursor B (Yellow dashed)
        painter.setPen(QPen(QColor(255, 220, 0), 1.5, Qt::DashLine));
        painter.drawLine(pxB, 0, pxB, h);
        painter.drawText(pxB + 3, 14, "X2");

        // Delta-T readout box
        double dt = std::abs(m_timeCursorB - m_timeCursorA);
        double curFreq = (dt > 1e-15) ? (1.0 / dt) : 0.0;
        QString hudText = QString("ΔX: %1 | 1/ΔX: %2").arg(formatValueSI(dt, "s"), formatValueSI(curFreq, "Hz"));
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(QRectF(graphW / 2 - 90, h - 26, 180, 20), 4, 4);

        painter.setPen(QColor(0, 240, 255));
        painter.setFont(QFont("Inter", 8, QFont::Bold));
        painter.drawText(QRectF(graphW / 2 - 90, h - 26, 180, 20), Qt::AlignCenter, hudText);
    }

    if (m_cursorMode == CursorVoltage || m_cursorMode == CursorBoth) {
        double pyA = mapY(m_voltCursorA);
        double pyB = mapY(m_voltCursorB);

        // Voltage Cursor A (Magenta dashed)
        painter.setPen(QPen(QColor(255, 0, 220), 1.5, Qt::DashDotLine));
        painter.drawLine(0, pyA, graphW, pyA);
        painter.drawText(6, pyA - 3, "Y1: " + formatValueSI(m_voltCursorA, "V"));

        // Voltage Cursor B (Greenish dashed)
        painter.setPen(QPen(QColor(0, 255, 120), 1.5, Qt::DashDotLine));
        painter.drawLine(0, pyB, graphW, pyB);
        painter.drawText(6, pyB - 3, "Y2: " + formatValueSI(m_voltCursorB, "V"));

        double dv = std::abs(m_voltCursorA - m_voltCursorB);
        QString vHud = QString("ΔY: %1").arg(formatValueSI(dv, "V"));
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(QRectF(10, h / 2 - 10, 80, 20), 4, 4);

        painter.setPen(QColor(255, 120, 220));
        painter.setFont(QFont("Inter", 8, QFont::Bold));
        painter.drawText(QRectF(10, h / 2 - 10, 80, 20), Qt::AlignCenter, vHud);
    }
    
    // Graticule Divisions & Timebase Label
    painter.setPen(QColor(130, 150, 130));
    QFont fontDiv = painter.font();
    fontDiv.setPointSize(7);
    painter.setFont(fontDiv);
    painter.drawText(6, 14, "+4 Div");
    painter.drawText(6, h / 2 - 2, " 0 Div");
    painter.drawText(6, h - 6, "-4 Div");
    
    // Bottom Timebase Display (e.g. "TB: 1.00ms/Div (10.00ms)")
    double tbSpan = m_timebase * 10.0;
    QString tbText = QString("T/Div: %1 (%2 Screen)").arg(formatValueSI(m_timebase, "s"), formatValueSI(tbSpan, "s"));
    painter.drawText(QRect(0, h - 16, graphW - 6, 14), Qt::AlignRight, tbText);
}

void MiniScopeWidget::mousePressEvent(QMouseEvent* event) {
    int graphW = width() - 120;
    int h = height();
    if (graphW <= 0 || h <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    auto mapX = [&](double x) {
        double range = std::max(1e-9, m_maxX - m_minX);
        return (x - m_minX) / range * graphW;
    };
    auto mapY = [&](double divY) {
        double divHeight = (double)h / 8.0;
        return (h / 2.0) - (divY * divHeight);
    };

    const double tol = 8.0;
    const double mouseX = event->pos().x();
    const double mouseY = event->pos().y();

    // Check if dragging precision cursors
    if (event->button() == Qt::LeftButton && m_cursorMode != CursorNone) {
        if (m_cursorMode == CursorTime || m_cursorMode == CursorBoth) {
            if (std::abs(mouseX - mapX(m_timeCursorA)) <= tol) {
                m_activeDrag = DragTimeA;
                return;
            }
            if (std::abs(mouseX - mapX(m_timeCursorB)) <= tol) {
                m_activeDrag = DragTimeB;
                return;
            }
        }

        if (m_cursorMode == CursorVoltage || m_cursorMode == CursorBoth) {
            if (std::abs(mouseY - mapY(m_voltCursorA)) <= tol) {
                m_activeDrag = DragVoltA;
                return;
            }
            if (std::abs(mouseY - mapY(m_voltCursorB)) <= tol) {
                m_activeDrag = DragVoltB;
                return;
            }
        }
    }

    // Start rubber band zoom if clicking in graticule area with Left Button
    if (event->button() == Qt::LeftButton && mouseX <= graphW) {
        m_rubberBandActive = true;
        m_rubberBandOrigin = event->pos();
        if (m_rubberBand) {
            m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
            m_rubberBand->show();
        }
        return;
    }

    QWidget::mousePressEvent(event);
}

void MiniScopeWidget::mouseMoveEvent(QMouseEvent* event) {
    int graphW = width() - 120;
    int h = height();
    if (graphW <= 0 || h <= 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    auto unmapX = [&](double px) {
        double range = std::max(1e-9, m_maxX - m_minX);
        return m_minX + (px / graphW) * range;
    };
    auto unmapY = [&](double py) {
        double divHeight = (double)h / 8.0;
        return ((h / 2.0) - py) / divHeight;
    };

    if (m_rubberBandActive && m_rubberBand) {
        QRect rect = QRect(m_rubberBandOrigin, event->pos()).normalized();
        rect = rect.intersected(QRect(0, 0, graphW, h));
        m_rubberBand->setGeometry(rect);
        return;
    }

    if (m_activeDrag != DragNone) {
        if (m_activeDrag == DragTimeA) {
            m_timeCursorA = std::clamp(unmapX(event->pos().x()), m_minX, m_maxX);
        } else if (m_activeDrag == DragTimeB) {
            m_timeCursorB = std::clamp(unmapX(event->pos().x()), m_minX, m_maxX);
        } else if (m_activeDrag == DragVoltA) {
            m_voltCursorA = std::clamp(unmapY(event->pos().y()), -4.0, 4.0);
        } else if (m_activeDrag == DragVoltB) {
            m_voltCursorB = std::clamp(unmapY(event->pos().y()), -4.0, 4.0);
        }
        
        double dt = std::abs(m_timeCursorB - m_timeCursorA);
        double f = (dt > 1e-15) ? 1.0 / dt : 0.0;
        double dv = std::abs(m_voltCursorA - m_voltCursorB);
        Q_EMIT cursorsChanged(dt, f, dv);
        update();
        return;
    }

    // Update Hover Cursor
    if (m_cursorMode != CursorNone) {
        auto mapX = [&](double x) {
            double range = std::max(1e-9, m_maxX - m_minX);
            return (x - m_minX) / range * graphW;
        };
        auto mapY = [&](double divY) {
            double divHeight = (double)h / 8.0;
            return (h / 2.0) - (divY * divHeight);
        };

        const double tol = 8.0;
        const double mouseX = event->pos().x();
        const double mouseY = event->pos().y();

        if ((m_cursorMode == CursorTime || m_cursorMode == CursorBoth) &&
            (std::abs(mouseX - mapX(m_timeCursorA)) <= tol || std::abs(mouseX - mapX(m_timeCursorB)) <= tol)) {
            setCursor(Qt::SizeHorCursor);
        } else if ((m_cursorMode == CursorVoltage || m_cursorMode == CursorBoth) &&
                   (std::abs(mouseY - mapY(m_voltCursorA)) <= tol || std::abs(mouseY - mapY(m_voltCursorB)) <= tol)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            setCursor(Qt::CrossCursor);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }

    QWidget::mouseMoveEvent(event);
}

void MiniScopeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_rubberBandActive && event->button() == Qt::LeftButton) {
        m_rubberBandActive = false;
        if (m_rubberBand) m_rubberBand->hide();

        int graphW = width() - 120;
        int h = height();
        if (graphW > 0 && h > 0) {
            QRect rect = QRect(m_rubberBandOrigin, event->pos()).normalized();
            rect = rect.intersected(QRect(0, 0, graphW, h));

            auto unmapX = [&](double px) {
                double range = std::max(1e-9, m_maxX - m_minX);
                return m_minX + (px / graphW) * range;
            };
            auto unmapY = [&](double py) {
                double divHeight = (double)h / 8.0;
                return ((h / 2.0) - py) / divHeight;
            };

            if (rect.width() > 8) {
                double newMinX = unmapX(rect.left());
                double newMaxX = unmapX(rect.right());
                if (newMaxX > newMinX) {
                    m_minX = newMinX;
                    m_maxX = newMaxX;
                    m_timebase = (newMaxX - newMinX) / 10.0;
                    Q_EMIT timebaseChanged(m_timebase);
                }

                if (rect.height() > 8) {
                    double topDiv = unmapY(rect.top());
                    double botDiv = unmapY(rect.bottom());
                    Q_EMIT rubberBandZoomCompleted(m_minX, m_maxX, botDiv, topDiv);
                }
                update();
                return;
            }
        }
    }

    m_activeDrag = DragNone;
    QWidget::mouseReleaseEvent(event);
}

void MiniScopeWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1e1e24; color: #f0f0f0; border: 1px solid #444; padding: 4px; }"
        "QMenu::item { padding: 6px 24px; border-radius: 3px; }"
        "QMenu::item:selected { background-color: #2563eb; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #333; margin: 4px 8px; }"
    );

    // Cursors Submenu
    QMenu* cursorMenu = menu.addMenu("Cursors");
    QAction* actCurOff = cursorMenu->addAction("Off");
    QAction* actCurTime = cursorMenu->addAction("Time (X Cursors)");
    QAction* actCurVolt = cursorMenu->addAction("Voltage (Y Cursors)");
    QAction* actCurBoth = cursorMenu->addAction("Both (X & Y Cursors)");

    actCurOff->setCheckable(true);
    actCurTime->setCheckable(true);
    actCurVolt->setCheckable(true);
    actCurBoth->setCheckable(true);

    actCurOff->setChecked(m_cursorMode == CursorNone);
    actCurTime->setChecked(m_cursorMode == CursorTime);
    actCurVolt->setChecked(m_cursorMode == CursorVoltage);
    actCurBoth->setChecked(m_cursorMode == CursorBoth);

    connect(actCurOff, &QAction::triggered, [this]() { setCursorMode(CursorNone); });
    connect(actCurTime, &QAction::triggered, [this]() { setCursorMode(CursorTime); });
    connect(actCurVolt, &QAction::triggered, [this]() { setCursorMode(CursorVoltage); });
    connect(actCurBoth, &QAction::triggered, [this]() { setCursorMode(CursorBoth); });

    menu.addSeparator();

    // View & Scaling Actions
    menu.addAction("Zoom to Fit", [this]() {
        Q_EMIT zoomToFitRequested();
    });
    menu.addAction("Fit Axis Y", [this]() {
        Q_EMIT fitYAxisRequested();
    });

    menu.addSeparator();

    // Memory Snapshot Actions
    menu.addAction("Freeze Traces", [this]() { freezeCurrentTraces(); });
    menu.addAction("Clear Memories", [this]() { clearMemories(); });

    menu.addSeparator();

    // Export Actions
    menu.addAction("Copy Image to Clipboard", [this]() {
        QImage img = renderToImage(size());
        QApplication::clipboard()->setImage(img);
    });

    menu.addAction("Export Waveforms (CSV)...", [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Export Waveforms CSV", QString(), "CSV Files (*.csv)");
        if (!path.isEmpty()) exportToCsv(path);
    });

    menu.addAction("Save Display Image...", [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Save Scope Screenshot", QString(), "PNG Images (*.png);;JPEG Images (*.jpg)");
        if (!path.isEmpty()) {
            QImage img = renderToImage(QSize(1200, 720));
            img.save(path);
        }
    });

    menu.addSeparator();
    menu.addAction("Instrument Properties...", [this]() {
        Q_EMIT propertiesRequested();
    });

    menu.exec(event->globalPos());
}

void MiniScopeWidget::zoomToFit() {
    auto overall = getOverallTraceStats();
    if (overall.hasData && overall.maxT > overall.minT) {
        double tSpan = std::max(1e-12, overall.maxT - overall.minT);
        double idealTdiv = tSpan / 10.0;
        double exp = std::floor(std::log10(idealTdiv));
        double mant = idealTdiv / std::pow(10.0, exp);
        double stdMant = 1.0;
        if (mant > 5.0) stdMant = 10.0;
        else if (mant > 2.0) stdMant = 5.0;
        else if (mant > 1.0) stdMant = 2.0;

        double niceTdiv = stdMant * std::pow(10.0, exp);
        niceTdiv = std::clamp(niceTdiv, 1e-9, 10.0);

        m_timebase = niceTdiv;
        m_minX = overall.minT;
        m_maxX = overall.minT + (niceTdiv * 10.0);
        Q_EMIT timebaseChanged(m_timebase);
    }
    update();
}

void MiniScopeWidget::fitYAxis() {
    update();
}
