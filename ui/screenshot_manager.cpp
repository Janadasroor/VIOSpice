/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "screenshot_manager.h"

#include <QApplication>
#include <QClipboard>
#include <QWidget>
#include <QScreen>
#include <QDateTime>
#include <QRegularExpression>

ScreenshotManager& ScreenshotManager::instance() {
    static ScreenshotManager s_instance;
    return s_instance;
}

ScreenshotManager::ScreenshotManager(QObject* parent)
    : QObject(parent) {
}

QWidget* ScreenshotManager::findWidget(const QString& nameOrClass, bool includeHidden) const {
    if (nameOrClass.isEmpty()) return nullptr;

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!includeHidden && !widget->isVisible()) continue;

        if (widget->metaObject()->className() == nameOrClass)
            return widget;

        if (widget->windowTitle().contains(nameOrClass, Qt::CaseInsensitive))
            return widget;

        if (QString(widget->metaObject()->className()).contains(nameOrClass, Qt::CaseInsensitive))
            return widget;

        if (widget->objectName() == nameOrClass)
            return widget;

        if (QWidget* child = findChildWidget(widget, nameOrClass, includeHidden))
            return child;
    }
    return nullptr;
}

QWidget* ScreenshotManager::findChildWidget(QWidget* parent, const QString& nameOrClass, bool includeHidden) const {
    if (!parent) return nullptr;

    for (QWidget* child : parent->findChildren<QWidget*>()) {
        if (!includeHidden && !child->isVisible()) continue;

        if (child->metaObject()->className() == nameOrClass)
            return child;

        if (QString(child->metaObject()->className()).contains(nameOrClass, Qt::CaseInsensitive))
            return child;

        if (child->objectName() == nameOrClass)
            return child;

        if (child->windowTitle().contains(nameOrClass, Qt::CaseInsensitive))
            return child;

        if (child->accessibleName().contains(nameOrClass, Qt::CaseInsensitive))
            return child;
    }
    return nullptr;
}

void ScreenshotManager::collectChildNames(QWidget* parent, QStringList& names, bool includeHidden) const {
    if (!parent) return;

    for (QWidget* child : parent->findChildren<QWidget*>()) {
        if (!includeHidden && !child->isVisible()) continue;

        QString className = child->metaObject()->className();
        QString objectName = child->objectName();
        QString title = child->windowTitle();

        QString display = className;
        if (!objectName.isEmpty())
            display += " (" + objectName + ")";
        if (!title.isEmpty())
            display += " - " + title;

        names.append(display);
    }
}

QList<ScreenshotManager::WindowInfo> ScreenshotManager::listWindows(bool includeHidden) const {
    QList<WindowInfo> result;
    int index = 0;

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!includeHidden && !widget->isVisible()) continue;

        WindowInfo info;
        info.className = widget->metaObject()->className();
        info.windowTitle = widget->windowTitle();
        info.objectName = widget->objectName();
        info.index = index++;
        info.isVisible = widget->isVisible();

        collectChildNames(widget, info.childWidgets, includeHidden);

        result.append(info);
    }
    return result;
}

QPixmap ScreenshotManager::captureWindow(const QString& nameOrClass, const CaptureOptions& opts) const {
    QWidget* widget = findWidget(nameOrClass, opts.includeHidden);
    if (!widget) return QPixmap();

    QPixmap pixmap = widget->grab();

    if (opts.scale > 0.0 && opts.scale != 1.0) {
        pixmap = pixmap.scaled(
            static_cast<int>(pixmap.width() * opts.scale),
            static_cast<int>(pixmap.height() * opts.scale),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }

    if (!opts.region.isNull()) {
        pixmap = applyRegion(pixmap, opts.region);
    }

    return pixmap;
}

QList<QPair<ScreenshotManager::WindowInfo, QPixmap>> ScreenshotManager::captureAll(const CaptureOptions& opts) const {
    QList<QPair<WindowInfo, QPixmap>> result;
    int index = 0;

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!opts.includeHidden && !widget->isVisible()) continue;

        WindowInfo info;
        info.className = widget->metaObject()->className();
        info.windowTitle = widget->windowTitle();
        info.objectName = widget->objectName();
        info.index = index++;
        info.isVisible = widget->isVisible();

        QPixmap pixmap = widget->grab();
        if (opts.scale > 0.0 && opts.scale != 1.0) {
            pixmap = pixmap.scaled(
                static_cast<int>(pixmap.width() * opts.scale),
                static_cast<int>(pixmap.height() * opts.scale),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );
        }
        if (!opts.region.isNull()) {
            pixmap = applyRegion(pixmap, opts.region);
        }

        result.append({info, pixmap});
    }
    return result;
}

QStringList ScreenshotManager::listChildren(const QString& parentName) const {
    QStringList result;
    QWidget* parent = findWidget(parentName);
    if (parent) {
        collectChildNames(parent, result, true);
    }
    return result;
}

bool ScreenshotManager::saveToFile(const QPixmap& pixmap, const QString& filePath, const QString& format) {
    if (pixmap.isNull()) return false;
    return pixmap.save(filePath, format.toUtf8().constData());
}

bool ScreenshotManager::copyToClipboard(const QPixmap& pixmap) {
    if (pixmap.isNull()) return false;
    QApplication::clipboard()->setPixmap(pixmap);
    return true;
}

QString ScreenshotManager::generateFileName(const QString& windowName, const QString& format) {
    QString cleanName = windowName;
    cleanName.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_");
    cleanName.truncate(50);

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString ext = format.toLower();
    if (ext == "jpeg") ext = "jpg";

    return QString("%1_%2.%3").arg(cleanName, timestamp, ext);
}

QPixmap ScreenshotManager::applyRegion(const QPixmap& pixmap, const QRect& region) const {
    if (region.isNull() || !QRect(0, 0, pixmap.width(), pixmap.height()).intersects(region)) {
        return pixmap;
    }
    return pixmap.copy(region.intersected(QRect(0, 0, pixmap.width(), pixmap.height())));
}
