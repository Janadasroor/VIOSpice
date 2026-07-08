/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_HIGHLIGHTER_H
#define IDE_HIGHLIGHTER_H

#include "../../schematic/ui/flux_code_editor.h"

namespace IDE {

class IdeHighlighter : public Flux::FluxHighlighter {
    Q_OBJECT
public:
    explicit IdeHighlighter(QTextDocument* parent = nullptr);

    void setLanguage(const QString& lang);

protected:
    void highlightBlock(const QString& text) override;

private:
    void highlightFluxScript(const QString& text);
    void highlightJson(const QString& text);

    struct JsonRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<JsonRule> m_jsonRules;
    QTextCharFormat m_jsonKeyFormat;
    QTextCharFormat m_jsonStringFormat;
    QTextCharFormat m_jsonNumberFormat;
    QTextCharFormat m_jsonBoolFormat;
    QTextCharFormat m_jsonNullFormat;

    QString m_language; // "flux" or "json"
};

} // namespace IDE

#endif // IDE_HIGHLIGHTER_H
