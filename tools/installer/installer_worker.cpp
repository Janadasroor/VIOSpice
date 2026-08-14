/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

namespace {
bool createShortcutWin32(const QString& linkPath, const QString& targetPath,
                         const QString& args, const QString& workingDir,
                         const QString& description, const QString& iconPath, int iconIndex) {
    HRESULT hr = CoInitialize(nullptr);
    bool coInitialized = SUCCEEDED(hr);

    IShellLinkW* psl = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        psl->SetPath((LPCWSTR)targetPath.utf16());
        if (!args.isEmpty()) {
            psl->SetArguments((LPCWSTR)args.utf16());
        }
        if (!workingDir.isEmpty()) {
            psl->SetWorkingDirectory((LPCWSTR)workingDir.utf16());
        }
        if (!description.isEmpty()) {
            psl->SetDescription((LPCWSTR)description.utf16());
        }
        if (!iconPath.isEmpty()) {
            psl->SetIconLocation((LPCWSTR)iconPath.utf16(), iconIndex);
        }

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save((LPCWSTR)linkPath.utf16(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }

    if (coInitialized) {
        CoUninitialize();
    }
    return SUCCEEDED(hr);
}
}
#endif

InstallerWorker::InstallerWorker(const InstallConfig& config, QObject* parent)
    : QThread(parent), m_config(config) {
}

InstallerWorker::~InstallerWorker() {
    cancel();
    wait();
}

void InstallerWorker::cancel() {
    m_cancelled.storeRelaxed(1);
}

void InstallerWorker::run() {
    if (m_config.isUninstall) {
        performUninstallation();
    } else {
        performInstallation();
    }
}

void InstallerWorker::discoverFilesToInstall() {
    m_filesToCopy.clear();
    m_totalBytes = 0;

    QString appDir = QCoreApplication::applicationDirPath();
    QString rootDir = QDir::cleanPath(appDir + "/..");

    QStringList searchRoots = {
        appDir,
        rootDir,
        "C:/VioraEDA",
        "C:/VioraEDA/build"
    };

    auto addDirFiles = [&](const QString& srcBaseDir, const QString& targetSubDir) {
        QDir dir(srcBaseDir);
        if (!dir.exists()) return;

        QDirIterator it(srcBaseDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString srcPath = it.next();
            QString rel = dir.relativeFilePath(srcPath);
            QString destRel = targetSubDir.isEmpty() ? rel : (targetSubDir + "/" + rel);
            QFileInfo fi(srcPath);
            m_filesToCopy.append(qMakePair(srcPath, destRel));
            m_totalBytes += (uint64_t)fi.size();
        }
    };

    auto addSingleFile = [&](const QString& srcPath, const QString& destRel) {
        if (QFile::exists(srcPath)) {
            QFileInfo fi(srcPath);
            m_filesToCopy.append(qMakePair(srcPath, destRel));
            m_totalBytes += (uint64_t)fi.size();
        }
    };

    // 1. Core Suite Binaries and Libraries
    if (m_config.components.coreSuite) {
        for (const QString& root : searchRoots) {
            addSingleFile(root + "/VioraEDA.exe", "bin/VioraEDA.exe");
            addSingleFile(root + "/bin/VioraEDA.exe", "bin/VioraEDA.exe");
            addSingleFile(root + "/VioraEDA_Setup.exe", "bin/VioraEDA_Setup.exe");
            addSingleFile(root + "/bin/VioraEDA_Setup.exe", "bin/VioraEDA_Setup.exe");
            addSingleFile(root + "/viospice-merge.exe", "bin/viospice-merge.exe");
            addSingleFile(root + "/bin/viospice-merge.exe", "bin/viospice-merge.exe");

            QDir rootD(root);
            for (const QString& dll : rootD.entryList({"*.dll"}, QDir::Files)) {
                addSingleFile(root + "/" + dll, "bin/" + dll);
            }
            QDir binD(root + "/bin");
            if (binD.exists()) {
                for (const QString& dll : binD.entryList({"*.dll"}, QDir::Files)) {
                    addSingleFile(root + "/bin/" + dll, "bin/" + dll);
                }
            }

            addDirFiles(root + "/plugins", "bin/plugins");
            addDirFiles(root + "/platforms", "bin/platforms");
            addDirFiles(root + "/imageformats", "bin/imageformats");
            addDirFiles(root + "/iconengines", "bin/iconengines");
            addDirFiles(root + "/styles", "bin/styles");
        }
    }

    // 2. Simulators and SPICE Code Models
    if (m_config.components.simulators) {
        for (const QString& root : searchRoots) {
            addDirFiles(root + "/cm", "cm");
            addDirFiles(root + "/viomatrixc-prebuilt", "bin");
            addDirFiles(root + "/vioavr-prebuilt", "bin");
            addDirFiles(root + "/models", "models");
        }
    }

    // 3. Component Library
    if (m_config.components.componentLibrary) {
        QStringList libSources = {
            "C:/Users/rdpuser/ViospiceLib",
            "C:/VioraEDA/ViospiceLib",
            rootDir + "/ViospiceLib",
            appDir + "/ViospiceLib"
        };
        for (const QString& libSrc : libSources) {
            if (QDir(libSrc).exists()) {
                addDirFiles(libSrc, "ViospiceLib");
                break;
            }
        }
    }

    // 4. CLI Tools
    if (m_config.components.cliTools) {
        for (const QString& root : searchRoots) {
            addSingleFile(root + "/viora.exe", "bin/viora.exe");
            addSingleFile(root + "/bin/viora.exe", "bin/viora.exe");
            addSingleFile(root + "/flux_runner.exe", "bin/flux_runner.exe");
            addSingleFile(root + "/bin/flux_runner.exe", "bin/flux_runner.exe");
            addSingleFile(root + "/flux-lsp.exe", "bin/flux-lsp.exe");
            addSingleFile(root + "/bin/flux-lsp.exe", "bin/flux-lsp.exe");
        }
    }

    // 5. Examples and Templates
    if (m_config.components.examplesAndTemplates) {
        for (const QString& root : searchRoots) {
            addDirFiles(root + "/examples", "examples");
            addDirFiles(root + "/templates", "templates");
        }
    }

    // Deduplicate file list by relative destination path
    QList<QPair<QString, QString>> uniqueList;
    QSet<QString> seenDests;
    uint64_t uniqueBytes = 0;

    for (const auto& pair : m_filesToCopy) {
        if (!seenDests.contains(pair.second)) {
            seenDests.insert(pair.second);
            uniqueList.append(pair);
            QFileInfo fi(pair.first);
            uniqueBytes += (uint64_t)fi.size();
        }
    }

    m_filesToCopy = uniqueList;
    m_totalBytes = uniqueBytes;
}

void InstallerWorker::emitProgressThrottled(const QString& currentFile, bool force) {
    qint64 now = m_timer.elapsed();
    if (!force && (now - m_lastProgressEmitTime < 50)) {
        return;
    }
    m_lastProgressEmitTime = now;

    if (now - m_lastSpeedCheckTime >= 100) {
        qint64 deltaMs = now - m_lastSpeedCheckTime;
        uint64_t deltaBytes = m_bytesCopied - m_lastSpeedCheckBytes;
        if (deltaMs > 0) {
            m_currentSpeedMBps = ((double)deltaBytes / (1024.0 * 1024.0)) / ((double)deltaMs / 1000.0);
        }
        m_lastSpeedCheckTime = now;
        m_lastSpeedCheckBytes = m_bytesCopied;
    }

    ProgressMetrics metrics;
    metrics.percentage = m_totalBytes > 0 ? (int)((m_bytesCopied * 92) / m_totalBytes) : 0;
    metrics.currentFileName = currentFile;
    metrics.currentStatus = QString("Installing %1").arg(currentFile);
    metrics.transferSpeedMBps = m_currentSpeedMBps;
    metrics.bytesTransferred = m_bytesCopied;
    metrics.totalBytes = m_totalBytes;
    metrics.filesProcessed = m_filesCopied;
    metrics.totalFiles = m_filesToCopy.size();

    if (m_currentSpeedMBps > 0.1 && m_totalBytes > m_bytesCopied) {
        double remainingMB = (double)(m_totalBytes - m_bytesCopied) / (1024.0 * 1024.0);
        metrics.estimatedSecondsRemaining = (int)(remainingMB / m_currentSpeedMBps);
    } else {
        metrics.estimatedSecondsRemaining = 0;
    }

    emit progressUpdated(metrics);
}

bool InstallerWorker::copyFast(const QString& srcPath, const QString& dstPath, qint64 fileSize) {
    if (m_cancelled.loadRelaxed() != 0) {
        return false;
    }

    QFileInfo dstInfo(dstPath);
    QString dirPath = dstInfo.absolutePath();

    // Cache directory creation to avoid redundant filesystem traversals
    if (!m_createdDirSet.contains(dirPath)) {
        QDir().mkpath(dirPath);
        m_createdDirSet.insert(dirPath);
        m_createdDirs.append(dirPath);
    }

#ifdef _WIN32
    // High-speed native Windows kernel copy for standard and small files (< 4MB)
    if (fileSize < 4 * 1024 * 1024) {
        std::wstring wSrc = QDir::toNativeSeparators(srcPath).toStdWString();
        std::wstring wDst = QDir::toNativeSeparators(dstPath).toStdWString();
        if (CopyFileW(wSrc.c_str(), wDst.c_str(), FALSE)) {
            m_bytesCopied += (uint64_t)fileSize;
            m_createdFiles.append(dstPath);
            emitProgressThrottled(dstInfo.fileName());
            return true;
        }
    }
#endif

    // Chunked copy stream for large files (> 4MB) or fallback
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) return false;

    QFile dst(dstPath);
    if (!dst.open(QIODevice::WriteOnly)) return false;

    constexpr qint64 CHUNK_SIZE = 256 * 1024; // 256 KB chunk
    char buffer[CHUNK_SIZE];

    while (!src.atEnd()) {
        if (m_cancelled.loadRelaxed() != 0) {
            dst.close();
            dst.remove();
            return false;
        }

        qint64 bytesRead = src.read(buffer, CHUNK_SIZE);
        if (bytesRead <= 0) break;

        qint64 bytesWritten = dst.write(buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            dst.close();
            dst.remove();
            return false;
        }

        m_bytesCopied += (uint64_t)bytesWritten;
        emitProgressThrottled(dstInfo.fileName());
    }

    dst.close();
    m_createdFiles.append(dstPath);
    return true;
}

void InstallerWorker::rollback() {
    emit statusUpdated("Installation cancelled. Rolling back changes...");
    for (const QString& f : m_createdFiles) {
        QFile::remove(f);
    }
    for (int i = m_createdDirs.size() - 1; i >= 0; --i) {
        QDir d(m_createdDirs[i]);
        if (d.isEmpty()) {
            d.rmdir(m_createdDirs[i]);
        }
    }
}

bool InstallerWorker::performInstallation() {
    emit statusUpdated("Preparing installation payload...");
    m_timer.start();
    m_lastSpeedCheckTime = 0;
    m_lastSpeedCheckBytes = 0;
    m_lastProgressEmitTime = 0;
    m_bytesCopied = 0;
    m_filesCopied = 0;
    m_createdDirSet.clear();

    discoverFilesToInstall();

    if (m_filesToCopy.isEmpty()) {
        emit finished(false, "No installation payloads were found in the source distribution directory.");
        return false;
    }

    QDir targetDir(m_config.installDir);
    if (!targetDir.exists()) {
        targetDir.mkpath(".");
        m_createdDirSet.insert(m_config.installDir);
        m_createdDirs.append(m_config.installDir);
    }

    emit statusUpdated("Installing files and component libraries...");

    for (const auto& pair : m_filesToCopy) {
        if (m_cancelled.loadRelaxed() != 0) {
            rollback();
            emit finished(false, "Installation was cancelled by the user.");
            return false;
        }

        QString src = pair.first;
        QString dst = QDir::cleanPath(m_config.installDir + "/" + pair.second);
        QFileInfo fi(src);

        if (!copyFast(src, dst, fi.size())) {
            if (m_cancelled.loadRelaxed() != 0) {
                rollback();
                emit finished(false, "Installation was cancelled by the user.");
                return false;
            }
            rollback();
            emit finished(false, QString("Failed to write destination file: %1").arg(dst));
            return false;
        }
        m_filesCopied++;
    }

    // Step: Windows System & Shell Integration
#ifdef _WIN32
    emit statusUpdated("Configuring Windows shell shortcuts and file associations...");

    if (m_config.systemOptions.createDesktopShortcut || m_config.systemOptions.createStartMenuShortcuts) {
        createWindowsShortcuts();
    }
    if (m_config.systemOptions.registerFileAssociations) {
        registerFileAssociations();
    }
    if (m_config.systemOptions.addToPathEnvironment) {
        updatePathEnvironment();
    }
    registerUninstaller();
#endif

    ProgressMetrics finalMetrics;
    finalMetrics.percentage = 100;
    finalMetrics.currentFileName = "Complete";
    finalMetrics.currentStatus = "VioraEDA installed successfully!";
    finalMetrics.transferSpeedMBps = 0.0;
    finalMetrics.bytesTransferred = m_totalBytes;
    finalMetrics.totalBytes = m_totalBytes;
    finalMetrics.filesProcessed = m_filesToCopy.size();
    finalMetrics.totalFiles = m_filesToCopy.size();
    finalMetrics.estimatedSecondsRemaining = 0;

    emit progressUpdated(finalMetrics);
    emit statusUpdated("Installation completed successfully!");
    emit finished(true, QString());
    return true;
}

bool InstallerWorker::performUninstallation() {
    emit statusUpdated("Removing VioraEDA installation...");
    
#ifdef _WIN32
    emit statusUpdated("Removing shell shortcuts and file associations...");
    removeWindowsShortcuts();
    unregisterFileAssociations();
    removeFromPathEnvironment();
    unregisterUninstaller();
#endif

    emit statusUpdated("Deleting installed files and directories...");
    QDir targetDir(m_config.installDir);
    if (targetDir.exists()) {
        targetDir.removeRecursively();
    }

    ProgressMetrics finalMetrics;
    finalMetrics.percentage = 100;
    finalMetrics.currentFileName = "Done";
    finalMetrics.currentStatus = "VioraEDA uninstalled successfully.";
    
    emit progressUpdated(finalMetrics);
    emit statusUpdated("Uninstallation complete.");
    emit finished(true, QString());
    return true;
}

#ifdef _WIN32
bool InstallerWorker::createWindowsShortcuts() {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_config.installDir + "/bin"));
    QString appExe = binDir + "\\VioraEDA.exe";
    QString cliExe = binDir + "\\viora.exe";
    QString setupExe = binDir + "\\VioraEDA_Setup.exe";

    if (!QFile::exists(appExe)) return false;

    if (m_config.systemOptions.createDesktopShortcut) {
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (!desktopPath.isEmpty()) {
            QString linkFile = desktopPath + "/VioraEDA.lnk";
            createShortcutWin32(linkFile, appExe, "", binDir, "VioraEDA 2026 High-Performance EDA Suite", appExe, 0);
        }
    }

    if (m_config.systemOptions.createStartMenuShortcuts) {
        QString programsPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        if (!programsPath.isEmpty()) {
            QString menuFolder = programsPath + "/VioraEDA";
            QDir().mkpath(menuFolder);

            createShortcutWin32(menuFolder + "/VioraEDA.lnk", appExe, "", binDir, "VioraEDA 2026", appExe, 0);
            if (QFile::exists(cliExe)) {
                createShortcutWin32(menuFolder + "/Viora CLI.lnk", cliExe, "", binDir, "VioraEDA Command Line Tools", cliExe, 0);
            }
            if (QFile::exists(setupExe)) {
                createShortcutWin32(menuFolder + "/Uninstall VioraEDA.lnk", setupExe, "--uninstall", binDir, "Uninstall VioraEDA", setupExe, 0);
            }
        }
    }
    return true;
}

bool InstallerWorker::removeWindowsShortcuts() {
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktopPath.isEmpty()) {
        QFile::remove(desktopPath + "/VioraEDA.lnk");
    }

    QString programsPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!programsPath.isEmpty()) {
        QDir(programsPath + "/VioraEDA").removeRecursively();
    }
    return true;
}

bool InstallerWorker::registerFileAssociations() {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_config.installDir + "/bin"));
    QString appExe = binDir + "\\VioraEDA.exe";
    QString openCmd = "\"" + appExe + "\" \"%1\"";

    struct FileTypeAssoc {
        const char* ext;
        const char* progId;
        const char* desc;
    };

    const FileTypeAssoc assocs[] = {
        { ".flxsch",    "VioraEDA.Schematic.1",  "VioraEDA Schematic Document" },
        { ".flux",      "VioraEDA.FluxScript.1", "FluxScript Source Document" },
        { ".flxpcb",    "VioraEDA.PCB.1",        "VioraEDA PCB Layout Document" },
        { ".cir",       "VioraEDA.Netlist.1",    "SPICE Netlist Document" },
        { ".sp",        "VioraEDA.Netlist.1",    "SPICE Netlist Document" },
        { ".asc",       "VioraEDA.Schematic.1",  "LTspice Schematic Document" },
        { ".kicad_sch", "VioraEDA.Schematic.1",  "KiCad Schematic Document" },
        { ".kicad_pcb", "VioraEDA.PCB.1",        "KiCad PCB Layout Document" },
    };

    for (const auto& a : assocs) {
        QString progKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.progId);
        QSettings regProg(progKey, QSettings::NativeFormat);
        regProg.setValue(".", a.desc);
        regProg.setValue("DefaultIcon/.", QString("%1,0").arg(appExe));
        regProg.setValue("shell/open/command/.", openCmd);

        QString extKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.ext);
        QSettings regExt(extKey, QSettings::NativeFormat);
        regExt.setValue(".", a.progId);
        regExt.setValue("Content Type", "application/x-vioraeda");
    }

    // Asynchronous shell notification without blocking UI
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
    return true;
}

bool InstallerWorker::unregisterFileAssociations() {
    const char* exts[] = { ".flxsch", ".flux", ".flxpcb", ".cir", ".sp", ".asc", ".kicad_sch", ".kicad_pcb" };
    const char* progIds[] = { "VioraEDA.Schematic.1", "VioraEDA.FluxScript.1", "VioraEDA.PCB.1", "VioraEDA.Netlist.1" };

    for (const char* p : progIds) {
        QSettings regProg(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(p), QSettings::NativeFormat);
        regProg.clear();
    }
    for (const char* e : exts) {
        QSettings regExt(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(e), QSettings::NativeFormat);
        regExt.clear();
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
    return true;
}

bool InstallerWorker::updatePathEnvironment() {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_config.installDir + "/bin"));
    QSettings regEnv("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
    QString currentPath = regEnv.value("Path", "").toString();

    QStringList parts = currentPath.split(';', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        if (p.trimmed().compare(binDir, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    parts.append(binDir);
    QString newPath = parts.join(';');
    regEnv.setValue("Path", newPath);

    // Only notify asynchronously and avoid desktop lock in Remote Desktop sessions
    if (GetSystemMetrics(SM_REMOTESESSION) == 0) {
        SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment");
    }
    return true;
}

bool InstallerWorker::removeFromPathEnvironment() {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_config.installDir + "/bin"));
    QSettings regEnv("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
    QString currentPath = regEnv.value("Path", "").toString();

    QStringList parts = currentPath.split(';', Qt::SkipEmptyParts);
    QStringList filteredParts;
    bool modified = false;

    for (const QString& p : parts) {
        if (p.trimmed().compare(binDir, Qt::CaseInsensitive) == 0) {
            modified = true;
        } else {
            filteredParts.append(p);
        }
    }

    if (modified) {
        regEnv.setValue("Path", filteredParts.join(';'));
        if (GetSystemMetrics(SM_REMOTESESSION) == 0) {
            SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment");
        }
    }
    return true;
}

bool InstallerWorker::registerUninstaller() {
    QString uninstallKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VioraEDA";
    QSettings regUninst(uninstallKey, QSettings::NativeFormat);

    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_config.installDir + "/bin"));
    QString appExe = binDir + "\\VioraEDA.exe";
    QString setupExe = binDir + "\\VioraEDA_Setup.exe";

    regUninst.setValue("DisplayName", "VioraEDA 2026 Suite");
    regUninst.setValue("DisplayVersion", "2026.1.0");
    regUninst.setValue("Publisher", "Janada Sroor");
    regUninst.setValue("InstallLocation", QDir::toNativeSeparators(m_config.installDir));
    regUninst.setValue("DisplayIcon", appExe + ",0");
    regUninst.setValue("UninstallString", "\"" + setupExe + "\" --uninstall");
    regUninst.setValue("QuietUninstallString", "\"" + setupExe + "\" --uninstall --silent");
    regUninst.setValue("NoModify", 1);
    regUninst.setValue("NoRepair", 1);
    return true;
}

bool InstallerWorker::unregisterUninstaller() {
    QString uninstallKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VioraEDA";
    QSettings regUninst(uninstallKey, QSettings::NativeFormat);
    regUninst.clear();
    return true;
}
#endif
