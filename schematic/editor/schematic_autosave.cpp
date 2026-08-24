/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "schematic_autosave.h"
#include "schematic_file_io.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <algorithm>

namespace {

bool filesEqual(const QString& a, const QString& b) {
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    const QByteArray da = fa.readAll();
    const QByteArray db = fb.readAll();
    return da == db;
}

} // namespace

QString SchematicAutosaveManager::snapshotPathFor(const QString& originalPath) {
    return originalPath + "~";
}

QString SchematicAutosaveManager::autosaveDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir d(base + "/autosave");
    if (!d.exists()) d.mkpath(".");
    return d.absolutePath();
}

QString SchematicAutosaveManager::untitledSnapshotPath() {
    return autosaveDir() + "/untitled_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".flxsch~";
}

bool SchematicAutosaveManager::writeSnapshot(QGraphicsScene* scene,
                                             const QString& originalPath,
                                             const QString& pageSize,
                                             const TitleBlockData& titleBlock,
                                             const QMap<QString, QList<QString>>& busAliases,
                                             const QSet<QString>& ercExclusions,
                                             const QJsonObject* simulationSetup) {
    if (!scene) return false;
    const QString snapshotPath = originalPath.isEmpty() ? untitledSnapshotPath() : snapshotPathFor(originalPath);
    QDir().mkpath(QFileInfo(snapshotPath).absolutePath());
    // Reuses the full save pipeline (path normalization, metadata, items).
    // The snapshot lives next to the original, so relative external paths stay valid.
    return SchematicFileIO::saveSchematic(scene, snapshotPath, pageSize, QString(),
                                          titleBlock, busAliases, ercExclusions, simulationSetup);
}

void SchematicAutosaveManager::clearSnapshot(const QString& originalPath) {
    if (originalPath.isEmpty()) return;
    const QString snapshotPath = snapshotPathFor(originalPath);
    if (QFile::exists(snapshotPath)) QFile::remove(snapshotPath);
}

QList<SchematicAutosaveManager::RecoveryCandidate>
SchematicAutosaveManager::findRecoveryCandidates(const QString& projectDir) {
    QList<RecoveryCandidate> out;
    QSet<QString> seen;

    const auto consider = [&](const QString& snapshotPath) {
        if (snapshotPath.isEmpty() || !QFileInfo::exists(snapshotPath)) return;
        const QString canon = QFileInfo(snapshotPath).canonicalFilePath();
        if (canon.isEmpty() || seen.contains(canon)) return;
        seen.insert(canon);

        QString originalPath = snapshotPath;
        if (originalPath.endsWith('~')) originalPath.chop(1);

        // Nothing to recover if the original is up to date with the snapshot.
        if (QFileInfo::exists(originalPath) && filesEqual(snapshotPath, originalPath)) return;

        RecoveryCandidate c;
        c.snapshotPath = snapshotPath;
        c.originalPath = originalPath;
        c.modifiedMs = QFileInfo(snapshotPath).lastModified().toMSecsSinceEpoch();
        out.append(c);
    };

    if (!projectDir.isEmpty()) {
        QDirIterator sidecarIt(projectDir, QStringList() << "*.flxsch~" << "*.sch~", QDir::Files,
                               QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (sidecarIt.hasNext()) consider(sidecarIt.next());
    }

    QDirIterator untitledIt(autosaveDir(), QStringList() << "*.flxsch~" << "*.sch~", QDir::Files,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (untitledIt.hasNext()) consider(untitledIt.next());

    std::sort(out.begin(), out.end(), [](const RecoveryCandidate& a, const RecoveryCandidate& b) {
        return a.modifiedMs > b.modifiedMs;
    });
    return out;
}
