/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "theme_manager.h"
#include "config_manager.h"
#include <QApplication>
#include <QWidget>

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
}

ThemeManager::~ThemeManager() {
    delete m_theme;
}

void ThemeManager::setTheme(PCBTheme::ThemeType type) {
    delete m_theme;
    m_theme = new PCBTheme(type);

    ConfigManager::instance().setCurrentTheme(themeNameFromType(type));
    ConfigManager::instance().save();

    m_theme->applyToApplication();
    Q_EMIT themeChanged();

    // Invoke all registered theme-change callbacks
    for (auto it = m_themeCallbacks.begin(); it != m_themeCallbacks.end(); ++it) {
        if (it.value()) it.value();
    }

    // Force palette on every widget + repaint top-level windows
    if (qApp) {
        QPalette pal = qApp->palette();
        for (QWidget* w : qApp->allWidgets()) {
            if (w) w->setPalette(pal);
        }
        for (QWidget* w : qApp->topLevelWidgets()) {
            if (w) w->repaint();
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
        for (QWidget* w : qApp->allWidgets()) {
            if (w) w->setPalette(pal);
        }
        for (QWidget* w : qApp->topLevelWidgets()) {
            if (w) w->repaint();
        }
    }
}

void ThemeManager::registerThemeCallback(void* key, std::function<void()> callback) {
    m_themeCallbacks[key] = callback;
}

void ThemeManager::unregisterThemeCallback(void* key) {
    m_themeCallbacks.remove(key);
}
