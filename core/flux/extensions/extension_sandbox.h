/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_SANDBOX_H
#define EXTENSION_SANDBOX_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>

namespace IDE {

// Permission categories for extension sandboxing
enum class Permission {
    None = 0,
    SchematicRead,      // Read component values, nets, properties
    SchematicWrite,     // Modify component properties
    SimulationRun,      // Run simulations
    SimulationModify,   // Modify simulation parameters in real-time
    GuiCreate,          // Create Qt widgets (windows, buttons, etc.)
    GuiModify,          // Modify existing widget properties
    ConfigRead,         // Read extension config.json
    ConfigWrite,        // Write extension config.json
    FileRead,           // Read files from filesystem
    FileWrite,          // Write files to filesystem
    Network,            // Network access (HTTP, WebSocket)
    System,             // System commands, process execution
    Workspace,          // Access workspace variables
    Plotting            // Access waveform viewer
};

// Permission string to enum mapping
Permission permissionFromString(const QString& name);
QString permissionToString(Permission perm);

// Sandbox enforcement for extensions
class ExtensionSandbox : public QObject {
    Q_OBJECT
public:
    explicit ExtensionSandbox(QObject* parent = nullptr);

    // Set the active extension's permissions
    void setPermissions(const QString& extensionId, const QSet<Permission>& permissions);

    // Check if the current extension has a specific permission
    bool hasPermission(Permission perm) const;
    bool hasPermission(const QString& permName) const;

    // Get all permissions for current extension
    QSet<Permission> currentPermissions() const;

    // Check and enforce permission (returns false and emits violation if denied)
    bool checkPermission(Permission perm, const QString& operation = QString());

    // Get current extension ID
    QString currentExtensionId() const { return m_currentExtensionId; }

    // Set current extension (called when extension is activated)
    void setCurrentExtension(const QString& id);

    // Get list of all available permissions with descriptions
    static QMap<Permission, QString> availablePermissions();

    // Parse permission strings from manifest
    static QSet<Permission> parsePermissions(const QStringList& permStrings);

signals:
    void permissionDenied(const QString& extensionId, Permission perm, const QString& operation);
    void sandboxViolation(const QString& extensionId, const QString& details);

private:
    QString m_currentExtensionId;
    QMap<QString, QSet<Permission>> m_extensionPermissions;
};

// Global sandbox instance
ExtensionSandbox& sandbox();

} // namespace IDE

#endif // EXTENSION_SANDBOX_H
