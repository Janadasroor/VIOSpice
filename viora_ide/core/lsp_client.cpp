/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lsp_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QDebug>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

namespace IDE {

// ============================================================================
// Construction / Destruction
// ============================================================================

LspClient::LspClient(QObject* parent)
    : QObject(parent)
{
}

LspClient::~LspClient() {
    stopServer();
}

// ============================================================================
// Server lifecycle
// ============================================================================

bool LspClient::startServer(const QString& serverPath) {
    if (m_process && m_process->state() == QProcess::Running) return true;

    // Use the build-dir binary which has the updated server
    QString server = serverPath.isEmpty()
        ? QString("/home/jnd/qt_projects/fluxscript/build/flux-lsp")
        : serverPath;

    m_process = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("FLUX_HOME", QDir::homePath() + "/.flux");
    env.insert("LD_LIBRARY_PATH", QDir::homePath() + "/qt_projects/fluxscript/build");
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &LspClient::onReadReady);

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        QByteArray err = m_process->readAllStandardError();
        qWarning() << "LspClient Server Stderr:" << err;
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning() << "LspClient Server Error occurred:" << error << m_process->errorString();
        emit errorOccurred(m_process->errorString());
    });

    m_process->start(server, QStringList() << "--stdio");
    if (!m_process->waitForStarted(2000)) {
        qWarning() << "LspClient: failed to start process" << server;
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }

    qInfo() << "LspClient: Started" << server << "PID" << m_process->processId();

    sendInitialize();
    return true;
}

void LspClient::onReadReady() {
    if (!m_process) return;

    QByteArray data = m_process->readAllStandardOutput();
    m_readBuffer.append(data);

    // Process complete messages
    while (true) {
        int sep = m_readBuffer.indexOf("\r\n\r\n");
        if (sep < 0) break;

        QByteArray header = m_readBuffer.left(sep);
        int contentLength = -1;
        for (const QByteArray& line : header.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("Content-Length:")) {
                contentLength = trimmed.mid(15).trimmed().toInt();
            }
        }

        if (contentLength <= 0 || contentLength > 10 * 1024 * 1024) {
            m_readBuffer.clear();
            break;
        }

        int bodyStart = sep + 4;
        if (m_readBuffer.size() < bodyStart + contentLength) break;

        QByteArray body = m_readBuffer.mid(bodyStart, contentLength);
        m_readBuffer.remove(0, bodyStart + contentLength);
        processMessage(body);
    }
}

void LspClient::stopServer() {
    if (m_pollTimer) m_pollTimer->stop();

    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000)) {
            m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_initialized = false;
    m_readBuffer.clear();
    m_pendingRequests.clear();
}

bool LspClient::isRunning() const {
    return m_process && m_process->state() == QProcess::Running;
}

// ============================================================================
// JSON-RPC Transport
// ============================================================================

void LspClient::sendMessage(int id, const QString& method, const QString& params) {
    if (!m_process || m_process->state() != QProcess::Running) return;

    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    if (id > 0) msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        msg["params"] = doc.object();
    }

    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray frame = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;

    qInfo() << "LspClient SEND:" << method << "id=" << id << "body=" << body;
    m_process->write(frame);
}

void LspClient::sendNotification(const QString& method, const QString& params) {
    sendMessage(0, method, params);
}

// ============================================================================
// Message Processing
// ============================================================================

void LspClient::processMessage(const QByteArray& json) {
    qInfo() << "LspClient RECV:" << json;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "LspClient: JSON parse error:" << parseError.errorString();
        return;
    }

    QJsonObject msg = doc.object();

    // Response to a request
    if (msg.contains("id") && (msg.contains("result") || msg.contains("error"))) {
        int id = msg["id"].toInt();
        if (msg.contains("error")) {
            processError(id, msg["error"].toObject());
        } else {
            processResponse(id, msg["result"]);
        }
        return;
    }

    // Server-initiated notification
    if (msg.contains("method") && !msg.contains("id")) {
        processNotification(msg["method"].toString(), msg["params"].toObject());
    }
}

void LspClient::processNotification(const QString& method, const QJsonObject& params) {
    if (method == "textDocument/publishDiagnostics") {
        QString uri = params["uri"].toString();
        QString filePath = uriToFilePath(uri);
        QList<LspDiagnostic> diagnostics;

        QJsonArray diagArray = params["diagnostics"].toArray();
        for (const QJsonValue& v : diagArray) {
            QJsonObject d = v.toObject();
            LspDiagnostic diag;
            QJsonObject range = d["range"].toObject();
            diag.range.start.line = range["start"].toObject()["line"].toInt();
            diag.range.start.character = range["start"].toObject()["character"].toInt();
            diag.range.end.line = range["end"].toObject()["line"].toInt();
            diag.range.end.character = range["end"].toObject()["character"].toInt();
            diag.severity = d["severity"].toInt(1);
            diag.message = d["message"].toString();
            diag.source = d["source"].toString();
            diagnostics.append(diag);
        }

        m_diagnostics[filePath] = diagnostics;
        emit diagnosticsReceived(filePath, diagnostics);
    }
}

void LspClient::processResponse(int id, const QJsonValue& result) {
    auto it = m_pendingRequests.find(id);
    if (it == m_pendingRequests.end()) return;

    PendingRequest req = it.value();
    m_pendingRequests.erase(it);

    if (req.callback) {
        req.callback(result);
    }
}

void LspClient::processError(int id, const QJsonObject& error) {
    qWarning() << "LspClient: Request error id=" << id
               << "code=" << error["code"].toInt()
               << "message=" << error["message"].toString();
    m_pendingRequests.remove(id);
    emit errorOccurred(error["message"].toString());
}

// ============================================================================
// Document Lifecycle
// ============================================================================

void LspClient::openDocument(const QString& filePath, const QString& text) {
    QString uri = filePathToUri(filePath);
    DocumentState state;
    state.version = 1;
    state.text = text;
    m_documents[uri] = state;

    if (!isRunning() || !m_initialized) return;

    QJsonObject textDoc;
    textDoc["uri"] = uri;
    textDoc["languageId"] = filePath.endsWith(".json") ? "json" : "flux";
    textDoc["version"] = 1;
    textDoc["text"] = text;

    QJsonObject params;
    params["textDocument"] = textDoc;

    sendNotification("textDocument/didOpen", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::changeDocument(const QString& filePath, const QString& text, int version) {
    QString uri = filePathToUri(filePath);
    m_documents[uri].version = version;
    m_documents[uri].text = text;

    if (!isRunning() || !m_initialized) return;

    QJsonObject textDoc;
    textDoc["uri"] = uri;
    textDoc["version"] = version;

    QJsonObject change;
    change["text"] = text;

    QJsonArray changes;
    changes.append(change);

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["contentChanges"] = changes;

    sendNotification("textDocument/didChange", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::closeDocument(const QString& filePath) {
    QString uri = filePathToUri(filePath);
    m_documents.remove(uri);
    m_diagnostics.remove(filePath);

    if (!isRunning() || !m_initialized) return;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;

    sendNotification("textDocument/didClose", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::saveDocument(const QString& filePath) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;

    sendNotification("textDocument/didSave", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

// ============================================================================
// Feature Requests
// ============================================================================

void LspClient::requestCompletions(const QString& filePath, int line, int character) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["position"] = position;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/completion", filePath,
        [this, id, filePath, line, character](const QJsonValue& result) {
            QList<LspCompletionItem> items;

            if (result.isObject()) {
                QJsonObject obj = result.toObject();
                QJsonArray arr = obj["items"].toArray();
                for (const QJsonValue& v : arr) {
                    QJsonObject item = v.toObject();
                    LspCompletionItem ci;
                    ci.label = item["label"].toString();
                    ci.kind = item["kind"].toInt(1);
                    ci.detail = item["detail"].toString();
                    ci.documentation = item["documentation"].isString()
                        ? item["documentation"].toString()
                        : item["documentation"].toObject()["value"].toString();
                    ci.insertText = item["insertText"].toString();
                    ci.insertTextFormat = item["insertTextFormat"].toInt(1);
                    items.append(ci);
                }
            } else if (result.isArray()) {
                QJsonArray arr = result.toArray();
                for (const QJsonValue& v : arr) {
                    QJsonObject item = v.toObject();
                    LspCompletionItem ci;
                    ci.label = item["label"].toString();
                    ci.kind = item["kind"].toInt(1);
                    ci.detail = item["detail"].toString();
                    ci.insertText = item["insertText"].toString();
                    items.append(ci);
                }
            }

            emit completionsReady(id, items);
        }
    };

    sendMessage(id, "textDocument/completion", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestHover(const QString& filePath, int line, int character) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["position"] = position;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/hover", filePath,
        [this, filePath, line, character](const QJsonValue& result) {
            QString contents;
            if (result.isObject()) {
                QJsonObject obj = result.toObject();
                QJsonValue c = obj["contents"];
                if (c.isString()) {
                    contents = c.toString();
                } else if (c.isObject()) {
                    contents = c.toObject()["value"].toString();
                } else if (c.isArray()) {
                    for (const QJsonValue& v : c.toArray()) {
                        if (v.isString()) contents += v.toString();
                        else if (v.isObject()) contents += v.toObject()["value"].toString();
                        contents += "\n";
                    }
                }
            }
            emit hoverReady(contents.trimmed(), filePath, line, character);
        }
    };

    sendMessage(id, "textDocument/hover", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestDefinition(const QString& filePath, int line, int character) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["position"] = position;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/definition", filePath,
        [this, filePath, line, character](const QJsonValue& result) {
            // Emit the first definition location
            if (result.isObject()) {
                QJsonObject loc = result.toObject();
                QString targetUri = loc["uri"].toString();
                QJsonObject range = loc["range"].toObject();
                int targetLine = range["start"].toObject()["line"].toInt();
                int targetChar = range["start"].toObject()["character"].toInt();
                emit definitionReady(uriToFilePath(targetUri), targetLine, targetChar);
            } else if (result.isArray()) {
                QJsonArray arr = result.toArray();
                if (!arr.isEmpty()) {
                    QJsonObject loc = arr[0].toObject();
                    QString targetUri = loc["uri"].toString();
                    QJsonObject range = loc["range"].toObject();
                    int targetLine = range["start"].toObject()["line"].toInt();
                    int targetChar = range["start"].toObject()["character"].toInt();
                    emit definitionReady(uriToFilePath(targetUri), targetLine, targetChar);
                }
            }
        }
    };

    sendMessage(id, "textDocument/definition", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestReferences(const QString& filePath, int line, int character) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["position"] = position;
    params["context"] = QJsonObject{{"includeDeclaration", true}};

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/references", filePath,
        [this](const QJsonValue& result) {
            QList<LspLocation> locations;
            if (result.isArray()) {
                for (const QJsonValue& v : result.toArray()) {
                    QJsonObject loc = v.toObject();
                    LspLocation l;
                    l.uri = loc["uri"].toString();
                    l.range.start.line = loc["range"].toObject()["start"].toObject()["line"].toInt();
                    l.range.start.character = loc["range"].toObject()["start"].toObject()["character"].toInt();
                    l.range.end.line = loc["range"].toObject()["end"].toObject()["line"].toInt();
                    l.range.end.character = loc["range"].toObject()["end"].toObject()["character"].toInt();
                    locations.append(l);
                }
            }
            emit referencesReady(locations);
        }
    };

    sendMessage(id, "textDocument/references", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestDocumentSymbol(const QString& filePath) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/documentSymbol", filePath,
        [this](const QJsonValue& result) {
            QList<LspSymbol> symbols;
            if (result.isArray()) {
                for (const QJsonValue& v : result.toArray()) {
                    QJsonObject s = v.toObject();
                    LspSymbol sym;
                    sym.name = s["name"].toString();
                    sym.kind = s["kind"].toInt();
                    sym.range.start.line = s["range"].toObject()["start"].toObject()["line"].toInt();
                    sym.range.start.character = s["range"].toObject()["start"].toObject()["character"].toInt();
                    sym.range.end.line = s["range"].toObject()["end"].toObject()["line"].toInt();
                    sym.range.end.character = s["range"].toObject()["end"].toObject()["character"].toInt();
                    symbols.append(sym);
                }
            }
            emit documentSymbolReady(symbols);
        }
    };

    sendMessage(id, "textDocument/documentSymbol", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestSignatureHelp(const QString& filePath, int line, int character) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["position"] = position;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/signatureHelp", filePath,
        [this](const QJsonValue& result) {
            QList<LspSignature> signatures;
            if (result.isObject()) {
                QJsonArray sigArr = result.toObject()["signatures"].toArray();
                for (const QJsonValue& v : sigArr) {
                    QJsonObject s = v.toObject();
                    LspSignature sig;
                    sig.label = s["label"].toString();
                    sig.documentation = s["documentation"].isString()
                        ? s["documentation"].toString()
                        : s["documentation"].toObject()["value"].toString();
                    QJsonArray params = s["parameters"].toArray();
                    for (const QJsonValue& p : params) {
                        sig.parameters.append(p.toObject()["label"].isString()
                            ? p.toObject()["label"].toString()
                            : p.toObject()["label"].toArray()[0].toString());
                    }
                    signatures.append(sig);
                }
            }
            emit signatureHelpReady(signatures);
        }
    };

    sendMessage(id, "textDocument/signatureHelp", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::requestFormatting(const QString& filePath) {
    if (!isRunning() || !m_initialized) return;

    QString uri = filePathToUri(filePath);

    QJsonObject textDoc;
    textDoc["uri"] = uri;

    QJsonObject options;
    options["tabSize"] = 4;
    options["insertSpaces"] = true;

    QJsonObject params;
    params["textDocument"] = textDoc;
    params["options"] = options;

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"textDocument/formatting", filePath,
        [this, filePath](const QJsonValue& result) {
            if (result.isArray() && !result.toArray().isEmpty()) {
                // Apply the first text edit — full document replacement
                QJsonObject edit = result.toArray()[0].toObject();
                // For simplicity, emit the whole new text if it's a replace-all edit
                if (edit["range"].toObject()["start"].toObject()["line"].toInt() == 0 &&
                    edit["range"].toObject()["start"].toObject()["character"].toInt() == 0) {
                    // It's a full document format — just signal
                    emit formattingReady(edit["newText"].toString());
                }
            }
        }
    };

    sendMessage(id, "textDocument/formatting", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

// ============================================================================
// Diagnostics Cache
// ============================================================================

QList<LspDiagnostic> LspClient::diagnosticsForFile(const QString& filePath) const {
    return m_diagnostics.value(filePath);
}

// ============================================================================
// Protocol Handshake
// ============================================================================

void LspClient::sendInitialize() {
    QJsonObject processClient;
    processClient["name"] = "VioraIDE";
    processClient["version"] = "1.0.0";

    QJsonObject textDocument;
    QJsonObject completionItem;
    completionItem["snippetSupport"] = true;
    completionItem["documentationFormat"] = QJsonArray{"markdown", "plaintext"};

    QJsonObject completion;
    completion["completionItem"] = completionItem;

    QJsonObject hover;
    hover["contentFormat"] = QJsonArray{"markdown", "plaintext"};

    QJsonObject synchronization;
    synchronization["didSave"] = true;
    synchronization["dynamicRegistration"] = false;

    QJsonObject formatting;
    formatting["dynamicRegistration"] = false;

    QJsonObject def;
    def["dynamicRegistration"] = false;

    QJsonObject references;
    references["dynamicRegistration"] = false;

    QJsonObject docSymbol;
    docSymbol["dynamicRegistration"] = false;

    QJsonObject sigHelp;
    sigHelp["dynamicRegistration"] = false;

    textDocument["completion"] = completion;
    textDocument["hover"] = hover;
    textDocument["synchronization"] = synchronization;
    textDocument["formatting"] = formatting;
    textDocument["definition"] = def;
    textDocument["references"] = references;
    textDocument["documentSymbol"] = docSymbol;
    textDocument["signatureHelp"] = sigHelp;

    QJsonObject capabilities;
    capabilities["textDocument"] = textDocument;

    QJsonObject params;
    params["processId"] = QCoreApplication::applicationPid();
    params["clientInfo"] = processClient;
    params["capabilities"] = capabilities;
    params["rootUri"] = QDir::currentPath().startsWith("/")
        ? "file://" + QDir::currentPath()
        : "file:///" + QDir::currentPath();

    int id = m_nextRequestId++;
    m_pendingRequests[id] = {"initialize", QString(),
        [this](const QJsonValue& result) {
            Q_UNUSED(result);
            m_initialized = true;
            sendInitialized();

            // Notify the LSP server about all documents that were opened prior to initialization completing
            for (auto it = m_documents.begin(); it != m_documents.end(); ++it) {
                QString uri = it.key();
                QString filePath = uriToFilePath(uri);
                DocumentState state = it.value();

                QJsonObject textDoc;
                textDoc["uri"] = uri;
                textDoc["languageId"] = filePath.endsWith(".json") ? "json" : "flux";
                textDoc["version"] = state.version;
                textDoc["text"] = state.text;

                QJsonObject params;
                params["textDocument"] = textDoc;

                sendNotification("textDocument/didOpen", QJsonDocument(params).toJson(QJsonDocument::Compact));
            }

            emit serverStarted();
        }
    };

    sendMessage(id, "initialize", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

void LspClient::sendInitialized() {
    QJsonObject params;
    sendNotification("initialized", QJsonDocument(params).toJson(QJsonDocument::Compact));
}

// ============================================================================
// Helpers
// ============================================================================

QString LspClient::filePathToUri(const QString& filePath) const {
    if (filePath.startsWith("/")) {
        return "file://" + filePath;
    }
    return "file:///" + filePath;
}

QString LspClient::uriToFilePath(const QString& uri) const {
    if (uri.startsWith("file://")) {
        QString path = uri.mid(7);
        // On Windows, strip leading /
        if (path.length() >= 3 && path[1] == ':' && path[0] == '/') {
            path = path.mid(1);
        }
        return path;
    }
    return uri;
}

QString LspClient::escapeJsonString(const QString& s) const {
    QString result = s;
    result.replace("\\", "\\\\");
    result.replace("\"", "\\\"");
    result.replace("\n", "\\n");
    result.replace("\r", "\\r");
    result.replace("\t", "\\t");
    return result;
}

} // namespace IDE
