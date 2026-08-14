/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INSTALLER_WORKER_H
#define INSTALLER_WORKER_H

#include <QThread>
#include <QString>
#include <QStringList>
#include <QPair>
#include <QSet>
#include <QAtomicInt>
#include <QElapsedTimer>
#include "installer_types.h"

class InstallerWorker : public QThread {
    Q_OBJECT

public:
    explicit InstallerWorker(const InstallConfig& config, QObject* parent = nullptr);
    ~InstallerWorker() override;

    void cancel();

signals:
    void progressUpdated(const ProgressMetrics& metrics);
    void statusUpdated(const QString& message);
    void finished(bool success, const QString& errorMessage);

protected:
    void run() override;

private:
    bool performInstallation();
    bool performUninstallation();
    void discoverFilesToInstall();
    bool copyFast(const QString& srcPath, const QString& dstPath, qint64 fileSize);
    void emitProgressThrottled(const QString& currentFile, bool force = false);
    void rollback();

    // Windows Shell and System Integrations
#ifdef _WIN32
    bool createWindowsShortcuts();
    bool removeWindowsShortcuts();
    bool registerFileAssociations();
    bool unregisterFileAssociations();
    bool updatePathEnvironment();
    bool removeFromPathEnvironment();
    bool setupGlobalEnvironmentVariables();
    bool removeGlobalEnvironmentVariables();
    bool registerUninstaller();
    bool unregisterUninstaller();
#endif

    InstallConfig m_config;
    QAtomicInt m_cancelled{0};
    QStringList m_createdFiles;
    QStringList m_createdDirs;
    QSet<QString> m_createdDirSet;
    
    // File list: <SourcePath, RelativeDestPath>
    QList<QPair<QString, QString>> m_filesToCopy;
    uint64_t m_totalBytes{0};
    uint64_t m_bytesCopied{0};
    int m_filesCopied{0};
    
    QElapsedTimer m_timer;
    qint64 m_lastSpeedCheckTime{0};
    uint64_t m_lastSpeedCheckBytes{0};
    qint64 m_lastProgressEmitTime{0};
    double m_currentSpeedMBps{0.0};
};

#endif // INSTALLER_WORKER_H
