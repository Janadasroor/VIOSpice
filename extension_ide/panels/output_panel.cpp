/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "output_panel.h"
#include "../core/ide_theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QToolButton>
#include <QTextBlock>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>

namespace IDE {

OutputPanel::OutputPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    ThemeManager::instance().registerThemeCallback(this, [this]() { reapplyTheme(); });
}

void OutputPanel::setupUI() {
    auto tc = currentTheme();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto* toolbar = new QWidget();
    toolbar->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;").arg(tc.bgPanel, tc.border)
    );
    auto* toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(12, 6, 12, 6);

    auto* titleLabel = new QLabel("Output");
    titleLabel->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 10pt; padding: 0 4px;").arg(tc.textPrimary)
    );
    toolLayout->addWidget(titleLabel);

    toolLayout->addStretch();

    m_copyBtn = new QToolButton();
    m_copyBtn->setText("Copy");
    m_copyBtn->setToolTip("Copy all output");
    m_copyBtn->setStyleSheet(
        QString(
            "QToolButton { background: transparent; color: %1; border: none; padding: 4px 10px; font-size: 9pt; border-radius: 4px; }"
            "QToolButton:hover { color: %2; background: %3; }"
        ).arg(tc.textSecondary, tc.textPrimary, tc.border)
    );
    connect(m_copyBtn, &QToolButton::clicked, this, &OutputPanel::onCopyClicked);
    toolLayout->addWidget(m_copyBtn);

    m_clearBtn = new QToolButton();
    m_clearBtn->setText("Clear");
    m_clearBtn->setToolTip("Clear output");
    m_clearBtn->setStyleSheet(m_copyBtn->styleSheet());
    connect(m_clearBtn, &QToolButton::clicked, this, &OutputPanel::onClearClicked);
    toolLayout->addWidget(m_clearBtn);

    layout->addWidget(toolbar);

    // Output text area
    m_output = new QTextEdit();
    m_output->setReadOnly(true);
    m_output->setStyleSheet(
        QString(
            "QTextEdit { background: %1; color: %2; border: none; "
            "  font-family: 'Consolas', monospace; font-size: 10pt; padding: 12px 16px; }"
        ).arg(tc.bgDarkest, tc.textPrimary)
    );
    m_output->setPlaceholderText("Extension output will appear here...");
    m_output->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_output->setMaximumHeight(200);

    connect(m_output, &QTextEdit::cursorPositionChanged, this, &OutputPanel::onTextClicked);

    layout->addWidget(m_output);
}

void OutputPanel::appendOutput(const QString& message) {
    m_output->append(QString("<span style='color: %1;'>%2</span>").arg(currentTheme().textPrimary, message.toHtmlEscaped()));
}

void OutputPanel::appendError(const QString& message) {
    m_output->append(QString("<span style='color: %1;'>%2</span>").arg(currentTheme().red, message.toHtmlEscaped()));
}

void OutputPanel::appendInfo(const QString& message) {
    m_output->append(QString("<span style='color: %1;'>%2</span>").arg(currentTheme().accentBlue, message.toHtmlEscaped()));
}

void OutputPanel::clear() {
    m_output->clear();
}

void OutputPanel::onClearClicked() {
    clear();
}

void OutputPanel::onCopyClicked() {
    QApplication::clipboard()->setText(m_output->toPlainText());
}

void OutputPanel::onTextClicked() {
    QTextCursor cursor = m_output->textCursor();
    QString line = cursor.block().text();

    QRegularExpression lineRe(R"(Line (\d+))");
    QRegularExpressionMatch match = lineRe.match(line);
    if (match.hasMatch()) {
        int lineNum = match.captured(1).toInt();
        emit errorClicked(lineNum);
    }
}

void OutputPanel::reapplyTheme() {
    auto tc = currentTheme();
    if (m_output) {
        m_output->setStyleSheet(
            QString(
                "QTextEdit { background: %1; color: %2; border: none; "
                "  font-family: 'Consolas', monospace; font-size: 10pt; padding: 12px 16px; }"
            ).arg(tc.bgDarkest, tc.textPrimary)
        );
    }
}

} // namespace IDE
