/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_editor.h"
#include "ide_highlighter.h"
#include "../core/ide_theme.h"
#include "../../core/visuals/theme_manager.h"
#include "../../core/visuals/theme.h"
#include <QPainter>
#include <QSyntaxHighlighter>
#include <QScrollBar>
#include <QKeyEvent>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QToolTip>

namespace IDE {

// ============================================================================
// LineNumberArea — separate widget that overlays the left margin
// ============================================================================

LineNumberArea::LineNumberArea(IdeEditor* editor)
    : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor->lineNumberAreaPaintEvent(event);
}

// ============================================================================
// IdeEditor
// ============================================================================

IdeEditor::IdeEditor(QWidget* parent)
    : Flux::CodeEditor(nullptr, nullptr, parent) {

    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::onModificationChanged);
    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::updateCursorInfo);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::updateLineNumberArea);

    // Apply theme and reconnect on changes
    applyEditorTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &IdeEditor::applyEditorTheme);

    document()->setModified(false);
}

void IdeEditor::applyEditorTheme() {
    auto tc = currentTheme();
    QPalette p = palette();
    p.setColor(QPalette::Base, QColor(tc.bgEditor));
    p.setColor(QPalette::Text, QColor(tc.textPrimary));
    p.setColor(QPalette::Highlight, QColor(tc.accentBlue));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    setPalette(p);

    setStyleSheet(QString("QPlainTextEdit { background: %1; color: %2; }").arg(tc.bgEditor, tc.textPrimary));

    // Re-highlight with new theme colors
    if (auto* hl = qobject_cast<QSyntaxHighlighter*>(document()->findChild<QSyntaxHighlighter*>())) {
        hl->rehighlight();
    }

    updateLineNumberArea();
}

void IdeEditor::showEvent(QShowEvent* e) {
    QPlainTextEdit::showEvent(e);
    updateLineNumberAreaWidth();
    updateLineNumberArea();
}

void IdeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    updateLineNumberAreaWidth();
    // Position the LineNumberArea widget in the left margin
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(),
        lineNumberAreaWidth(), cr.height()));
}

void IdeEditor::setFilePath(const QString& path) {
    if (m_filePath != path) {
        m_filePath = path;
        emit filePathChanged(path);

        if (path.endsWith(".json", Qt::CaseInsensitive)) {
            setLanguage("json");
        } else {
            setLanguage("flux");
        }
    }
}

void IdeEditor::setLanguage(const QString& lang) {
    if (m_language != lang) {
        m_language = lang;
    }
}

bool IdeEditor::openFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    setPlainText(in.readAll());
    setFilePath(path);
    document()->setModified(false);

    return true;
}

bool IdeEditor::saveFile(const QString& path) {
    QString target = path.isEmpty() ? m_filePath : path;
    if (target.isEmpty()) {
        target = QFileDialog::getSaveFileName(this, "Save File", "",
            "FluxScript (*.flux);;JSON (*.json);;All Files (*)");
        if (target.isEmpty()) return false;
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Save Error", "Could not open file for writing.");
        return false;
    }

    QTextStream out(&file);
    out << toPlainText();

    setFilePath(target);
    document()->setModified(false);
    return true;
}

int IdeEditor::currentLine() const {
    return textCursor().blockNumber() + 1;
}

int IdeEditor::currentColumn() const {
    return textCursor().columnNumber() + 1;
}

void IdeEditor::goToLine(int line) {
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (block.isValid()) {
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::StartOfBlock);
        setTextCursor(cursor);
        centerCursor();
    }
}

// ============================================================================
// Line Numbers
// ============================================================================

int IdeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int maxBlock = qMax(1, document()->blockCount());
    while (maxBlock >= 10) {
        maxBlock /= 10;
        ++digits;
    }
    int space = 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 12;
    return space;
}

void IdeEditor::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void IdeEditor::updateLineNumberArea() {
    if (m_lineNumberArea) {
        m_lineNumberArea->update();
    }
}

void IdeEditor::setFont(const QFont& font) {
    QPlainTextEdit::setFont(font);
    updateLineNumberAreaWidth();
    updateLineNumberArea();
}

void IdeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    int areaWidth = lineNumberAreaWidth();

    // Background for line number gutter
    painter.fillRect(event->rect(), QColor(currentTheme().bgDarkest));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    int currentLine = textCursor().blockNumber();

    QFont lineFont = font();
    lineFont.setFamily("Consolas");
    lineFont.setPointSize(10);
    painter.setFont(lineFont);

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            QColor textColor = (blockNumber == currentLine)
                ? QColor("#d4d4d4")
                : QColor("#6b7280");
            painter.setPen(textColor);
            painter.drawText(0, top, areaWidth - 8,
                fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }

    // Right border separator
    painter.setPen(QColor(currentTheme().border));
    painter.drawLine(areaWidth - 1, 0, areaWidth - 1, height());
}

void IdeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
        selection.format.setBackground(isLight ? QColor("#e8edf4") : QColor("#2a2d2e"));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.movePosition(QTextCursor::StartOfBlock);
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}

// ============================================================================
// Key Events
// ============================================================================

void IdeEditor::keyPressEvent(QKeyEvent* e) {
    if (e->text() == "(" || e->text() == "{" || e->text() == "[" || e->text() == "\"") {
        handleBracketAutoPair(e);
        return;
    }

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        handleAutoIndent(e);
        return;
    }

    if (e->key() == Qt::Key_Tab && !e->modifiers().testFlag(Qt::ControlModifier)) {
        QTextCursor tc = textCursor();
        tc.insertText("    ");
        setTextCursor(tc);
        return;
    }

    Flux::CodeEditor::keyPressEvent(e);
}

void IdeEditor::handleBracketAutoPair(QKeyEvent* e) {
    QString text = e->text();
    QChar open = text.at(0);
    QChar close;
    bool isQuote = (open == '"');

    if (open == '(') close = ')';
    else if (open == '{') close = '}';
    else if (open == '[') close = ']';
    else close = QChar();

    QTextCursor tc = textCursor();

    if (!isQuote && !close.isNull()) {
        QString nextChar = tc.block().text().mid(tc.columnNumber(), 1);
        if (nextChar == QString(close)) {
            tc.movePosition(QTextCursor::Right);
            setTextCursor(tc);
            return;
        }
    }

    tc.insertText(QString(open) + close);
    tc.movePosition(QTextCursor::Left);
    setTextCursor(tc);
}

void IdeEditor::handleAutoIndent(QKeyEvent* e) {
    QTextCursor tc = textCursor();
    QString currentLine = tc.block().text();
    QString trimmed = currentLine.trimmed();

    int indent = 0;
    while (indent < currentLine.size() && currentLine.at(indent).isSpace()) {
        indent++;
    }

    bool increaseIndent = trimmed.endsWith('{') || trimmed.endsWith('(') || trimmed.endsWith('[');

    QTextCursor temp = tc;
    temp.movePosition(QTextCursor::EndOfBlock);
    QString blockText = temp.block().text();
    bool decreaseIndent = blockText.trimmed().startsWith('}') || blockText.trimmed().startsWith(')');

    if (increaseIndent) {
        indent += 4;
    }

    QString indentStr(indent, ' ');
    tc.insertText("\n" + indentStr);

    setTextCursor(tc);
}

void IdeEditor::handleBracketMatch() {
    QTextCursor tc = textCursor();
    int pos = tc.position();
    QTextBlock block = tc.block();
    QString blockText = block.text();
    int col = tc.columnNumber();

    if (col > 0) {
        QChar ch = blockText.at(col - 1);
        QChar expected;

        if (ch == '(') expected = ')';
        else if (ch == '{') expected = '}';
        else if (ch == '[') expected = ']';
        else if (ch == ')' || ch == '}' || ch == ']') {
            int searchCol = col - 2;
            int depth = 1;
            QChar openBracket = (ch == ')') ? '(' : (ch == '}') ? '{' : '[';

            while (searchCol >= 0 && depth > 0) {
                if (blockText.at(searchCol) == ch) depth++;
                else if (blockText.at(searchCol) == openBracket) depth--;
                searchCol--;
            }

            if (depth == 0) {
                // Match found
            }
            return;
        }

        if (!expected.isNull()) {
            int searchCol = col;
            int depth = 1;
            while (searchCol < blockText.size() && depth > 0) {
                if (blockText.at(searchCol) == ch) depth++;
                else if (blockText.at(searchCol) == expected) depth--;
                searchCol++;
            }
        }
    }
}

bool IdeEditor::isMatchingBracket(QChar open, QChar close) const {
    return (open == '(' && close == ')') ||
           (open == '{' && close == '}') ||
           (open == '[' && close == ']');
}

// ============================================================================
// Events
// ============================================================================

void IdeEditor::onModificationChanged() {
    emit modificationChanged(document()->isModified());
}

void IdeEditor::updateCursorInfo() {
    emit cursorPositionChanged(currentLine(), currentColumn());
}

void IdeEditor::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        static const int minSize = 6;
        static const int maxSize = 40;
        QFont f = font();
        int delta = e->angleDelta().y() > 0 ? 1 : -1;
        int newSize = f.pointSize() + delta;
        if (newSize >= minSize && newSize <= maxSize) {
            f.setPointSize(newSize);
            setFont(f);
        }
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

} // namespace IDE
