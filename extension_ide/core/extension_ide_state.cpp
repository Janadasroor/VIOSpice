/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_ide_state.h"
#include <QSettings>
#include <QApplication>

namespace IDE {

ExtensionIdeState::ExtensionIdeState(QObject* parent)
    : QObject(parent) {
}

void ExtensionIdeState::saveState(const QByteArray& geometry, const QByteArray& dockState,
                                  const QStringList& openFiles, const QString& extensionDir) {
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("geometry", geometry);
    settings.setValue("dockState", dockState);
    settings.setValue("openFiles", openFiles);
    settings.setValue("extensionDir", extensionDir);
    settings.endGroup();
}

bool ExtensionIdeState::restoreState(QByteArray& geometry, QByteArray& dockState,
                                     QStringList& openFiles, QString& extensionDir) {
    QSettings settings;
    settings.beginGroup(kSettingsGroup);

    if (!settings.contains("geometry")) {
        settings.endGroup();
        return false;
    }

    geometry = settings.value("geometry").toByteArray();
    dockState = settings.value("dockState").toByteArray();
    openFiles = settings.value("openFiles").toStringList();
    extensionDir = settings.value("extensionDir").toString();
    settings.endGroup();
    return true;
}

} // namespace IDE
