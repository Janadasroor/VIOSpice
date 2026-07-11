#include "extension_runner.h"
#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QDebug>
#include "../../core/flux/engine/flux_script_engine.h"

namespace IDE {

// Custom message handler to capture qDebug output for the OutputPanel
static ExtensionRunner* s_runnerInstance = nullptr;
static QtMessageHandler s_originalHandler = nullptr;

static void ideMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    // Forward to original handler
    if (s_originalHandler) s_originalHandler(type, context, msg);

    // Capture debug/warning messages to the OutputPanel
    if (s_runnerInstance && (type == QtDebugMsg || type == QtWarningMsg)) {
        QMetaObject::invokeMethod(s_runnerInstance, "outputReceived", Qt::QueuedConnection,
            Q_ARG(QString, msg));
    }
}

ExtensionRunner::ExtensionRunner(QObject* parent)
    : QObject(parent) {
    // Install custom message handler to capture qDebug output
    if (!s_runnerInstance) {
        s_originalHandler = qInstallMessageHandler(ideMessageHandler);
        s_runnerInstance = this;
    }
}

bool ExtensionRunner::runSource(const QString& source, const QString& extensionDir) {
#ifdef HAVE_FLUXSCRIPT
    if (m_running) return false;

    m_running = true;
    runStarted();

    auto& eng = FluxScriptEngine::instance();
    if (!eng.isInitialized()) {
        eng.initialize();
    }

    // Reinitialize to clear old modules
    eng.finalize();
    eng.initialize();

    QString error;
    bool success = eng.executeString(source, &error);

    if (success) {
        outputReceived("Script compiled successfully.");

        // Parse manifest.json to see if a custom activate hook is defined
        QString onActivateHook = "init";
        bool hasCustomHook = false;

        if (!extensionDir.isEmpty()) {
            QString manifestPath = extensionDir + "/manifest.json";
            QFile file(manifestPath);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("hooks") && obj["hooks"].isObject()) {
                        QJsonObject hooks = obj["hooks"].toObject();
                        if (hooks.contains("onActivate") && hooks["onActivate"].isString()) {
                            onActivateHook = hooks["onActivate"].toString();
                            hasCustomHook = true;
                        }
                    }
                }
            }
        }

        if (hasCustomHook) {
            eng.callFunction(onActivateHook.toUtf8().constData(), {});
            // Also try to open the GUI panel if it exists
            eng.callFunction("open_panel", {});
        } else {
            // Try common activation function names
            eng.callFunction("init_ext", {});
            eng.callFunction("init", {});
            eng.callFunction("main", {});
            eng.callFunction("open_panel", {});
        }
        // Process GUI events so popups (flux_qt_msg_box) and windows can display
        QApplication::processEvents();
        QCoreApplication::processEvents();
        outputReceived("Extension activated.");
    } else {
        parseAndHighlightErrors(error);
    }

    m_running = false;
    runFinished(success);
    return success;
#else
    errorReceived("FluxScript engine not available (HAVE_FLUXSCRIPT not defined).");
    runFinished(false);
    return false;
#endif
}

void ExtensionRunner::stop() {
    if (!m_running) return;

#ifdef HAVE_FLUXSCRIPT
    auto& eng = FluxScriptEngine::instance();
    eng.finalize();
#endif

    m_running = false;
    outputReceived("Extension stopped.");
    runFinished(false);
}

void ExtensionRunner::parseAndHighlightErrors(const QString& error) {
    errorReceived(error);

    QRegularExpression lineRe(R"(<flux>:(\d+):(\d+):)");
    QRegularExpressionMatchIterator it = lineRe.globalMatch(error);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int line = match.captured(1).toInt();
        int col = match.captured(2).toInt();
        errorReceived(QString("  -> Line %1, Column %2").arg(line).arg(col));
    }
}

} // namespace IDE
