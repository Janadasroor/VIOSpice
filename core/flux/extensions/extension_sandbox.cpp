/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_sandbox.h"
#include <QDebug>

namespace IDE {

// Permission string to enum mapping
Permission permissionFromString(const QString& name) {
    static QMap<QString, Permission> map = {
        {"schematic.read", Permission::SchematicRead},
        {"schematic.write", Permission::SchematicWrite},
        {"simulation.run", Permission::SimulationRun},
        {"simulation.modify", Permission::SimulationModify},
        {"gui.create", Permission::GuiCreate},
        {"gui.modify", Permission::GuiModify},
        {"config.read", Permission::ConfigRead},
        {"config.write", Permission::ConfigWrite},
        {"file.read", Permission::FileRead},
        {"file.write", Permission::FileWrite},
        {"network", Permission::Network},
        {"system", Permission::System},
        {"workspace", Permission::Workspace},
        {"plotting", Permission::Plotting}
    };
    return map.value(name, Permission::None);
}

QString permissionToString(Permission perm) {
    static QMap<Permission, QString> map = {
        {Permission::SchematicRead, "schematic.read"},
        {Permission::SchematicWrite, "schematic.write"},
        {Permission::SimulationRun, "simulation.run"},
        {Permission::SimulationModify, "simulation.modify"},
        {Permission::GuiCreate, "gui.create"},
        {Permission::GuiModify, "gui.modify"},
        {Permission::ConfigRead, "config.read"},
        {Permission::ConfigWrite, "config.write"},
        {Permission::FileRead, "file.read"},
        {Permission::FileWrite, "file.write"},
        {Permission::Network, "network"},
        {Permission::System, "system"},
        {Permission::Workspace, "workspace"},
        {Permission::Plotting, "plotting"}
    };
    return map.value(perm, "unknown");
}

// ============================================================================
// ExtensionSandbox
// ============================================================================

ExtensionSandbox& sandbox() {
    static ExtensionSandbox instance;
    return instance;
}

ExtensionSandbox::ExtensionSandbox(QObject* parent)
    : QObject(parent) {
}

void ExtensionSandbox::setCurrentExtension(const QString& id) {
    m_currentExtensionId = id;
}

void ExtensionSandbox::setPermissions(const QString& extensionId, const QSet<Permission>& permissions) {
    m_extensionPermissions[extensionId] = permissions;
}

bool ExtensionSandbox::hasPermission(Permission perm) const {
    if (m_currentExtensionId.isEmpty()) return true; // No sandbox if no extension

    auto it = m_extensionPermissions.find(m_currentExtensionId);
    if (it == m_extensionPermissions.end()) return true; // No permissions set = unrestricted

    return it.value().contains(perm);
}

bool ExtensionSandbox::hasPermission(const QString& permName) const {
    Permission perm = permissionFromString(permName);
    return hasPermission(perm);
}

QSet<Permission> ExtensionSandbox::currentPermissions() const {
    if (m_currentExtensionId.isEmpty()) return QSet<Permission>();

    auto it = m_extensionPermissions.find(m_currentExtensionId);
    if (it == m_extensionPermissions.end()) return QSet<Permission>();

    return it.value();
}

bool ExtensionSandbox::checkPermission(Permission perm, const QString& operation) {
    if (hasPermission(perm)) return true;

    QString permName = permissionToString(perm);
    QString details = operation.isEmpty()
        ? QString("Extension '%1' denied permission '%2'")
            .arg(m_currentExtensionId, permName)
        : QString("Extension '%1' denied permission '%2' for '%3'")
            .arg(m_currentExtensionId, permName, operation);

    qWarning() << "[Sandbox]" << details;
    emit permissionDenied(m_currentExtensionId, perm, operation);
    emit sandboxViolation(m_currentExtensionId, details);

    return false;
}

QSet<Permission> ExtensionSandbox::parsePermissions(const QStringList& permStrings) {
    QSet<Permission> result;
    for (const QString& s : permStrings) {
        Permission p = permissionFromString(s.trimmed().toLower());
        if (p != Permission::None) {
            result.insert(p);
        }
    }
    return result;
}

QMap<Permission, QString> ExtensionSandbox::availablePermissions() {
    return {
        {Permission::SchematicRead, "Read component values, nets, and properties"},
        {Permission::SchematicWrite, "Modify component properties and values"},
        {Permission::SimulationRun, "Run DC/AC/Transient simulations"},
        {Permission::SimulationModify, "Modify simulation parameters in real-time"},
        {Permission::GuiCreate, "Create Qt widgets (windows, buttons, labels)"},
        {Permission::GuiModify, "Modify properties of existing widgets"},
        {Permission::ConfigRead, "Read extension configuration files"},
        {Permission::ConfigWrite, "Write extension configuration files"},
        {Permission::FileRead, "Read files from the filesystem"},
        {Permission::FileWrite, "Write files to the filesystem"},
        {Permission::Network, "Access network resources (HTTP, WebSocket)"},
        {Permission::System, "Execute system commands and processes"},
        {Permission::Workspace, "Access workspace variables"},
        {Permission::Plotting, "Access waveform viewer and plot data"}
    };
}

} // namespace IDE
