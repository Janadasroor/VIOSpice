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
#include <QToolBar>

GuiManager& GuiManager::instance() {
    static GuiManager s_instance;
    return s_instance;
}

GuiManager::GuiManager(QObject* parent)
    : QObject(parent) {
}

// ============================================================================
// Fuzzy match
// ============================================================================

bool GuiManager::fuzzyMatch(const QString& candidate, const QString& query) {
    if (query.isEmpty()) return false;
    return candidate.contains(query, Qt::CaseInsensitive);
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

    for (QPushButton* btn : parent->findChildren<QPushButton*>()) {
        if (!btn->isVisible()) continue;
        if (fuzzyMatch(btn->text(), label)) return btn;
        if (fuzzyMatch(btn->toolTip(), label)) return btn;
        if (fuzzyMatch(btn->objectName(), label)) return btn;
    }

    for (QToolButton* btn : parent->findChildren<QToolButton*>()) {
        if (!btn->isVisible()) continue;
        if (fuzzyMatch(btn->text(), label)) return btn;
        if (fuzzyMatch(btn->toolTip(), label)) return btn;
        if (fuzzyMatch(btn->objectName(), label)) return btn;
        if (btn->defaultAction() && fuzzyMatch(btn->defaultAction()->text(), label))
            return btn;
        if (btn->defaultAction() && fuzzyMatch(btn->defaultAction()->toolTip(), label))
            return btn;
    }

    for (QLineEdit* edit : parent->findChildren<QLineEdit*>()) {
        if (!edit->isVisible()) continue;
        if (fuzzyMatch(edit->objectName(), label)) return edit;
        if (fuzzyMatch(edit->placeholderText(), label)) return edit;
    }

    return nullptr;
}

// ============================================================================
// Action lookup by text in menus and toolbars
// ============================================================================

QAction* GuiManager::findActionByText(QWidget* window, const QString& text) const {
    if (!window || text.isEmpty()) return nullptr;

    QMenuBar* menuBar = window->findChild<QMenuBar*>();
    if (menuBar) {
        for (QMenu* menu : menuBar->findChildren<QMenu*>()) {
            for (QAction* action : menu->actions()) {
                if (action->isSeparator()) continue;
                if (fuzzyMatch(action->text(), text)) return action;
                if (fuzzyMatch(action->toolTip(), text)) return action;
            }
        }
    }

    for (QToolBar* toolbar : window->findChildren<QToolBar*>()) {
        for (QAction* action : toolbar->actions()) {
            if (action->isSeparator()) continue;
            if (fuzzyMatch(action->text(), text)) return action;
            if (fuzzyMatch(action->toolTip(), text)) return action;
        }
    }

    return nullptr;
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
