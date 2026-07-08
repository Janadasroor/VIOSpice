/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_IDE_STATE_H
#define EXTENSION_IDE_STATE_H

#include <QObject>
#include <QStringList>
#include <QByteArray>

namespace IDE {

class ExtensionIdeState : public QObject {
    Q_OBJECT
public:
    explicit ExtensionIdeState(QObject* parent = nullptr);

    void saveState(const QByteArray& geometry, const QByteArray& dockState,
                   const QStringList& openFiles, const QString& extensionDir);
    bool restoreState(QByteArray& geometry, QByteArray& dockState,
                      QStringList& openFiles, QString& extensionDir);

private:
    static constexpr const char* kSettingsGroup = "ExtensionIDE";
};

} // namespace IDE

#endif // EXTENSION_IDE_STATE_H
