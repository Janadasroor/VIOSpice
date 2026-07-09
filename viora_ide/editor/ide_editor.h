/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_EDITOR_H
#define IDE_EDITOR_H

#include "../../schematic/ui/flux_code_editor.h"

class QCompleter;
class QTimer;
class QLabel;

namespace IDE {

struct LspDiagnostic;

// Line number area widget (forward declaration)
class LineNumberArea;

class IdeEditor : public Flux::CodeEditor {
    Q_OBJECT
public:
    explicit IdeEditor(QWidget* parent = nullptr);

    void setFilePath(const QString& path);
    QString filePath() const { return m_filePath; }

    void setLanguage(const QString& lang);
    QString language() const { return m_language; }

    bool openFile(const QString& path);
    bool saveFile(const QString& path = QString());
    bool isModified() const { return document()->isModified(); }

    int currentLine() const;
    int currentColumn() const;
    void goToLine(int line);

    void setFont(const QFont& font);

    int lineNumberAreaWidth();
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    // LSP integration
    void applyDiagnostics(const QList<LspDiagnostic>& diagnostics);
    void showHoverTooltip(const QString& content, int line, int col);
    void showContextMenu(const QPoint& pos);

    // Word under cursor helpers
    QString wordAtPosition(const QPoint& pos) const;
    int cursorLine() const;
    int cursorColumn() const;

    // Hover state
    QTimer* m_hoverTimer = nullptr;
    QString m_lastHoverWord;
    int m_hoverLine = -1;
    int m_hoverCol = -1;
    QLabel* m_hoverLabel = nullptr;

signals:
    void filePathChanged(const QString& path);
    void modificationChanged(bool modified);
    void cursorPositionChanged(int line, int col);
    void contentsChangedForLsp(const QString& filePath, const QString& text, int version);
    void fileOpenedForLsp(const QString& filePath, const QString& text);
    void fileSavedForLsp(const QString& filePath);
    void hoverRequested(const QString& filePath, int line, int character);
    void goToDefinitionRequested(const QString& filePath, int line, int character);
    void findReferencesRequested(const QString& filePath, int line, int character);
    void formatDocumentRequested(const QString& filePath);
    void signatureHelpRequested(const QString& filePath, int line, int character);

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void changeEvent(QEvent* e) override;

    friend class LineNumberArea;

private slots:
    void onModificationChanged();
    void updateCursorInfo();
    void applyEditorTheme();

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea();
    void highlightCurrentLine();
    void handleBracketAutoPair(QKeyEvent* e);
    void handleAutoIndent(QKeyEvent* e);
    void handleBracketMatch();
    bool isMatchingBracket(QChar open, QChar close) const;

    LineNumberArea* m_lineNumberArea = nullptr;
    QString m_filePath;
    QString m_language;

    // LSP state
    QTimer* m_lspDebounceTimer = nullptr;
    int m_lspVersion = 0;
};

// Line number area widget
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(IdeEditor* editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    IdeEditor* m_editor;
};

} // namespace IDE

#endif // IDE_EDITOR_H
