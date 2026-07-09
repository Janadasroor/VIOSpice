/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_editor.h"
#include "ide_highlighter.h"
#include "../core/ide_theme.h"
#include "../core/lsp_client.h"
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
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QApplication>

namespace IDE {

static QString markdownToHtml(const QString& markdown) {
    QString html;
    int i = 0;
    bool inCodeBlock = false;
    bool inInlineCode = false;
    bool inBold = false;
    
    while (i < markdown.length()) {
        // Check for triple backticks (code block)
        if (markdown.mid(i, 3) == "```") {
            if (inCodeBlock) {
                html += "</pre></div>";
                inCodeBlock = false;
            } else {
                // Check if there is a language name, e.g., ```fluxscript
                int nextNewline = markdown.indexOf('\n', i + 3);
                QString lang = "";
                if (nextNewline != -1) {
                    lang = markdown.mid(i + 3, nextNewline - (i + 3)).trimmed();
                    i = nextNewline; // Skip the line containing language name
                } else {
                    i += 2;
                }
                html += "<div style='background:#0f172a; padding:6px 8px; border-radius:4px; margin-bottom:6px; border:1px solid #1e293b;'><pre style='margin:0; font-family:\"Fira Code\",Consolas,monospace; color:#38bdf8;'>";
                inCodeBlock = true;
            }
            i += 3;
            continue;
        }
        
        // Check for inline backtick
        if (markdown[i] == '`') {
            if (inInlineCode) {
                html += "</code>";
                inInlineCode = false;
            } else {
                html += "<code style='background:#1e293b; padding:2px 4px; border-radius:3px; font-family:monospace; color:#f472b6;'>";
                inInlineCode = true;
            }
            i++;
            continue;
        }
        
        // Check for bold **
        if (markdown.mid(i, 2) == "**") {
            if (inBold) {
                html += "</b>";
                inBold = false;
            } else {
                html += "<b>";
                inBold = true;
            }
            i += 2;
            continue;
        }
        
        // Check for newline
        if (markdown[i] == '\n') {
            if (inCodeBlock) {
                html += "\n";
            } else {
                html += "<br>";
            }
            i++;
            continue;
        }
        
        // Standard character escape
        if (markdown[i] == '<') {
            html += "&lt;";
        } else if (markdown[i] == '>') {
            html += "&gt;";
        } else {
            html += markdown[i];
        }
        i++;
    }
    
    // Close any unclosed tags
    if (inCodeBlock) html += "</pre></div>";
    if (inInlineCode) html += "</code>";
    if (inBold) html += "</b>";
    
    return html;
}

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

    // LSP debounce timer — must be created BEFORE textChanged connections
    // because applyEditorTheme() triggers rehighlight() → textChanged
    m_lspDebounceTimer = new QTimer(this);
    m_lspDebounceTimer->setSingleShot(true);
    m_lspDebounceTimer->setInterval(300);
    connect(m_lspDebounceTimer, &QTimer::timeout, this, [this]() {
        if (!m_filePath.isEmpty()) {
            m_lspVersion++;
            emit contentsChangedForLsp(m_filePath, toPlainText(), m_lspVersion);
        }
    });

    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::onModificationChanged);
    connect(this, &QPlainTextEdit::textChanged, this, &IdeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        m_lspDebounceTimer->start();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::updateCursorInfo);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &IdeEditor::updateLineNumberArea);

    // Enable mouse tracking for hover
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    // Hover debounce timer — fires 400ms after mouse stops moving
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(400);
    connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
        if (!m_filePath.isEmpty() && m_hoverLine >= 0) {
            emit hoverRequested(m_filePath, m_hoverLine, m_hoverCol);
        }
    });

    // Context menu
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &IdeEditor::showContextMenu);

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
    emit fileSavedForLsp(target);
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
    // Forward window-level shortcuts to parent so QShortcut objects fire
    if (e->modifiers() & Qt::ControlModifier) {
        bool isWindowShortcut = (e->key() == Qt::Key_B) ||           // Ctrl+B
                                (e->key() == Qt::Key_P && e->modifiers() & Qt::ShiftModifier) || // Ctrl+Shift+P
                                (e->key() == Qt::Key_E) ||           // Ctrl+E
                                (e->key() == Qt::Key_G) ||           // Ctrl+G
                                (e->key() == Qt::Key_Backtab) ||     // Ctrl+Tab
                                (e->key() == Qt::Key_Tab);           // Ctrl+Tab
        if (isWindowShortcut && parentWidget()) {
            QKeyEvent fwd(e->type(), e->key(), e->modifiers(), e->text());
            QApplication::sendEvent(parentWidget(), &fwd);
            if (fwd.isAccepted()) {
                e->accept();
                return;
            }
        }
    }

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

    // Signature help on '('
    if (e->text() == "(") {
        Flux::CodeEditor::keyPressEvent(e);
        // Request signature help after inserting the '('
        if (!m_filePath.isEmpty()) {
            int line = cursorLine() - 1;
            int col = cursorColumn() - 1;
            emit signatureHelpRequested(m_filePath, line, col);
        }
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

void IdeEditor::setFilePath(const QString& path) {
    if (m_filePath != path) {
        // Emit didClose for old file
        if (!m_filePath.isEmpty()) {
            // Window will handle closing the old document in LSP
        }

        m_filePath = path;
        m_lspVersion = 0;
        emit filePathChanged(path);

        if (path.endsWith(".json", Qt::CaseInsensitive)) {
            setLanguage("json");
        } else {
            setLanguage("flux");
        }

        // Notify window that a new file is opened for LSP
        if (!path.isEmpty()) {
            emit fileOpenedForLsp(path, toPlainText());
        }
    }
}

void IdeEditor::applyDiagnostics(const QList<LspDiagnostic>& diagnostics) {
    QList<QTextEdit::ExtraSelection> selections = extraSelections();

    // Remove old diagnostic selections (they have a specific property)
    for (int i = selections.size() - 1; i >= 0; --i) {
        if (selections[i].format.hasProperty(QTextFormat::UserProperty)) {
            selections.removeAt(i);
        }
    }

    for (const LspDiagnostic& diag : diagnostics) {
        QTextBlock block = document()->findBlockByNumber(diag.range.start.line);
        if (!block.isValid()) continue;

        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(block);
        sel.cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, diag.range.start.character);
        sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
            qMax(1, diag.range.end.character - diag.range.start.character));

        QTextCharFormat fmt;
        if (diag.isError()) {
            fmt.setForeground(QColor("#f44336"));
            fmt.setFontUnderline(true);
            fmt.setUnderlineColor(QColor("#f44336"));
            fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        } else if (diag.isWarning()) {
            fmt.setForeground(QColor("#ff9800"));
            fmt.setFontUnderline(true);
            fmt.setUnderlineColor(QColor("#ff9800"));
            fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        } else {
            fmt.setForeground(QColor("#2196f3"));
            fmt.setFontUnderline(true);
            fmt.setUnderlineColor(QColor("#2196f3"));
            fmt.setUnderlineStyle(QTextCharFormat::DashUnderline);
        }

        sel.format = fmt;
        sel.format.setProperty(QTextFormat::UserProperty, true); // Mark as diagnostic
        selections.append(sel);
    }

    setExtraSelections(selections);
}

void IdeEditor::showHoverTooltip(const QString& content, int line, int col) {
    if (content.isEmpty()) {
        if (m_hoverLabel) m_hoverLabel->hide();
        return;
    }

    QTextBlock block = document()->findBlockByNumber(line);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, col);

    QRect r = cursorRect(cursor);

    // Convert markdown content to rich text HTML
    QString tooltip = markdownToHtml(content);

    // Create or reuse a floating QLabel for the tooltip
    if (!m_hoverLabel) {
        m_hoverLabel = new QLabel(this);
        m_hoverLabel->setWindowFlags(Qt::SubWindow | Qt::FramelessWindowHint);
        m_hoverLabel->setMargin(10);
        m_hoverLabel->setTextFormat(Qt::RichText);
    }

    m_hoverLabel->setText(QString(
        "<div style='background:rgba(15, 23, 42, 0.96); color:#cbd5e1; border:1px solid #334155; "
        "border-top:3px solid #3b82f6; padding:10px 14px; border-radius:6px; "
        "font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif; "
        "font-size:12px; line-height:1.45; max-width:550px;'>%1</div>"
    ).arg(tooltip));

    // Position below the cursor, ensuring left edge clears line number gutter
    QPoint localPos = r.bottomLeft() + QPoint(0, 4);
    int gutterWidth = lineNumberAreaWidth() + 4;
    if (localPos.x() < gutterWidth) {
        localPos.setX(gutterWidth);
    }
    m_hoverLabel->move(localPos);
    m_hoverLabel->adjustSize();
    m_hoverLabel->show();
}

// ============================================================================
// Context Menu (right-click)
// ============================================================================

void IdeEditor::showContextMenu(const QPoint& pos) {
    QMenu menu;
    menu.setStyleSheet(
        "QMenu { background: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"
        "QMenu::item { padding: 6px 24px 6px 12px; }"
        "QMenu::item:selected { background: #3b82f6; color: white; }"
        "QMenu::separator { height: 1px; background: #334155; margin: 4px 8px; }"
    );

    QString filePath = m_filePath;
    int line = cursorLine() - 1; // 0-based for LSP
    int col = cursorColumn() - 1;

    // LSP actions
    QAction* goToDef = menu.addAction("Go to Definition");
    goToDef->setShortcut(QKeySequence("F12"));
    connect(goToDef, &QAction::triggered, this, [this, filePath, line, col]() {
        emit goToDefinitionRequested(filePath, line, col);
    });

    QAction* findRefs = menu.addAction("Find All References");
    findRefs->setShortcut(QKeySequence("Shift+F12"));
    connect(findRefs, &QAction::triggered, this, [this, filePath, line, col]() {
        emit findReferencesRequested(filePath, line, col);
    });

    menu.addSeparator();

    QAction* format = menu.addAction("Format Document");
    format->setShortcut(QKeySequence("Ctrl+Shift+F"));
    connect(format, &QAction::triggered, this, [this, filePath]() {
        emit formatDocumentRequested(filePath);
    });

    menu.addSeparator();

    // Standard edit actions
    QAction* cut = menu.addAction("Cut");
    cut->setShortcut(QKeySequence::Cut);
    connect(cut, &QAction::triggered, this, &QPlainTextEdit::cut);

    QAction* copy = menu.addAction("Copy");
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, &QPlainTextEdit::copy);

    QAction* paste = menu.addAction("Paste");
    paste->setShortcut(QKeySequence::Paste);
    connect(paste, &QAction::triggered, this, &QPlainTextEdit::paste);

    QAction* selectAll = menu.addAction("Select All");
    selectAll->setShortcut(QKeySequence::SelectAll);
    connect(selectAll, &QAction::triggered, this, &QPlainTextEdit::selectAll);

    menu.exec(mapToGlobal(pos));
}

// ============================================================================
// Word at position
// ============================================================================

QString IdeEditor::wordAtPosition(const QPoint& pos) const {
    QTextCursor tc = cursorForPosition(pos);
    QTextBlock block = tc.block();
    QString text = block.text();
    int col = tc.columnNumber();

    if (col > 0 && col <= text.size()) {
        int start = col - 1;
        while (start > 0 && (text[start - 1].isLetterOrNumber() || text[start - 1] == '_')) {
            start--;
        }
        int end = col;
        while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == '_')) {
            end++;
        }
        return text.mid(start, end - start);
    }
    return QString();
}

int IdeEditor::cursorLine() const {
    return textCursor().blockNumber() + 1;
}

int IdeEditor::cursorColumn() const {
    return textCursor().columnNumber() + 1;
}

// ============================================================================
// Mouse events (hover tracking)
// ============================================================================

void IdeEditor::mouseMoveEvent(QMouseEvent* e) {
    QPlainTextEdit::mouseMoveEvent(e);

    QTextCursor tc = cursorForPosition(e->pos());
    QString word = wordAtPosition(e->pos());

    // Only request hover if word changed
    if (word != m_lastHoverWord || tc.blockNumber() != m_hoverLine) {
        m_lastHoverWord = word;
        m_hoverLine = tc.blockNumber();
        m_hoverCol = tc.columnNumber();

        if (!word.isEmpty() && !m_filePath.isEmpty()) {
            // Debounce — wait 400ms after mouse stops
            m_hoverTimer->start();
        } else {
            m_hoverTimer->stop();
            if (m_hoverLabel) m_hoverLabel->hide();
        }
    }
}

void IdeEditor::leaveEvent(QEvent* e) {
    QPlainTextEdit::leaveEvent(e);
    m_hoverTimer->stop();
    if (m_hoverLabel) m_hoverLabel->hide();
    m_lastHoverWord.clear();
}

void IdeEditor::focusOutEvent(QFocusEvent* e) {
    QPlainTextEdit::focusOutEvent(e);
    if (m_hoverLabel) m_hoverLabel->hide();
}

void IdeEditor::changeEvent(QEvent* e) {
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::ActivationChange) {
        if (!isActiveWindow()) {
            if (m_hoverLabel) m_hoverLabel->hide();
        }
    }
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
