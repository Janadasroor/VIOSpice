/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_CONFIG_H
#define EXTENSION_CONFIG_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QDir>

namespace IDE {

// Extension configuration persistence
// Stores per-extension settings in <extension_dir>/config.json
class ExtensionConfig : public QObject {
    Q_OBJECT
public:
    explicit ExtensionConfig(const QString& extensionDir, QObject* parent = nullptr);

    // Read/write individual settings
    QVariant get(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void set(const QString& key, const QVariant& value);

    // Bulk operations
    QJsonObject getAll() const;
    void setAll(const QJsonObject& settings);

    // Persistence
    bool load();
    bool save();

    // Reset to defaults (clears config.json)
    void reset();

    // Check if a config key exists
    bool contains(const QString& key) const;

    // Remove a key
    void remove(const QString& key);

signals:
    void configChanged(const QString& key, const QVariant& value);

private:
    QString configFilePath() const;

    QString m_extensionDir;
    QJsonObject m_settings;
    bool m_loaded = false;
};

} // namespace IDE

#endif // EXTENSION_CONFIG_H
