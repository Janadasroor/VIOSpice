/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_command_server.h"
#include "screenshot_manager.h"
#include "gui_manager.h"

#include <QDebug>
#include <QAction>
#include <QMenuBar>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonObject>

UICommandServer::UICommandServer(QObject* parent)
    : QObject(parent)
{
}

UICommandServer::~UICommandServer() {
    stop();
}

UICommandServer& UICommandServer::instance() {
    static UICommandServer s_instance;
    return s_instance;
}

bool UICommandServer::start(int port) {
#if VIOSPICE_HAS_QT_WEBSOCKETS
    if (m_server && m_server->isListening()) {
        return true;
    }

    m_server = new QWebSocketServer(QStringLiteral("VioSpice UI Command Server"),
                                     QWebSocketServer::NonSecureMode, this);

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "UICommandServer: Failed to listen on port" << port
                   << "-" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_port = port;
    connect(m_server, &QWebSocketServer::newConnection, this, &UICommandServer::onNewConnection);

    qDebug() << "UICommandServer listening on port" << port;
    return true;
#else
    if (m_tcpServer && m_tcpServer->isListening()) {
        return true;
    }

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "UICommandServer: TCP Fallback failed to listen on port" << port;
        delete m_tcpServer;
        m_tcpServer = nullptr;
        return false;
    }

    m_port = port;
    connect(m_tcpServer, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        if (!socket) return;
        m_tcpClients.append(socket);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray data = socket->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull()) {
                QVariantMap response = handleCommand(doc.toVariant().toMap());
                socket->write(QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact));
                socket->flush();
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_tcpClients.removeAll(socket);
            socket->deleteLater();
        });
    });

    qDebug() << "UICommandServer: TCP Fallback listening on port" << port;
    return true;
#endif
}

void UICommandServer::stop() {
#if VIOSPICE_HAS_QT_WEBSOCKETS
    if (m_server) {
        // Disconnect all clients
        for (QWebSocket* client : m_clients) {
            client->close();
            client->deleteLater();
        }
        m_clients.clear();

        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
#endif
    if (m_tcpServer) {
        for (QTcpSocket* socket : m_tcpClients) {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
        m_tcpClients.clear();
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }
    qDebug() << "UICommandServer stopped";
}

bool UICommandServer::isRunning() const {
#if VIOSPICE_HAS_QT_WEBSOCKETS
    if (m_server && m_server->isListening()) return true;
#endif
    return m_tcpServer && m_tcpServer->isListening();
}

void UICommandServer::registerCommand(const QString& cmd, CommandHandler handler) {
    m_commandHandlers[cmd] = std::move(handler);
}

void UICommandServer::broadcastToClients(const QVariantMap& message) {
#if VIOSPICE_HAS_QT_WEBSOCKETS
    QJsonDocument doc = QJsonDocument::fromVariant(message);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (QWebSocket* client : m_clients) {
        client->sendTextMessage(QString::fromUtf8(data));
    }
#else
    Q_UNUSED(message);
#endif
}

#if VIOSPICE_HAS_QT_WEBSOCKETS
void UICommandServer::onNewConnection() {
    QWebSocket* socket = m_server->nextPendingConnection();
    if (!socket) return;

    connect(socket, &QWebSocket::textMessageReceived, this, &UICommandServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &UICommandServer::onDisconnected);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.append(socket);
    }

    qDebug() << "UICommandServer: Python client connected:" << socket->peerName();
}

void UICommandServer::onTextMessageReceived(const QString& message) {
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QVariantMap response;
        response["ok"] = false;
        response["error"] = QStringLiteral("Invalid JSON: ") + error.errorString();
        sendResponse(client, response);
        return;
    }

    QVariantMap request = doc.toVariant().toMap();
    QVariantMap response = handleCommand(request);
    sendResponse(client, response);
}

void UICommandServer::onDisconnected() {
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.removeAll(client);
    }

    qDebug() << "UICommandServer: Python client disconnected";
    client->deleteLater();
}
#else
void UICommandServer::onNewConnection() {}
void UICommandServer::onTextMessageReceived(const QString&) {}
void UICommandServer::onDisconnected() {}
#endif

QVariantMap UICommandServer::handleCommand(const QVariantMap& request) {
    QVariantMap response;
    response["ok"] = false;

    // Echo the request ID back so the client can match the response
    if (request.contains("id")) {
        response["id"] = request.value("id");
    }

    QString cmd = request.value("cmd").toString();
    if (cmd.isEmpty()) {
        response["error"] = "Missing 'cmd' field";
        return response;
    }

    QVariantMap params = request.value("params").toMap();

    // Check registered custom handlers first
    if (m_commandHandlers.contains(cmd)) {
        try {
            return m_commandHandlers[cmd](params);
        } catch (const std::exception& e) {
            response["error"] = QString("Handler error: %1").arg(e.what());
            return response;
        }
    }

    // Built-in commands
    if (cmd == "show_message") {
        QString title = params.value("title", "VioSpice").toString();
        QString text = params.value("text", "").toString();
        QString type = params.value("type", "info").toString();

        if (m_showMessageFn) {
            m_showMessageFn(title, text);
        } else {
            // Fallback: print to debug
            qInfo() << "UICommandServer show_message:" << title << "-" << text;
        }
        response["ok"] = true;
    }
    else if (cmd == "add_menu_item") {
        QString menu = params.value("menu", "Tools").toString();
        QString label = params.value("label", "Menu Item").toString();
        QString id = params.value("id", QString("%1_%2").arg(menu).arg(label)).toString();
        QString code = params.value("code", "").toString();

        MenuItem item;
        item.menu = menu;
        item.label = label;
        item.pythonCode = code;

        if (m_addMenuItemFn) {
            item.action = m_addMenuItemFn(menu, label, [this, id]() {
                onMenuItemTriggered(id);
            });
        }

        m_menuItems[id] = item;
        response["ok"] = true;
        response["id"] = id;
    }
    else if (cmd == "remove_menu_item") {
        QString id = params.value("id").toString();
        if (m_menuItems.contains(id)) {
            MenuItem item = m_menuItems.take(id);
            if (m_removeMenuItemFn) {
                m_removeMenuItemFn(item.menu, item.label);
            }
            if (item.action) {
                item.action->deleteLater();
            }
        }
        response["ok"] = true;
    }
    else if (cmd == "run_python_code") {
        QString code = params.value("code").toString();
        if (m_runPythonCodeFn) {
            response = m_runPythonCodeFn(code);
        } else {
            response["ok"] = false;
            response["error"] = "Python code execution not available";
        }
    }
    else if (cmd == "open_schematic") {
        QString path = params.value("path").toString();
        if (m_openSchematicFn) {
            response["ok"] = m_openSchematicFn(path);
        } else {
            response["ok"] = false;
            response["error"] = "Open schematic not available";
        }
    }
    else if (cmd == "open_project") {
        QString path = params.value("path").toString();
        if (m_openProjectFn) {
            response["ok"] = m_openProjectFn(path);
        } else {
            response["ok"] = false;
            response["error"] = "Open project not available";
        }
    }
    else if (cmd == "load_simulation_results") {
        QString path = params.value("path").toString();
        if (m_loadSimResultsFn) {
            m_loadSimResultsFn(path);
            response["ok"] = true;
        } else {
            response["ok"] = false;
            response["error"] = "Load simulation results not available";
        }
    }
    else if (cmd == "get_schematic_context") {
        if (m_getSchematicContextFn) {
            response = m_getSchematicContextFn();
            response["ok"] = true;
        } else {
            response["ok"] = true;
            response["has_schematic"] = false;
            response["message"] = "No schematic context available";
        }
    }
    else if (cmd == "screenshot_list") {
        bool includeHidden = params.value("include_hidden", false).toBool();
        QList<ScreenshotManager::WindowInfo> windows = ScreenshotManager::instance().listWindows(includeHidden);
        QJsonArray windowArray;
        for (const auto& w : windows) {
            QJsonObject obj;
            obj["class"] = w.className;
            obj["title"] = w.windowTitle;
            obj["objectName"] = w.objectName;
            obj["index"] = w.index;
            obj["visible"] = w.isVisible;
            if (!w.childWidgets.isEmpty()) {
                QJsonArray children;
                for (const auto& c : w.childWidgets)
                    children.append(c);
                obj["children"] = children;
            }
            windowArray.append(obj);
        }
        response["ok"] = true;
        response["windows"] = windowArray;
    }
    else if (cmd == "screenshot_capture") {
        QString name = params.value("name", "").toString();
        QString output = params.value("output", "").toString();
        bool clipboard = params.value("clipboard", true).toBool();
        qreal scale = params.value("scale", 1.0).toDouble();
        QString format = params.value("format", "PNG").toString();
        bool includeHidden = params.value("include_hidden", false).toBool();
        QRect region;
        if (params.contains("region")) {
            QVariantList r = params.value("region").toList();
            if (r.size() == 4)
                region = QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
        }

        CaptureOptions opts;
        opts.format = format;
        opts.scale = scale;
        opts.clipboard = clipboard;
        opts.region = region;
        opts.includeHidden = includeHidden;

        QPixmap pixmap = ScreenshotManager::instance().captureWindow(name, opts);
        if (pixmap.isNull()) {
            response["error"] = QString("Window not found: %1").arg(name);
        } else {
            if (output.isEmpty())
                output = ScreenshotManager::generateFileName(name, format);
            ScreenshotManager::instance().saveToFile(pixmap, output, format);
            response["path"] = output;
            if (clipboard) {
                ScreenshotManager::instance().copyToClipboard(pixmap);
            }
            response["ok"] = true;
            response["clipboard"] = clipboard;
            response["width"] = pixmap.width();
            response["height"] = pixmap.height();
            response["format"] = format;
        }
    }
    else if (cmd == "screenshot_all") {
        QString outputDir = params.value("output_dir", "").toString();
        bool clipboard = params.value("clipboard", false).toBool();
        qreal scale = params.value("scale", 1.0).toDouble();
        QString format = params.value("format", "PNG").toString();
        bool includeHidden = params.value("include_hidden", false).toBool();

        CaptureOptions opts;
        opts.format = format;
        opts.scale = scale;
        opts.clipboard = clipboard;
        opts.includeHidden = includeHidden;

        auto results = ScreenshotManager::instance().captureAll(opts);
        QJsonArray filesArray;

        for (const auto& pair : results) {
            const auto& info = pair.first;
            const auto& pixmap = pair.second;

            if (!outputDir.isEmpty()) {
                QString fileName = ScreenshotManager::generateFileName(info.className, format);
                QString fullPath = outputDir + "/" + fileName;
                ScreenshotManager::instance().saveToFile(pixmap, fullPath, format);
                filesArray.append(fullPath);
            }
            if (clipboard) {
                ScreenshotManager::instance().copyToClipboard(pixmap);
            }
        }

        response["ok"] = true;
        response["count"] = results.size();
        response["files"] = filesArray;
    }
    else if (cmd == "screenshot_children") {
        QString parent = params.value("parent", "").toString();
        QStringList children = ScreenshotManager::instance().listChildren(parent);
        QJsonArray childrenArray;
        for (const auto& c : children)
            childrenArray.append(c);
        response["ok"] = true;
        response["children"] = childrenArray;
    }
    else if (cmd == "gui_list_elements") {
        QString window = params.value("window", "").toString();
        QString filterType = params.value("type", "").toString();
        QString filterParent = params.value("parent", "").toString();
        QVariantList elements = GuiManager::instance().listElements(window, filterType, filterParent);
        response["ok"] = true;
        response["elements"] = elements;
    }
    else if (cmd == "gui_click") {
        QString window = params.value("window", "").toString();
        QString target = params.value("target", "").toString();
        response = GuiManager::instance().clickButton(window, target);
    }
    else if (cmd == "gui_type") {
        QString window = params.value("window", "").toString();
        QString target = params.value("target", "").toString();
        QString text = params.value("text", "").toString();
        bool append = params.value("append", false).toBool();
        response = GuiManager::instance().typeInField(window, target, text, append);
    }
    else if (cmd == "gui_menu") {
        QString window = params.value("window", "").toString();
        QString action = params.value("action", "").toString();
        response = GuiManager::instance().triggerMenuAction(window, action);
    }
    else if (cmd == "gui_press_key") {
        QString window = params.value("window", "").toString();
        QString key = params.value("key", "").toString();
        response = GuiManager::instance().pressKey(window, key);
    }
    else if (cmd == "gui_switch_tab") {
        QString window = params.value("window", "").toString();
        QString tab = params.value("tab", "").toString();
        response = GuiManager::instance().switchTab(window, tab);
    }
    else if (cmd == "gui_get_text") {
        QString window = params.value("window", "").toString();
        QString widget = params.value("widget", "").toString();
        response = GuiManager::instance().getText(window, widget);
    }
    else if (cmd == "gui_drag") {
        QString window = params.value("window", "").toString();
        int x1 = params.value("x1", 0).toInt();
        int y1 = params.value("y1", 0).toInt();
        int x2 = params.value("x2", 0).toInt();
        int y2 = params.value("y2", 0).toInt();
        int delay = params.value("delay", 100).toInt();
        response = GuiManager::instance().drag(window, x1, y1, x2, y2, delay);
    }
    else if (cmd == "gui_scroll") {
        QString window = params.value("window", "").toString();
        int x = params.value("x", 0).toInt();
        int y = params.value("y", 0).toInt();
        int deltaY = params.value("deltaY", 0).toInt();
        int deltaX = params.value("deltaX", 0).toInt();
        response = GuiManager::instance().scroll(window, x, y, deltaY, deltaX);
    }
    else if (cmd == "gui_click_at") {
        QString window = params.value("window", "").toString();
        int x = params.value("x", 0).toInt();
        int y = params.value("y", 0).toInt();
        QString button = params.value("button", "left").toString();
        response = GuiManager::instance().clickAt(window, x, y, button);
    }
    else if (cmd == "ping") {
        response["ok"] = true;
        response["pong"] = true;
        response["server"] = "VioSpice UI Command Server";
    }
    else {
        response["error"] = QString("Unknown command: %1").arg(cmd);
    }

    return response;
}

#if VIOSPICE_HAS_QT_WEBSOCKETS
void UICommandServer::sendResponse(QWebSocket* client, const QVariantMap& response) {
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;

    QJsonDocument doc = QJsonDocument::fromVariant(response);
    client->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}
#endif

void UICommandServer::onMenuItemTriggered(const QString& id) {
    MenuItem item;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_menuItems.contains(id)) return;
        item = m_menuItems[id];
    }

    // Broadcast to all Python clients
    QVariantMap broadcast;
    broadcast["type"] = "menu_item_triggered";
    broadcast["id"] = id;
    broadcast["label"] = item.label;
    broadcast["code"] = item.pythonCode;
    broadcastToClients(broadcast);

    // Emit signal for C++ listeners
    emit menuItemTriggered(id);
}
