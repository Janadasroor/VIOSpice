/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCREENSHOT_MANAGER_H
#define SCREENSHOT_MANAGER_H

#include <QObject>
#include <QPixmap>
#include <QList>
#include <QPair>
#include <QRect>
#include <QStringList>

struct CaptureOptions {
    QString format = "PNG";
    qreal scale = 1.0;
    bool clipboard = false;
    QRect region;
    bool includeHidden = false;
};

class ScreenshotManager : public QObject {
    Q_OBJECT
public:
    static ScreenshotManager& instance();

    struct WindowInfo {
        QString className;
        QString windowTitle;
        QString objectName;
        int index;
        bool isVisible;
        QStringList childWidgets;
    };

    QList<WindowInfo> listWindows(bool includeHidden = false) const;
    QPixmap captureWindow(const QString& nameOrClass, const CaptureOptions& opts = CaptureOptions()) const;
    QList<QPair<WindowInfo, QPixmap>> captureAll(const CaptureOptions& opts = CaptureOptions()) const;
    QStringList listChildren(const QString& parentName) const;

    static bool saveToFile(const QPixmap& pixmap, const QString& filePath, const QString& format = "PNG");
    static bool copyToClipboard(const QPixmap& pixmap);
    static QString generateFileName(const QString& windowName, const QString& format = "PNG");

private:
    explicit ScreenshotManager(QObject* parent = nullptr);
    QWidget* findWidget(const QString& nameOrClass, bool includeHidden = false) const;
    QWidget* findChildWidget(QWidget* parent, const QString& nameOrClass, bool includeHidden = false) const;
    void collectChildNames(QWidget* parent, QStringList& names, bool includeHidden) const;
    QPixmap applyRegion(const QPixmap& pixmap, const QRect& region) const;
};

#endif // SCREENSHOT_MANAGER_H
