/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef API_REFERENCE_DATA_H
#define API_REFERENCE_DATA_H

#include <QString>
#include <QVector>

namespace IDE {

struct ApiFunction {
    QString name;
    QString signature;
    QString description;
    QString category; // "Qt Widgets", "Workspace", "Simulation", "Math"

    bool operator==(const ApiFunction& other) const { return name == other.name; }
};

class ApiReferenceData {
public:
    static const QVector<ApiFunction>& allFunctions();
    static QVector<ApiFunction> byCategory(const QString& category);
    static QVector<ApiFunction> search(const QString& query);
};

} // namespace IDE

#endif // API_REFERENCE_DATA_H
