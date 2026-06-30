/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODEL_INJECTOR_H
#define MODEL_INJECTOR_H

#include <QSet>
#include <QString>
#include <QStringList>
#include <QMap>

class ModelInjector {
public:
    static void inject(const QSet<QString>& includePaths,
                       const QSet<QString>& libPaths,
                       const QStringList& embeddedModelLines,
                       const QMap<QString, QString>& embeddedSubcircuits,
                       const QString& projectDir,
                       const QSet<QString>& userDeclaredModelFiles,
                       QString& netlist);

    static QString normalizeIncludePathForNetlist(const QString& includePath, const QString& projectDir);
    static QString sanitizeModelIncludeForNgspice(const QString& path);
};

#endif // MODEL_INJECTOR_H
