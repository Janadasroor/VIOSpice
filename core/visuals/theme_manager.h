/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include "theme.h"
#include <QObject>
#include <QMap>
#include <functional>

class ThemeManager : public QObject {
    Q_OBJECT

public:
    virtual ~ThemeManager();
    static ThemeManager& instance();
    static PCBTheme* theme();

    void setTheme(PCBTheme::ThemeType type);
    void setTheme(PCBTheme* theme);
    PCBTheme* currentTheme() const { return m_theme; }

    // Register a callback to be invoked on every theme change
    void registerThemeCallback(void* key, std::function<void()> callback);
    void unregisterThemeCallback(void* key);

Q_SIGNALS:
    void themeChanged();

private:
    ThemeManager();

    PCBTheme* m_theme;
    QMap<void*, std::function<void()>> m_themeCallbacks;
};

#endif // THEME_MANAGER_H
