/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_window.h"
#include <QApplication>
#include <QStringList>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("VioraEDA Setup");
    app.setOrganizationName("VIO");

    bool isUninstall = false;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--uninstall" || arg == "-u" || arg == "/uninstall") {
            isUninstall = true;
        }
    }

    InstallerWindow window(isUninstall);
    window.show();

    return app.exec();
}
