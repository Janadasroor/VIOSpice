/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QTimer>
#include <QMutex>
#include <QProcess>

namespace IDE {

// ============================================================================
// LSP Types
// ============================================================================

struct LspPosition {
    int line = 0;
    int character = 0;
};

struct LspRange {
    LspPosition start;
    LspPosition end;
};

struct LspLocation {
    QString uri;
    LspRange range;
};

struct LspDiagnostic {
    LspRange range;
    int severity = 1; // 1=Error, 2=Warning, 3=Info, 4=Hint
    QString message;
    QString source;

    bool isError() const { return severity == 1; }
    bool isWarning() const { return severity == 2; }
};

struct LspCompletionItem {
    QString label;
    int kind = 1;   // CompletionItemKind
    QString detail;
    QString documentation;
    QString insertText;
    int insertTextFormat = 1; // 1=plaintext, 2=snippet
};

struct LspSymbol {
    QString name;
    int kind = 0;
    LspRange range;
    LspRange selectionRange;
};

struct LspSignature {
    QString label;
    QString documentation;
    QList<QString> parameters;
};

// ============================================================================
// LSP Client
// ============================================================================

class LspClient : public QObject {
    Q_OBJECT
public:
    explicit LspClient(QObject* parent = nullptr);
    ~LspClient();

    bool startServer(const QString& serverPath = QString());
    void stopServer();
    bool isRunning() const;

    // Document lifecycle
    void openDocument(const QString& filePath, const QString& text);
    void changeDocument(const QString& filePath, const QString& text, int version);
    void closeDocument(const QString& filePath);
    void saveDocument(const QString& filePath);

    // Async requests
    void requestCompletions(const QString& filePath, int line, int character);
    void requestHover(const QString& filePath, int line, int character);
    void requestDefinition(const QString& filePath, int line, int character);
    void requestReferences(const QString& filePath, int line, int character);
    void requestDocumentSymbol(const QString& filePath);
    void requestSignatureHelp(const QString& filePath, int line, int character);
    void requestFormatting(const QString& filePath);

    // Diagnostics
    QList<LspDiagnostic> diagnosticsForFile(const QString& filePath) const;

signals:
    void serverStarted();
    void serverStopped();
    void diagnosticsReceived(const QString& filePath, const QList<LspDiagnostic>& diagnostics);
    void completionsReady(int requestId, const QList<LspCompletionItem>& items);
    void hoverReady(const QString& contents, const QString& filePath, int line, int col);
    void definitionReady(const QString& filePath, int line, int character);
    void referencesReady(const QList<LspLocation>& locations);
    void documentSymbolReady(const QList<LspSymbol>& symbols);
    void signatureHelpReady(const QList<LspSignature>& signatures);
    void formattingReady(const QString& newText);
    void errorOccurred(const QString& message);

private slots:
    void onReadReady();

private:
    // JSON-RPC transport
    void sendMessage(int id, const QString& method, const QString& params = QString());
    void sendNotification(const QString& method, const QString& params = QString());
    void processMessage(const QByteArray& json);
    void processNotification(const QString& method, const QJsonObject& params);
    void processResponse(int id, const QJsonValue& result);
    void processError(int id, const QJsonObject& error);

    // JSON helpers
    QString filePathToUri(const QString& filePath) const;
    QString uriToFilePath(const QString& uri) const;
    QString escapeJsonString(const QString& s) const;

    // Protocol
    void sendInitialize();
    void sendInitialized();

    QProcess* m_process = nullptr;
    QByteArray m_readBuffer;
    int m_nextRequestId = 1;

    struct PendingRequest {
        QString method;
        QString filePath; // for context
        std::function<void(const QJsonValue&)> callback;
    };
    QMap<int, PendingRequest> m_pendingRequests;

    // Document state
    struct DocumentState {
        int version = 0;
        QString text;
    };
    QMap<QString, DocumentState> m_documents;

    // Diagnostics cache
    QMap<QString, QList<LspDiagnostic>> m_diagnostics;

    bool m_initialized = false;
    QTimer* m_pollTimer = nullptr;
};

} // namespace IDE

#endif // LSP_CLIENT_H
