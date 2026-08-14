/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_window.h"
#include "installer_worker.h"
#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QDir>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
namespace {
bool checkIsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}
}
#endif

int main(int argc, char *argv[]) {
    bool isUninstall = false;
    bool isSilent = false;
    QString customDir;

#ifdef _WIN32
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(GetCommandLineW(), &numArgs);
    if (argList) {
        for (int i = 1; i < numArgs; ++i) {
            QString arg = QString::fromWCharArray(argList[i]);
            if (arg.compare("--uninstall", Qt::CaseInsensitive) == 0 ||
                arg.compare("-u", Qt::CaseInsensitive) == 0 ||
                arg.compare("/uninstall", Qt::CaseInsensitive) == 0) {
                isUninstall = true;
            } else if (arg.compare("--silent", Qt::CaseInsensitive) == 0 ||
                       arg.compare("-s", Qt::CaseInsensitive) == 0 ||
                       arg.compare("/silent", Qt::CaseInsensitive) == 0 ||
                       arg.compare("/S", Qt::CaseInsensitive) == 0) {
                isSilent = true;
            } else if ((arg.compare("--dir", Qt::CaseInsensitive) == 0 ||
                        arg.compare("-d", Qt::CaseInsensitive) == 0 ||
                        arg.compare("/D", Qt::CaseInsensitive) == 0) && i + 1 < numArgs) {
                customDir = QString::fromWCharArray(argList[++i]);
            }
        }
        LocalFree(argList);
    }
#else
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--uninstall" || arg == "-u" || arg == "/uninstall") {
            isUninstall = true;
        } else if (arg == "--silent" || arg == "-s" || arg == "/silent" || arg == "/S") {
            isSilent = true;
        } else if ((arg == "--dir" || arg == "-d" || arg == "/D") && i + 1 < argc) {
            customDir = QString::fromLocal8Bit(argv[++i]);
        }
    }
#endif

    if (isSilent) {
#ifdef _WIN32
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
#endif
        QCoreApplication app(argc, argv);
        InstallConfig config;
        config.isUninstall = isUninstall;
        config.isSilent = true;

        if (!customDir.isEmpty()) {
            config.installDir = QDir::cleanPath(customDir);
        } else {
#ifdef _WIN32
            if (checkIsAdmin()) {
                config.installDir = "C:/Program Files/VioraEDA";
            } else {
                QString localApp = qEnvironmentVariable("LOCALAPPDATA");
                if (localApp.isEmpty()) localApp = QDir::homePath() + "/AppData/Local";
                config.installDir = QDir::cleanPath(localApp + "/Programs/VioraEDA");
            }
#else
            config.installDir = "/opt/VioraEDA";
#endif
        }

        std::cout << "[VioraEDA Setup] Target: " << config.installDir.toStdString() << std::endl;
        InstallerWorker worker(config);
        QObject::connect(&worker, &InstallerWorker::statusUpdated, [](const QString& status) {
            std::cout << "[Setup] " << status.toStdString() << std::endl;
        });

        int exitCode = 0;
        QObject::connect(&worker, &InstallerWorker::finished, [&app, &exitCode](bool success, const QString& err) {
            if (!success) {
                std::cerr << "[Setup Error] " << err.toStdString() << std::endl;
                exitCode = 1;
            } else {
                std::cout << "[Setup] Complete. Success." << std::endl;
            }
            app.quit();
        });

        worker.start();
        app.exec();
        worker.wait();
        return exitCode;
    }

    QApplication app(argc, argv);
    app.setApplicationName("VioraEDA Setup");
    app.setOrganizationName("VIO");

    InstallerWindow window(isUninstall);
    window.show();

    return app.exec();
}
