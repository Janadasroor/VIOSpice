/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "symbol_commands.h"
#include "common.h"
#include "../command_registry.h"

#include "symbols/models/symbol_definition.h"
#include "symbols/symbol_library.h"
#include "symbols/kicad_symbol_importer.h"
#include "symbols/ltspice_symbol_importer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <iostream>
#include <cmath>
#include <optional>
#include <algorithm>
#include <limits>
#include <qmath.h>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {

QPointF scalePoint(const QPointF& p, const QPointF& fromCenter, const QPointF& toCenter, double scale) {
    return QPointF((p.x() - fromCenter.x()) * scale + toCenter.x(),
                   (p.y() - fromCenter.y()) * scale + toCenter.y());
}

QRectF standardSymbolBounds() {
    SymbolLibraryManager& libMgr = SymbolLibraryManager::instance();
    const SymbolDefinition* res = libMgr.findSymbol("Resistor");
    const SymbolDefinition* cap = libMgr.findSymbol("Capacitor");
    if (!res && !cap) return QRectF();
    if (res && cap) {
        const QRectF r = res->boundingRect();
        const QRectF c = cap->boundingRect();
        const qreal w = qMax(r.width(), c.width());
        const qreal h = qMax(r.height(), c.height());
        const QPointF center = QPointF(0.0, 0.0);
        return QRectF(center.x() - w * 0.5, center.y() - h * 0.5, w, h);
    }
    return QRectF();
}

void normalizeSymbolToStandardSize(SymbolDefinition& symbol) {
    const QRectF target = standardSymbolBounds();
    if (!target.isValid()) return;
    QRectF current = symbol.boundingRect();
    if (!current.isValid() || current.width() <= 0.0 || current.height() <= 0.0) return;

    const double scale = qMin(target.width() / current.width(), target.height() / current.height());
    if (!std::isfinite(scale) || scale <= 0.0) return;

    const QPointF fromCenter = current.center();
    const QPointF toCenter = target.center();

    for (SymbolPrimitive& prim : symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
            QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
            p1 = scalePoint(p1, fromCenter, toCenter, scale);
            p2 = scalePoint(p2, fromCenter, toCenter, scale);
            prim.data["x1"] = p1.x();
            prim.data["y1"] = p1.y();
            prim.data["x2"] = p2.x();
            prim.data["y2"] = p2.y();
            break;
        }
        case SymbolPrimitive::Rect:
        case SymbolPrimitive::Arc:
        case SymbolPrimitive::Image: {
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                     prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble(),
                     prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble());
            QPointF tl = scalePoint(r.topLeft(), fromCenter, toCenter, scale);
            QPointF br = scalePoint(r.bottomRight(), fromCenter, toCenter, scale);
            QRectF nr(tl, br);
            prim.data["x"] = nr.x();
            prim.data["y"] = nr.y();
            if (prim.data.contains("width")) prim.data["width"] = nr.width();
            if (prim.data.contains("height")) prim.data["height"] = nr.height();
            if (prim.data.contains("w")) prim.data["w"] = nr.width();
            if (prim.data.contains("h")) prim.data["h"] = nr.height();
            break;
        }
        case SymbolPrimitive::Circle: {
            QPointF c(prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble(),
                      prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble());
            c = scalePoint(c, fromCenter, toCenter, scale);
            const double r = (prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble()) * scale;
            if (prim.data.contains("centerX")) prim.data["centerX"] = c.x();
            if (prim.data.contains("centerY")) prim.data["centerY"] = c.y();
            if (prim.data.contains("cx")) prim.data["cx"] = c.x();
            if (prim.data.contains("cy")) prim.data["cy"] = c.y();
            if (prim.data.contains("radius")) prim.data["radius"] = r;
            if (prim.data.contains("r")) prim.data["r"] = r;
            break;
        }
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = prim.data["points"].toArray();
            QJsonArray outPts;
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                QPointF p(pt["x"].toDouble(), pt["y"].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                QJsonObject o;
                o["x"] = p.x();
                o["y"] = p.y();
                outPts.append(o);
            }
            prim.data["points"] = outPts;
            break;
        }
        case SymbolPrimitive::Text: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("fontSize")) {
                prim.data["fontSize"] = prim.data["fontSize"].toDouble() * scale;
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("length")) prim.data["length"] = prim.data["length"].toDouble() * scale;
            if (prim.data.contains("nameSize")) prim.data["nameSize"] = prim.data["nameSize"].toDouble() * scale;
            if (prim.data.contains("numSize")) prim.data["numSize"] = prim.data["numSize"].toDouble() * scale;
            break;
        }
        case SymbolPrimitive::Bezier: {
            for (int i = 1; i <= 4; ++i) {
                QPointF p(prim.data[QString("x%1").arg(i)].toDouble(),
                          prim.data[QString("y%1").arg(i)].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                prim.data[QString("x%1").arg(i)] = p.x();
                prim.data[QString("y%1").arg(i)] = p.y();
            }
            break;
        }
        default:
            break;
        }
    }

    symbol.setReferencePos(scalePoint(symbol.referencePos(), fromCenter, toCenter, scale));
    symbol.setNamePos(scalePoint(symbol.namePos(), fromCenter, toCenter, scale));
}

bool renderSymbolToPng(const SymbolDefinition& symbol, const QString& outPath, bool transparent = false, qreal scale = 4.0) {
    QRectF rect = symbol.boundingRect();
    if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
        rect = QRectF(-20, -20, 40, 40);
    }

    const qreal margin = 10.0;
    QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                            qCeil((rect.height() + margin * 2.0) * scale));

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Scale and translate painter to center the symbol
    painter.scale(scale, scale);
    painter.translate(-rect.x() + margin, -rect.y() + margin);

    // Draw symbol body/geometry
    QColor bodyColor(220, 220, 220);
    QPen linePen(bodyColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(linePen);
    painter.setBrush(Qt::NoBrush);

    auto parseLineStyle = [](const QString& style) {
        const QString s = style.trimmed().toLower();
        if (s == "dash") return Qt::DashLine;
        if (s == "dot") return Qt::DotLine;
        if (s == "dashdot") return Qt::DashDotLine;
        return Qt::SolidLine;
    };

    auto parseColorOrDefault = [](const QJsonObject& data, const QString& key, const QColor& fallback) {
        if (data.contains(key)) {
            QColor c(data.value(key).toString());
            if (c.isValid()) return c;
        }
        return fallback;
    };

    for (const auto& prim : symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            QPen pen = linePen;
            if (prim.data.contains("color")) {
                pen.setColor(parseColorOrDefault(prim.data, "color", bodyColor));
            }
            if (prim.data.contains("style")) {
                pen.setStyle(parseLineStyle(prim.data.value("style").toString()));
            }
            if (prim.data.contains("width")) {
                pen.setWidthF(prim.data.value("width").toDouble(1.5));
            }
            painter.setPen(pen);
            painter.drawLine(QPointF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble()),
                             QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()));
            break;
        }
        case SymbolPrimitive::Rect: {
            QPen pen = linePen;
            if (prim.data.contains("color")) {
                pen.setColor(parseColorOrDefault(prim.data, "color", bodyColor));
            }
            painter.setPen(pen);
            if (prim.data.value("fill").toBool(false)) {
                painter.setBrush(QBrush(parseColorOrDefault(prim.data, "fillColor", QColor(40, 40, 45))));
            } else {
                painter.setBrush(Qt::NoBrush);
            }
            const qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            const qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            painter.drawRect(QRectF(prim.data["x"].toDouble(), prim.data["y"].toDouble(), w, h));
            break;
        }
        case SymbolPrimitive::Circle: {
            QPen pen = linePen;
            if (prim.data.contains("color")) {
                pen.setColor(parseColorOrDefault(prim.data, "color", bodyColor));
            }
            painter.setPen(pen);
            if (prim.data.value("fill").toBool(false)) {
                painter.setBrush(QBrush(parseColorOrDefault(prim.data, "fillColor", QColor(40, 40, 45))));
            } else {
                painter.setBrush(Qt::NoBrush);
            }
            const qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
            const qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
            const qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
            painter.drawEllipse(QPointF(cx, cy), r, r);
            break;
        }
        case SymbolPrimitive::Arc: {
            QPen pen = linePen;
            if (prim.data.contains("color")) {
                pen.setColor(parseColorOrDefault(prim.data, "color", bodyColor));
            }
            painter.setPen(pen);
            const qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            const qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(), w, h);
            int startAngle = prim.data.value("startAngle").toInt(0);
            int spanAngle = prim.data.value("spanAngle").toInt(5760);
            painter.drawArc(r, startAngle, spanAngle);
            break;
        }
        case SymbolPrimitive::Polygon: {
            QPen pen = linePen;
            if (prim.data.contains("color")) {
                pen.setColor(parseColorOrDefault(prim.data, "color", bodyColor));
            }
            painter.setPen(pen);
            if (prim.data.value("fill").toBool(false)) {
                painter.setBrush(QBrush(parseColorOrDefault(prim.data, "fillColor", QColor(40, 40, 45))));
            } else {
                painter.setBrush(Qt::NoBrush);
            }
            QPolygonF poly;
            QJsonArray pts = prim.data.value("points").toArray();
            for (const auto& pt : pts) {
                poly << QPointF(pt.toObject().value("x").toDouble(), pt.toObject().value("y").toDouble());
            }
            painter.drawPolygon(poly);
            break;
        }
        case SymbolPrimitive::Text: {
            QPen pen = linePen;
            pen.setColor(parseColorOrDefault(prim.data, "color", QColor(180, 180, 190)));
            painter.setPen(pen);
            QFont font("Outfit", prim.data.value("fontSize").toDouble(8));
            painter.setFont(font);
            painter.drawText(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()),
                             prim.data.value("text").toString());
            break;
        }
        case SymbolPrimitive::Pin: {
            // Draw pin line
            QPen pen(QColor(100, 150, 255), 1.25);
            painter.setPen(pen);
            qreal px = prim.data.value("x").toDouble();
            qreal py = prim.data.value("y").toDouble();
            qreal len = prim.data.value("length").toDouble(15.0);
            QString orient = prim.data.value("orientation").toString("Right");
            QPointF p1(px, py);
            QPointF p2 = p1;
            if (orient == "Right") p2.rx() += len;
            else if (orient == "Left") p2.rx() -= len;
            else if (orient == "Up") p2.ry() -= len;
            else if (orient == "Down") p2.ry() += len;
            painter.drawLine(p1, p2);
            break;
        }
        default:
            break;
        }
    }

    painter.end();
    return image.save(outPath);
}

struct SpiceEntity {
    QString type; // ".subckt" or ".model"
    QString name;
    QStringList pins;
    QString modelType; // For .model: D, NPN, PNP, etc.
};

QStringList collapseContinuationLines(const QString& text) {
    QStringList collapsed;
    QString current;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith('+')) {
            const QString continuation = trimmed.mid(1).trimmed();
            if (current.isEmpty()) {
                current = continuation;
            } else if (!continuation.isEmpty()) {
                if (!current.endsWith(' ')) current += ' ';
                current += continuation;
            }
            continue;
        }
        if (!current.isEmpty()) {
            collapsed.append(current);
            current.clear();
        }
        if (!trimmed.isEmpty()) {
            current = trimmed;
        }
    }
    if (!current.isEmpty()) {
        collapsed.append(current);
    }
    return collapsed;
}

QList<SpiceEntity> parseSpiceEntities(const QString& text) {
    const QStringList lines = collapseContinuationLines(text);
    QList<SpiceEntity> parsed;
    for (const QString& line : lines) {
        if (!line.startsWith('.', Qt::CaseInsensitive)) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        
        if (parts.first().compare(".subckt", Qt::CaseInsensitive) == 0 && parts.size() >= 2) {
            SpiceEntity ent;
            ent.type = ".subckt";
            ent.name = parts.at(1);
            for (int i = 2; i < parts.size(); ++i) {
                if (parts.at(i).contains('=')) break;
                ent.pins.append(parts.at(i));
            }
            parsed.append(ent);
        } else if (parts.first().compare(".model", Qt::CaseInsensitive) == 0 && parts.size() >= 3) {
            SpiceEntity ent;
            ent.type = ".model";
            ent.name = parts.at(1);
            QString mType = parts.at(2);
            if (mType.contains('(')) mType = mType.left(mType.indexOf('('));
            ent.modelType = mType.toUpper();
            parsed.append(ent);
        }
    }
    return parsed;
}

struct MappingRule {
    QString name;
    QString templateName;
    QString spiceType;
    int pinCount = -1;
    QRegularExpression nameRegex;
    QStringList modelTypes;
};

class SymbolMatcher {
public:
    bool loadMapping(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) return false;
        
        QJsonArray rulesArr = doc.object().value("rules").toArray();
        for (const QJsonValue& val : rulesArr) {
            QJsonObject obj = val.toObject();
            QJsonObject match = obj.value("match").toObject();
            
            MappingRule rule;
            rule.name = obj.value("name").toString();
            rule.templateName = obj.value("template").toString();
            rule.spiceType = match.value("spice_type").toString();
            rule.pinCount = match.value("pin_count").toInt(-1);
            QString regexStr = match.value("name_regex").toString();
            if (!regexStr.isEmpty()) {
                rule.nameRegex = QRegularExpression(regexStr, QRegularExpression::CaseInsensitiveOption);
            }
            
            QJsonValue mType = match.value("model_type");
            if (mType.isArray()) {
                for (const QJsonValue& v : mType.toArray()) rule.modelTypes << v.toString().toUpper();
            } else if (!mType.isUndefined()) {
                rule.modelTypes << mType.toString().toUpper();
            }
            
            m_rules << rule;
        }
        return true;
    }

    QString match(const SpiceEntity& ent) const {
        for (const auto& rule : m_rules) {
            if (!rule.spiceType.isEmpty() && rule.spiceType != ent.type) continue;
            if (rule.pinCount != -1 && rule.pinCount != (int)ent.pins.size() && ent.type == ".subckt") continue;
            
            if (ent.type == ".model") {
                if (!rule.modelTypes.isEmpty() && !rule.modelTypes.contains(ent.modelType.toUpper())) continue;
            }
            
            if (rule.nameRegex.isValid()) {
                auto res = rule.nameRegex.match(ent.name);
                if (!res.hasMatch()) {
                    auto matchCi = QRegularExpression(rule.nameRegex.pattern(), QRegularExpression::CaseInsensitiveOption).match(ent.name);
                    if (!matchCi.hasMatch()) continue;
                }
            }
            
            if (g_debug) std::cout << "Matched " << ent.name.toStdString() << " to " << rule.templateName.toStdString() << std::endl;
            return rule.templateName;
        }
        if (g_debug) std::cout << "Failed to match " << ent.name.toStdString() << " (Type: " << ent.type.toStdString() << ", Pins: " << ent.pins.size() << ")" << std::endl;
        return "";
    }

private:
    QList<MappingRule> m_rules;
};

int generateSymbolsForLibrary(const QString& inputPath, const QString& outDir, const QString& symbolType, const QString& targetName = QString(), const SymbolMatcher* matcher = nullptr) {
    QFile file(inputPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot read input file: " << inputPath.toStdString() << std::endl;
        return 0;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QList<SpiceEntity> entities = parseSpiceEntities(content);
    if (entities.isEmpty()) {
        return 0;
    }

    QDir().mkpath(outDir);
    int count = 0;

    for (const auto& sub : entities) {
        if (!targetName.isEmpty() && sub.name.compare(targetName, Qt::CaseInsensitive) != 0) continue;

        auto getPinName = [&](int i) -> QString {
            if (i >= 0 && i < sub.pins.size()) return sub.pins[i];
            return QString::number(i + 1);
        };

        QString typeToUse = symbolType;
        if (typeToUse.isEmpty() && matcher) {
            typeToUse = matcher->match(sub);
        }

        SymbolDefinition def(sub.name);
        def.setDescription(QString("Auto-generated %1 symbol for %2 %3").arg(typeToUse.isEmpty() ? "IC" : typeToUse.toUpper(), sub.type, sub.name));
        def.setCategory(typeToUse == "op" ? "Amplifiers" : "Integrated Circuits");
        def.setReferencePrefix("U");
        def.setDefaultValue(sub.name);
        def.setSpiceModelName(sub.name);
        def.setModelSource("project");
        def.setModelPath(QFileInfo(inputPath).fileName());

        int pinCount = sub.pins.size();
        if (sub.type == ".model") {
            if (sub.modelType == "D") pinCount = 2;
            else if (sub.modelType == "NPN" || sub.modelType == "PNP" || sub.modelType == "NJF" || sub.modelType == "PJF") pinCount = 3;
            else if (sub.modelType == "NMOS" || sub.modelType == "PMOS" || sub.modelType == "VDMOS") pinCount = 3;
            else if (sub.modelType == "SW" || sub.modelType == "CSW") pinCount = 2;
        }
        const qreal bodyWidth = 90.0;
        const qreal pinSpacing = 30.0;
        const qreal pinLength = 15.0;

        if (typeToUse == "triode") {
            def.setCategory("Vacuum Tubes");
            def.setReferencePrefix("V");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 37.5, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-7.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-3.75, 0), QPointF(3.75, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 0), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(11.25, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 45)));
            if (pinCount >= 5) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 37.5), QPointF(0, 26.25)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 37.5), QPointF(0, 26.25)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 3, "K", "Up", 0));
            if (pinCount >= 5) {
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-15, 45), 4, "H1", "Up", 0));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 45), 5, "H2", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "pentode") {
            def.setCategory("Vacuum Tubes");
            def.setReferencePrefix("V");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 37.5, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -45)));
            for (int i = 0; i < 3; ++i) {
                qreal y = -11.25 + i * 11.25;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, y), QPointF(-7.5, y)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-3.75, y), QPointF(3.75, y)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, y), QPointF(15, y)));
            }
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-45, 11.25)));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(45, 0)));
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -11.25), QPointF(45, -11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(11.25, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 45)));
            if (pinCount >= 6) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 37.5), QPointF(0, 26.25)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 37.5), QPointF(0, 26.25)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "G2", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 11.25), 3, "G1", "Right", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 4, "K", "Up", 0));
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -11.25), 5, "G3", "Left", 0));
            if (pinCount >= 7) {
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-15, 45), 6, "H1", "Up", 0));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 45), 7, "H2", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "zener") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DZ");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "schottky") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DS");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 3.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 3.75), QPointF(7.5, 3.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 18.75), QPointF(-7.5, 18.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "varicap") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DV");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 18.75), QPointF(15, 18.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 18.75), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "inductor") {
            def.setCategory("Passives");
            def.setReferencePrefix("L");
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-15, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(0, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "adc" || typeToUse == "dac") {
            def.setCategory("Data Converters");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -45, 90, 90), false));
            def.addPrimitive(SymbolPrimitive::createText(typeToUse.toUpper(), QPointF(-15, -10), 10));
            QMap<int, QString> mapping;
            for (int i = 0; i < qMin(pinCount, 8); ++i) {
                qreal y = -30 + (i % 4) * 20;
                bool left = (i < 4);
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(left ? -45 : 45, y), QPointF(left ? -60 : 60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(left ? -60 : 60, y), i + 1, QString("P%1").arg(i+1), left ? "Right" : "Left", 0));
                mapping.insert(i + 1, getPinName(i));
            }
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "njfet" || typeToUse == "pjfet") {
            const bool pjfet = (typeToUse == "pjfet");
            def.setCategory("Semiconductors");
            def.setReferencePrefix("J");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(0, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(30, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -22.5), QPointF(30, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(30, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 22.5), QPointF(30, 45)));
            QList<QPointF> arrow;
            if (pjfet) {
                arrow << QPointF(-22.5, 0) << QPointF(-7.5, -7.5) << QPointF(-7.5, 7.5);
            } else {
                arrow << QPointF(-7.5, 0) << QPointF(-22.5, -7.5) << QPointF(-22.5, 7.5);
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, "D", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, "S", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "optocoupler_4pin") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -37.5, 90, 75), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -7.5), QPointF(-30, 7.5)));
            QList<QPointF> tri; tri << QPointF(-40, -7.5) << QPointF(-20, -7.5) << QPointF(-30, 7.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -7.5), QPointF(30, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 7.5), QPointF(30, 22.5)));
            QList<QPointF> eArrow; eArrow << QPointF(30, 22.5) << QPointF(22.5, 11.25) << QPointF(15, 18.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(eArrow, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -7.5), QPointF(5, -7.5)));
            QList<QPointF> tip1; tip1 << QPointF(5, -7.5) << QPointF(0, -11.25) << QPointF(0, -3.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 7.5), QPointF(5, 7.5)));
            QList<QPointF> tip2; tip2 << QPointF(5, 7.5) << QPointF(0, 3.75) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -7.5), QPointF(-30, -52.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 7.5), QPointF(-30, 52.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -22.5), QPointF(30, -52.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 22.5), QPointF(30, 52.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -52.5), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 52.5), 2, "K", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -52.5), 3, "C", "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 52.5), 4, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "photodiode") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -30), QPointF(-15, -15)));
            QList<QPointF> tip1; tip1 << QPointF(-15, -15) << QPointF(-22.5, -15) << QPointF(-15, -22.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-37.5, -22.5), QPointF(-22.5, -7.5)));
            QList<QPointF> tip2; tip2 << QPointF(-22.5, -7.5) << QPointF(-30, -7.5) << QPointF(-22.5, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -37.5), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "tl431") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-11.25, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 1, "K", "Up", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "REF", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 3, "A", "Down", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "op") {
            def.setCategory("Amplifiers");
            def.setReferencePrefix("U");
            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };
            int idxINP = findPin(nodes, "in\\+|\\+in|inp");
            int idxINN = findPin(nodes, "in\\-|\\-in|inn");
            int idxOUT = findPin(nodes, "out");
            int idxVCC = findPin(nodes, "vcc|v\\+|vdd|vp");
            int idxVEE = findPin(nodes, "vss|v\\-|vee|vn");

            if (idxINP == -1 && nodes.size() >= 1) idxINP = 1;
            if (idxINN == -1 && nodes.size() >= 2) idxINN = 2;
            if (idxVCC == -1 && nodes.size() >= 3) idxVCC = 3;
            if (idxVEE == -1 && nodes.size() >= 4) idxVEE = 4;
            if (idxOUT == -1 && nodes.size() >= 5) idxOUT = 5;

            if (pinCount <= 5) {
                QList<QPointF> tri; tri << QPointF(-20, -25) << QPointF(-20, 25) << QPointF(20, 0);
                def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
                def.addPrimitive(SymbolPrimitive::createText("+", QPointF(-17, 10), 8));
                def.addPrimitive(SymbolPrimitive::createText("-", QPointF(-17, -15), 10));
                
                if (idxINN != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -12.5), QPointF(-40, -12.5)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, -12.5), idxINN, "IN-", "Right", 0));
                }
                if (idxINP != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 12.5), QPointF(-40, 12.5)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 12.5), idxINP, "IN+", "Right", 0));
                }
                if (idxOUT != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, 0), QPointF(40, 0)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), idxOUT, "OUT", "Left", 0));
                }
                if (idxVCC != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12.5), QPointF(0, -30)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), idxVCC, "V+", "Down", 0));
                }
                if (idxVEE != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12.5), QPointF(0, 30)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), idxVEE, "V-", "Up", 0));
                }
            } else {
                const int ampCount = (pinCount >= 14) ? 4 : 2;
                const qreal h = ampCount * 40.0;
                def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -h/2, 60, h), false));
                for (int i=0; i<ampCount; ++i) {
                    qreal yOff = -h/2 + 20 + i*40;
                    QList<QPointF> tri; 
                    tri << QPointF(-15, yOff-10) << QPointF(-15, yOff+10) << QPointF(5, yOff);
                    def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
                    def.addPrimitive(SymbolPrimitive::createText(QString::number(i+1), QPointF(10, yOff-5), 6));
                }
                const int half = (pinCount+1)/2;
                for (int i = 0; i < pinCount; ++i) {
                    bool left = i < half;
                    qreal y = -h/2 + 10 + (left ? i : (pinCount - 1 - i)) * 20;
                    QPointF pos(left ? -45 : 45, y);
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(left?-30:30, y), pos));
                    def.addPrimitive(SymbolPrimitive::createPin(pos, i+1, getPinName(i), left?"Right":"Left", 0));
                }
            }
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "npn" || typeToUse == "pnp") {
            const bool pnp = (typeToUse == "pnp");
            def.setCategory("Semiconductors");
            def.setReferencePrefix(pnp ? "QP" : "QN");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(0, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(45, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, -45), QPointF(45, -60)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(45, 45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, 45), QPointF(45, 60)));
            QList<QPointF> arrow;
            if (pnp) {
                arrow << QPointF(0, 15) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
            } else {
                arrow << QPointF(45, 45) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -60), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 60), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "nmos" || typeToUse == "pmos") {
            const bool pmos = (typeToUse == "pmos");
            def.setCategory("Semiconductors");
            def.setReferencePrefix(pmos ? "MP" : "MN");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -37.5), QPointF(0, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -7.5), QPointF(0, 7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -30), QPointF(-7.5, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-7.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(30, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(30, 45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(30, 30)));
            QList<QPointF> arrow;
            if (pmos) {
                arrow << QPointF(7.5, 0) << QPointF(0, -3.75) << QPointF(0, 3.75);
            } else {
                arrow << QPointF(0, 0) << QPointF(7.5, -3.75) << QPointF(7.5, 3.75);
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-22.5, 0), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "diode") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "led") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(30, -30)));
            QList<QPointF> tip1; tip1 << QPointF(30, -30) << QPointF(22.5, -30) << QPointF(30, -22.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -7.5), QPointF(37.5, -22.5)));
            QList<QPointF> tip2; tip2 << QPointF(37.5, -22.5) << QPointF(30, -22.5) << QPointF(37.5, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "triac") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("TR");
            QList<QPointF> tri1; tri1 << QPointF(-15, -15) << QPointF(15, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-15, 15) << QPointF(15, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 7.5), QPointF(22.5, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 22.5), QPointF(45, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "MT2", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 22.5), 2, "G", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, "MT1", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "scr") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("SCR");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(15, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 22.5), QPointF(45, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 22.5), 2, "G", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "diac") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DIA");
            QList<QPointF> tri1; tri1 << QPointF(-15, -15) << QPointF(15, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-15, 15) << QPointF(15, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "igbt") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("QG");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -22.5), QPointF(-7.5, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-7.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(30, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(30, 45)));
            QList<QPointF> arrow; arrow << QPointF(30, 30) << QPointF(18.75, 22.5) << QPointF(15, 33.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, "C", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-22.5, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "darlington_npn") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("QD");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -15), QPointF(-15, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -7.5), QPointF(7.5, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 7.5), QPointF(0, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 3.75), QPointF(0, 33.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 26.25), QPointF(15, 37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -22.5), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -22.5), QPointF(15, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 37.5), QPointF(15, 52.5)));
            QList<QPointF> arrow; arrow << QPointF(15, 37.5) << QPointF(7.5, 26.25) << QPointF(0, 33.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, -45), 1, "C", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "B", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 52.5), 3, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "tvs_bidir") {
            def.setCategory("Protective");
            def.setReferencePrefix("D");
            QList<QPointF> tri1; tri1 << QPointF(-11.25, -15) << QPointF(11.25, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-11.25, 15) << QPointF(11.25, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 0), QPointF(11.25, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 0), QPointF(-11.25, -7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(11.25, 0), QPointF(11.25, 7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "varistor") {
            def.setCategory("Protective");
            def.setReferencePrefix("RV");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-22.5, -7.5, 45, 15), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 15), QPointF(22.5, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 15), QPointF(-30, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -15), QPointF(30, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-37.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 0), QPointF(37.5, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-37.5, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(37.5, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "oscillator_4pin") {
            def.setCategory("Oscillators");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-7.5, -11.25, 15, 22.5), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, -15), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(11.25, -15), QPointF(11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -15), QPointF(45, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 15), QPointF(45, 15)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, "GND", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 15), 3, "OUT", "Left", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -15), 4, "OE", "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "vref_series") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("VREF", QPointF(-20, -10), 8));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "IN", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "GND", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "current_source") {
            def.setCategory("Simulation");
            def.setReferencePrefix("I");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, 11.25)));
            QList<QPointF> tip; tip << QPointF(0, 11.25) << QPointF(-3.75, 3.75) << QPointF(3.75, 3.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -37.5), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "antenna") {
            def.setCategory("Miscellaneous");
            def.setReferencePrefix("ANT");
            QList<QPointF> tri; tri << QPointF(0, 0) << QPointF(-15, -15) << QPointF(15, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(0, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 1, "1", "Up", 0));
            for(int i=0; i<qMin(pinCount, 1); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "battery") {
            def.setCategory("Miscellaneous");
            def.setReferencePrefix("B");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -7.5), QPointF(15, -7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 7.5), QPointF(7.5, 7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -7.5), QPointF(0, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 7.5), QPointF(0, 22.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -22.5), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 22.5), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "relay") {
            def.setCategory("Electromechanical");
            def.setReferencePrefix("RLY");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -15, 30, 30), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-30, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, -5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -5), QPointF(30, 10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 30), 2, getPinName(1), "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, -30), 3, getPinName(2), "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 30), 4, getPinName(3), "Up", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "fuse") {
            def.setCategory("Passives");
            def.setReferencePrefix("F");
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-15, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(0, -7.5, 15, 15), 180 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "bridge_rectifier") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("BR");
            QList<QPointF> diamond; diamond << QPointF(0, -30) << QPointF(30, 0) << QPointF(0, 30) << QPointF(-30, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(diamond, false));
            def.addPrimitive(SymbolPrimitive::createText("+", QPointF(-5, -20), 8));
            def.addPrimitive(SymbolPrimitive::createText("-", QPointF(-5, 10), 8));
            def.addPrimitive(SymbolPrimitive::createText("~", QPointF(15, -5), 8));
            def.addPrimitive(SymbolPrimitive::createText("~", QPointF(-25, -5), 8));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, getPinName(1), "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 3, getPinName(2), "Right", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 4, getPinName(3), "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "transformer") {
            def.setCategory("Passives");
            def.setReferencePrefix("T");
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -30, 15, 20), 90 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -10, 15, 20), 90 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, 10, 15, 20), 90 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -30, 15, 20), 270 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -10, 15, 20), 270 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, 10, 15, 20), 270 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -25), QPointF(-5, 25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -25), QPointF(5, 25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, -30), QPointF(-45, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 30), QPointF(-45, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -30), QPointF(45, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 30), QPointF(45, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -30), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 30), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -30), 3, getPinName(2), "Left", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 30), 4, getPinName(3), "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "potentiometer") {
            def.setCategory("Passives");
            def.setReferencePrefix("RV");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-20, -10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -10), QPointF(0, 10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 10), QPointF(20, -10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, -10), QPointF(30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 10), QPointF(0, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, getPinName(1), "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "crystal") {
            def.setCategory("Passives");
            def.setReferencePrefix("Y");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -15), QPointF(-7.5, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, -15), QPointF(7.5, 15)));
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-3.75, -11.25, 7.5, 22.5), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "comparator") {
            def.setCategory("Comparators");
            def.setReferencePrefix("U");
            QList<QPointF> tri; tri << QPointF(-30, -30) << QPointF(-30, 30) << QPointF(30, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createText("CMP", QPointF(-25, -5), 8));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 3, getPinName(2), "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 4, getPinName(3), "Up", 0));
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 5, getPinName(4), "Left", 0));
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "ic8_timer") {
            def.setCategory("Integrated Circuits");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -60,  90, 120), false));
            def.addPrimitive(SymbolPrimitive::createText("555", QPointF(-15, -10), 10));
            QMap<int, QString> mapping;
            QStringList timerNames = {"GND", "TRIG", "OUT", "RESET", "CTRL", "THRES", "DISCH", "VCC"};
            for (int i = 0; i < 4; ++i) {
                qreal y = -45 + i * 30;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-45, y), QPointF(-60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-60, y), i + 1, timerNames.at(i), "Right", 0));
            }
            for (int i = 0; i < 4; ++i) {
                qreal y = 45 - i * 30;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, y), QPointF(60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(60, y), 8 - i, timerNames.at(7 - i), "Left", 0));
            }
            for(int i=0; i<8; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "vref_shunt") {
            def.setCategory("Integrated Circuits");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "K", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "A", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 3, "REF", "Right", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "logic_vco") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-40, -30, 80, 60), false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 15, false));
            QPainterPath sine; sine.moveTo(-7.5, 0); sine.quadTo(-3.75, -7.5, 0, 0); sine.quadTo(3.75, 7.5, 7.5, 0);
            for (int i=0; i<sine.toFillPolygon().size()-1; ++i) def.addPrimitive(SymbolPrimitive::createLine(sine.toFillPolygon().at(i), sine.toFillPolygon().at(i+1)));
            def.addPrimitive(SymbolPrimitive::createText("VCO", QPointF(-12, -25), 8));
            QMap<int, QString> mapping;
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-40, 0), QPointF(-55, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-55, 0), 1, "IN", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(40, 0), QPointF(55, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(55, 0), 2, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "switch_v" || typeToUse == "switch_i") {
            def.setCategory("Switches");
            def.setReferencePrefix("S");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(11.25, -11.25)));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-15, 0), 1.875, false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(15, 0), 1.875, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-37.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(37.5, 0)));
            if (typeToUse == "switch_i") {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(15, 15)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(-15, 30)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 30)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-37.5, 0), 1, "1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(37.5, 0), 2, "2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "speaker") {
            def.setCategory("Audio");
            def.setReferencePrefix("SPK");
            QList<QPointF> cone; cone << QPointF(-7.5, -7.5) << QPointF(15, -22.5) << QPointF(15, 22.5) << QPointF(-7.5, 7.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(cone, false));
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-15, -7.5, 7.5, 15), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, -7.5), QPointF(-11.25, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 7.5), QPointF(-11.25, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-11.25, -30), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-11.25, 30), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "hall_sensor") {
            def.setCategory("Sensors");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("HALL", QPointF(-18.75, -7.5), 8));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, "GND", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "gate_and" || typeToUse == "gate_nand") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool nand = (typeToUse == "gate_nand");
            QPainterPath path;
            path.moveTo(-20, -25);
            path.lineTo(0, -25);
            path.arcTo(QRectF(-25, -25, 50, 50), 90, -180);
            path.lineTo(-20, 25);
            path.closeSubpath();
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            if (nand) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(28.75, 0), 3.75, false));
            const int inCount = qMax(2, pinCount - 1);
            for (int i = 0; i < inCount; ++i) {
                qreal y = (inCount == 2) ? (i == 0 ? -12.5 : 12.5) : (-20.0 + i * (40.0 / (inCount - 1)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, y), QPointF(-40, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, y), i + 1, getPinName(i), "Right", 0));
            }
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(nand ? 32.5 : 25, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), inCount + 1, getPinName(inCount), "Left", 0));
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "gate_or" || typeToUse == "gate_nor" || typeToUse == "gate_xor" || typeToUse == "gate_xnor") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool invert = (typeToUse == "gate_nor" || typeToUse == "gate_xnor");
            const bool xor_gate = (typeToUse == "gate_xor" || typeToUse == "gate_xnor");
            QPainterPath path;
            path.moveTo(-15, -25);
            path.quadTo(QPointF(-5, 0), QPointF(-15, 25));
            path.quadTo(QPointF(10, 25), QPointF(30, 0));
            path.quadTo(QPointF(10, -25), QPointF(-15, -25));
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            if (xor_gate) {
                for (int i = 0; i < 10; ++i) {
                    qreal t1 = i / 10.0;
                    qreal t2 = (i + 1) / 10.0;
                    auto quadPoint = [](qreal t, QPointF p0, QPointF p1, QPointF p2) {
                        return (1-t)*(1-t)*p0 + 2*(1-t)*t*p1 + t*t*p2;
                    };
                    QPointF p0(-22.5, -25), p1(-12.5, 0), p2(-22.5, 25);
                    def.addPrimitive(SymbolPrimitive::createLine(quadPoint(t1, p0, p1, p2), quadPoint(t2, p0, p1, p2)));
                }
            }
            if (invert) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(33.75, 0), 3.75, false));
            const int inCount = qMax(2, pinCount - 1);
            for (int i = 0; i < inCount; ++i) {
                qreal y = (inCount == 2) ? (i == 0 ? -12.5 : 12.5) : (-20.0 + i * (40.0 / (inCount - 1)));
                qreal t = (y + 25.0) / 50.0;
                qreal x = (1-t)*(1-t)*(-15.0) + 2*(1-t)*t*(-5.0) + t*t*(-15.0);
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(x, y), QPointF(-40, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, y), i + 1, getPinName(i), "Right", 0));
            }
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(invert ? 37.5 : 30, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), inCount + 1, getPinName(inCount), "Left", 0));
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "gate_not" || typeToUse == "gate_buf") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool invert = (typeToUse == "gate_not");
            QList<QPointF> tri; 
            tri << QPointF(-15, -20) << QPointF(-15, 20) << QPointF(15, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            if (invert) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(18.75, 0), 3.75, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-35, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 0), 1, "A", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(invert ? 22.5 : 15, 0), QPointF(40, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "Y", "Left", 0));
            QMap<int, QString> mapping;
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "logic_dff") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -40, 60, 80), false));
            def.addPrimitive(SymbolPrimitive::createText("DFF", QPointF(-12, -5), 10));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 5), QPointF(-20, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 15), QPointF(-30, 25)));
            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };
            int idxD = findPin(nodes, "^d$|^data$");
            int idxCLK = findPin(nodes, "clk|clock|cp");
            int idxQ = findPin(nodes, "^q$|^out$");
            int idxQN = findPin(nodes, "qbar|qn|q\\\\");
            int idxPRE = findPin(nodes, "pre|set");
            int idxCLR = findPin(nodes, "clr|clear|res|reset");

            if (idxCLR == -1 && nodes.size() >= 1) idxCLR = 1;
            if (idxD == -1 && nodes.size() >= 2) idxD = 2;
            if (idxCLK == -1 && nodes.size() >= 3) idxCLK = 3;
            if (idxPRE == -1 && nodes.size() >= 4) idxPRE = 4;
            if (idxQ == -1 && nodes.size() >= 5) idxQ = 5;
            if (idxQN == -1 && nodes.size() >= 6) idxQN = 6;

            if (idxD != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), idxD, "D", "Right", 0));
            }
            if (idxCLK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), idxCLK, "CLK", "Right", 0));
            }
            if (idxQ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -15), QPointF(45, -15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -15), idxQ, "Q", "Left", 0));
            }
            if (idxQN != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 15), QPointF(45, 15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 15), idxQN, "Q\\", "Left", 0));
            }
            if (idxPRE != -1) {
                bool activeLow = nodes[idxPRE-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxPRE-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxPRE-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, -41.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -43.75), QPointF(0, -55)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -40), QPointF(0, -55)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -55), idxPRE, "PRE", "Down", 0));
            }
            if (idxCLR != -1) {
                bool activeLow = nodes[idxCLR-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxCLR-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxCLR-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 41.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 43.75), QPointF(0, 55)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 40), QPointF(0, 55)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 55), idxCLR, "CLR", "Up", 0));
            }
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "logic_jkff") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -50, 60, 100), false));
            def.addPrimitive(SymbolPrimitive::createText("JKFF", QPointF(-18, -5), 10));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -10), QPointF(-20, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 0), QPointF(-30, 10)));
            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };
            int idxJ = findPin(nodes, "^j$");
            int idxK = findPin(nodes, "^k$");
            int idxCLK = findPin(nodes, "clk|clock|cp");
            int idxQ = findPin(nodes, "^q$");
            int idxQN = findPin(nodes, "qbar|qn|q\\\\");
            int idxPRE = findPin(nodes, "pre|set");
            int idxCLR = findPin(nodes, "clr|clear|res|reset");

            if (idxCLK == -1 && nodes.size() >= 1) idxCLK = 1;
            if (idxPRE == -1 && nodes.size() >= 2) idxPRE = 2;
            if (idxCLR == -1 && nodes.size() >= 3) idxCLR = 3;
            if (idxJ == -1 && nodes.size() >= 4) idxJ = 4;
            if (idxK == -1 && nodes.size() >= 5) idxK = 5;
            if (idxQ == -1 && nodes.size() >= 6) idxQ = 6;
            if (idxQN == -1 && nodes.size() >= 7) idxQN = 7;

            if (idxJ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -30), QPointF(-45, -30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -30), idxJ, "J", "Right", 0));
            }
            if (idxCLK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), idxCLK, "CLK", "Right", 0));
            }
            if (idxK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 30), QPointF(-45, 30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 30), idxK, "K", "Right", 0));
            }
            if (idxQ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(45, -30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -30), idxQ, "Q", "Left", 0));
            }
            if (idxQN != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(45, 30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 30), idxQN, "Q\\", "Left", 0));
            }
            if (idxPRE != -1) {
                bool activeLow = nodes[idxPRE-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxPRE-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxPRE-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, -51.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -53.75), QPointF(0, -65)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -50), QPointF(0, -65)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -65), idxPRE, "PRE", "Down", 0));
            }
            if (idxCLR != -1) {
                bool activeLow = nodes[idxCLR-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxCLR-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxCLR-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 51.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 53.75), QPointF(0, 65)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 50), QPointF(0, 65)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 65), idxCLR, "CLR", "Up", 0));
            }
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "logic_mux") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            QList<QPointF> muxShape = {QPointF(-20, -40), QPointF(20, -25), QPointF(20, 25), QPointF(-20, 40)};
            def.addPrimitive(SymbolPrimitive::createPolygon(muxShape, false));
            def.addPrimitive(SymbolPrimitive::createText("MUX", QPointF(-10, -5), 8));
            QMap<int, QString> mapping;
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -20), QPointF(-35, -20)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, -20), 1, "I0", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 20), QPointF(-35, 20)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 20), 2, "I1", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 32.5), QPointF(0, 45)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 3, "S", "Up", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, 0), QPointF(35, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(35, 0), 4, "Y", "Left", 0));
            for(int i=0; i<qMin(4, pinCount); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "logic_gate_3pin" || typeToUse == "logic_gate_5pin") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            QPainterPath path;
            path.moveTo(-15, -30);
            path.lineTo(0, -30);
            path.arcTo(QRectF(-15, -30, 30, 60), 90, -180);
            path.lineTo(-15, 30);
            path.closeSubpath();
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            QMap<int, QString> mapping;
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -15), QPointF(-30, -15)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -15), 1, "A", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(-30, 15)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 15), 2, "B", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 3, "Y", "Left", 0));
            if (typeToUse == "logic_gate_5pin") {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, -45)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 4, "VCC", "Down", 0));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 5, "GND", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "regulator") {
            def.setCategory("Power");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "IN", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "GND", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "supervisor") {
            def.setCategory("Power");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("RESET", QPointF(-20, -10), 8));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, "RST", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 3, "GND", "Right", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else if (typeToUse == "switch_v") {
            def.setCategory("Switches");
            def.setReferencePrefix("S");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-15, 0), 3.75, false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(15, 0), 3.75, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, "P1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, "P2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);
        } else {
            const int leftCount = (pinCount + 1) / 2;
            const qreal bodyHeight = leftCount * pinSpacing;
            const qreal halfH = bodyHeight / 2.0;
            const qreal halfW = bodyWidth / 2.0;

            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-halfW, -halfH, bodyWidth, bodyHeight), false));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-11.25, -halfH - 5.625, 22.5, 11.25), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-halfW + 11.25, -halfH + 11.25), 3.75, true));
            def.addPrimitive(SymbolPrimitive::createText(sub.name, QPointF(-halfW + 5, -halfH - 20.0), 10));

            QMap<int, QString> mapping;
            for (int i = 0; i < leftCount; ++i) {
                const qreal y = -halfH + 15.0 + i * pinSpacing;
                const QPointF pos(-halfW - pinLength, y);
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-halfW, y), QPointF(-halfW - pinLength, y)));
                def.addPrimitive(SymbolPrimitive::createPin(pos, i + 1, getPinName(i), "Right", 0));
                mapping.insert(i + 1, getPinName(i));
            }
            const int rightCount = pinCount - leftCount;
            for (int i = 0; i < rightCount; ++i) {
                const qreal y = halfH - 15.0 - i * pinSpacing;
                const QPointF pos(halfW + pinLength, y);
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(halfW, y), QPointF(halfW + pinLength, y)));
                def.addPrimitive(SymbolPrimitive::createPin(pos, leftCount + i + 1, getPinName(leftCount + i), "Left", 0));
                mapping.insert(leftCount + i + 1, getPinName(leftCount + i));
            }
            def.setSpiceNodeMapping(mapping);
        }

        QString safeName = sub.name.toLower().replace('/', '_').replace('\\', '_');
        QString outPath = QDir(outDir).filePath(safeName + ".viosym");
        QFile outFile(outPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(def.toJson());
            outFile.write(doc.toJson(QJsonDocument::Indented));
            outFile.close();
            if (!g_quiet) std::cout << "Generated symbol: " << outPath.toStdString() << " (from " << QFileInfo(inputPath).fileName().toStdString() << ")" << std::endl;
            count++;
        } else {
            std::cerr << "Failed to open output file: " << outPath.toStdString() << " error: " << outFile.errorString().toStdString() << std::endl;
        }
    }
    return count;
}

class SymbolRenderCommand : public CLICommand {
public:
    QString name() const override { return "symbol-render"; }
    QString description() const override { return "Render a symbol definition (.viosym) to PNG."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("transparent", "Render PNG with transparent background"));
        parser.addOption(QCommandLineOption("scale", "Render scale (default 4.0)", "scale", "4"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.viosym", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"output", "string"}, {"transparent", "bool"}, {"scale", "number"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora symbol-render <file.viosym> <out.png> [options]" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        const QString outPath = args.at(1);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray bytes = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("library")) {
            std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
            return 1;
        }

        SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
        if (symbol.name().trimmed().isEmpty()) {
            symbol.setName(QFileInfo(filePath).completeBaseName());
        }

        const bool transparent = parser.isSet("transparent");
        const qreal scale = qMax(0.1, parser.value("scale").toDouble());
        if (!renderSymbolToPng(symbol, outPath, transparent, scale)) {
            std::cerr << "Error: Failed to render symbol to " << outPath.toStdString() << std::endl;
            return 1;
        }
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["output"] = outPath;
            out["transparent"] = transparent;
            out["scale"] = scale;
            printJsonValue(out);
        } else {
            printInfoStd("Rendered symbol to " + outPath.toStdString());
        }
        return 0;
    }
};

class SymbolSearchCommand : public CLICommand {
public:
    QString name() const override { return "symbol-search"; }
    QString description() const override { return "Search symbol definition metadata."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora symbol-search <query>" << std::endl;
            return 1;
        }
        const QString query = args.at(0);
        
        QList<SymbolLibrary::SymbolInfo> results = SymbolLibraryManager::instance().searchMetadata(query);
        
        QJsonObject out;
        out["query"] = query;
        out["count"] = results.size();
        QJsonArray items;
        for (const auto& info : results) {
            QJsonObject item;
            item["name"] = info.name;
            item["library"] = info.library;
            item["category"] = info.category;
            item["description"] = info.description;
            item["tags"] = info.tags;
            items.append(item);
        }
        out["results"] = items;
        printJsonValue(out);
        return 0;
    }
};

class SymbolListCommand : public CLICommand {
public:
    QString name() const override { return "symbol-list"; }
    QString description() const override { return "List symbols in a folder or library file (.sclib)."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"folder|library.sclib"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"path", "string"}, {"symbols", "array[{name,source}]"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora symbol-list <folder|library.sclib>" << std::endl;
            return 1;
        }
        const QString path = args.at(0);
        QFileInfo info(path);
        if (!info.exists()) {
            std::cerr << "Error: Path not found: " << path.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["path"] = path;
        QJsonArray symbols;

        auto appendSymbol = [&](const QString& name, const QString& source) {
            QJsonObject s;
            s["name"] = name;
            s["source"] = source;
            symbols.append(s);
        };

        if (info.isFile() && path.endsWith(".sclib", Qt::CaseInsensitive)) {
            SymbolLibrary lib;
            if (!lib.load(path)) {
                std::cerr << "Error: Failed to load library: " << path.toStdString() << std::endl;
                return 1;
            }
            for (const QString& name : lib.symbolNames()) {
                appendSymbol(name, QFileInfo(path).fileName());
            }
        } else if (info.isDir()) {
            SymbolLibraryManager& manager = SymbolLibraryManager::instance();
            manager.loadUserLibraries(path, false); 
            
            for (SymbolLibrary* lib : manager.libraries()) {
                if (lib->path().startsWith(path)) {
                    const QList<SymbolLibrary::SymbolInfo> infos = lib->symbolInfos();
                    for (const auto& i : infos) {
                        QJsonObject s;
                        s["name"] = i.name;
                        s["source"] = lib->name();
                        s["category"] = i.category;
                        s["description"] = i.description;
                        symbols.append(s);
                    }
                }
            }
        } else {
            std::cerr << "Error: Unsupported path. Use a folder or .sclib file." << std::endl;
            return 1;
        }

        out["symbols"] = symbols;
        printJsonValue(out);
        return 0;
    }
};

class SymbolExportCommand : public CLICommand {
public:
    QString name() const override { return "symbol-export"; }
    QString description() const override { return "Extract a symbol definition from a library file."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"symbolName", "library.sclib", "out.viosym"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"ok", "bool"}, {"output", "string"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 3) {
            std::cerr << "Usage: viora symbol-export <symbolName> <library.sclib> <out.viosym>" << std::endl;
            return 1;
        }
        const QString symName = args.at(0);
        const QString libPath = args.at(1);
        const QString outPath = args.at(2);

        if (!QFileInfo::exists(libPath)) {
            std::cerr << "Error: Library not found: " << libPath.toStdString() << std::endl;
            return 1;
        }

        SymbolLibrary lib;
        if (!lib.load(libPath)) {
            std::cerr << "Error: Failed to load library: " << libPath.toStdString() << std::endl;
            return 1;
        }

        SymbolDefinition* sym = lib.findSymbol(symName);
        if (!sym) {
            std::cerr << "Error: Symbol not found: " << symName.toStdString() << std::endl;
            return 1;
        }

        QString finalOut = outPath;
        if (!finalOut.endsWith(".viosym", Qt::CaseInsensitive)) finalOut += ".viosym";

        QJsonDocument doc(sortJsonValue(sym->toJson()).toObject());
        QFile file(finalOut);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
            return 1;
        }
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        printInfoStd("Exported " + symName.toStdString() + " to " + finalOut.toStdString());
        return 0;
    }
};

class SymbolImportCommand : public CLICommand {
public:
    QString name() const override { return "symbol-import"; }
    QString description() const override { return "Import KiCad (.kicad_sym) or LTspice (.asy) symbols."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("symbol-name", "Symbol name (for KiCad .kicad_sym import)", "symname"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"input.asy|input.kicad_sym", "out.viosym|out.sclib"}}, {"options", QJsonObject{{"name", "symbolName (for KiCad)"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"input", "string"}, {"output", "string"}, {"name", "string"}, {"footprint", "string"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora symbol-import <input.asy|input.kicad_sym> <out.viosym|out.sclib> [--name SYMBOL]" << std::endl;
            return 1;
        }

        const QString inPath = args.at(0);
        const QString outPath = args.at(1);
        const QFileInfo inInfo(inPath);
        if (!inInfo.exists()) {
            std::cerr << "Error: Input not found: " << inPath.toStdString() << std::endl;
            return 1;
        }

        const QString lowerIn = inPath.toLower();
        SymbolDefinition symbol;
        QString detectedFootprint;

        if (lowerIn.endsWith(".asy")) {
            auto result = LtspiceSymbolImporter::importSymbolDetailed(inPath);
            if (!result.success || !result.symbol.isValid()) {
                const QString msg = result.errorMessage.isEmpty()
                                        ? "Failed to import LTspice symbol."
                                        : result.errorMessage;
                std::cerr << "Error: " << msg.toStdString() << std::endl;
                return 1;
            }
            symbol = result.symbol;
        } else if (lowerIn.endsWith(".kicad_sym")) {
            const QString symName = parser.value("symbol-name").trimmed();
            if (symName.isEmpty()) {
                QStringList names = KicadSymbolImporter::getSymbolNames(inPath);
                names.sort(Qt::CaseInsensitive);

                QJsonObject out;
                out["file"] = inPath;
                QJsonArray list;
                for (const QString& n : names) list.append(n);
                out["symbols"] = list;
                printJsonValue(out);
                return 0;
            }
            auto result = KicadSymbolImporter::importSymbolDetailed(inPath, symName);
            symbol = result.symbol;
            detectedFootprint = result.detectedFootprint;
            if (!symbol.isValid()) {
                std::cerr << "Error: Failed to import KiCad symbol: " << symName.toStdString() << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Unsupported input. Use .asy or .kicad_sym" << std::endl;
            return 1;
        }

        normalizeSymbolToStandardSize(symbol);

        QString finalOut = outPath;
        const QString lowerOut = outPath.toLower();
        if (lowerOut.endsWith(".viosym")) {
            QJsonDocument doc(sortJsonValue(symbol.toJson()).toObject());
            QFile file(finalOut);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
                return 1;
            }
            file.write(doc.toJson(QJsonDocument::Indented));
            file.close();
        } else if (lowerOut.endsWith(".sclib")) {
            SymbolLibrary lib;
            if (QFileInfo::exists(finalOut)) {
                if (!lib.load(finalOut)) {
                    std::cerr << "Error: Failed to load library: " << finalOut.toStdString() << std::endl;
                    return 1;
                }
            } else {
                lib.setName(QFileInfo(finalOut).completeBaseName());
                lib.setBuiltIn(false);
                lib.setPath(finalOut);
            }
            lib.addSymbol(symbol);
            if (!lib.save(finalOut)) {
                std::cerr << "Error: Failed to save library: " << finalOut.toStdString() << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Output must be .viosym or .sclib" << std::endl;
            return 1;
        }

        QJsonObject out;
        out["input"] = inPath;
        out["output"] = finalOut;
        out["name"] = symbol.name();
        if (!detectedFootprint.isEmpty()) out["footprint"] = detectedFootprint;
        printJsonValue(out);
        return 0;
    }
};

class SymbolQueryCommand : public CLICommand {
public:
    QString name() const override { return "symbol-query"; }
    QString description() const override { return "Query symbol definition attributes."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.viosym"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"name", "string"}, {"modelName", "string"}, {"pins", "array[pin]"}, {"boundingRect", "rect"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora symbol-query <file.viosym>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray bytes = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }
        QJsonObject obj = doc.object();
        if (obj.contains("library")) {
            std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
            return 1;
        }

        SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
        if (symbol.name().trimmed().isEmpty()) {
            symbol.setName(QFileInfo(filePath).completeBaseName());
        }

        QJsonObject out;
        out["file"] = filePath;
        out["name"] = symbol.name();
        out["description"] = symbol.description();
        out["category"] = symbol.category();
        out["referencePrefix"] = symbol.referencePrefix();
        out["defaultValue"] = symbol.defaultValue();
        out["modelSource"] = symbol.modelSource();
        out["modelPath"] = symbol.modelPath();
        out["modelName"] = symbol.modelName();
        int pinCount = 0;
        for (const auto& prim : symbol.primitives()) {
            if (prim.type == SymbolPrimitive::Pin) pinCount++;
        }
        out["pinCount"] = pinCount;

        QJsonArray pins;
        for (const auto& prim : symbol.primitives()) {
            if (prim.type != SymbolPrimitive::Pin) continue;
            QJsonObject p;
            p["number"] = prim.data.value("number").toInt();
            p["name"] = prim.data.value("name").toString();
            p["x"] = prim.data.value("x").toDouble();
            p["y"] = prim.data.value("y").toDouble();
            p["orientation"] = prim.data.value("orientation").toString();
            p["length"] = prim.data.value("length").toDouble(15.0);
            p["visible"] = prim.data.value("visible").toBool(true);
            pins.append(p);
        }
        out["pins"] = pins;

        QJsonObject bounds;
        const QRectF rect = symbol.boundingRect();
        bounds["x"] = rect.x();
        bounds["y"] = rect.y();
        bounds["w"] = rect.width();
        bounds["h"] = rect.height();
        out["boundingRect"] = bounds;

        printJsonValue(out);
        return 0;
    }
};

class SymbolValidateCommand : public CLICommand {
public:
    QString name() const override { return "symbol-validate"; }
    QString description() const override { return "Validate symbol file compliance."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.viosym"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"name", "string"}, {"pinCount", "int"}, {"issues", "array[issue]"}, {"summary", "object"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora symbol-validate <file.viosym>" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray bytes = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }
        QJsonObject obj = doc.object();
        if (obj.contains("library")) {
            std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
            return 1;
        }

        SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
        if (symbol.name().trimmed().isEmpty()) {
            symbol.setName(QFileInfo(filePath).completeBaseName());
        }

        QJsonArray issues;
        auto addIssue = [&](const QString& severity, const QString& message) {
            QJsonObject i;
            i["severity"] = severity;
            i["message"] = message;
            issues.append(i);
        };

        if (symbol.name().trimmed().isEmpty()) {
            addIssue("Error", "Symbol name is empty.");
        }
        if (symbol.referencePrefix().trimmed().isEmpty()) {
            addIssue("Warning", "Reference prefix is empty.");
        }

        int pinCount = 0;
        for (const auto& prim : symbol.primitives()) {
            if (prim.type != SymbolPrimitive::Pin) continue;
            pinCount++;
            const QString pinName = prim.data.value("name").toString().trimmed();
            const int number = prim.data.value("number").toInt();
            const QString orient = prim.data.value("orientation").toString("Right");
            const qreal len = prim.data.value("length").toDouble(15.0);
            if (number <= 0) {
                addIssue("Warning", QString("Pin has invalid number (%1).").arg(number));
            }
            if (pinName.isEmpty()) {
                addIssue("Warning", QString("Pin %1 has empty name.").arg(number > 0 ? QString::number(number) : "?"));
            }
            if (orient != "Left" && orient != "Right" && orient != "Up" && orient != "Down") {
                addIssue("Warning", QString("Pin %1 has invalid orientation '%2'.").arg(number > 0 ? QString::number(number) : "?", orient));
            }
            if (len <= 0.0) {
                addIssue("Warning", QString("Pin %1 has non-positive length.").arg(number > 0 ? QString::number(number) : "?"));
            }
        }
        if (pinCount == 0) {
            addIssue("Error", "Symbol has no pins.");
        }

        const QString modelName = symbol.modelName().trimmed();
        const QString modelPath = symbol.modelPath().trimmed();
        const QString modelSource = symbol.modelSource().trimmed().toLower();
        const QSet<QString> validSources = {"", "none", "library", "project", "absolute"};
        if (!modelSource.isEmpty() && !validSources.contains(modelSource)) {
            addIssue("Warning", QString("Model source '%1' is not recognized.").arg(symbol.modelSource()));
        }
        if (!modelName.isEmpty() && modelPath.isEmpty()) {
            addIssue("Warning", "Model name is set but model file path is empty.");
        }
        if (modelName.isEmpty() && !modelPath.isEmpty()) {
            addIssue("Warning", "Model file path is set but model name is empty.");
        }

        QJsonObject out;
        out["file"] = filePath;
        out["name"] = symbol.name();
        out["pinCount"] = pinCount;
        out["issues"] = issues;
        QJsonObject summary;
        summary["count"] = issues.size();
        out["summary"] = summary;

        printJsonValue(out);
        return 0;
    }
};

class SymbolFromSubcktCommand : public CLICommand {
public:
    QString name() const override { return "symbol-from-subckt"; }
    QString description() const override { return "Synthesize a symbol (.viosym) from a subcircuit (.cir)."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("name", "Name of specific subcircuit in library", "name"));
        parser.addOption(QCommandLineOption("symbol-type", "Type of symbol (triode|pentode|zener|schottky|varicap|inductor|njfet|pjfet|op|diode|led|triac|scr|diac|igbt|darlington_npn|tvs_bidir|varistor|oscillator_4pin|vref_series|current_source|antenna|battery|relay|fuse|bridge_rectifier|transformer|potentiometer|crystal|comparator|ic8_timer|vref_shunt|logic_vco|switch_v|switch_i|speaker|hall_sensor|gate_and|gate_nand|gate_or|gate_nor|gate_xor|gate_xnor|gate_not|gate_buf|logic_dff|logic_jkff|logic_mux|logic_gate_3pin|logic_gate_5pin|regulator|supervisor)", "type", "ic"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora symbol-from-subckt <input.cir|lib> <out_dir> [options]" << std::endl;
            return 1;
        }
        const QString inputPath = args.at(0);
        const QString outDir = args.at(1);
        const QString targetName = parser.value("name");
        const QString symbolType = parser.value("symbol-type").toLower();

        int count = generateSymbolsForLibrary(inputPath, outDir, symbolType, targetName);

        if (parser.isSet("json")) {
            QJsonObject res;
            res["ok"] = true;
            res["count"] = count;
            printJsonValue(res);
        }

        return count > 0 ? 0 : 1;
    }
};

class LibraryToSymbolsCommand : public CLICommand {
public:
    QString name() const override { return "library-to-symbols"; }
    QString description() const override { return "Batch-convert library subcircuits to symbol definitions."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("symbol-type", "Type of symbol to generate", "type", "ic"));
        parser.addOption(QCommandLineOption("recursive", "Search directory recursively"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora library-to-symbols <input_path> <out_dir> [options]" << std::endl;
            return 1;
        }
        QString inputPath = args.at(0);
        const QString outBaseDir = args.at(1);
        const QString symbolType = parser.value("symbol-type").toLower();
        const bool recursive = parser.isSet("recursive");

        if (inputPath.startsWith("~")) {
            inputPath.replace(0, 1, QDir::homePath());
        }

        QString rootToUse;
        QStringList filesToProcess;
        QFileInfo inInfo(inputPath);
        
        if (inInfo.isDir()) {
            rootToUse = inInfo.absoluteFilePath();
            QDirIterator it(rootToUse, {"*.lib", "*.sub", "*.spi", "*.mod", "*.cir"}, 
                            QDir::Files, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                filesToProcess << it.next();
            }
        } else if (inInfo.exists()) {
            rootToUse = inInfo.absolutePath();
            filesToProcess << inputPath;
        } else {
            std::cerr << "Error: Input path not found: " << inputPath.toStdString() << std::endl;
            return 1;
        }

        if (filesToProcess.isEmpty()) {
            std::cerr << "No SPICE library files found to process." << std::endl;
            return 1;
        }

        int totalSymbols = 0;
        int filesProcessed = 0;

        for (const QString& filePath : filesToProcess) {
            QFileInfo fi(filePath);
            QString relativePath = QDir(rootToUse).relativeFilePath(fi.absolutePath());
            QString libName = fi.completeBaseName();
            
            QDir targetDirObj(outBaseDir);
            if (relativePath != ".") {
                targetDirObj.setPath(targetDirObj.filePath(relativePath));
            }
            QString targetDir = targetDirObj.filePath(libName);
            
            int count = generateSymbolsForLibrary(filePath, targetDir, symbolType);
            if (count > 0) {
                totalSymbols += count;
                filesProcessed++;
            }
        }

        if (!g_quiet) {
            std::cout << "Successfully processed " << filesProcessed << " library files." << std::endl;
            std::cout << "Generated total of " << totalSymbols << " symbols in " << outBaseDir.toStdString() << std::endl;
        }

        if (parser.isSet("json")) {
            QJsonObject res;
            res["ok"] = true;
            res["files_processed"] = filesProcessed;
            res["total_symbols"] = totalSymbols;
            printJsonValue(res);
        }

        return filesProcessed > 0 ? 0 : 1;
    }
};

class LibraryAutoConvertCommand : public CLICommand {
public:
    QString name() const override { return "library-auto-convert"; }
    QString description() const override { return "Auto-convert subcircuits to matched symbols based on mapping rules."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("mapping", "Mapping JSON file for automatic symbol assignment", "mapping.json"));
        parser.addOption(QCommandLineOption("recursive", "Search directory recursively"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 2) {
            std::cerr << "Usage: viora library-auto-convert <input_path> <out_dir> [options]" << std::endl;
            return 1;
        }
        QString inputPath = args.at(0);
        const QString outBaseDir = args.at(1);
        const QString mappingPath = parser.value("mapping");
        const bool recursive = parser.isSet("recursive");

        SymbolMatcher matcher;
        if (!mappingPath.isEmpty()) {
            if (!matcher.loadMapping(mappingPath)) {
                std::cerr << "Error: Failed to load mapping JSON: " << mappingPath.toStdString() << std::endl;
                return 1;
            }
        }

        if (inputPath.startsWith("~")) {
            inputPath.replace(0, 1, QDir::homePath());
        }

        QString rootToUse;
        QStringList filesToProcess;
        QFileInfo inInfo(inputPath);
        
        if (inInfo.isDir()) {
            rootToUse = inInfo.absoluteFilePath();
            QDirIterator it(rootToUse, {"*.lib", "*.sub", "*.spi", "*.mod", "*.cir"}, 
                            QDir::Files, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                filesToProcess << it.next();
            }
        } else if (inInfo.exists()) {
            rootToUse = inInfo.absolutePath();
            filesToProcess << inputPath;
        } else {
            std::cerr << "Error: Input path not found: " << inputPath.toStdString() << std::endl;
            return 1;
        }

        if (filesToProcess.isEmpty()) {
            std::cerr << "No SPICE library files found to process." << std::endl;
            return 1;
        }

        int totalSymbols = 0;
        int filesProcessed = 0;

        for (const QString& filePath : filesToProcess) {
            QFileInfo fi(filePath);
            QString relativePath = QDir(rootToUse).relativeFilePath(fi.absolutePath());
            QString libName = fi.completeBaseName();
            
            QDir targetDirObj(outBaseDir);
            if (relativePath != ".") {
                targetDirObj.setPath(targetDirObj.filePath(relativePath));
            }
            QString targetDir = targetDirObj.filePath(libName);
            
            int count = generateSymbolsForLibrary(filePath, targetDir, "", "", &matcher);
            if (count > 0) {
                totalSymbols += count;
                filesProcessed++;
            }
        }

        if (!g_quiet) {
            std::cout << "Successfully auto-converted " << filesProcessed << " library files." << std::endl;
            std::cout << "Generated total of " << totalSymbols << " symbols in " << outBaseDir.toStdString() << std::endl;
        }

        return filesProcessed > 0 ? 0 : 1;
    }
};

class LibraryIndexCommand : public CLICommand {
public:
    QString name() const override { return "library-index"; }
    QString description() const override { return "Generate index database of symbols and models in folder."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("include-comments", "Parse commented .model/.subckt lines"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"folder"}}, {"options", QJsonObject{{"include-comments", "bool"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"root", "string"}, {"symbols", "array[{name,path,type}]"}, {"models", "array[{path,type,subckts,models}]"}, {"modelIndex", "object"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora library-index <folder> [options]" << std::endl;
            return 1;
        }
        const QString root = args.at(0);
        QDir dir(root);
        if (!dir.exists()) {
            std::cerr << "Error: Folder not found: " << root.toStdString() << std::endl;
            return 1;
        }

        QJsonObject out;
        out["root"] = root;

        QJsonArray symArr;
        QJsonArray modelArr;
        QJsonObject modelMap;
        QJsonObject subcktMap;

        const bool includeComments = parser.isSet("include-comments");

        QDirIterator it(root, QStringList() << "*.viosym" << "*.sclib" << "*.lib" << "*.sub" << "*.spi" << "*.mod" << "*.cir",
                        QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString path = it.next();
            QFileInfo info(path);
            const QString ext = info.suffix().toLower();
            const QString rel = dir.relativeFilePath(path);

            if (ext == "viosym") {
                QJsonObject s;
                s["name"] = info.completeBaseName();
                s["path"] = rel;
                s["type"] = "viosym";
                symArr.append(s);
            } else if (ext == "sclib") {
                SymbolLibrary lib;
                if (lib.load(path)) {
                    for (const QString& name : lib.symbolNames()) {
                        QJsonObject s;
                        s["name"] = name;
                        s["path"] = rel;
                        s["type"] = "sclib";
                        symArr.append(s);
                    }
                }
            } else {
                QFile file(path);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QJsonObject m;
                    m["path"] = rel;
                    m["type"] = ext;
                    
                    QJsonArray subckts;
                    QJsonArray models;

                    QTextStream in(&file);
                    while (!in.atEnd()) {
                        QString line = in.readLine().trimmed();
                        if (line.isEmpty()) continue;
                        
                        if (!includeComments && (line.startsWith('*') || line.startsWith(';'))) continue;
                        if (includeComments && (line.startsWith('*') || line.startsWith(';'))) {
                            line = line.mid(1).trimmed();
                        }

                        if (line.startsWith(".subckt", Qt::CaseInsensitive)) {
                            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                            if (parts.size() >= 2) {
                                const QString name = parts.at(1);
                                subckts.append(name);
                                QJsonObject subObj;
                                subObj["path"] = rel;
                                subObj["type"] = ext;
                                subcktMap[name.toLower()] = subObj;
                            }
                        } else if (line.startsWith(".model", Qt::CaseInsensitive)) {
                            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                            if (parts.size() >= 3) {
                                const QString name = parts.at(1);
                                QString mType = parts.at(2);
                                if (mType.contains('(')) mType = mType.left(mType.indexOf('('));
                                QJsonObject modInfo;
                                modInfo["name"] = name;
                                modInfo["type"] = mType.toUpper();
                                models.append(modInfo);

                                QJsonObject mapObj;
                                mapObj["path"] = rel;
                                mapObj["type"] = mType.toUpper();
                                modelMap[name.toLower()] = mapObj;
                            }
                        }
                    }
                    m["subckts"] = subckts;
                    m["models"] = models;
                    modelArr.append(m);
                    file.close();
                }
            }
        }

        out["symbols"] = symArr;
        out["models"] = modelArr;

        QJsonObject indexObj;
        indexObj["models"] = modelMap;
        indexObj["subckts"] = subcktMap;
        out["modelIndex"] = indexObj;

        printJsonValue(out);
        return 0;
    }
};

} // namespace

void registerSymbolCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<SymbolRenderCommand>());
    reg.registerCommand(std::make_unique<SymbolSearchCommand>());
    reg.registerCommand(std::make_unique<SymbolListCommand>());
    reg.registerCommand(std::make_unique<SymbolExportCommand>());
    reg.registerCommand(std::make_unique<SymbolImportCommand>());
    reg.registerCommand(std::make_unique<SymbolQueryCommand>());
    reg.registerCommand(std::make_unique<SymbolValidateCommand>());
    reg.registerCommand(std::make_unique<SymbolFromSubcktCommand>());
    reg.registerCommand(std::make_unique<LibraryToSymbolsCommand>());
    reg.registerCommand(std::make_unique<LibraryAutoConvertCommand>());
    reg.registerCommand(std::make_unique<LibraryIndexCommand>());
}
