/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_THEME_H
#define IDE_THEME_H

#include "../../core/visuals/theme_manager.h"
#include "../../core/visuals/theme.h"
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSize>

namespace IDE {

struct IdeTheme {
    QString bgDarkest, bgPanel, bgEditor;
    QString textPrimary, textSecondary;
    QString accentBlue, green, red, border;
    QString bgTabActive, bgTabInactive;
};

inline IdeTheme currentTheme() {
    IdeTheme tc;
    auto* t = ThemeManager::theme();
    bool isLight = t && t->type() == PCBTheme::Light;

    tc.bgDarkest     = isLight ? "#f8fafc" : "#0f172a";
    tc.bgPanel       = isLight ? "#f1f5f9" : "#1e293b";
    tc.bgEditor      = isLight ? "#ffffff" : "#1a2332";
    tc.textPrimary   = isLight ? "#1e293b" : "#e2e8f0";
    tc.textSecondary = isLight ? "#64748b" : "#94a3b8";
    tc.accentBlue    = t ? t->accentColor().name() : "#3b82f6";
    tc.green         = isLight ? "#059669" : "#10b981";
    tc.red           = isLight ? "#dc2626" : "#ef4444";
    tc.border        = isLight ? "#e2e8f0" : "#334155";
    tc.bgTabActive   = isLight ? "#ffffff" : "#1e293b";
    tc.bgTabInactive = isLight ? "#f1f5f9" : "#0f172a";
    return tc;
}

// Theme-aware icon tinting (same pattern as SchematicEditor::getThemeIcon)
inline QIcon themeIcon(const QString& path) {
    QIcon icon(path);
    auto* t = ThemeManager::theme();
    if (!t) return icon;

    QPixmap pixmap = icon.pixmap(QSize(32, 32));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), t->textColor());
    painter.end();
    return QIcon(pixmap);
}

} // namespace IDE

#endif // IDE_THEME_H
