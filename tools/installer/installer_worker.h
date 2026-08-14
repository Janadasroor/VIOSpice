/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INSTALLER_WORKER_H
#define INSTALLER_WORKER_H

#include <QThread>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <atomic>

struct InstallOptions {
    QString installDir;
    bool createDesktopShortcut{true};
    bool createStartMenuShortcut{true};
    bool addToPath{true};
    bool associateFiles{true};
    bool isUninstall{false};
};

class InstallerWorker : public QThread {
    Q_OBJECT

public:
    explicit InstallerWorker(const InstallOptions& options, QObject* parent = nullptr);
    ~InstallerWorker() override = default;

    void requestCancel();

signals:
    void progressChanged(int percentage, const QString& currentFile, double speedMBps, quint64 bytesCopied, quint64 totalBytes);
    void statusChanged(const QString& statusText);
    void installationFinished(bool success, const QString& message);

protected:
    void run() override;

private:
    void doInstall();
    void doUninstall();

    bool collectFilesToCopy(QList<QPair<QString, QString>>& outFileList, quint64& outTotalBytes);
    bool copyFileChunked(const QString& src, const QString& dst, quint64& copiedBytes, quint64 totalBytes);

    // Windows System Integration Helpers
    bool createWindowsShortcuts();
    bool removeWindowsShortcuts();
    bool setupFileAssociations();
    bool removeFileAssociations();
    bool updatePathEnvironment(bool add);
    bool registerUninstaller(quint64 totalBytes);
    bool removeUninstallerRegistry();

    InstallOptions m_options;
    std::atomic<bool> m_cancelRequested{false};
    QElapsedTimer m_timer;
};

#endif // INSTALLER_WORKER_H
