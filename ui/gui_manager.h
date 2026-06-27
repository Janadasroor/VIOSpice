/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QRect>

class QAction;

class GuiManager : public QObject {
    Q_OBJECT
public:
    static GuiManager& instance();

    struct ElementInfo {
        QString type;
        QString label;
        QString objectName;
        QRect geometry;
        bool enabled;
        QString parentName;
    };

    QVariantList listElements(const QString& windowName,
                              const QString& filterType = "",
                              const QString& filterParent = "") const;
    QVariantMap clickButton(const QString& windowName, const QString& target);
    QVariantMap typeInField(const QString& windowName, const QString& objectName,
                            const QString& text, bool append = false);
    QVariantMap triggerMenuAction(const QString& windowName, const QString& actionText);
    QVariantMap pressKey(const QString& windowName, const QString& shortcut);
    QVariantMap switchTab(const QString& windowName, const QString& tabName);

private:
    explicit GuiManager(QObject* parent = nullptr);
    QWidget* findWindow(const QString& name) const;
    QWidget* findChildByLabel(QWidget* parent, const QString& label) const;
    QAction* findActionByText(QWidget* window, const QString& text) const;
    static bool fuzzyMatch(const QString& candidate, const QString& query);
};

#endif // GUI_MANAGER_H
