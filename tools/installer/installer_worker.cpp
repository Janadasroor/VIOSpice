/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winnls.h>
#include <shlwapi.h>
#endif

namespace {

#ifdef _WIN32
bool createShortcutWin32(const QString& linkPath, const QString& targetPath,
                         const QString& arguments, const QString& workingDir,
                         const QString& description, const QString& iconPath, int iconIndex) {
    HRESULT hr = CoInitialize(nullptr);
    bool coInit = SUCCEEDED(hr);

    IShellLinkW* psl = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr) || !psl) {
        if (coInit) CoUninitialize();
        return false;
    }

    psl->SetPath(reinterpret_cast<LPCWSTR>(targetPath.toStdWString().c_str()));
    if (!arguments.isEmpty()) {
        psl->SetArguments(reinterpret_cast<LPCWSTR>(arguments.toStdWString().c_str()));
    }
    if (!workingDir.isEmpty()) {
        psl->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(workingDir.toStdWString().c_str()));
    }
    if (!description.isEmpty()) {
        psl->SetDescription(reinterpret_cast<LPCWSTR>(description.toStdWString().c_str()));
    }
    if (!iconPath.isEmpty()) {
        psl->SetIconLocation(reinterpret_cast<LPCWSTR>(iconPath.toStdWString().c_str()), iconIndex);
    }

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
    bool success = false;
    if (SUCCEEDED(hr) && ppf) {
        QFileInfo fi(linkPath);
        QDir().mkpath(fi.absolutePath());
        hr = ppf->Save(reinterpret_cast<LPCWSTR>(linkPath.toStdWString().c_str()), TRUE);
        success = SUCCEEDED(hr);
        ppf->Release();
    }

    psl->Release();
    if (coInit) CoUninitialize();
    return success;
}
#endif

} // namespace

InstallerWorker::InstallerWorker(const InstallOptions& options, QObject* parent)
    : QThread(parent), m_options(options) {
}

void InstallerWorker::requestCancel() {
    m_cancelRequested.store(true);
}

void InstallerWorker::run() {
    if (m_options.isUninstall) {
        doUninstall();
    } else {
        doInstall();
    }
}

bool InstallerWorker::collectFilesToCopy(QList<QPair<QString, QString>>& outFileList, quint64& outTotalBytes) {
    outFileList.clear();
    outTotalBytes = 0;

    QString appDir = QCoreApplication::applicationDirPath();
    QString targetRoot = QDir::cleanPath(m_options.installDir);

    auto addDirectoryRecursive = [&](const QString& srcDir, const QString& dstSubDir) {
        QDir dir(srcDir);
        if (!dir.exists()) return;

        QDirIterator it(srcDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QString srcFile = it.filePath();
            QString relPath = dir.relativeFilePath(srcFile);
            QString dstFile = QDir::cleanPath(targetRoot + "/" + dstSubDir + "/" + relPath);

            QFileInfo fi(srcFile);
            outTotalBytes += fi.size();
            outFileList.append(qMakePair(srcFile, dstFile));
        }
    };

    // 1. If installer is running from a packaged layout where bin/, cm/, etc. sit alongside it
    bool isStagedLayout = QDir(appDir + "/bin").exists() || QDir(appDir + "/../bin").exists();
    QString baseDir = QDir(appDir + "/bin").exists() ? appDir : (QDir(appDir + "/../bin").exists() ? QDir::cleanPath(appDir + "/..") : appDir);

    if (isStagedLayout) {
        addDirectoryRecursive(baseDir + "/bin", "bin");
        addDirectoryRecursive(baseDir + "/cm", "cm");
        addDirectoryRecursive(baseDir + "/python", "python");
        addDirectoryRecursive(baseDir + "/examples", "examples");
        addDirectoryRecursive(baseDir + "/templates", "templates");
        addDirectoryRecursive(baseDir + "/core/simulation/model_params", "core/simulation/model_params");
        addDirectoryRecursive(baseDir + "/ViospiceLib", "ViospiceLib");
    } else {
        // 2. Running in development/build environment (e.g. C:/VioraEDA/build)
        // Copy build binaries to bin/
        QDir buildDir(appDir);
        QFileInfoList binFiles = buildDir.entryInfoList(QDir::Files | QDir::NoSymLinks);
        for (const QFileInfo& fi : binFiles) {
            QString name = fi.fileName();
            if (name.endsWith(".exe", Qt::CaseInsensitive) || name.endsWith(".dll", Qt::CaseInsensitive)) {
                outTotalBytes += fi.size();
                outFileList.append(qMakePair(fi.absoluteFilePath(), QDir::cleanPath(targetRoot + "/bin/" + name)));
            }
        }
        // Also copy subdirs in build if any (e.g. cm/, platforms/, sqldrivers/)
        addDirectoryRecursive(appDir + "/cm", "cm");
        addDirectoryRecursive(appDir + "/platforms", "bin/platforms");
        addDirectoryRecursive(appDir + "/sqldrivers", "bin/sqldrivers");
        addDirectoryRecursive(appDir + "/styles", "bin/styles");

        // Copy repository source assets
        QString repoRoot = QDir::cleanPath(appDir + "/..");
        addDirectoryRecursive(repoRoot + "/python", "python");
        addDirectoryRecursive(repoRoot + "/examples", "examples");
        addDirectoryRecursive(repoRoot + "/templates", "templates");
        addDirectoryRecursive(repoRoot + "/core/simulation/model_params", "core/simulation/model_params");

        // Check for ViospiceLib in user profile or repo
        QString userLib = QDir::homePath() + "/ViospiceLib";
        if (QDir(userLib).exists()) {
            addDirectoryRecursive(userLib, "ViospiceLib");
        } else if (QDir(repoRoot + "/ViospiceLib").exists()) {
            addDirectoryRecursive(repoRoot + "/ViospiceLib", "ViospiceLib");
        }
    }

    return !outFileList.isEmpty();
}

bool InstallerWorker::copyFileChunked(const QString& src, const QString& dst, quint64& copiedBytes, quint64 totalBytes) {
    if (m_cancelRequested.load()) return false;

    QFileInfo dstInfo(dst);
    QDir().mkpath(dstInfo.absolutePath());

    if (QFile::exists(dst)) {
        QFile::remove(dst);
    }

    QFile srcFile(src);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    QFile dstFile(dst);
    if (!dstFile.open(QIODevice::WriteOnly)) {
        srcFile.close();
        return false;
    }

    constexpr qint64 kChunkSize = 64 * 1024;
    char buffer[kChunkSize];

    while (!srcFile.atEnd()) {
        if (m_cancelRequested.load()) {
            srcFile.close();
            dstFile.close();
            QFile::remove(dst);
            return false;
        }

        qint64 bytesRead = srcFile.read(buffer, kChunkSize);
        if (bytesRead <= 0) break;

        qint64 bytesWritten = dstFile.write(buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            srcFile.close();
            dstFile.close();
            QFile::remove(dst);
            return false;
        }

        copiedBytes += bytesWritten;

        double elapsedSec = m_timer.elapsed() / 1000.0;
        double speedMBps = (elapsedSec > 0.05) ? ((copiedBytes / (1024.0 * 1024.0)) / elapsedSec) : 0.0;
        int pct = totalBytes > 0 ? static_cast<int>((copiedBytes * 100) / totalBytes) : 0;
        if (pct > 100) pct = 100;

        emit progressChanged(pct, dstInfo.fileName(), speedMBps, copiedBytes, totalBytes);
    }

    srcFile.close();
    dstFile.close();
    return true;
}

void InstallerWorker::doInstall() {
    emit statusChanged("Preparing installation directory...");
    m_timer.start();

    QList<QPair<QString, QString>> fileList;
    quint64 totalBytes = 0;
    if (!collectFilesToCopy(fileList, totalBytes)) {
        emit installationFinished(false, "Failed to locate installation source payload files.");
        return;
    }

    emit statusChanged("Copying files to destination...");
    quint64 copiedBytes = 0;
    QList<QString> copiedDestFiles;

    for (const auto& pair : fileList) {
        if (m_cancelRequested.load()) break;

        if (!copyFileChunked(pair.first, pair.second, copiedBytes, totalBytes)) {
            if (m_cancelRequested.load()) break;
            // Continue or report warning
        } else {
            copiedDestFiles.append(pair.second);
        }
    }

    if (m_cancelRequested.load()) {
        emit statusChanged("Canceling installation and rolling back...");
        for (const QString& file : copiedDestFiles) {
            QFile::remove(file);
        }
        emit installationFinished(false, "Installation canceled by user.");
        return;
    }

    // Windows Shell & System Integration
#ifdef _WIN32
    if (m_options.createDesktopShortcut || m_options.createStartMenuShortcut) {
        emit statusChanged("Creating application shortcuts...");
        createWindowsShortcuts();
    }

    if (m_options.associateFiles) {
        emit statusChanged("Registering schematic and script file associations...");
        setupFileAssociations();
    }

    if (m_options.addToPath) {
        emit statusChanged("Adding VioraEDA to user PATH environment...");
        updatePathEnvironment(true);
    }

    emit statusChanged("Registering uninstaller with Windows...");
    registerUninstaller(totalBytes);
#endif

    emit progressChanged(100, "Done", 0.0, totalBytes, totalBytes);
    emit statusChanged("Installation complete!");
    emit installationFinished(true, QString());
}

void InstallerWorker::doUninstall() {
    emit statusChanged("Removing Windows system integrations...");

#ifdef _WIN32
    removeWindowsShortcuts();
    removeFileAssociations();
    updatePathEnvironment(false);
    removeUninstallerRegistry();
#endif

    emit statusChanged("Deleting application files...");
    QString targetRoot = QDir::cleanPath(m_options.installDir);
    if (!targetRoot.isEmpty() && QDir(targetRoot).exists()) {
        QDir dir(targetRoot);
        dir.removeRecursively();
    }

    emit progressChanged(100, "Done", 0.0, 100, 100);
    emit statusChanged("Uninstallation complete!");
    emit installationFinished(true, QString());
}

#ifdef _WIN32
bool InstallerWorker::createWindowsShortcuts() {
    QString binDir = QDir::cleanPath(m_options.installDir + "/bin");
    QString appExe = binDir + "/VioraEDA.exe";
    QString cliExe = binDir + "/viora.exe";

    if (!QFile::exists(appExe)) return false;

    // Desktop Shortcut
    if (m_options.createDesktopShortcut) {
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (!desktopPath.isEmpty()) {
            QString linkFile = desktopPath + "/VioraEDA.lnk";
            createShortcutWin32(linkFile, appExe, "", binDir, "VioraEDA 2026.1 High-Performance EDA Suite", appExe, 0);
        }
    }

    // Start Menu Shortcuts
    if (m_options.createStartMenuShortcut) {
        QString programsPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        if (!programsPath.isEmpty()) {
            QString menuFolder = programsPath + "/VioraEDA";
            QDir().mkpath(menuFolder);

            createShortcutWin32(menuFolder + "/VioraEDA.lnk", appExe, "", binDir, "VioraEDA 2026.1", appExe, 0);
            if (QFile::exists(cliExe)) {
                createShortcutWin32(menuFolder + "/Viora CLI.lnk", cliExe, "", binDir, "VioraEDA Command Line Interface", cliExe, 0);
            }
            // Add Uninstall shortcut in start menu
            QString setupExe = binDir + "/VioraEDA_Setup.exe";
            if (QFile::exists(setupExe)) {
                createShortcutWin32(menuFolder + "/Uninstall VioraEDA.lnk", setupExe, "--uninstall", binDir, "Uninstall VioraEDA", setupExe, 0);
            }
        }
    }

    return true;
}

bool InstallerWorker::removeWindowsShortcuts() {
    // Remove Desktop Shortcut
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktopPath.isEmpty()) {
        QFile::remove(desktopPath + "/VioraEDA.lnk");
    }

    // Remove Start Menu Folder
    QString programsPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!programsPath.isEmpty()) {
        QString menuFolder = programsPath + "/VioraEDA";
        QDir(menuFolder).removeRecursively();
    }
    return true;
}

bool InstallerWorker::setupFileAssociations() {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_options.installDir + "/bin"));
    QString appExe = binDir + "\\VioraEDA.exe";
    QString openCmd = "\"" + appExe + "\" \"%1\"";

    struct FileTypeAssoc {
        const char* ext;
        const char* progId;
        const char* desc;
    };

    const FileTypeAssoc assocs[] = {
        { ".flxsch",    "VioraEDA.Schematic.1",  "VioraEDA Schematic Document" },
        { ".flux",      "VioraEDA.FluxScript.1", "FluxScript Document" },
        { ".flxpcb",    "VioraEDA.PCB.1",        "VioraEDA PCB Document" },
        { ".cir",       "VioraEDA.Netlist.1",    "SPICE Netlist Document" },
        { ".sp",        "VioraEDA.Netlist.1",    "SPICE Netlist Document" },
        { ".asc",       "VioraEDA.Schematic.1",  "LTspice Schematic Document" },
        { ".kicad_sch", "VioraEDA.Schematic.1",  "KiCad Schematic Document" },
        { ".kicad_pcb", "VioraEDA.PCB.1",        "KiCad PCB Document" },
    };

    for (const auto& a : assocs) {
        // HKCU\Software\Classes\<ProgID>
        QString progKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.progId);
        QSettings regProg(progKey, QSettings::NativeFormat);
        regProg.setValue(".", a.desc);
        regProg.setValue("DefaultIcon/.", QString("%1,0").arg(appExe));
        regProg.setValue("shell/open/command/.", openCmd);

        // HKCU\Software\Classes\<.ext>
        QString extKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.ext);
        QSettings regExt(extKey, QSettings::NativeFormat);
        regExt.setValue(".", a.progId);
        regExt.setValue("Content Type", "application/x-vioraeda");
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool InstallerWorker::removeFileAssociations() {
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

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool InstallerWorker::updatePathEnvironment(bool add) {
    QString binDir = QDir::toNativeSeparators(QDir::cleanPath(m_options.installDir + "/bin"));
    QSettings envSettings("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);

    QString currentPath = envSettings.value("Path").toString();
    QStringList pathList = currentPath.split(';', Qt::SkipEmptyParts);

    bool changed = false;
    if (add) {
        bool alreadyPresent = false;
        for (const QString& p : pathList) {
            if (p.trimmed().compare(binDir, Qt::CaseInsensitive) == 0) {
                alreadyPresent = true;
                break;
            }
        }
        if (!alreadyPresent) {
            pathList.append(binDir);
            changed = true;
        }
    } else {
        for (int i = pathList.size() - 1; i >= 0; --i) {
            if (pathList[i].trimmed().compare(binDir, Qt::CaseInsensitive) == 0) {
                pathList.removeAt(i);
                changed = true;
            }
        }
    }

    if (changed) {
        envSettings.setValue("Path", pathList.join(';'));
        envSettings.sync();
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, nullptr);
    }
    return true;
}

bool InstallerWorker::registerUninstaller(quint64 totalBytes) {
    QString targetRoot = QDir::toNativeSeparators(QDir::cleanPath(m_options.installDir));
    QString binDir = targetRoot + "\\bin";
    QString appExe = binDir + "\\VioraEDA.exe";
    QString setupExe = binDir + "\\VioraEDA_Setup.exe";

    QSettings uninstReg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VioraEDA", QSettings::NativeFormat);
    uninstReg.setValue("DisplayName", "VioraEDA 2026.1");
    uninstReg.setValue("DisplayVersion", "2026.1");
    uninstReg.setValue("Publisher", "Janadasroor");
    uninstReg.setValue("InstallLocation", targetRoot);
    uninstReg.setValue("DisplayIcon", QString("%1,0").arg(appExe));
    uninstReg.setValue("UninstallString", QString("\"%1\" --uninstall").arg(setupExe));
    uninstReg.setValue("QuietUninstallString", QString("\"%1\" --uninstall --silent").arg(setupExe));
    uninstReg.setValue("EstimatedSize", static_cast<qulonglong>(totalBytes / 1024));
    uninstReg.setValue("URLInfoAbout", "https://github.com/Janadasroor/VioraEDA");
    uninstReg.setValue("NoModify", 1);
    uninstReg.setValue("NoRepair", 1);
    uninstReg.sync();

    return true;
}

bool InstallerWorker::removeUninstallerRegistry() {
    QSettings uninstReg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VioraEDA", QSettings::NativeFormat);
    uninstReg.clear();
    uninstReg.sync();
    return true;
}
#endif
