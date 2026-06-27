/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gui_manager.h"

#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QTextEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTabBar>
#include <QToolBar>

GuiManager& GuiManager::instance() {
    static GuiManager s_instance;
    return s_instance;
}

GuiManager::GuiManager(QObject* parent)
    : QObject(parent) {
}

// ============================================================================
// Fuzzy match with scoring: exact > starts-with > contains
// ============================================================================

static int matchScore(const QString& candidate, const QString& query) {
    if (query.isEmpty() || candidate.isEmpty()) return 0;

    QString c = candidate.toLower();
    QString q = query.toLower();

    if (c == q) return 100;                    // Exact match
    if (c.startsWith(q)) return 80;            // Starts with
    if (c.contains(q)) return 60;              // Contains
    if (candidate.contains(query, Qt::CaseInsensitive)) return 60;

    return 0;
}

bool GuiManager::fuzzyMatch(const QString& candidate, const QString& query) {
    return matchScore(candidate, query) > 0;
}

// ============================================================================
// Window lookup
// ============================================================================

QWidget* GuiManager::findWindow(const QString& name) const {
    if (name.isEmpty()) return nullptr;

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget->isVisible()) continue;

        if (widget->metaObject()->className() == name)
            return widget;
        if (widget->windowTitle().contains(name, Qt::CaseInsensitive))
            return widget;
        if (QString(widget->metaObject()->className()).contains(name, Qt::CaseInsensitive))
            return widget;
        if (widget->objectName() == name)
            return widget;
    }
    return nullptr;
}

// ============================================================================
// Child widget lookup by label/objectName/tooltip
// ============================================================================

QWidget* GuiManager::findChildByLabel(QWidget* parent, const QString& label) const {
    if (!parent || label.isEmpty()) return nullptr;

    QWidget* bestMatch = nullptr;
    int bestScore = 0;

    auto checkWidget = [&](QWidget* widget, int extraScore = 0) {
        if (!widget || !widget->isVisible()) return;
        int score = matchScore(widget->metaObject()->className(), label);
        if (score > bestScore) { bestScore = score; bestMatch = widget; }
    };

    for (QPushButton* btn : parent->findChildren<QPushButton*>()) {
        if (!btn->isVisible()) continue;
        int s = 0;
        if ((s = matchScore(btn->text(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        else if ((s = matchScore(btn->toolTip(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        else if ((s = matchScore(btn->objectName(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
    }

    for (QToolButton* btn : parent->findChildren<QToolButton*>()) {
        if (!btn->isVisible()) continue;
        int s = 0;
        if ((s = matchScore(btn->text(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        else if ((s = matchScore(btn->toolTip(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        else if ((s = matchScore(btn->objectName(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        else if (btn->defaultAction()) {
            if ((s = matchScore(btn->defaultAction()->text(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
            else if ((s = matchScore(btn->defaultAction()->toolTip(), label)) > bestScore) { bestScore = s; bestMatch = btn; }
        }
    }

    for (QLineEdit* edit : parent->findChildren<QLineEdit*>()) {
        if (!edit->isVisible()) continue;
        int s = 0;
        if ((s = matchScore(edit->objectName(), label)) > bestScore) { bestScore = s; bestMatch = edit; }
        else if ((s = matchScore(edit->placeholderText(), label)) > bestScore) { bestScore = s; bestMatch = edit; }
    }

    return bestMatch;
}

// ============================================================================
// Action lookup by text in menus and toolbars
// ============================================================================

QAction* GuiManager::findActionByText(QWidget* window, const QString& text) const {
    if (!window || text.isEmpty()) return nullptr;

    QAction* bestMatch = nullptr;
    int bestScore = 0;

    QMenuBar* menuBar = window->findChild<QMenuBar*>();
    if (menuBar) {
        for (QMenu* menu : menuBar->findChildren<QMenu*>()) {
            for (QAction* action : menu->actions()) {
                if (action->isSeparator()) continue;
                int s = 0;
                if ((s = matchScore(action->text(), text)) > bestScore) { bestScore = s; bestMatch = action; }
                else if ((s = matchScore(action->toolTip(), text)) > bestScore) { bestScore = s; bestMatch = action; }
            }
        }
    }

    for (QToolBar* toolbar : window->findChildren<QToolBar*>()) {
        for (QAction* action : toolbar->actions()) {
            if (action->isSeparator()) continue;
            int s = 0;
            if ((s = matchScore(action->text(), text)) > bestScore) { bestScore = s; bestMatch = action; }
            else if ((s = matchScore(action->toolTip(), text)) > bestScore) { bestScore = s; bestMatch = action; }
        }
    }

    return bestMatch;
}

// ============================================================================
// Element discovery
// ============================================================================

QVariantList GuiManager::listElements(const QString& windowName,
                                      const QString& filterType,
                                      const QString& filterParent) const {
    QVariantList result;
    QWidget* window = findWindow(windowName);
    if (!window) return result;

    if (filterType.isEmpty() || filterType == "QToolButton") {
        for (QToolButton* btn : window->findChildren<QToolButton*>()) {
            if (!btn->isVisible()) continue;

            QString parentName;
            if (QToolBar* tb = qobject_cast<QToolBar*>(btn->parentWidget()))
                parentName = tb->objectName();

            if (!filterParent.isEmpty() && !parentName.contains(filterParent, Qt::CaseInsensitive))
                continue;

            QString label;
            if (btn->defaultAction())
                label = btn->defaultAction()->text().remove("&");
            if (label.isEmpty()) label = btn->text();
            if (label.isEmpty()) label = btn->toolTip();
            if (label.isEmpty()) label = btn->objectName();

            QVariantMap elem;
            elem["type"] = "QToolButton";
            elem["label"] = label;
            elem["objectName"] = btn->objectName();
            QPoint gp = btn->mapToGlobal(QPoint(0, 0));
            elem["x"] = gp.x();
            elem["y"] = gp.y();
            elem["w"] = btn->width();
            elem["h"] = btn->height();
            elem["enabled"] = btn->isEnabled();
            elem["parentName"] = parentName;
            result.append(elem);
        }
    }

    if (filterType.isEmpty() || filterType == "QPushButton") {
        for (QPushButton* btn : window->findChildren<QPushButton*>()) {
            if (!btn->isVisible()) continue;

            QVariantMap elem;
            elem["type"] = "QPushButton";
            elem["label"] = btn->text().remove("&");
            elem["objectName"] = btn->objectName();
            QPoint gp = btn->mapToGlobal(QPoint(0, 0));
            elem["x"] = gp.x();
            elem["y"] = gp.y();
            elem["w"] = btn->width();
            elem["h"] = btn->height();
            elem["enabled"] = btn->isEnabled();
            result.append(elem);
        }
    }

    if (filterType.isEmpty() || filterType == "QAction") {
        QMenuBar* menuBar = window->findChild<QMenuBar*>();
        if (menuBar) {
            for (QAction* action : menuBar->actions()) {
                if (action->isSeparator()) continue;

                QMenu* menu = action->menu();
                QString menuName = menu ? menu->title().remove("&") : "MenuBar";

                QVariantMap elem;
                elem["type"] = "QAction";
                elem["label"] = action->text().remove("&");
                elem["objectName"] = action->objectName();
                elem["enabled"] = action->isEnabled();
                elem["parentName"] = menuName;
                result.append(elem);
            }
        }
    }

    if (filterType.isEmpty() || filterType == "QLineEdit") {
        for (QLineEdit* edit : window->findChildren<QLineEdit*>()) {
            if (!edit->isVisible()) continue;

            QVariantMap elem;
            elem["type"] = "QLineEdit";
            elem["label"] = edit->placeholderText();
            elem["objectName"] = edit->objectName();
            QPoint gp = edit->mapToGlobal(QPoint(0, 0));
            elem["x"] = gp.x();
            elem["y"] = gp.y();
            elem["w"] = edit->width();
            elem["h"] = edit->height();
            elem["enabled"] = edit->isEnabled();
            result.append(elem);
        }
    }

    return result;
}

// ============================================================================
// Click action
// ============================================================================

QVariantMap GuiManager::clickButton(const QString& windowName, const QString& target) {
    QVariantMap resp;
    resp["ok"] = false;

    QWidget* window = findWindow(windowName);
    if (!window) {
        resp["error"] = QString("Window not found: %1").arg(windowName);
        return resp;
    }

    if (QAction* action = findActionByText(window, target)) {
        action->trigger();
        resp["ok"] = true;
        resp["type"] = "QAction";
        resp["label"] = action->text().remove("&");
        return resp;
    }

    if (QWidget* widget = findChildByLabel(window, target)) {
        if (!widget->isEnabled()) {
            resp["error"] = QString("Widget is disabled: %1").arg(target);
            return resp;
        }

        QPoint center = widget->rect().center();
        QPoint globalPos = widget->mapToGlobal(center);

        QMouseEvent pressEvent(QEvent::MouseButtonPress, center, globalPos,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &pressEvent);

        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, center, globalPos,
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &releaseEvent);

        resp["ok"] = true;
        resp["type"] = widget->metaObject()->className();
        resp["label"] = target;
        return resp;
    }

    resp["error"] = QString("No matching widget found: %1").arg(target);
    return resp;
}

// ============================================================================
// Type in field
// ============================================================================

QVariantMap GuiManager::typeInField(const QString& windowName, const QString& objectName,
                                    const QString& text, bool append) {
    QVariantMap resp;
    resp["ok"] = false;

    QWidget* window = findWindow(windowName);
    if (!window) {
        resp["error"] = QString("Window not found: %1").arg(windowName);
        return resp;
    }

    for (QLineEdit* edit : window->findChildren<QLineEdit*>()) {
        if (edit->objectName() == objectName || fuzzyMatch(edit->objectName(), objectName)) {
            if (append) {
                edit->insert(text);
            } else {
                edit->setText(text);
            }
            resp["ok"] = true;
            resp["field"] = edit->objectName();
            return resp;
        }
    }

    for (QTextEdit* edit : window->findChildren<QTextEdit*>()) {
        if (edit->objectName() == objectName || fuzzyMatch(edit->objectName(), objectName)) {
            if (append) {
                edit->insertPlainText(text);
            } else {
                edit->setPlainText(text);
            }
            resp["ok"] = true;
            resp["field"] = edit->objectName();
            return resp;
        }
    }

    resp["error"] = QString("Field not found: %1").arg(objectName);
    return resp;
}

// ============================================================================
// Menu action trigger
// ============================================================================

QVariantMap GuiManager::triggerMenuAction(const QString& windowName, const QString& actionText) {
    QVariantMap resp;
    resp["ok"] = false;

    QWidget* window = findWindow(windowName);
    if (!window) {
        resp["error"] = QString("Window not found: %1").arg(windowName);
        return resp;
    }

    if (QAction* action = findActionByText(window, actionText)) {
        action->trigger();
        resp["ok"] = true;
        resp["action"] = action->text().remove("&");
        return resp;
    }

    resp["error"] = QString("Menu action not found: %1").arg(actionText);
    return resp;
}

// ============================================================================
// Press key / keyboard shortcut
// ============================================================================

QVariantMap GuiManager::pressKey(const QString& windowName, const QString& shortcut) {
    QVariantMap resp;
    resp["ok"] = false;

    QWidget* window = findWindow(windowName);
    if (!window) {
        resp["error"] = QString("Window not found: %1").arg(windowName);
        return resp;
    }

    QWidget* focusWidget = window->focusWidget();
    if (!focusWidget) focusWidget = window;

    Qt::KeyboardModifiers mods = Qt::NoModifier;
    QString keyStr = shortcut;

    // Parse modifier keys
    while (true) {
        if (keyStr.startsWith("Ctrl+", Qt::CaseInsensitive)) {
            mods |= Qt::ControlModifier;
            keyStr = keyStr.mid(5);
        } else if (keyStr.startsWith("Alt+", Qt::CaseInsensitive)) {
            mods |= Qt::AltModifier;
            keyStr = keyStr.mid(4);
        } else if (keyStr.startsWith("Shift+", Qt::CaseInsensitive)) {
            mods |= Qt::ShiftModifier;
            keyStr = keyStr.mid(6);
        } else if (keyStr.startsWith("Meta+", Qt::CaseInsensitive)) {
            mods |= Qt::MetaModifier;
            keyStr = keyStr.mid(5);
        } else {
            break;
        }
    }

    // Map key string to Qt::Key
    int key = 0;
    if (keyStr.length() == 1) {
        key = keyStr.at(0).toUpper().unicode();
    } else if (keyStr == "Escape") {
        key = Qt::Key_Escape;
    } else if (keyStr == "Return" || keyStr == "Enter") {
        key = Qt::Key_Return;
    } else if (keyStr == "Tab") {
        key = Qt::Key_Tab;
    } else if (keyStr == "Backspace") {
        key = Qt::Key_Backspace;
    } else if (keyStr == "Delete") {
        key = Qt::Key_Delete;
    } else if (keyStr == "Space") {
        key = Qt::Key_Space;
    } else if (keyStr == "Up") {
        key = Qt::Key_Up;
    } else if (keyStr == "Down") {
        key = Qt::Key_Down;
    } else if (keyStr == "Left") {
        key = Qt::Key_Left;
    } else if (keyStr == "Right") {
        key = Qt::Key_Right;
    } else if (keyStr == "Home") {
        key = Qt::Key_Home;
    } else if (keyStr == "End") {
        key = Qt::Key_End;
    } else if (keyStr == "PageUp") {
        key = Qt::Key_PageUp;
    } else if (keyStr == "PageDown") {
        key = Qt::Key_PageDown;
    } else if (keyStr == "F1") {
        key = Qt::Key_F1;
    } else if (keyStr == "F2") {
        key = Qt::Key_F2;
    } else if (keyStr == "F3") {
        key = Qt::Key_F3;
    } else if (keyStr == "F4") {
        key = Qt::Key_F4;
    } else if (keyStr == "F5") {
        key = Qt::Key_F5;
    } else if (keyStr == "F6") {
        key = Qt::Key_F6;
    } else if (keyStr == "F7") {
        key = Qt::Key_F7;
    } else if (keyStr == "F8") {
        key = Qt::Key_F8;
    } else if (keyStr == "F9") {
        key = Qt::Key_F9;
    } else if (keyStr == "F10") {
        key = Qt::Key_F10;
    } else if (keyStr == "F11") {
        key = Qt::Key_F11;
    } else if (keyStr == "F12") {
        key = Qt::Key_F12;
    } else {
        resp["error"] = QString("Unknown key: %1").arg(shortcut);
        return resp;
    }

    QKeyEvent pressEvent(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(focusWidget, &pressEvent);

    QKeyEvent releaseEvent(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(focusWidget, &releaseEvent);

    // Also send to tab bars if it's a tab-switching shortcut
    if (mods == Qt::ControlModifier && (key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Tab)) {
        for (QTabBar* tabBar : window->findChildren<QTabBar*>()) {
            if (tabBar->isVisible()) {
                QKeyEvent tabPress(QEvent::KeyPress, key, mods);
                QApplication::sendEvent(tabBar, &tabPress);
                QKeyEvent tabRelease(QEvent::KeyRelease, key, mods);
                QApplication::sendEvent(tabBar, &tabRelease);
            }
        }
    }

    resp["ok"] = true;
    resp["shortcut"] = shortcut;
    resp["target"] = focusWidget->metaObject()->className();
    return resp;
}

// ============================================================================
// Switch tab by name
// ============================================================================

QVariantMap GuiManager::switchTab(const QString& windowName, const QString& tabName) {
    QVariantMap resp;
    resp["ok"] = false;

    QWidget* window = findWindow(windowName);
    if (!window) {
        resp["error"] = QString("Window not found: %1").arg(windowName);
        return resp;
    }

    // Find QTabBar or QMainWindowTabBar
    for (QTabBar* tabBar : window->findChildren<QTabBar*>()) {
        for (int i = 0; i < tabBar->count(); ++i) {
            QString tabText = tabBar->tabText(i);
            if (tabText.contains(tabName, Qt::CaseInsensitive) ||
                tabBar->tabToolTip(i).contains(tabName, Qt::CaseInsensitive)) {
                tabBar->setCurrentIndex(i);
                resp["ok"] = true;
                resp["tab"] = tabText;
                resp["index"] = i;
                return resp;
            }
        }
    }

    resp["error"] = QString("Tab not found: %1").arg(tabName);
    return resp;
}
