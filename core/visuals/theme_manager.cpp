/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "theme_manager.h"
#include "config_manager.h"
#include <QApplication>
#include <QWidget>
#include <QEvent>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

static void applyDarkTitlebarNative(HWND hwnd, bool isDark) {
    if (!hwnd) return;
    typedef HRESULT (WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributeFunc pDwmSetWindowAttribute = nullptr;
    static bool resolved = false;

    if (!resolved) {
        HMODULE hModule = LoadLibraryA("dwmapi.dll");
        if (hModule) {
            pDwmSetWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunc>(
                GetProcAddress(hModule, "DwmSetWindowAttribute"));
        }
        resolved = true;
    }

    if (pDwmSetWindowAttribute) {
        BOOL useDarkMode = isDark ? TRUE : FALSE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Win 11 & Win 10 2004+), 19 for Win 10 1809-1909
        pDwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
        pDwmSetWindowAttribute(hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }
}
#endif

namespace {
PCBTheme::ThemeType themeTypeFromName(const QString& name) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == "light") return PCBTheme::Light;
    if (normalized == "engineering") return PCBTheme::Engineering;
    return PCBTheme::Dark;
}

QString themeNameFromType(PCBTheme::ThemeType type) {
    switch (type) {
    case PCBTheme::Light: return "Light";
    case PCBTheme::Engineering: return "Engineering";
    case PCBTheme::Dark:
    default:
        return "Dark";
    }
}
}

void ThemeManager::applyTitlebarTheme(QWidget* widget, bool isDark) {
#if defined(_WIN32) || defined(_WIN64)
    if (widget && widget->isWindow()) {
        applyDarkTitlebarNative(reinterpret_cast<HWND>(widget->winId()), isDark);
    }
#else
    Q_UNUSED(widget);
    Q_UNUSED(isDark);
#endif
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    static bool firstCall = true;
    if (firstCall && qApp) {
        instance.m_theme->applyToApplication();
        firstCall = false;
    }
    return instance;
}

PCBTheme* ThemeManager::theme() {
    return instance().currentTheme();
}

ThemeManager::ThemeManager()
    : m_theme(new PCBTheme(themeTypeFromName(ConfigManager::instance().currentTheme()))) {
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

ThemeManager::~ThemeManager() {
    delete m_theme;
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            if (w->isWindow()) {
                bool isDark = m_theme ? (m_theme->type() != PCBTheme::Light) : true;
                applyTitlebarTheme(w, isDark);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeManager::setTheme(PCBTheme::ThemeType type) {
    delete m_theme;
    m_theme = new PCBTheme(type);

    ConfigManager::instance().setCurrentTheme(themeNameFromType(type));
    ConfigManager::instance().save();

    m_theme->applyToApplication();
    Q_EMIT themeChanged();

    for (auto it = m_themeCallbacks.begin(); it != m_themeCallbacks.end(); ++it) {
        if (it.value()) it.value();
    }

    if (qApp) {
        QPalette pal = qApp->palette();
        bool isDark = (type != PCBTheme::Light);
        for (QWidget* w : qApp->allWidgets()) {
            if (w) w->setPalette(pal);
        }
        for (QWidget* w : qApp->topLevelWidgets()) {
            if (w) {
                w->repaint();
                applyTitlebarTheme(w, isDark);
            }
        }
    }
}

void ThemeManager::setTheme(PCBTheme* theme) {
    if (theme == m_theme) return;
    delete m_theme;
    m_theme = theme;

    if (m_theme) {
        ConfigManager::instance().setCurrentTheme(themeNameFromType(m_theme->type()));
        ConfigManager::instance().save();
        m_theme->applyToApplication();
    }

    Q_EMIT themeChanged();

    for (auto it = m_themeCallbacks.begin(); it != m_themeCallbacks.end(); ++it) {
        if (it.value()) it.value();
    }

    if (qApp) {
        QPalette pal = qApp->palette();
        bool isDark = m_theme ? (m_theme->type() != PCBTheme::Light) : true;
        for (QWidget* w : qApp->allWidgets()) {
            if (w) w->setPalette(pal);
        }
        for (QWidget* w : qApp->topLevelWidgets()) {
            if (w) {
                w->repaint();
                applyTitlebarTheme(w, isDark);
            }
        }
    }
}

void ThemeManager::registerThemeCallback(void* key, std::function<void()> callback) {
    m_themeCallbacks[key] = callback;
}

void ThemeManager::unregisterThemeCallback(void* key) {
    m_themeCallbacks.remove(key);
}
