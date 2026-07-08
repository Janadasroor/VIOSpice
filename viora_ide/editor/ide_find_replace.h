/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_FIND_REPLACE_H
#define IDE_FIND_REPLACE_H

#include <QWidget>

class QLineEdit;
class QToolButton;
class QLabel;

namespace IDE {

class IdeEditor;

class IdeFindReplace : public QWidget {
    Q_OBJECT
public:
    explicit IdeFindReplace(IdeEditor* editor, QWidget* parent = nullptr);

    void setEditor(IdeEditor* editor);
    void activate();
    void deactivate();
    bool isVisible() const;

signals:
    void closeRequested();

private slots:
    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAll();
    void updateMatchCount();

private:
    void performFind(bool forward);

    IdeEditor* m_editor;
    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QToolButton* m_regexBtn;
    QToolButton* m_caseBtn;
    QLabel* m_matchCountLabel;
    bool m_regexEnabled = false;
    bool m_caseSensitive = false;
};

} // namespace IDE

#endif // IDE_FIND_REPLACE_H
