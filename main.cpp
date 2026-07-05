/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui/splash_screen.h"
#include "theme_manager.h"
#include "recent_projects.h"
#include "ws_server.h"
#include "schematic/editor/schematic_editor.h"
#include "schematic/ui/netlist_editor.h"
#include "ui/csv_viewer.h"
#include "ui/project_manager.h"
#include "ui/app_command_server.h"
#include "config_manager.h"
#include "symbols/symbol_editor.h"
#include "schematic/factories/schematic_item_registry.h"
#include "schematic/tools/schematic_tool_registry_builtin.h"
#include "symbols/symbol_library.h"
#include "simulator/bridge/model_library_manager.h"
#include "simulator/bridge/flux_sim_bridge.h"
#include "core/flux/engine/flux_script_engine.h"
#include "pcb/editor/mainwindow.h"
#include "pcb/factories/pcb_item_registry.h"
#include "pcb/tools/pcb_tool_registry_builtin.h"
#include "footprints/footprint_library.h"
#include "simulator/bridge/sim_manager.h"

#include "core/update_checker.h"

#include <QIcon>
#include <QApplication>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>
#include <QFuture>
#include <QMessageBox>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>

extern void initEmbeddedPython();
extern void shutdownEmbeddedPython();

static int sigFd[2] = {-1, -1};

static void sigIntHandler(int) {
    char c = 1;
    if (sigFd[1] != -1) write(sigFd[1], &c, sizeof(c));
}

extern "C" {
#include "ui/python_hooks.h"
}

#include <iostream>
#include <QStandardPaths>

static void saveCurrentSession(void* excluding) {
    QStringList openFiles;
    QString activePcbFile;
    SchematicEditor* lastSch = nullptr;
    MainWindow* lastPcb = nullptr;

    QWidget* excludingWidget = static_cast<QWidget*>(excluding);

    for (auto w : QApplication::topLevelWidgets()) {
        if (w == excludingWidget) continue;
        if (w->isVisible()) {
            if (auto* sch = qobject_cast<SchematicEditor*>(w)) {
                lastSch = sch;
                for (int i = 0; i < sch->tabCount(); ++i) {
                    QString path = sch->tabFilePath(i);
                    if (!path.isEmpty()) openFiles.append(path);
                }
            }
            if (auto* pcb = qobject_cast<MainWindow*>(w)) {
                lastPcb = pcb;
                activePcbFile = pcb->currentFilePath();
            }
        }
    }

    ConfigManager::instance().setToolProperty("SchematicEditor", "openFiles", openFiles);
    ConfigManager::instance().setToolProperty("SchematicEditor", "windowOpen", lastSch != nullptr);
    if (lastSch) {
        ConfigManager::instance().setToolProperty("SchematicEditor", "activeTabIndex", lastSch->currentTabIndex());
        ConfigManager::instance().saveWindowState("SchematicEditor", lastSch->saveGeometry(), lastSch->saveState());
    } else {
        ConfigManager::instance().setToolProperty("SchematicEditor", "openFiles", QStringList());
    }

    ConfigManager::instance().setToolProperty("PCBEditor", "openFile", activePcbFile);
    ConfigManager::instance().setToolProperty("PCBEditor", "windowOpen", lastPcb != nullptr);
    if (lastPcb) {
        ConfigManager::instance().saveWindowState("PCBEditor", lastPcb->saveGeometry(), lastPcb->saveState());
    }
}

int main(int argc, char *argv[])
{
    // Early exit for --help and --version to avoid QApplication + ngspice/Python init
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << "VioraEDA - Electronic Design Automation Suite" << std::endl;
            std::cout << "Usage: VioraEDA [options] [file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --help, -h     Show this help" << std::endl;
            std::cout << "  --version, -V  Show version" << std::endl;
            std::cout << std::endl;
            std::cout << "If a .flxsch or .pcb file is specified, it is opened on startup." << std::endl;
            std::cout.flush();
            std::_Exit(0);
        }
        if (a == "--version" || a == "-V") {
            std::cout << "VioraEDA 1.0" << std::endl;
            std::cout.flush();
            std::_Exit(0);
        }
    }

    // Enable ASan-friendly exit behavior
    qputenv("ASAN_OPTIONS", "detect_leaks=1");

#if defined(_WIN32) || defined(_WIN64)
    // Force native desktop OpenGL composition to prevent DXGI/D3D11 composition conflicts with QOpenGLWidget (causing black lines and empty waveforms)
    qputenv("QT_OPENGL", "desktop");
    qputenv("QT_RHI_BACKEND", "opengl");
    qputenv("QSG_RHI_BACKEND", "opengl");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

    QApplication a(argc, argv);

    // Register callback for dynamic session saving
    ConfigManager::setSessionSaveCallback(saveCurrentSession);

    // MANDATORY: Setup custom simulation engine environment BEFORE any engine code is loaded
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::homePath() + "/.viospice";
    }
    QDir().mkpath(appDataPath);

    qputenv("SPICE_SCRIPTS", appDataPath.toUtf8());
    qputenv("SPICE_LIB_DIR", appDataPath.toUtf8());
    initEmbeddedPython();
    ThemeManager::instance();
    initializeFluxSimBridge();
    FluxScriptEngine::instance().initialize();

    a.setApplicationName("VioraEDA");
    a.setOrganizationName("VIO");
    a.setWindowIcon(QIcon(":/icons/viora_eda_logo.png"));

    QString serverName = "VioraEDA_instance_server";
    QString fileToOpen;
    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (!arg.startsWith("-")) fileToOpen = QFileInfo(arg).absoluteFilePath();
    }

    QLocalServer* server = new QLocalServer(&a);
    QLocalServer::removeServer(serverName);
    server->listen(serverName);

    SplashScreen* splash = new SplashScreen();
    splash->show();
    a.processEvents();

    SchematicItemRegistry::registerBuiltInItems();
    SchematicToolRegistryBuiltIn::registerBuiltInTools();
    PCBItemRegistry::registerBuiltInItems();
    PCBToolRegistryBuiltIn::registerBuiltInTools();

    FootprintLibraryManager::instance();
    ModelLibraryManager::instance();

    // Start UI Command Server for Python interaction
    if (ConfigManager::instance().isFeatureEnabled("ui_command_server", true)) {
        int port = ConfigManager::instance().toolProperty("Connectivity", "Port", 18790).toInt();
        UICommandServer::instance().start(port);
    }

    QMetaObject::invokeMethod(qApp, [splash, fileToOpen]() {
        if (!fileToOpen.isEmpty()) {
            SchematicEditor* sch = new SchematicEditor();
            sch->setAttribute(Qt::WA_DeleteOnClose);
            sch->openFile(fileToOpen);
            sch->show();
        } else {
            ProjectManager* pm = new ProjectManager;
            pm->setAttribute(Qt::WA_DeleteOnClose);
            pm->show();

            // Restore previously open schematic tabs
            bool schOpen = ConfigManager::instance().toolProperty("SchematicEditor", "windowOpen", false).toBool();
            if (schOpen) {
                SchematicEditor* sch = new SchematicEditor();
                sch->setAttribute(Qt::WA_DeleteOnClose);
                sch->show();
            }

            // Restore previously open PCB editor
            bool pcbOpen = ConfigManager::instance().toolProperty("PCBEditor", "windowOpen", false).toBool();
            if (pcbOpen) {
                MainWindow* pcb = new MainWindow();
                pcb->setAttribute(Qt::WA_DeleteOnClose);
                QString lastPcb = ConfigManager::instance().toolProperty("PCBEditor", "openFile").toString();
                if (!lastPcb.isEmpty()) {
                    if (QFile::exists(lastPcb)) {
                        pcb->openFile(lastPcb);
                    } else {
                        QFileInfo fi(lastPcb);
                        pcb->setProjectContext(fi.completeBaseName(), fi.absolutePath());
                    }
                }
                pcb->show();
            }
        }
        splash->deleteLater();

        auto *checker = new UpdateChecker("0.1", qApp);
        QObject::connect(checker, &UpdateChecker::updateAvailable,
                         [](const QString &ver, const QString &url) {
            qDebug() << "Update available:" << ver << url;
            if (auto *w = qApp->activeWindow()) {
                if (auto *sb = w->findChild<QStatusBar*>()) {
                    sb->showMessage(
                        QStringLiteral("VioraEDA v%1 is available (you have v0.1) — %2")
                            .arg(ver, url), 15000);
                }
            }
        });
        checker->checkAsync();
    }, Qt::QueuedConnection);

    // Graceful Ctrl+C: pipe signal to event loop via QSocketNotifier
    if (pipe(sigFd) == 0) {
        auto *sn = new QSocketNotifier(sigFd[0], QSocketNotifier::Read, &a);
        QObject::connect(sn, &QSocketNotifier::activated, &a, [&a]() {
            char tmp;
            read(sigFd[0], &tmp, sizeof(tmp));
            a.quit();
        });
        signal(SIGINT, sigIntHandler);
    }

    int exitCode = a.exec();

    // Save session state BEFORE closing windows — only currently open files are saved
    for (auto w : QApplication::topLevelWidgets()) {
        if (auto* sch = qobject_cast<SchematicEditor*>(w)) {
            QStringList openFiles;
            for (int i = 0; i < sch->tabCount(); ++i) {
                QString path = sch->tabFilePath(i);
                if (!path.isEmpty()) openFiles.append(path);
            }
            ConfigManager::instance().setToolProperty("SchematicEditor", "openFiles", openFiles);
            ConfigManager::instance().setToolProperty("SchematicEditor", "activeTabIndex", sch->currentTabIndex());
            ConfigManager::instance().saveWindowState("SchematicEditor", sch->saveGeometry(), sch->saveState());
        }
        if (auto* pcb = qobject_cast<MainWindow*>(w)) {
            ConfigManager::instance().setToolProperty("PCBEditor", "openFile", pcb->currentFilePath());
            ConfigManager::instance().saveWindowState("PCBEditor", pcb->saveGeometry(), pcb->saveState());
        }
    }

    qDebug() << "Closing all windows...";
    for (auto w : QApplication::topLevelWidgets()) {
        w->close();
    }

    // Process events to let deleteLater() run for WA_DeleteOnClose widgets
    for(int i=0; i<10; ++i) QApplication::processEvents(QEventLoop::AllEvents, 200);

    SchematicToolRegistryBuiltIn::cleanup();
    shutdownEmbeddedPython();
    return exitCode;
}
