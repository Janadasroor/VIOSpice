/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_DEPS_H
#define EXTENSION_DEPS_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <QStringList>

namespace IDE {

// Dependency resolution for extensions
// Extensions declare dependencies in manifest.json:
//   "dependencies": {
//     "mylib": ">=1.0.0",
//     "other-ext": "*"
//   }
class ExtensionDeps : public QObject {
    Q_OBJECT
public:
    explicit ExtensionDeps(QObject* parent = nullptr);

    // Register an extension's dependencies from manifest
    void registerExtension(const QString& id, const QString& version,
                          const QMap<QString, QString>& dependencies);

    // Unregister an extension
    void unregisterExtension(const QString& id);

    // Resolve load order using topological sort
    // Returns extensions in dependency order (dependencies first)
    QStringList resolveLoadOrder(const QStringList& availableIds,
                                 const QMap<QString, QString>& versions) const;

    // Check if adding an extension would create a cycle
    bool wouldCreateCycle(const QString& id, const QMap<QString, QString>& dependencies) const;

    // Get all extensions that depend on a given extension
    QStringList getDependents(const QString& id) const;

    // Get all dependencies of an extension (recursive)
    QStringList getDependencies(const QString& id, int depth = -1) const;

    // Validate that all dependencies are satisfied
    QStringList validateDependencies(const QString& id,
                                    const QMap<QString, QString>& availableVersions) const;

    // Check version compatibility (simple semver comparison)
    static bool isVersionCompatible(const QString& required, const QString& actual);

signals:
    void dependencyError(const QString& extId, const QString& message);

private:
    // adjacency list: extId -> list of dependency extIds
    QMap<QString, QStringList> m_dependsOn;
    // reverse: extId -> list of extensions that depend on it
    QMap<QString, QStringList> m_dependents;

    bool wouldCreateCycleHelper(const QString& id, const QString& target,
                               QSet<QString>& visited) const;
};

} // namespace IDE

#endif // EXTENSION_DEPS_H
