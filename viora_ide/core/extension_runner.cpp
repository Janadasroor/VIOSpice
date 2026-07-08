/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_runner.h"
#include <QRegularExpression>
#include "../../core/flux/engine/flux_script_engine.h"

namespace IDE {

ExtensionRunner::ExtensionRunner(QObject* parent)
    : QObject(parent) {
}

bool ExtensionRunner::runSource(const QString& source) {
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
        eng.callFunction("init", {});
        eng.callFunction("open_panel", {});
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
