/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LTSPICE_REWRITER_H
#define LTSPICE_REWRITER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

class LtspiceRewriter {
public:
    static bool convertLtspiceConditionToStepExpr(const QString& condition, QString* stepExpr);
    static void updateSubcktDepthForLine(const QString& line, int& subcktDepth);
    static QString rewriteLtspiceBehavioralIf(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceVoltageSourceExtras(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceTriggeredPulseSource(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceTriggeredPwlSource(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceTriggeredWaveSource(const QString& line, const QString& kind, QStringList* warnings = nullptr);
    static QString rewriteLtspiceBehavioralFunctions(const QString& line, QStringList* warnings = nullptr);
    static QStringList tokenizeLtspiceOtaLine(const QString& line);
    static QString buildNgspiceOtaTranslation(const QString& line);
    static QString rewriteUnsupportedLtspiceBehavioralTimeFunctions(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteUnsupportedLtspiceTableFunction(const QString& line, QStringList* warnings = nullptr);
    static QString buildCurrentTableExpr(const QString& xExpr, const QStringList& args, QString* error = nullptr);
    static QString rewriteUnsupportedLtspiceStochasticFunctions(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceBSourceLaplaceOptions(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceBehavioralSourceRpar(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceSourceTripOptions(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceBSourceTripOptions(const QString& line, QStringList* warnings = nullptr);
    static void appendLtspiceBSourceOptionWarnings(const QString& line, QStringList* warnings);
    static void appendLtspiceSourceOptionWarnings(const QString& line, QStringList* warnings);
    static QString rewriteLtspiceStartupSourceLine(const QString& line, QStringList* warnings = nullptr);
    static QString rewriteLtspiceDirectiveLine(const QString& line, QStringList* warnings = nullptr, bool emulateStartup = false, const QString& projectDir = QString());
    static QStringList collapseSpiceContinuationLines(const QString& text);
    static bool rewriteLtspiceCurrentSourceSpecial(const QString& ref, const QString& nplus, const QString& nminus, const QString& value, const QString& projectDir, QString* replacement, QStringList* warnings = nullptr);
    static QString inlinePwlFileIfNeeded(const QString& value, const QString& projectDir, QStringList* warnings = nullptr);

    // Helpers
    static QStringList splitTopLevelSpiceArgs(const QString& text);
    static int findMatchingParen(const QString& text, int openIndex);
};

#endif // LTSPICE_REWRITER_H
