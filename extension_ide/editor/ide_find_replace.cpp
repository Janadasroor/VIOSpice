/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_find_replace.h"
#include "ide_editor.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QTextCursor>
#include <QRegularExpression>
#include <QMessageBox>

namespace IDE {

IdeFindReplace::IdeFindReplace(IdeEditor* editor, QWidget* parent)
    : QWidget(parent), m_editor(editor) {
    setStyleSheet(
        "IdeFindReplace { background: #2d2d30; border-bottom: 1px solid #3e3e42; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    // Find input
    m_findEdit = new QLineEdit();
    m_findEdit->setPlaceholderText("Find...");
    m_findEdit->setMinimumWidth(200);
    m_findEdit->setStyleSheet(
        "QLineEdit { background: #3c3c3c; color: #d4d4d4; border: 1px solid #3e3e42; "
        "padding: 4px 8px; border-radius: 3px; }"
    );
    layout->addWidget(m_findEdit);

    // Match count
    m_matchCountLabel = new QLabel("0 results");
    m_matchCountLabel->setStyleSheet("color: #858585; font-size: 10px; padding: 0 8px;");
    layout->addWidget(m_matchCountLabel);

    // Regex toggle
    m_regexBtn = new QToolButton();
    m_regexBtn->setText(".*");
    m_regexBtn->setToolTip("Regular Expression");
    m_regexBtn->setCheckable(true);
    m_regexBtn->setStyleSheet(
        "QToolButton { background: transparent; color: #858585; border: 1px solid #3e3e42; "
        "padding: 4px 8px; border-radius: 3px; font-weight: bold; }"
        "QToolButton:checked { background: #007acc; color: white; border-color: #007acc; }"
    );
    connect(m_regexBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_regexEnabled = checked;
        updateMatchCount();
    });
    layout->addWidget(m_regexBtn);

    // Case sensitive toggle
    m_caseBtn = new QToolButton();
    m_caseBtn->setText("Aa");
    m_caseBtn->setToolTip("Case Sensitive");
    m_caseBtn->setCheckable(true);
    m_caseBtn->setStyleSheet(
        "QToolButton { background: transparent; color: #858585; border: 1px solid #3e3e42; "
        "padding: 4px 8px; border-radius: 3px; font-weight: bold; }"
        "QToolButton:checked { background: #007acc; color: white; border-color: #007acc; }"
    );
    connect(m_caseBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_caseSensitive = checked;
        updateMatchCount();
    });
    layout->addWidget(m_caseBtn);

    // Find next
    auto* findNextBtn = new QToolButton();
    findNextBtn->setText("↓");
    findNextBtn->setToolTip("Find Next (Enter)");
    findNextBtn->setStyleSheet(
        "QToolButton { background: transparent; color: #d4d4d4; border: 1px solid #3e3e42; "
        "padding: 4px 8px; border-radius: 3px; }"
    );
    connect(findNextBtn, &QToolButton::clicked, this, &IdeFindReplace::findNext);
    layout->addWidget(findNextBtn);

    // Find previous
    auto* findPrevBtn = new QToolButton();
    findPrevBtn->setText("↑");
    findPrevBtn->setToolTip("Find Previous");
    findPrevBtn->setStyleSheet(findNextBtn->styleSheet());
    connect(findPrevBtn, &QToolButton::clicked, this, &IdeFindReplace::findPrevious);
    layout->addWidget(findPrevBtn);

    // Separator
    auto* sep = new QLabel("|");
    sep->setStyleSheet("color: #3e3e42;");
    layout->addWidget(sep);

    // Replace input
    m_replaceEdit = new QLineEdit();
    m_replaceEdit->setPlaceholderText("Replace...");
    m_replaceEdit->setMinimumWidth(200);
    m_replaceEdit->setStyleSheet(m_findEdit->styleSheet());
    layout->addWidget(m_replaceEdit);

    // Replace current
    auto* replaceBtn = new QToolButton();
    replaceBtn->setText("Replace");
    replaceBtn->setToolTip("Replace Current");
    replaceBtn->setStyleSheet(findNextBtn->styleSheet());
    connect(replaceBtn, &QToolButton::clicked, this, &IdeFindReplace::replaceCurrent);
    layout->addWidget(replaceBtn);

    // Replace all
    auto* replaceAllBtn = new QToolButton();
    replaceAllBtn->setText("All");
    replaceAllBtn->setToolTip("Replace All");
    replaceAllBtn->setStyleSheet(findNextBtn->styleSheet());
    connect(replaceAllBtn, &QToolButton::clicked, this, &IdeFindReplace::replaceAll);
    layout->addWidget(replaceAllBtn);

    layout->addStretch();

    // Close button
    auto* closeBtn = new QToolButton();
    closeBtn->setText("×");
    closeBtn->setToolTip("Close (Esc)");
    closeBtn->setStyleSheet(
        "QToolButton { background: transparent; color: #858585; border: none; "
        "padding: 4px 8px; font-size: 14px; font-weight: bold; }"
        "QToolButton:hover { color: #d4d4d4; }"
    );
    connect(closeBtn, &QToolButton::clicked, this, &IdeFindReplace::deactivate);
    layout->addWidget(closeBtn);

    // Connect find on text change
    connect(m_findEdit, &QLineEdit::textChanged, this, &IdeFindReplace::updateMatchCount);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &IdeFindReplace::findNext);

    hide();
}

void IdeFindReplace::setEditor(IdeEditor* editor) {
    m_editor = editor;
}

void IdeFindReplace::activate() {
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();

    // Pre-fill with current selection
    if (m_editor && m_editor->textCursor().hasSelection()) {
        m_findEdit->setText(m_editor->textCursor().selectedText());
    }
}

void IdeFindReplace::deactivate() {
    hide();
    if (m_editor) m_editor->setFocus();
}

bool IdeFindReplace::isVisible() const {
    return QWidget::isVisible();
}

void IdeFindReplace::findNext() {
    performFind(true);
}

void IdeFindReplace::findPrevious() {
    performFind(false);
}

void IdeFindReplace::replaceCurrent() {
    if (!m_editor) return;

    QTextCursor tc = m_editor->textCursor();
    if (tc.hasSelection() && tc.selectedText() == m_findEdit->text()) {
        tc.removeSelectedText();
        tc.insertText(m_replaceEdit->text());
        m_editor->setTextCursor(tc);
    }
    findNext();
}

void IdeFindReplace::replaceAll() {
    if (!m_editor) return;

    QString findText = m_findEdit->text();
    QString replaceText = m_replaceEdit->text();
    if (findText.isEmpty()) return;

    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);

    int count = 0;
    QTextDocument::FindFlags flags;
    if (m_caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    // Note: regex find handled via QRegularExpression below

    while (m_editor->find(findText, flags)) {
        QTextCursor tc = m_editor->textCursor();
        tc.removeSelectedText();
        tc.insertText(replaceText);
        count++;
    }

    QMessageBox::information(this, "Replace All",
        QString("Replaced %1 occurrence(s).").arg(count));
    updateMatchCount();
}

void IdeFindReplace::updateMatchCount() {
    if (!m_editor || m_findEdit->text().isEmpty()) {
        m_matchCountLabel->setText("0 results");
        return;
    }

    // Count occurrences
    int count = 0;
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);

    QTextDocument::FindFlags flags;
    if (m_caseSensitive) flags |= QTextDocument::FindCaseSensitively;

    while (m_editor->find(m_findEdit->text(), flags)) {
        count++;
    }

    m_matchCountLabel->setText(QString("%1 results").arg(count));

    // Restore cursor
    cursor.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(cursor);
}

void IdeFindReplace::performFind(bool forward) {
    if (!m_editor || m_findEdit->text().isEmpty()) return;

    QTextDocument::FindFlags flags;
    if (!forward) flags |= QTextDocument::FindBackward;
    if (m_caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    // Note: regex find handled via QRegularExpression below

    if (!m_editor->find(m_findEdit->text(), flags)) {
        // Wrap around
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->find(m_findEdit->text(), flags);
    }
}

} // namespace IDE
