/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATIC_AUTOSAVE_H
#define SCHEMATIC_AUTOSAVE_H

#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include "../items/schematic_page_item.h"

class QGraphicsScene;
class QJsonObject;

/**
 * @brief Recovery snapshots for schematic files.
 *
 * Periodic snapshots are written to a sidecar "<file>.flxsch~" next to the
 * original so that a crash never clobbers the user's real file. Untitled
 * schematics snapshot into the per-app autosave directory. Snapshots are
 * removed again on a clean save or close, so a leftover snapshot on the next
 * launch means the last session did not shut down cleanly.
 */
class SchematicAutosaveManager {
public:
    struct RecoveryCandidate {
        QString snapshotPath;
        // Path of the file the snapshot belongs to ("" for unsaved snapshots,
        // or a path that may no longer exist when the original was deleted).
        QString originalPath;
        qint64 modifiedMs = 0;
    };

    // Sidecar snapshot path for an original schematic file ("<file>.flxsch~").
    static QString snapshotPathFor(const QString& originalPath);

    // Per-app autosave directory (created on demand).
    static QString autosaveDir();

    // Snapshot path for a schematic that has never been saved to disk.
    static QString untitledSnapshotPath();

    /**
     * @brief Write a recovery snapshot of the given scene.
     * @param originalPath Empty for an unsaved (untitled) schematic.
     */
    static bool writeSnapshot(QGraphicsScene* scene,
                              const QString& originalPath,
                              const QString& pageSize,
                              const TitleBlockData& titleBlock,
                              const QMap<QString, QList<QString>>& busAliases,
                              const QSet<QString>& ercExclusions,
                              const QJsonObject* simulationSetup);

    // Remove the snapshot for an original file (after save / clean close).
    static void clearSnapshot(const QString& originalPath);

    /**
     * @brief Scan the project directory and the autosave dir for leftover
     *        recovery snapshots that contain work newer than the originals.
     */
    static QList<RecoveryCandidate> findRecoveryCandidates(const QString& projectDir);
};

#endif // SCHEMATIC_AUTOSAVE_H
