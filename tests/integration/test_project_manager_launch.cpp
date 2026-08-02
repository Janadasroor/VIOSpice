/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduces the "schematic editor opens home dir instead of current project"
 * bug. Drives ProjectManager through several open + launch scenarios and
 * inspects the SchematicEditor's project context.
 */

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QMetaObject>

#include "../../ui/project_manager.h"
#include "../../schematic/editor/schematic_editor.h"
#include "../../core/project/config_manager.h"

static SchematicEditor* lastEditor() {
    SchematicEditor* editor = nullptr;
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* s = qobject_cast<SchematicEditor*>(w)) { editor = s; break; }
    }
    return editor;
}

static void settle(int ms = 400) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) return 1;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());

    const QString homeDir = tmp.path() + "/home";      // stands in for "home"
    const QString projDir = tmp.path() + "/MyProject"; // the "current project"
    QDir().mkpath(homeDir);
    QDir().mkpath(projDir);
    {
        QFile f(projDir + "/MyProject.flux");
        if (f.open(QIODevice::WriteOnly)) f.write("{}");
    }
    {
        QFile f(projDir + "/MyProject.flxsch");
        if (f.open(QIODevice::WriteOnly)) f.write("{}");
    }

    int failures = 0;

    // Scenario A: single project folder opened, then launch via tile.
    {
        ProjectManager pm;
        QMetaObject::invokeMethod(&pm, "openProject", Qt::DirectConnection, Q_ARG(QString, projDir));
        QMetaObject::invokeMethod(&pm, "openSchematicEditor", Qt::DirectConnection);
        settle();
        SchematicEditor* e = lastEditor();
        QString title = e ? e->windowTitle() : "none";
        bool ok = title.contains("MyProject");
        fprintf(stderr, "A title=%s ok=%d\n", title.toUtf8().constData(), ok ? 1 : 0);
        if (!ok) ++failures;
        if (e) e->close();
        settle(50);
    }

    // Scenario B: workspace = [home, MyProject]; tile should use MyProject (current project)
    {
        for (auto* w : QApplication::topLevelWidgets())
            if (auto* pm2 = qobject_cast<ProjectManager*>(w)) { pm2->close(); }
        settle(50);

        ConfigManager::instance().setWorkspaceFolders(QStringList() << homeDir << projDir);
        ProjectManager pm;
        settle(50);
        QTreeWidget* tree = pm.findChild<QTreeWidget*>();
        if (tree) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                fprintf(stderr, "B tree[%d]=%s\n", i, tree->topLevelItem(i)->text(0).toUtf8().constData());
        }
        QMetaObject::invokeMethod(&pm, "openSchematicEditor", Qt::DirectConnection);
        settle();
        SchematicEditor* e = lastEditor();
        QString title = e ? e->windowTitle() : "none";
        bool ok = title.contains("MyProject") && !title.contains("home");
        fprintf(stderr, "B title=%s ok=%d\n", title.toUtf8().constData(), ok ? 1 : 0);
        if (!ok) ++failures;
        if (e) e->close();
        settle(50);
    }

    // Scenario C: startup restore path — main.cpp calls restoreSchematicEditorWindow().
    {
        for (auto* w : QApplication::topLevelWidgets())
            if (auto* pm2 = qobject_cast<ProjectManager*>(w)) { pm2->close(); }
        settle(50);

        ConfigManager::instance().setToolProperty("SchematicEditor", "windowOpen", true);
        ConfigManager::instance().setWorkspaceFolders(QStringList() << homeDir << projDir);
        ProjectManager pm;
        settle(50);
        pm.restoreSchematicEditorWindow();
        settle();
        SchematicEditor* e = lastEditor();
        QString title = e ? e->windowTitle() : "none";
        bool ok = title.contains("MyProject") && !title.contains("home");
        fprintf(stderr, "C title=%s ok=%d\n", title.toUtf8().constData(), ok ? 1 : 0);
        if (!ok) ++failures;
        if (e) e->close();
        settle(50);
    }

    fprintf(stderr, "RESULT ok=%d\n", failures == 0 ? 1 : 0);
    return failures == 0 ? 0 : 2;
}
