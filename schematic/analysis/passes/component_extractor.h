/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMPONENT_EXTRACTOR_H
#define COMPONENT_EXTRACTOR_H

#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>
#include "eco_types.h"

struct SimModel;

class ComponentExtractor {
public:
    struct ExtractionResult {
        QSet<QString> includePaths;
        QSet<QString> libPaths;
        QMap<QString, QString> embeddedSubcircuits;
        QStringList embeddedModelLines;
        QStringList runtimeWarnings;
    };

    struct SpiceTokenSplit {
        QString head;
        QString tail;
    };

    static ExtractionResult extract(const ECOPackage& pkg,
                                    const QString& projectDir,
                                    QSet<QString>& switchModelsAdded);

    static QString resolveModelPath(const QString& modelPath, const QString& projectDir);
    static SpiceTokenSplit splitLeadingSpiceToken(const QString& raw);
    static QString modelToSpiceLine(const SimModel& model);
};

#endif // COMPONENT_EXTRACTOR_H
