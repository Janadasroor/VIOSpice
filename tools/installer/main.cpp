/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_window.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("VioraEDA Setup");
    app.setOrganizationName("VIO");

    InstallerWindow window;
    window.show();

    return app.exec();
}
