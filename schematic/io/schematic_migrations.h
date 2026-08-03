/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATIC_MIGRATIONS_H
#define SCHEMATIC_MIGRATIONS_H

#include <QJsonObject>
#include <QString>
#include <functional>

/**
 * @brief Registry of one-step file-format migrations.
 *
 * The on-disk schematic format carries a version number (see
 * SchematicFileIO::FILE_FORMAT_VERSION). When a file written by an older
 * build is loaded, each intermediate version step is upgraded in sequence
 * so that deserialization always sees the current format.
 *
 * A migration for step N upgrades a document from version N to version N+1.
 * Only fields are rewritten; the loader then reads the migrated document.
 * Steps without a registered migration are treated as identity migrations
 * (the version is still bumped).
 */
class SchematicMigrations {
public:
    using MigrationFn = std::function<bool(QJsonObject& root, QString& error)>;

    /**
     * @brief Register the migration that upgrades version @p fromVersion to
     *        version @p fromVersion + 1. Only one migration per step.
     */
    static void registerMigration(int fromVersion, MigrationFn fn);

    /**
     * @brief Upgrade a document from @p fromVersion to the current
     *        SchematicFileIO::FILE_FORMAT_VERSION, mutating @p root in place.
     *        The metadata.version is updated on success.
     */
    static bool applyMigrations(QJsonObject& root, int fromVersion, QString& error);

    /// Remove all registered migrations (used by tests / plugin teardown).
    static void clearForTesting();
};

#endif // SCHEMATIC_MIGRATIONS_H
