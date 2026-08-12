/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "misc_commands.h"
#include "common.h"
#include "../command_registry.h"

// Schematic Includes
#include "flux/schematic/analysis/schematic_annotator.h"
#include "flux/schematic/analysis/schematic_erc.h"
#include "schematic/items/schematic_item.h"
#include "factories/schematic_item_registry.h"
#include "factories/schematic_item_factory.h"
#include "items/virtual_terminal_item.h"
#include "items/instrument_probe_item.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "flux/schematic/items/wire_item.h"
#include "flux/schematic/editor/schematic_api.h"

// PCB Includes (optional)
#if __has_include("pcb/drc/pcb_drc.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/drc/pcb_drc.h"
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#include "vioraeda/editor/pcb_api.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif

#include "core/flux/extensions/native/plugin_manager.h"
#include "symbols/models/symbol_definition.h"
#include "symbols/symbol_library.h"
#include "../flux_command.h"

#include <QGraphicsScene>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <QThread>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QVariantMap>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <iostream>
#include <cmath>

int cmdExtensionInit(const QStringList& args);
int cmdExtensionValidate(const QStringList& args);
int cmdExtensionInstall(const QStringList& args);

namespace {

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

static bool sendWebSocketCommand(const QString& host, int port, const QVariantMap& cmd, QVariantMap& response) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(3000)) {
        return false;
    }

    QByteArray payload = QJsonDocument::fromVariant(cmd).toJson(QJsonDocument::Compact);

    // --- WebSocket handshake (RFC 6455) ---
    QByteArray key(16, 0);
    for (int i = 0; i < 16; ++i)
        key[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    QByteArray keyBase64 = key.toBase64();

    QByteArray hostBytes = host.toUtf8();
    QByteArray request;
    request.append("GET / HTTP/1.1\r\n");
    request.append("Host: " + hostBytes + ":" + QByteArray::number(port) + "\r\n");
    request.append("Upgrade: websocket\r\n");
    request.append("Connection: Upgrade\r\n");
    request.append("Sec-WebSocket-Key: " + keyBase64 + "\r\n");
    request.append("Sec-WebSocket-Version: 13\r\n");
    request.append("\r\n");

    socket.write(request);
    socket.waitForBytesWritten(1000);

    if (!socket.waitForReadyRead(5000)) {
        return false;
    }

    QByteArray handshakeResponse = socket.readAll();
    if (!handshakeResponse.contains("101")) {
        return false;
    }

    // --- Build masked WebSocket frame (client must mask) ---
    QByteArray frame;
    frame.append(static_cast<char>(0x81)); // FIN + TEXT opcode

    int len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | len));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
    }

    QByteArray maskKey(4, 0);
    for (int i = 0; i < 4; ++i)
        maskKey[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    frame.append(maskKey);

    QByteArray maskedPayload = payload;
    for (int i = 0; i < maskedPayload.size(); ++i)
        maskedPayload[i] ^= maskKey[i % 4];
    frame.append(maskedPayload);

    socket.write(frame);
    socket.waitForBytesWritten(1000);

    // --- Read and parse response frame ---
    if (!socket.waitForReadyRead(5000)) {
        return false;
    }

    QByteArray rawData = socket.readAll();
    if (rawData.size() < 2) return false;

    int opcode = rawData[0] & 0x0F;
    if (opcode == 0x08) return false; // Connection close

    quint64 payloadLen = rawData[1] & 0x7F;
    int offset = 2;

    if (payloadLen == 126) {
        if (rawData.size() < 4) return false;
        payloadLen = (static_cast<quint8>(rawData[2]) << 8) | static_cast<quint8>(rawData[3]);
        offset = 4;
    } else if (payloadLen == 127) {
        if (rawData.size() < 10) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | static_cast<quint8>(rawData[2 + i]);
        offset = 10;
    }

    if (rawData.size() < offset + payloadLen) return false;
    QByteArray responseData = rawData.mid(offset, payloadLen);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    response = doc.toVariant().toMap();
    return true;
}

static bool sendGuiCommand(const QVariantMap& cmd, QVariantMap& response) {
    return sendWebSocketCommand("127.0.0.1", 18790, cmd, response);
}

class DrcCommand : public CLICommand {
public:
    QString name() const override { return "drc"; }
    QString description() const override { return "Run design rules check (DRC) on a PCB layout."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora drc <file.pcb>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
#if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
#else
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Running DRC on " << filePath.toStdString() << "..." << std::endl;
        PCBDRC drc;
        drc.runFullCheck(&scene);

        if (drc.violations().isEmpty()) {
            if (!g_quiet) std::cout << "DRC Passed! No violations found." << std::endl;
        } else {
            if (!g_quiet) std::cout << "DRC Failed! Found " << drc.violations().size() << " violations:" << std::endl;
            for (const auto& v : drc.violations()) {
                if (!g_quiet) std::cout << "  [" << v.severityString().toStdString() << "] " 
                          << v.typeString().toStdString() << ": "
                          << v.message().toStdString() << " at (" 
                          << v.location().x() << ", " << v.location().y() << ")" << std::endl;
            }
            return drc.errorCount() > 0 ? 1 : 0;
        }
        return 0;
#endif
    }
};

class RenderCommand : public CLICommand {
public:
    QString name() const override { return "render"; }
    QString description() const override { return "Render a PCB layout file (.pcb) to a PNG image."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.pcb", "output.png"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora render <file.pcb> <output.png>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        const QString output = args.at(1);
#if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
#else
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Rendering " << filePath.toStdString() << " to " << output.toStdString() << "..." << std::endl;
        
        QRectF rect = scene.itemsBoundingRect();
        if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
        rect.adjust(-10, -10, 10, 10); // Adding margin

        QImage image(rect.size().toSize() * 4, QImage::Format_ARGB32); // 4x scale for high res
        image.fill(QColor(20, 20, 25)); // Dark board color
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), rect);
        painter.end();

        if (image.save(output)) {
            if (!g_quiet) std::cout << "Successfully rendered scene to " << output.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save image to " << output.toStdString() << std::endl;
            return 1;
        }
        return 0;
#endif
    }
};

class AuditCommand : public CLICommand {
public:
    QString name() const override { return "audit"; }
    QString description() const override { return "Audit project directory for errors and compliance issues."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"project_path"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora audit <file.sch|file.pcb|project_dir>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        if (!g_quiet) std::cout << "Project Doctor (Audit) starting on: " << filePath.toStdString() << "..." << std::endl;
        
        QJsonObject report;
        report["project_path"] = filePath;
        report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        QJsonArray pcbAudits;
        QJsonArray schAudits;

        QFileInfo info(filePath);
        QStringList pcbFiles;
        QStringList schFiles;

        if (info.isDir()) {
            QDir dir(filePath);
            for (const QString& f : dir.entryList({"*.pcb"}, QDir::Files)) pcbFiles << dir.filePath(f);
            for (const QString& f : dir.entryList({"*.sch"}, QDir::Files)) schFiles << dir.filePath(f);
        } else {
            if (filePath.endsWith(".pcb")) pcbFiles << filePath;
            else if (filePath.endsWith(".sch")) schFiles << filePath;
        }

        // Run DRC on PCBs
#if VIOSPICE_HAS_PCB
        for (const QString& pcbFile : pcbFiles) {
            QJsonObject pcbReport;
            pcbReport["file"] = QFileInfo(pcbFile).fileName();
            
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, pcbFile)) {
                PCBDRC drc;
                drc.runFullCheck(&scene);
                
                QJsonArray violations;
                for (const auto& v : drc.violations()) {
                    QJsonObject vio;
                    vio["type"] = v.typeString();
                    vio["severity"] = v.severityString();
                    vio["message"] = v.message();
                    vio["x"] = v.location().x();
                    vio["y"] = v.location().y();
                    violations.append(vio);
                }
                pcbReport["violations"] = violations;
                pcbReport["status"] = drc.errorCount() == 0 ? "Healthy" : "Needs Attention";
            } else {
                pcbReport["status"] = "Error Loading";
            }
            pcbAudits.append(pcbReport);
        }
#endif

        // Run ERC on Schematics
        for (const QString& schFile : schFiles) {
            QJsonObject schReport;
            schReport["file"] = QFileInfo(schFile).fileName();
            
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, schFile, pageSize, dummyTB)) {
                auto violations = SchematicERC::run(&scene, QFileInfo(schFile).absolutePath());
                
                QJsonArray issues;
                for (const auto& v : violations) {
                    QJsonObject issue;
                    issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" : 
                                       (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
                    issue["message"] = v.message;
                    issue["x"] = v.position.x();
                    issue["y"] = v.position.y();
                    issues.append(issue);
                }
                schReport["issues"] = issues;
                schReport["status"] = issues.isEmpty() ? "Healthy" : "Needs Attention";
            } else {
                schReport["status"] = "Error Loading";
            }
            schAudits.append(schReport);
        }

        report["pcb_reports"] = pcbAudits;
        report["schematic_reports"] = schAudits;
        report["overall_status"] = (pcbFiles.size() + schFiles.size() > 0) ? "Audit Complete" : "No files found";

        QJsonDocument doc(sortJsonValue(report).toObject());
        QDir().mkpath("docs/reports");
        QString reportPath = "docs/reports/project_health_report.json";
        QFile file(reportPath);
        if (file.open(QFile::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
            if (!g_quiet) std::cout << "Project health report generated: " << reportPath.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save health report." << std::endl;
        }
        return 0;
    }
};

class AutofixCommand : public CLICommand {
public:
    QString name() const override { return "autofix"; }
    QString description() const override { return "Attempt to automatically resolve common schematic/PCB ERC/DRC violations."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.sch|file.pcb"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora autofix <file.sch|file.pcb>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        if (!g_quiet) std::cout << "Project Autofix starting on: " << filePath.toStdString() << "..." << std::endl;
        
        if (filePath.endsWith(".sch")) {
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
                int fixedCount = 0;
                
                // 1. Run Annotation
                SchematicAnnotator::annotate(&scene, false);
                if (!g_quiet) std::cout << "  - Reference annotation completed." << std::endl;

                // 2. Remove duplicate/redundant wires
                QList<WireItem*> wires;
                for (auto* item : scene.items()) {
                    if (auto* w = dynamic_cast<WireItem*>(item)) wires.append(w);
                }

                for (int i = 0; i < wires.size(); ++i) {
                    for (int j = i + 1; j < wires.size(); ++j) {
                        if (wires[i]->startPoint() == wires[j]->startPoint() && 
                            wires[i]->endPoint() == wires[j]->endPoint()) {
                            scene.removeItem(wires[j]);
                            delete wires[j];
                            wires.removeAt(j);
                            j--;
                            fixedCount++;
                        }
                    }
                }
                
                if (!g_quiet && fixedCount > 0) std::cout << "  - Removed " << fixedCount << " duplicate wires." << std::endl;
                
                if (SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
                    if (!g_quiet) std::cout << "  - Schematic fixed and saved successfully." << std::endl;
                }
            }
        } else if (filePath.endsWith(".pcb")) {
#if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
#else
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, filePath)) {
                int fixedTraces = 0;
                int snappedPoints = 0;
                int snappedComponents = 0;
                PCBDRC drc;
                double minWidth = drc.rules().minTraceWidth();
                double gridSize = 0.1;

                auto snap = [&](QPointF p) {
                    double x = std::round(p.x() / gridSize) * gridSize;
                    double y = std::round(p.y() / gridSize) * gridSize;
                    return QPointF(x, y);
                };

                QMap<QString, QPointF> pointMap; 
                auto ptKey = [](QPointF p) { return QString("%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4); };

                QList<TraceItem*> traces;
                for (auto* item : scene.items()) {
                    if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                        traces.append(trace);
                        if (trace->width() < minWidth) {
                            trace->setWidth(minWidth);
                            fixedTraces++;
                        }
                        pointMap[ptKey(trace->startPoint())] = snap(trace->startPoint());
                        pointMap[ptKey(trace->endPoint())] = snap(trace->endPoint());
                    } else if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                        QPointF oldPos = comp->pos();
                        QPointF newPos = snap(oldPos);
                        if (oldPos != newPos) {
                            comp->setPos(newPos);
                            snappedComponents++;
                        }
                    }
                }

                for (auto* trace : traces) {
                    QPointF oldS = trace->startPoint();
                    QPointF oldE = trace->endPoint();
                    QPointF newS = pointMap[ptKey(oldS)];
                    QPointF newE = pointMap[ptKey(oldE)];

                    if (newS != oldS || newE != oldE) {
                        trace->setStartPoint(newS);
                        trace->setEndPoint(newE);
                        snappedPoints++;
                    }
                }

                if (!g_quiet && fixedTraces > 0) std::cout << "  - Adjusted " << fixedTraces << " traces to minimum width (" << minWidth << "mm)." << std::endl;
                if (!g_quiet && snappedPoints > 0) std::cout << "  - Snapped " << snappedPoints << " trace points to " << gridSize << "mm grid." << std::endl;
                if (!g_quiet && snappedComponents > 0) std::cout << "  - Realigned " << snappedComponents << " components to " << gridSize << "mm grid." << std::endl;
                
                if (PCBFileIO::savePCB(&scene, filePath)) {
                    if (!g_quiet) std::cout << "  - PCB fixed and saved successfully." << std::endl;
                }
            }
#endif
        } else {
            std::cerr << "Error: Autofix only supports .sch and .pcb files." << std::endl;
            return 1;
        }
        return 0;
    }
};

class ProcessCommand : public CLICommand {
public:
    QString name() const override { return "process"; }
    QString description() const override { return "Apply programmatic script-based changes to schematic/PCB files."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.sch|.pcb", "script.json", "[output.file]"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora process <file.sch|.pcb> <script.json> [output.file]" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        const QString scriptPath = args.at(1);
        const QString outputPath = (args.size() >= 3) ? args.at(2) : filePath;
        
        QGraphicsScene scene;
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error reading script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QJsonDocument scriptDoc = QJsonDocument::fromJson(scriptFile.readAll());
        if (!scriptDoc.isArray()) {
            std::cerr << "Error: Script must be a JSON array of commands." << std::endl;
            return 1;
        }

        if (filePath.endsWith(".sch")) {
            SchematicAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading schematic: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " schematic commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed schematic to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed schematic." << std::endl;
                return 1;
            }
        } else if (filePath.endsWith(".pcb")) {
#if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
#else
            PCBAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading PCB: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " PCB commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed PCB to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed PCB." << std::endl;
                return 1;
            }
#endif
        } else {
            std::cerr << "Error: Unsupported file extension for process." << std::endl;
            return 1;
        }
        return 0;
    }
};

class PythonCommand : public CLICommand {
public:
    QString name() const override { return "python"; }
    QString description() const override { return "Execute inline python scripts for layout math and analysis (experimental)."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"script.py", "[args...]"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora python <script.py> [args...]" << std::endl;
            return 1;
        }
        std::cerr << "Error: embedded Python support is not available in this build." << std::endl;
        return 1;
    }
};

class PluginsSmokeCommand : public CLICommand {
public:
    QString name() const override { return "plugins-smoke"; }
    QString description() const override { return "Unloads/reloads active C++ extension plugins for a lifecycle sanity check."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        PluginManager::instance().unloadPlugins();
        PluginManager::instance().loadPlugins();

        const auto plugins = PluginManager::instance().activePlugins();
        if (!g_quiet) std::cout << "Loaded plugins: " << plugins.size() << std::endl;
        for (FluxPlugin* plugin : plugins) {
            if (!g_quiet) std::cout << "  - " << plugin->name().toStdString()
                      << " v" << plugin->version().toStdString()
                      << " (sdk " << plugin->sdkVersionMajor()
                      << "." << plugin->sdkVersionMinor() << ")" << std::endl;
        }

        PluginManager::instance().unloadPlugins();
        if (!g_quiet) std::cout << "Plugin lifecycle smoke test completed." << std::endl;
        return 0;
    }
};

class ScreenshotCommand : public CLICommand {
public:
    QString name() const override { return "screenshot"; }
    QString description() const override { return "Capture screenshot of running GUI schematic/oscilloscope windows."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        // Since we are running in registry execute, parser arguments might have been passed
        // via parser or we need to extract from full arguments. Let's do it based on full args
        // via parser or manually.
        QStringList fullArgs = QCoreApplication::arguments();
        // Since execute gets args where command is stripped, we need to map to what runScreenshot expects.
        // Actually, runScreenshot parses args starting from index 2 because it expects: viora screenshot ...
        // Let's adapt runScreenshot parsing slightly.
        QString name;
        QString output;
        qreal scale = 1.0;
        bool clipboard = false;
        bool jsonOutput = parser.isSet("json");
        bool includeHidden = false;
        bool listChildren = false;
        QString format = "PNG";
        QRect region;
        bool watchMode = false;
        int interval = 1000;
        QString outputDir;

        for (int i = 0; i < args.size(); ++i) {
            const QString& arg = args.at(i);
            if (arg == "--name" && i + 1 < args.size()) {
                name = args.at(++i);
            } else if (arg == "--output" && i + 1 < args.size()) {
                output = args.at(++i);
            } else if (arg == "--scale" && i + 1 < args.size()) {
                scale = args.at(++i).toDouble();
            } else if (arg == "--format" && i + 1 < args.size()) {
                format = args.at(++i).toUpper();
            } else if (arg == "--region" && i + 1 < args.size()) {
                QStringList parts = args.at(++i).split(",");
                if (parts.size() == 4)
                    region = QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
            } else if (arg == "--interval" && i + 1 < args.size()) {
                interval = args.at(++i).toInt();
            } else if (arg == "--output-dir" && i + 1 < args.size()) {
                outputDir = args.at(++i);
            } else if (arg == "--clipboard") {
                clipboard = true;
            } else if (arg == "--include-hidden") {
                includeHidden = true;
            } else if (arg == "--list-children") {
                listChildren = true;
            } else if (arg == "--watch") {
                watchMode = true;
            }
        }

        if (listChildren) {
            if (name.isEmpty()) {
                std::cerr << "Error: --list-children requires --name <parent>" << std::endl;
                return 1;
            }
            QVariantMap cmd;
            cmd["cmd"] = "screenshot_children";
            QVariantMap params;
            params["parent"] = name;
            cmd["params"] = params;
            QVariantMap response;
            if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                return 1;
            }
            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return 0;
            }
            QJsonArray children = response["children"].toJsonArray();
            if (children.isEmpty()) {
                std::cout << "No children found for: " << name.toStdString() << std::endl;
                return 0;
            }
            std::cout << "Children of " << name.toStdString() << ":" << std::endl;
            for (const auto& c : children) {
                std::cout << "  - " << c.toString().toStdString() << std::endl;
            }
            return 0;
        }

        if (name.isEmpty() && !watchMode) {
            QVariantMap cmd;
            cmd["cmd"] = "screenshot_list";
            QVariantMap params;
            params["include_hidden"] = includeHidden;
            cmd["params"] = params;
            QVariantMap response;
            if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                std::cerr << "Make sure VioSpice GUI is running." << std::endl;
                return 1;
            }
            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return 0;
            }
            QJsonArray windows = response["windows"].toJsonArray();
            if (windows.isEmpty()) {
                std::cout << "No visible windows found." << std::endl;
                return 0;
            }
            std::cout << "Available windows:" << std::endl;
            for (const auto& w : windows) {
                QJsonObject obj = w.toObject();
                std::cout << "  [" << obj["index"].toInt() << "] "
                          << obj["class"].toString().toStdString()
                          << " - " << obj["title"].toString().toStdString()
                          << std::endl;
                if (obj.contains("children")) {
                    QJsonArray children = obj["children"].toArray();
                    for (const auto& c : children) {
                        std::cout << "      " << c.toString().toStdString() << std::endl;
                    }
                }
            }
            return 0;
        }

        auto captureOnce = [&]() -> bool {
            QVariantMap params;
            params["name"] = name;
            params["clipboard"] = clipboard;
            params["scale"] = scale;
            params["format"] = format;
            params["include_hidden"] = includeHidden;
            if (!output.isEmpty()) {
                params["output"] = output;
            }
            if (!region.isNull()) {
                QVariantList r;
                r << region.x() << region.y() << region.width() << region.height();
                params["region"] = r;
            }

            QVariantMap cmd;
            cmd["cmd"] = "screenshot_capture";
            cmd["params"] = params;

            QVariantMap response;
            if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                std::cerr << "Make sure VioSpice GUI is running." << std::endl;
                return false;
            }

            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return response.value("ok").toBool();
            }

            if (!response.value("ok").toBool()) {
                std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
                return false;
            }

            std::cout << "Screenshot captured: "
                      << response.value("width").toInt() << "x"
                      << response.value("height").toInt() << std::endl;
            if (response.contains("path")) {
                std::cout << "Saved to: " << response.value("path").toString().toStdString() << std::endl;
            }
            if (clipboard) {
                std::cout << "Copied to clipboard." << std::endl;
            }
            return true;
        };

        if (watchMode) {
            if (name.isEmpty()) {
                std::cerr << "Error: --watch requires --name <window>" << std::endl;
                return 1;
            }
            if (outputDir.isEmpty())
                outputDir = ".";
            std::cout << "Watching " << name.toStdString() << " every " << interval << "ms. Press Ctrl+C to stop." << std::endl;

            QElapsedTimer timer;
            timer.start();
            int frame = 0;

            while (true) {
                if (timer.elapsed() >= interval) {
                    QString origOutput = output;
                    output = QString("%1/%2_frame_%3.%4")
                        .arg(outputDir)
                        .arg(name)
                        .arg(frame++, 4, 10, QChar('0'))
                        .arg(format.toLower());
                    captureOnce();
                    output = origOutput;
                    timer.restart();
                }
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
        }

        return captureOnce() ? 0 : 1;
    }
};

class GuiCommand : public CLICommand {
public:
    QString name() const override { return "gui"; }
    QString description() const override { return "Remotely control running GUI instance."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora gui <subcommand> [options]\n";
            std::cerr << "\nSubcommands:\n";
            std::cerr << "  list-buttons   List interactive elements in a window\n";
            std::cerr << "  click <target> Click a button or trigger an action\n";
            std::cerr << "  type <field> <text>  Type text into an input field\n";
            std::cerr << "  menu <action>  Trigger a menu action\n";
            return 1;
        }

        QString subcmd = args.at(0);
        bool jsonOutput = parser.isSet("json");

        // --- Parse --window option ---
        QString window;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == "--window" && i + 1 < args.size()) {
                window = args.at(++i);
            }
        }
        if (window.isEmpty()) {
            window = "SchematicEditor";
        }

        if (subcmd == "list-buttons") {
            QString filterType;
            QString filterParent;
            for (int i = 1; i < args.size(); ++i) {
                if (args.at(i) == "--type" && i + 1 < args.size())
                    filterType = args.at(++i);
                else if (args.at(i) == "--parent" && i + 1 < args.size())
                    filterParent = args.at(++i);
                else if (args.at(i) == "--window" && i + 1 < args.size())
                    ++i;
            }

            QVariantMap cmd;
            cmd["cmd"] = "gui_list_elements";
            QVariantMap params;
            params["window"] = window;
            if (!filterType.isEmpty()) params["type"] = filterType;
            if (!filterParent.isEmpty()) params["parent"] = filterParent;
            cmd["params"] = params;

            QVariantMap response;
            if (!sendGuiCommand(cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                return 1;
            }

            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return 0;
            }

            QJsonArray elements = response["elements"].toJsonArray();
            if (elements.isEmpty()) {
                std::cout << "No interactive elements found in " << window.toStdString() << std::endl;
                return 0;
            }

            std::cout << "Interactive elements in " << window.toStdString() << ":" << std::endl;
            for (const auto& e : elements) {
                QJsonObject obj = e.toObject();
                std::cout << "  [" << obj["type"].toString().toStdString() << "] "
                          << obj["label"].toString().toStdString();
                if (!obj["objectName"].toString().isEmpty())
                    std::cout << " (" << obj["objectName"].toString().toStdString() << ")";
                if (!obj["parentName"].toString().isEmpty())
                    std::cout << " in " << obj["parentName"].toString().toStdString();
                std::cout << std::endl;
            }
            return 0;
        }

        if (subcmd == "click") {
            if (args.size() < 2) {
                std::cerr << "Usage: viora gui click <label-or-name> [--window <name>]" << std::endl;
                return 1;
            }
            QString target = args.at(1);

            QVariantMap cmd;
            cmd["cmd"] = "gui_click";
            QVariantMap params;
            params["window"] = window;
            params["target"] = target;
            cmd["params"] = params;

            QVariantMap response;
            if (!sendGuiCommand(cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                return 1;
            }

            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return response.value("ok").toBool() ? 0 : 1;
            }

            if (!response.value("ok").toBool()) {
                std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
                return 1;
            }

            std::cout << "Clicked: " << response.value("label").toString().toStdString()
                      << " (" << response.value("type").toString().toStdString() << ")" << std::endl;
            return 0;
        }

        if (subcmd == "type") {
            if (args.size() < 3) {
                std::cerr << "Usage: viora gui type <field-name> <text> [--window <name>] [--append]" << std::endl;
                return 1;
            }
            QString fieldName = args.at(1);
            QString text = args.at(2);
            bool append = false;
            for (int i = 3; i < args.size(); ++i) {
                if (args.at(i) == "--append") append = true;
            }

            QVariantMap cmd;
            cmd["cmd"] = "gui_type";
            QVariantMap params;
            params["window"] = window;
            params["target"] = fieldName;
            params["text"] = text;
            params["append"] = append;
            cmd["params"] = params;

            QVariantMap response;
            if (!sendGuiCommand(cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                return 1;
            }

            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return response.value("ok").toBool() ? 0 : 1;
            }

            if (!response.value("ok").toBool()) {
                std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
                return 1;
            }

            std::cout << "Typed into " << response.value("target").toString().toStdString() << std::endl;
            return 0;
        }

        if (subcmd == "menu") {
            if (args.size() < 2) {
                std::cerr << "Usage: viora gui menu <action-name> [--window <name>]" << std::endl;
                return 1;
            }
            QString action = args.at(1);

            QVariantMap cmd;
            cmd["cmd"] = "gui_menu";
            QVariantMap params;
            params["window"] = window;
            params["target"] = action;
            cmd["params"] = params;

            QVariantMap response;
            if (!sendGuiCommand(cmd, response)) {
                std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
                return 1;
            }

            if (jsonOutput) {
                std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
                return response.value("ok").toBool() ? 0 : 1;
            }

            if (!response.value("ok").toBool()) {
                std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
                return 1;
            }

            std::cout << "Triggered menu action: " << response.value("target").toString().toStdString() << std::endl;
            return 0;
        }

        std::cerr << "Unknown gui subcommand: " << subcmd.toStdString() << std::endl;
        std::cerr << "Available subcommands: list-buttons, click, type, menu" << std::endl;
        return 1;
    }
};

class FluxCommandWrapper : public CLICommand {
public:
    QString name() const override { return "flux"; }
    QString description() const override { return "Run FluxScript integration commands."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("time", "Time value for template execution", "value"));
        parser.addOption(QCommandLineOption("inputs", "Input values for template (comma separated)", "values"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        VioSpice::FluxCommand fluxCmd;
        return fluxCmd.run(args, parser, g_quiet);
    }
};

class ExtensionCommandWrapper : public CLICommand {
public:
    QString name() const override { return "extension"; }
    QString description() const override { return "Manage C++ extension plugins."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora extension <init|validate|install> [name|dir]\n";
            return 1;
        }
        QString action = args[0];
        QStringList rest = args.mid(1);
        if (action == "init")      return cmdExtensionInit(rest);
        if (action == "validate")  return cmdExtensionValidate(rest);
        if (action == "install")   return cmdExtensionInstall(rest);
        std::cerr << "Unknown extension action: " << action.toStdString() << "\n";
        return 1;
    }
};

class ItemRenderCommand : public CLICommand {
public:
    QString name() const override { return "item-render"; }
    QString description() const override { return "Render a single schematic item to PNG."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("transparent", "Render PNG with transparent background"));
        parser.addOption(QCommandLineOption("scale", "Render scale", "scale", "1.0"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.json", "out.png"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora item-render <file.json> <out.png> [options]" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        const QString outPath = args.at(1);
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read item JSON file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid item JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }
        
        QJsonObject itemJson = doc.object();
        QString type = itemJson.value("type").toString();
        if (type.isEmpty()) {
            std::cerr << "Error: JSON missing 'type' field." << std::endl;
            return 1;
        }
        
        QGraphicsScene scene;
        SchematicItem* item = SchematicItemFactory::instance().createItem(type, QPointF(0, 0), itemJson, nullptr);
        if (!item) {
            std::cerr << "Error: Failed to create item of type: " << type.toStdString() << std::endl;
            return 1;
        }
        scene.addItem(item);
        
        QRectF rect = item->boundingRect();
        if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
            rect = QRectF(-20, -20, 40, 40); // fallback
        }

        const qreal margin = 10.0;
        const bool transparent = parser.isSet("transparent");
        const qreal scale = qMax(0.1, parser.value("scale").toDouble());
        
        QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                                qCeil((rect.height() + margin * 2.0) * scale));

        QImage image(imageSize, QImage::Format_ARGB32);
        image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        
        painter.translate((-rect.left() + margin) * scale, (-rect.top() + margin) * scale);
        painter.scale(scale, scale);

        QStyleOptionGraphicsItem opt;
        item->paint(&painter, &opt, nullptr);
        painter.end();
        
        if (!image.save(outPath)) {
            std::cerr << "Error: Failed to save rendered item to " << outPath.toStdString() << std::endl;
            return 1;
        }

        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["output"] = outPath;
            out["transparent"] = transparent;
            out["scale"] = scale;
            out["type"] = type;
            printJsonValue(out);
        } else {
            printInfoStd("Rendered item to " + outPath.toStdString());
        }
        return 0;
    }
};

} // namespace

void registerMiscCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<DrcCommand>());
    reg.registerCommand(std::make_unique<RenderCommand>());
    reg.registerCommand(std::make_unique<AuditCommand>());
    reg.registerCommand(std::make_unique<AutofixCommand>());
    reg.registerCommand(std::make_unique<ProcessCommand>());
    reg.registerCommand(std::make_unique<PythonCommand>());
    reg.registerCommand(std::make_unique<PluginsSmokeCommand>());
    reg.registerCommand(std::make_unique<ScreenshotCommand>());
    reg.registerCommand(std::make_unique<GuiCommand>());
    reg.registerCommand(std::make_unique<FluxCommandWrapper>());
    reg.registerCommand(std::make_unique<ExtensionCommandWrapper>());
    reg.registerCommand(std::make_unique<ItemRenderCommand>());
}
