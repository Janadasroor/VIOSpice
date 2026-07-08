/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OUTPUT_PANEL_H
#define OUTPUT_PANEL_H

#include <QWidget>

class QTextEdit;
class QPushButton;
class QToolButton;

namespace IDE {

class OutputPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutputPanel(QWidget* parent = nullptr);
    void reapplyTheme();

    void appendOutput(const QString& message);
    void appendError(const QString& message);
    void appendInfo(const QString& message);
    void clear();

signals:
    void errorClicked(int lineNumber);

private slots:
    void onClearClicked();
    void onCopyClicked();
    void onTextClicked();

private:
    void setupUI();

    QTextEdit* m_output = nullptr;
    QToolButton* m_clearBtn = nullptr;
    QToolButton* m_copyBtn = nullptr;
    bool m_autoScroll = true;
};

} // namespace IDE

#endif // OUTPUT_PANEL_H
