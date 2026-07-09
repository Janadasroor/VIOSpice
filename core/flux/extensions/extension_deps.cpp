/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_deps.h"
#include <QStack>
#include <QSet>
#include <QDebug>

namespace IDE {

ExtensionDeps::ExtensionDeps(QObject* parent)
    : QObject(parent) {
}

void ExtensionDeps::registerExtension(const QString& id, const QString& version,
                                      const QMap<QString, QString>& dependencies) {
    Q_UNUSED(version);

    // Clear old dependencies
    for (const QString& dep : m_dependsOn.value(id)) {
        m_dependents[dep].removeAll(id);
    }

    m_dependsOn[id] = dependencies.keys();

    // Build reverse dependencies
    for (auto it = dependencies.constBegin(); it != dependencies.constEnd(); ++it) {
        if (!m_dependents[it.key()].contains(id)) {
            m_dependents[it.key()].append(id);
        }
    }
}

void ExtensionDeps::unregisterExtension(const QString& id) {
    // Remove from forward dependencies
    for (const QString& dep : m_dependsOn.value(id)) {
        m_dependents[dep].removeAll(id);
    }
    m_dependsOn.remove(id);

    // Remove from reverse dependencies
    m_dependents.remove(id);
}

QStringList ExtensionDeps::resolveLoadOrder(const QStringList& availableIds,
                                            const QMap<QString, QString>& versions) const {
    // Topological sort using Kahn's algorithm
    QMap<QString, int> inDegree;
    QMap<QString, QStringList> adjacency;

    // Initialize
    for (const QString& id : availableIds) {
        inDegree[id] = 0;
        adjacency[id] = QStringList();
    }

    // Build adjacency list (only count dependencies that are available)
    for (auto it = m_dependsOn.constBegin(); it != m_dependsOn.constEnd(); ++it) {
        if (!availableIds.contains(it.key())) continue;
        for (const QString& dep : it.value()) {
            if (!availableIds.contains(dep)) continue;
            adjacency[dep].append(it.key());
            inDegree[it.key()]++;
        }
    }

    // Kahn's algorithm
    QStringList queue;
    for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
        if (it.value() == 0) queue.append(it.key());
    }

    QStringList sorted;
    while (!queue.isEmpty()) {
        QString current = queue.takeFirst();
        sorted.append(current);

        for (const QString& dependent : adjacency.value(current)) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                queue.append(dependent);
            }
        }
    }

    // If sorted size != available count, there's a cycle
    if (sorted.size() != availableIds.size()) {
        qWarning() << "[ExtDeps] Cycle detected in extension dependencies";
        // Return what we could sort, with remaining at the end
        for (const QString& id : availableIds) {
            if (!sorted.contains(id)) sorted.append(id);
        }
    }

    return sorted;
}

bool ExtensionDeps::wouldCreateCycle(const QString& id,
                                     const QMap<QString, QString>& dependencies) const {
    QSet<QString> visited;
    for (auto it = dependencies.constBegin(); it != dependencies.constEnd(); ++it) {
        if (wouldCreateCycleHelper(it.key(), id, visited)) {
            return true;
        }
    }
    return false;
}

bool ExtensionDeps::wouldCreateCycleHelper(const QString& id, const QString& target,
                                           QSet<QString>& visited) const {
    if (id == target) return true;
    if (visited.contains(id)) return false;
    visited.insert(id);

    for (const QString& dep : m_dependsOn.value(id)) {
        if (wouldCreateCycleHelper(dep, target, visited)) {
            return true;
        }
    }
    return false;
}

QStringList ExtensionDeps::getDependents(const QString& id) const {
    return m_dependents.value(id);
}

QStringList ExtensionDeps::getDependencies(const QString& id, int depth) const {
    QSet<QString> result;
    QStack<QPair<QString, int>> stack;
    stack.push({id, 0});

    while (!stack.isEmpty()) {
        auto [current, currentDepth] = stack.pop();
        if (depth >= 0 && currentDepth >= depth) continue;
        if (result.contains(current)) continue;
        result.insert(current);

        for (const QString& dep : m_dependsOn.value(current)) {
            if (dep != id) { // avoid self-reference
                stack.push({dep, currentDepth + 1});
            }
        }
    }

    result.remove(id); // remove self
    return result.values();
}

QStringList ExtensionDeps::validateDependencies(const QString& id,
                                               const QMap<QString, QString>& availableVersions) const {
    QStringList errors;
    QMap<QString, QString> deps = QMap<QString, QString>(); // empty for now, would need to store version requirements

    // For now, just check that all dependencies exist
    for (const QString& dep : m_dependsOn.value(id)) {
        if (!availableVersions.contains(dep)) {
            errors.append(QString("Missing dependency: %1").arg(dep));
        }
    }

    return errors;
}

bool ExtensionDeps::isVersionCompatible(const QString& required, const QString& actual) {
    if (required.isEmpty() || required == "*") return true;
    if (actual.isEmpty()) return false;

    // Simple version comparison: ">=1.0.0", "~1.0.0", "1.0.0"
    QString req = required.trimmed();
    QString act = actual.trimmed();

    if (req.startsWith(">=")) {
        return act >= req.mid(2);
    } else if (req.startsWith(">")) {
        return act > req.mid(1);
    } else if (req.startsWith("<=")) {
        return act <= req.mid(2);
    } else if (req.startsWith("<")) {
        return act < req.mid(1);
    } else if (req.startsWith("~")) {
        // Compatible: same major.minor
        QString base = req.mid(1);
        QString baseMajor = base.section('.', 0, 0);
        QString baseMinor = base.section('.', 1, 1);
        QString actMajor = act.section('.', 0, 0);
        QString actMinor = act.section('.', 1, 1);
        return (actMajor == baseMajor && actMinor == baseMinor);
    } else {
        // Exact match
        return act == req;
    }
}

} // namespace IDE
