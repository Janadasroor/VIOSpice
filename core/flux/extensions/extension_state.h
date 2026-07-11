/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_STATE_H
#define EXTENSION_STATE_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVariant>

namespace IDE {

// Runtime state persistence for extensions
// Different from config — this stores computed/runtime data
// Auto-saves on extension unload, auto-loads on startup
class ExtensionState : public QObject {
    Q_OBJECT
public:
    explicit ExtensionState(const QString& extensionDir, QObject* parent = nullptr);

    // Read/write state values
    QVariant get(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void set(const QString& key, const QVariant& value);

    // String convenience
    QString getString(const QString& key, const QString& defaultValue = QString()) const;
    void setString(const QString& key, const QString& value);

    // Integer convenience
    int getInt(const QString& key, int defaultValue = 0) const;
    void setInt(const QString& key, int value);

    // Double convenience
    double getDouble(const QString& key, double defaultValue = 0.0) const;
    void setDouble(const QString& key, double value);

    // Bulk operations
    QJsonObject getAll() const;
    void setAll(const QJsonObject& state);

    // Persistence
    bool load();
    bool save();

    // Reset all state
    void reset();

    // Remove a key
    void remove(const QString& key);

    // Check if a key exists
    bool contains(const QString& key) const;

    // Clear dirty flag (for auto-save optimization)
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

signals:
    void stateChanged(const QString& key, const QVariant& value);

private:
    QString stateFilePath() const;

    QString m_extensionDir;
    QJsonObject m_state;
    bool m_loaded = false;
    bool m_dirty = false;
};

} // namespace IDE

#endif // EXTENSION_STATE_H
