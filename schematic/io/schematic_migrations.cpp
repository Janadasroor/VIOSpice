/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "schematic_migrations.h"
#include "schematic_file_io.h"

#include <QJsonArray>

namespace {

QMap<int, SchematicMigrations::MigrationFn>& registry() {
    static QMap<int, SchematicMigrations::MigrationFn> r;
    return r;
}

} // namespace

void SchematicMigrations::registerMigration(int fromVersion, MigrationFn fn) {
    if (!fn) return;
    registry().insert(fromVersion, std::move(fn));
}

bool SchematicMigrations::applyMigrations(QJsonObject& root, int fromVersion, QString& error) {
    if (fromVersion < 0) fromVersion = 0;
    if (fromVersion > SchematicFileIO::FILE_FORMAT_VERSION) {
        error = QString("File version %1 is newer than supported version %2")
                    .arg(fromVersion)
                    .arg(SchematicFileIO::FILE_FORMAT_VERSION);
        return false;
    }

    for (int v = fromVersion; v < SchematicFileIO::FILE_FORMAT_VERSION; ++v) {
        const auto it = registry().constFind(v);
        if (it != registry().cend()) {
            if (!it.value()(root, error)) return false;
        }
    }

    QJsonObject metadata = root["metadata"].toObject();
    metadata["version"] = SchematicFileIO::FILE_FORMAT_VERSION;
    root["metadata"] = metadata;
    return true;
}

void SchematicMigrations::clearForTesting() {
    registry().clear();
}
