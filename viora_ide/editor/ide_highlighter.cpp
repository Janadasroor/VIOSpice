/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_highlighter.h"
#include "../core/ide_theme.h"

namespace IDE {

IdeHighlighter::IdeHighlighter(QTextDocument* parent)
    : Flux::FluxHighlighter(parent), m_language("flux") {

    auto tc = currentTheme();
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;

    // JSON highlighting rules — theme-aware
    m_jsonKeyFormat.setForeground(isLight ? QColor("#0451a5") : QColor("#9cdcfe"));
    m_jsonStringFormat.setForeground(isLight ? QColor("#a31515") : QColor("#ce9178"));
    m_jsonNumberFormat.setForeground(isLight ? QColor("#098658") : QColor("#b5cea8"));
    m_jsonBoolFormat.setForeground(isLight ? QColor("#0000ff") : QColor("#569cd6"));
    m_jsonNullFormat.setForeground(isLight ? QColor("#0000ff") : QColor("#569cd6"));

    JsonRule rule;

    // JSON strings (values)
    rule.pattern = QRegularExpression(R"(":\\s*\"([^\"]*)\")");
    rule.format = m_jsonStringFormat;
    m_jsonRules.append(rule);

    // JSON keys
    rule.pattern = QRegularExpression(R"(^\s*\"([^\"]*)\")");
    rule.format = m_jsonKeyFormat;
    m_jsonRules.append(rule);

    // JSON numbers
    rule.pattern = QRegularExpression(R"(\b[0-9]+\.?[0-9]*\b)");
    rule.format = m_jsonNumberFormat;
    m_jsonRules.append(rule);

    // JSON booleans
    rule.pattern = QRegularExpression(R"(\b(true|false)\b)");
    rule.format = m_jsonBoolFormat;
    m_jsonRules.append(rule);

    // JSON null
    rule.pattern = QRegularExpression(R"(\bnull\b)");
    rule.format = m_jsonNullFormat;
    m_jsonRules.append(rule);
}

void IdeHighlighter::setLanguage(const QString& lang) {
    m_language = lang;
    rehighlight();
}

void IdeHighlighter::highlightBlock(const QString& text) {
    if (m_language == "json") {
        highlightJson(text);
    } else {
        highlightFluxScript(text);
    }
}

void IdeHighlighter::highlightFluxScript(const QString& text) {
    // Theme-aware FluxScript highlighting (overrides base class dark-only colors)
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;

    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    QVector<Rule> rules;

    // Keywords
    QTextCharFormat kwFmt;
    kwFmt.setForeground(isLight ? QColor("#0000ff") : QColor("#569cd6"));
    kwFmt.setFontWeight(QFont::Bold);
    QStringList kwPatterns = {
        "\\bdef\\b", "\\bextern\\b", "\\breturn\\b", "\\bvar\\b",
        "\\blet\\b", "\\bfn\\b", "\\bif\\b", "\\belse\\b", "\\bfor\\b",
        "\\bin\\b", "\\bdo\\b", "\\bwhile\\b", "\\bimport\\b", "\\bcase\\b",
        "\\bswitch\\b", "\\bdefault\\b", "\\bbreak\\b", "\\bcontinue\\b",
        "\\bstruct\\b", "\\bclass\\b", "\\bnamespace\\b"
    };
    for (const QString& pat : kwPatterns) {
        rules.append({QRegularExpression(pat), kwFmt});
    }

    // Types
    QTextCharFormat typeFmt;
    typeFmt.setForeground(isLight ? QColor("#8b008b") : QColor("#4ec9b0"));
    QStringList typePatterns = {
        "\\bfloat\\b", "\\bdouble\\b", "\\bint\\b", "\\bvoid\\b",
        "\\bcomplex\\b", "\\bstring\\b", "\\bvector\\b", "\\bmatrix\\b"
    };
    for (const QString& pat : typePatterns) {
        rules.append({QRegularExpression(pat), typeFmt});
    }

    // Comments (// and #)
    QTextCharFormat commentFmt;
    commentFmt.setForeground(isLight ? QColor("#008000") : QColor("#6a9955"));
    rules.append({QRegularExpression("//[^\n]*"), commentFmt});
    rules.append({QRegularExpression("#[^\n]*"), commentFmt});

    // Strings
    QTextCharFormat stringFmt;
    stringFmt.setForeground(isLight ? QColor("#a31515") : QColor("#ce9178"));
    rules.append({QRegularExpression("\".*\""), stringFmt});

    // Functions
    QTextCharFormat funcFmt;
    funcFmt.setFontItalic(true);
    funcFmt.setForeground(isLight ? QColor("#0000ff") : QColor("#dcdcaa"));
    rules.append({QRegularExpression("\\b[A-Za-z0-9_]+(?=\\()"), funcFmt});

    // Apply all rules
    for (const Rule& r : rules) {
        QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), r.format);
        }
    }
}

void IdeHighlighter::highlightJson(const QString& text) {
    // JSON comment highlighting (for // comments in JSON5-style files)
    QRegularExpression commentRe(R"(//.*)");
    auto commentMatch = commentRe.match(text);
    if (commentMatch.hasMatch()) {
        QTextCharFormat commentFormat;
        bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
        commentFormat.setForeground(isLight ? QColor("#008000") : QColor("#6a9955"));
        setFormat(commentMatch.capturedStart(), commentMatch.capturedLength(), commentFormat);
    }

    for (const JsonRule& rule : m_jsonRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

} // namespace IDE
