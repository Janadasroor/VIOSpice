/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_MANAGER_H
#define EXTENSION_MANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVector>
#include <QAction>
#include <QDir>
#include <QFileSystemWatcher>
#include <QSet>

struct ExtensionManifest {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;

    QString mainFile;
    QString onActivate;
    QString onDeactivate;

    struct MenuEntry {
        QString path;
        QString action;
        QString icon; // optional icon filename relative to extension dir
    };
    QVector<MenuEntry> menuEntries;

    struct ContextEntry {
        QString componentType;
        QString action;
    };
    QVector<ContextEntry> contexts;

    QStringList permissions;

    // Dependencies: {"extId": "versionConstraint", ...}
    QMap<QString, QString> dependencies;

    bool parse(const QJsonObject& json, QString* error = nullptr);
};

class ExtensionManager : public QObject {
    Q_OBJECT
public:
    static ExtensionManager& instance();

    void addScanPath(const QString& path);
    void scanDirectories();
    void loadAll();
    void loadExtension(const QString& id);
    void unloadExtension(const QString& id);
    void reloadExtension(const QString& id);
    void reloadAll();

    bool isExtensionEnabled(const QString& id) const;
    void setExtensionEnabled(const QString& id, bool enabled);

    void watchDirectories();
    void setFileWatcherEnabled(bool enabled);

    QVector<QAction*> createMenuActions(QWidget* parent);
    void dispatchComponentDoubleClick(const QString& componentType, double componentHandle);

    struct ExtensionInfo {
        QString id;
        QString name;
        QString version;
        QString author;
        bool loaded;
    };
    QVector<ExtensionInfo> listExtensions() const;

    // Dependency management
    QStringList getLoadOrder() const;
    QStringList getDependencies(const QString& id) const;
    QStringList getDependents(const QString& id) const;
    QStringList validateDependencies(const QString& id) const;

    // Config persistence
    QVariant getConfig(const QString& extId, const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setConfig(const QString& extId, const QString& key, const QVariant& value);
    void saveConfig(const QString& extId);

signals:
    void extensionLoaded(const QString& id);
    void extensionUnloaded(const QString& id);
    void extensionError(const QString& id, const QString& error);
    void extensionsChanged();

private:
    explicit ExtensionManager(QObject* parent = nullptr);

    struct Extension {
        ExtensionManifest manifest;
        QString dirPath;
        bool loaded = false;
    };

    QString sanitizeId(const QString& id) const;
    QString fnName(const QString& extId, const QString& action) const;
    bool callExtensionFn(const QString& extId, const QString& action);

    QVector<QString> m_scanPaths;
    QVector<Extension> m_extensions;
    QFileSystemWatcher* m_fileWatcher = nullptr;
    bool m_watcherEnabled = true;

    QSet<QString> m_disabledExtensions;
    void loadDisabledExtensions();
    void saveDisabledExtensions() const;
};

#endif
