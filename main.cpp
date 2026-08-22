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
#include "viora_ide/viora_ide_window.h"
#include "pcb/factories/pcb_item_registry.h"
#include "pcb/tools/pcb_tool_registry_builtin.h"
#include "footprints/footprint_library.h"
#include "simulator/bridge/sim_manager.h"

#include "core/update_checker.h"

#include <QIcon>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
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
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <csignal>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <shlobj.h>
#include <QSettings>
#endif

extern void initEmbeddedPython();
extern void shutdownEmbeddedPython();

#ifndef _WIN32
static int sigFd[2] = {-1, -1};

static void sigIntHandler(int) {
    char c = 1;
    if (sigFd[1] != -1) write(sigFd[1], &c, sizeof(c));
}
#else
static QApplication* g_app = nullptr;
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT && g_app) {
        QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
        return TRUE;
    }
    return FALSE;
}

static void ensureWindowsFileAssociations(bool force = false) {
    QString appExe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (appExe.isEmpty() || !QFile::exists(appExe)) return;
    QString appDir = QFileInfo(appExe).absolutePath();

    QSettings checkReg("HKEY_CURRENT_USER\\Software\\Classes\\.flxsch\\PersistentHandler", QSettings::NativeFormat);
    QString currentHandler = checkReg.value(".").toString();
    if (!force && currentHandler == "{5e941d80-bf96-11cd-b579-08002b30bfeb}") {
        return; // Already registered
    }

    struct FileTypeAssoc {
        const char* ext;
        const char* progId;
        const char* desc;
        const char* contentType;
    };

    const FileTypeAssoc assocs[] = {
        { ".flxsch",    "VioraEDA.Schematic.1",  "VioraEDA Schematic Document", "application/x-viora-schematic" },
        { ".fluxsch",   "VioraEDA.Schematic.1",  "VioraEDA Schematic Document", "application/x-viora-schematic" },
        { ".flux",      "VioraEDA.FluxScript.1", "FluxScript Source Document",   "text/plain" },
        { ".flxpcb",    "VioraEDA.PCB.1",        "VioraEDA PCB Layout Document", "application/x-viora-pcb" },
        { ".cir",       "VioraEDA.Netlist.1",    "SPICE Netlist Document",       "text/plain" },
        { ".sp",        "VioraEDA.Netlist.1",    "SPICE Netlist Document",       "text/plain" },
        { ".asc",       "VioraEDA.Schematic.1",  "Schematic Document",           "text/plain" },
        { ".kicad_sch", "VioraEDA.Schematic.1",  "Schematic Document",           "text/plain" },
        { ".kicad_pcb", "VioraEDA.PCB.1",        "PCB Layout Document",          "text/plain" },
    };

    QString openCmd = QString("\"%1\" \"%2\"").arg(appExe, "%1");

    for (const auto& a : assocs) {
        QString progKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.progId);
        QSettings regProg(progKey, QSettings::NativeFormat);
        regProg.setValue(".", a.desc);
        regProg.setValue("FriendlyTypeName", a.desc);
        regProg.setValue("DefaultIcon/.", QString("%1,0").arg(appExe));
        regProg.setValue("shell/open/command/.", openCmd);
        regProg.setValue("shell/open/FriendlyAppName", "VioraEDA Suite");

        QString extKey = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(a.ext);
        QSettings regExt(extKey, QSettings::NativeFormat);
        regExt.setValue(".", a.progId);
        regExt.setValue("Content Type", a.contentType);
        regExt.setValue("PerceivedType", "document");
        regExt.setValue(QString("OpenWithProgids/%1").arg(a.progId), "");
        regExt.setValue("PersistentHandler/.", "{5e941d80-bf96-11cd-b579-08002b30bfeb}");
    }

    QSettings regApp("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\VioraEDA.exe", QSettings::NativeFormat);
    regApp.setValue("FriendlyAppName", "VioraEDA Suite");
    regApp.setValue("shell/open/command/.", openCmd);
    for (const auto& a : assocs) {
        regApp.setValue(QString("SupportedTypes/%1").arg(a.ext), "");
    }

    QSettings regAppPath("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\VioraEDA.exe", QSettings::NativeFormat);
    regAppPath.setValue(".", appExe);
    regAppPath.setValue("Path", QString("%1").arg(appDir));

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
}
#endif

extern "C" {
#include "ui/python_hooks.h"
}

#include <iostream>
#include <QStandardPaths>

// Probe the actual OpenGL renderer. Under remote-desktop / headless sessions Qt
// falls back to a software OpenGL implementation (e.g. "GDI Generic",
// "llvmpipe", "Microsoft Basic Render Driver"). The QtQuick scene graph
// (Gemini panel's QQuickWidget) crashes under that software GL, so it must be
// switched to the software scene-graph backend instead of forcing OpenGL.
static bool isSoftwareOpenGl() {
    QOpenGLContext ctx;
    QSurfaceFormat fmt;
    fmt.setVersion(2, 1);
    ctx.setFormat(fmt);
    if (!ctx.create()) {
        // No GL context at all -> treat as software so we fall back gracefully.
        return true;
    }
    QOffscreenSurface surf;
    surf.setFormat(fmt);
    surf.create();
    if (!ctx.makeCurrent(&surf)) {
        return true;
    }
    const GLubyte* raw = ctx.functions()->glGetString(GL_RENDERER);
    bool software = true;
    if (raw) {
        const QString renderer = QString::fromLatin1(reinterpret_cast<const char*>(raw));
        const QString lower = renderer.toLower();
        // Explicitly known software renderers (GDI Generic / llvmpipe /
        // swiftshader / basic render driver / softpipe / virgl).
        software = lower.contains("gdi generic") ||
                   lower.contains("llvmpipe") ||
                   lower.contains("swiftshader") ||
                   lower.contains("basic render") ||
                   lower.contains("softpipe") ||
                   lower.contains("virgl") ||
                   lower.contains("software");
    }
    ctx.doneCurrent();
    return software;
}

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

    // If no other schematic editor is open, fall back to the one being closed so
    // its session (open tabs) is still restored on the next launch.
    if (!lastSch && excludingWidget) {
        if (auto* sch = qobject_cast<SchematicEditor*>(excludingWidget)) {
            lastSch = sch;
            for (int i = 0; i < sch->tabCount(); ++i) {
                QString path = sch->tabFilePath(i);
                if (!path.isEmpty()) openFiles.append(path);
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

static QElapsedTimer s_bootTimer;
static void logMilestone(const char* name) {
    fprintf(stderr, "[Startup Profile] %s: %lld ms\n", name, s_bootTimer.elapsed());
    fflush(stderr);
    qDebug("[Startup Profile] %s: %lld ms", name, s_bootTimer.elapsed());
}

int main(int argc, char *argv[])
{
    s_bootTimer.start();
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
            std::cout << "VioraEDA 0.2.0-beta" << std::endl;
            std::cout.flush();
            std::_Exit(0);
        }
        if (a == "--register-associations") {
#if defined(_WIN32) || defined(_WIN64)
            ensureWindowsFileAssociations(true);
            std::cout << "VioraEDA Windows file associations registered successfully." << std::endl;
            std::cout.flush();
            std::_Exit(0);
#endif
        }
    }

    // Enable ASan-friendly exit behavior
    qputenv("ASAN_OPTIONS", "detect_leaks=1");

#if defined(_WIN32) || defined(_WIN64)
    if (GetSystemMetrics(SM_REMOTESESSION) != 0) {
        qputenv("QT_OPENGL", "software");
        qputenv("QT_RHI_BACKEND", "software");
        qputenv("QSG_RHI_BACKEND", "software");
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    } else {
        qputenv("QT_OPENGL", "desktop");
        qputenv("QT_RHI_BACKEND", "opengl");
        qputenv("QSG_RHI_BACKEND", "opengl");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
#endif

    // High-DPI Display Scaling Attributes
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif

    QApplication a(argc, argv);
    QApplication::setApplicationName("VioraEDA");
    QApplication::setApplicationVersion("0.2.0-beta");

#if defined(_WIN32) || defined(_WIN64)
    ensureWindowsFileAssociations();
#endif


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

    a.setApplicationName("VioraEDA");
    a.setOrganizationName("VIO");
    a.setWindowIcon(QIcon(":/icons/viora_eda_logo.png"));

    // Apply dark theme and application palette early to prevent white flashes on Windows native windows
    ThemeManager::instance();

    SplashScreen* splash = new SplashScreen();
    splash->show();
    splash->setStatus("Initializing Embedded Python Environment...");
    splash->setProgress(1, 100);
    QCoreApplication::processEvents();

    initEmbeddedPython();

    splash->setStatus("Initializing Flux Simulation Bridge...");
    splash->setProgress(7, 100);
    initializeFluxSimBridge();

    splash->setStatus("Initializing FluxScript JIT Compiler...");
    splash->setProgress(10, 100);
    FluxScriptEngine::instance().initialize();

    QString serverName = "VioraEDA_instance_server";
    QString fileToOpen;
    bool openVioraIde = false;
    QString projectPath;
    QString extensionPath;
    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--ide") {
            openVioraIde = true;
        } else if (arg == "--project" && i + 1 < argc) {
            projectPath = QFileInfo(argv[++i]).absoluteFilePath();
            openVioraIde = true;
        } else if (arg == "--extension" && i + 1 < argc) {
            extensionPath = QFileInfo(argv[++i]).absoluteFilePath();
            openVioraIde = true;
        } else if (!arg.startsWith("-")) {
            fileToOpen = QFileInfo(arg).absoluteFilePath();
        }
    }

    QLocalServer* server = new QLocalServer(&a);
    QLocalServer::removeServer(serverName);
    server->listen(serverName);

    // Connect Library Progress Signals to Splash Screen
    QObject::connect(&FootprintLibraryManager::instance(), &FootprintLibraryManager::progressUpdated,
                     splash, [splash](const QString& status, int val, int total) {
                         splash->setStatus(status);
                         if (total > 0) splash->setProgress(20 + (val * 30 / total), 100);
                     });

    QObject::connect(&ModelLibraryManager::instance(), &ModelLibraryManager::progressUpdated,
                     splash, [splash](const QString& status, int val, int total) {
                         splash->setStatus(status);
                         if (total > 0) splash->setProgress(50 + (val * 35 / total), 100);
                     });

    splash->setStatus("Registering Built-In Component Registries...");
    splash->setProgress(15, 100);

    SchematicItemRegistry::registerBuiltInItems();
    SchematicToolRegistryBuiltIn::registerBuiltInTools();
    PCBItemRegistry::registerBuiltInItems();
    PCBToolRegistryBuiltIn::registerBuiltInTools();

    splash->setStatus("Loading Footprint Libraries...");
    splash->setProgress(20, 100);
    FootprintLibraryManager::instance();

    splash->setStatus("Indexing SPICE Models & Subcircuits...");
    splash->setProgress(50, 100);
    ModelLibraryManager::instance();

    splash->setStatus("Starting UI Services...");
    splash->setProgress(85, 100);
    if (ConfigManager::instance().isFeatureEnabled("ui_command_server", true)) {
        int port = ConfigManager::instance().toolProperty("Connectivity", "Port", 18790).toInt();
        UICommandServer::instance().start(port);
    }
    logMilestone("UI Command Server Ready");

    splash->setStatus("Restoring Workspace Session...");
    splash->setProgress(95, 100);





    QMetaObject::invokeMethod(qApp, [splash, fileToOpen, openVioraIde, projectPath, extensionPath]() {
        if (openVioraIde) {
            auto* ide = new IDE::VioraIdeWindow();
            ide->setAttribute(Qt::WA_DeleteOnClose);
            if (!extensionPath.isEmpty()) {
                ide->openVioraDirectory(extensionPath);
            } else if (!projectPath.isEmpty()) {
                ide->openVioraDirectory(projectPath);
            } else if (!fileToOpen.isEmpty()) {
                ide->openFile(fileToOpen);
            }
            ide->show();
            ThemeManager::applyTitlebarTheme(ide, true);
        } else if (!fileToOpen.isEmpty()) {
            if (fileToOpen.endsWith(".flux", Qt::CaseInsensitive)) {
                // Open .flux files in VioraIDE
                auto* ide = new IDE::VioraIdeWindow();
                ide->setAttribute(Qt::WA_DeleteOnClose);
                ide->openFile(fileToOpen);
                ide->show();
                ThemeManager::applyTitlebarTheme(ide, true);
            } else if (fileToOpen.endsWith(".pcb", Qt::CaseInsensitive) || 
                       fileToOpen.endsWith(".flxpcb", Qt::CaseInsensitive) ||
                       fileToOpen.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
                // Open .pcb / .flxpcb / .kicad_pcb files in PCB Editor MainWindow
                MainWindow* pcb = new MainWindow();
                pcb->setAttribute(Qt::WA_DeleteOnClose);
                pcb->openFile(fileToOpen);
                pcb->show();
                ThemeManager::applyTitlebarTheme(pcb, true);
            } else {
                SchematicEditor* sch = new SchematicEditor();
                sch->setAttribute(Qt::WA_DeleteOnClose);
                sch->openFile(fileToOpen);
                sch->show();
                ThemeManager::applyTitlebarTheme(sch, true);
            }
        } else {
            ProjectManager* pm = new ProjectManager;
            pm->setAttribute(Qt::WA_DeleteOnClose);
            pm->show();
            ThemeManager::applyTitlebarTheme(pm, true);

            // Process events so ProjectManager window paints its dark UI immediately
            QApplication::processEvents();

            // Defer secondary editor restoration to next event cycle to prevent main event loop starvation
            QTimer::singleShot(20, pm, [pm]() {
                // Restore previously open schematic editor (sets project context too)
                pm->restoreSchematicEditorWindow();

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
                    ThemeManager::applyTitlebarTheme(pcb, true);
                }
            });
        }
        logMilestone("Main Window Visible & Session Restored");
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

    // Graceful Ctrl+C handling
#ifndef _WIN32
    // POSIX: pipe signal to event loop via QSocketNotifier
    if (pipe(sigFd) == 0) {
        auto *sn = new QSocketNotifier(sigFd[0], QSocketNotifier::Read, &a);
        QObject::connect(sn, &QSocketNotifier::activated, &a, [&a]() {
            char tmp;
            read(sigFd[0], &tmp, sizeof(tmp));
            a.quit();
        });
        signal(SIGINT, sigIntHandler);
    }
#else
    // Windows: use SetConsoleCtrlHandler
    g_app = &a;
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#endif

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
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    SchematicToolRegistryBuiltIn::cleanup();
    shutdownEmbeddedPython();
    return exitCode;
}
