/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_injector.h"
#include <QFile>
#include <QCryptographicHash>
#include <QRegularExpression>
#include "xspice_block_translator.h"
#include <QFileInfo>
#include <QDir>
#include "../../../core/project/config_manager.h"

void ModelInjector::inject(const QSet<QString>& includePaths,
                           const QSet<QString>& libPaths,
                           const QStringList& embeddedModelLines,
                           const QMap<QString, QString>& embeddedSubcircuits,
                           const QString& projectDir,
                           const QSet<QString>& userDeclaredModelFiles,
                           QString& netlist) {
    // Write .include and .lib directives (subcircuit/model files from symbol metadata)
    if (!includePaths.isEmpty() || !libPaths.isEmpty()) {
        QStringList includeList = includePaths.values();
        includeList.sort();
        QStringList libList = libPaths.values();
        libList.sort();

        netlist += "* Model Includes\n";
        QSet<QString> emittedModelFiles = userDeclaredModelFiles;
        auto processPath = [&](const QString& inc, const QString& directive) {
            QString resolvedPath = ModelInjector::normalizeIncludePathForNetlist(inc, projectDir);
            if (resolvedPath.isEmpty()) return;

            if (emittedModelFiles.contains(resolvedPath)) return;

            QString emittedPath = resolvedPath;
            QString emittedDirective = directive;
            if (QFileInfo::exists(resolvedPath)) {
                emittedPath = ModelInjector::sanitizeModelIncludeForNgspice(resolvedPath);
                emittedPath = QDir::fromNativeSeparators(QDir::cleanPath(emittedPath));
                // ngspice accepts plain model/subckt files via .include. Using
                // .lib for standalone cached files causes parse failures because
                // there is no section selector.
                emittedDirective = "include";
                if (emittedModelFiles.contains(emittedPath)) return;
            }

            netlist += QString(".%1 \"%2\"\n").arg(emittedDirective, emittedPath);
            emittedModelFiles.insert(resolvedPath);
            emittedModelFiles.insert(emittedPath);
        };

        for (const QString& inc : includeList) processPath(inc, "include");
        for (const QString& lib : libList) processPath(lib, "lib");
        netlist += "\n";
    }

    // Write embedded .model lines
    if (!embeddedModelLines.isEmpty()) {
        netlist += "* Embedded Models\n";
        for (const QString& ml : embeddedModelLines) {
            netlist += ml + "\n";
        }
        netlist += "\n";
    }

    // Write embedded subcircuit definitions from symbol metadata
    if (!embeddedSubcircuits.isEmpty()) {
        netlist += "* Embedded Subcircuits (from symbol definitions)\n";
        for (auto it = embeddedSubcircuits.constBegin(); it != embeddedSubcircuits.constEnd(); ++it) {
            netlist += it.value();
            if (!it.value().endsWith('\n')) netlist += '\n';
            netlist += '\n';
        }
    }
}

namespace {

int findMatchingParen(const QString& text, int openIndex) {
    if (openIndex < 0 || openIndex >= text.size() || text.at(openIndex) != '(') return -1;
    int depth = 0;
    for (int i = openIndex; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == '(') ++depth;
        else if (ch == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

QStringList splitTopLevelSpiceArgs(const QString& text) {
    QStringList args;
    QString current;
    int parenDepth = 0;
    int braceDepth = 0;

    for (QChar ch : text) {
        if (ch == ',' && parenDepth == 0 && braceDepth == 0) {
            args.append(current.trimmed());
            current.clear();
            continue;
        }
        if (ch == '(') ++parenDepth;
        else if (ch == ')') --parenDepth;
        else if (ch == '{') ++braceDepth;
        else if (ch == '}') --braceDepth;

        current += ch;
    }
    args.append(current.trimmed());
    return args;
}

QString rewriteLtspiceBehavioralFunctions(const QString& line, QStringList* warnings = nullptr) {
    struct RewriteRule {
        QString name;
        int minArgs;
        int maxArgs;
    };

    const QList<RewriteRule> rules = {
        {"buf", 1, 1},
        {"inv", 1, 1},
        {"uramp", 1, 1},
        {"limit", 3, 3},
        {"dnlim", 3, 3},
        {"uplim", 3, 3},
    };

    QString out = line;
    bool changed = false;

    auto buildReplacement = [](const QString& name, const QStringList& args) {
        if (name.compare("buf", Qt::CaseInsensitive) == 0) {
            return QString("u((%1)-(0.5))").arg(args.at(0));
        }
        if (name.compare("inv", Qt::CaseInsensitive) == 0) {
            return QString("(1-u((%1)-(0.5)))").arg(args.at(0));
        }
        if (name.compare("uramp", Qt::CaseInsensitive) == 0) {
            return QString("((%1)*u(%1))").arg(args.at(0));
        }
        if (name.compare("limit", Qt::CaseInsensitive) == 0) {
            return QString("min(max((%1),min((%2),(%3))),max((%2),(%3)))").arg(args.at(0), args.at(1), args.at(2));
        }
        if (name.compare("dnlim", Qt::CaseInsensitive) == 0) {
            return QString("max((%1),(%2))").arg(args.at(0), args.at(1));
        }
        if (name.compare("uplim", Qt::CaseInsensitive) == 0) {
            return QString("min((%1),(%2))").arg(args.at(0), args.at(1));
        }
        return QString();
    };

    bool replaced = true;
    while (replaced) {
        replaced = false;
        for (const RewriteRule& rule : rules) {
            const QString needle = rule.name + "(";
            const int nameIndex = out.indexOf(needle, 0, Qt::CaseInsensitive);
            if (nameIndex < 0) continue;

            const int openIndex = nameIndex + rule.name.size();
            const int closeIndex = findMatchingParen(out, openIndex);
            if (closeIndex < 0) continue;

            const QString inner = out.mid(openIndex + 1, closeIndex - openIndex - 1);
            const QStringList args = splitTopLevelSpiceArgs(inner);
            if (args.size() < rule.minArgs || args.size() > rule.maxArgs) continue;

            const QString replacement = buildReplacement(rule.name, args);
            if (replacement.isEmpty()) continue;

            out.replace(nameIndex, closeIndex - nameIndex + 1, replacement);
            changed = true;
            replaced = true;
            break;
        }
    }

    if (changed && warnings) {
        warnings->append(QString("Rewrote LTspice behavioral helper functions for ngspice compatibility in: %1").arg(line.trimmed()));
    }

    return out;
}

QStringList tokenizeLtspiceOtaLine(const QString& line) {
    return line.simplified().split(' ', Qt::SkipEmptyParts);
}

QString buildNgspiceOtaTranslation(const QString& line) {
    const QStringList tokens = tokenizeLtspiceOtaLine(line);
    if (tokens.size() < 10) return QString();
    if (tokens.at(9).compare("OTA", Qt::CaseInsensitive) != 0) return QString();
    if (!tokens.at(0).startsWith('A', Qt::CaseInsensitive)) return QString();

    const QString ref = tokens.at(0);
    const QString n1 = tokens.at(1);
    const QString n2 = tokens.at(2);
    const QString n3 = tokens.at(3);
    const QString n4 = tokens.at(4);
    const QString rail = tokens.at(6);
    const QString out = tokens.at(7);
    const QString gnd = tokens.at(8);

    QMap<QString, QString> params;
    QSet<QString> flags;
    for (int i = 10; i < tokens.size(); ++i) {
        const QString token = tokens.at(i).trimmed();
        if (token.isEmpty()) continue;
        const int eq = token.indexOf('=');
        if (eq >= 0) {
            QString key = token.left(eq).trimmed().toLower();
            QString value = token.mid(eq + 1).trimmed();
            if (value.isEmpty() && i + 1 < tokens.size()) {
                value = tokens.at(++i).trimmed();
            }
            if (!key.isEmpty()) params.insert(key, value);
        } else {
            flags.insert(token.toLower());
        }
    }

    const QString gm = params.value("g", "1u");
    const QString refExpr = params.value("ref", "0");
    const QString upper = params.contains("iout") ? params.value("iout")
                       : params.contains("isrc") ? params.value("isrc")
                       : QStringLiteral("10u");
    const QString lower = params.contains("isink") ? params.value("isink")
                       : QStringLiteral("-(%1)").arg(upper);
    const QString rout = params.value("rout").trimmed();
    const QString cout = params.value("cout").trimmed();
    const QString vhigh = params.value("vhigh").trimmed();
    const QString vlow = params.value("vlow").trimmed();
    const QString epsilon = params.value("epsilon", "1u").trimmed();

    const QString diffExpr = QString("(((V(%1,%2))+(V(%3,%4)))-(%5))")
        .arg(n1, n2, n3, n4, refExpr);
    const QString rawExpr = QString("((%1)*(%2))").arg(gm, diffExpr);

    QString currentExpr;
    if (flags.contains("linear")) {
        currentExpr = QString("min(max((%1),(%2)),(%3))").arg(rawExpr, lower, upper);
    } else {
        const QString posExpr = QString("(u(%1)*((%2)*tanh((%1)/(max(abs((%2)),1e-30)))))")
            .arg(rawExpr, upper);
        const QString negExpr = QString("(u(-(%1))*((abs((%2)))*tanh((-(%1))/(max(abs((%2)),1e-30)))))")
            .arg(rawExpr, lower);
        currentExpr = QString("((%1)-(%2))").arg(posExpr, negExpr);
    }

    if (!vhigh.isEmpty() || !vlow.isEmpty()) {
        const QString highExpr = vhigh.isEmpty() ? QStringLiteral("1e308")
                                                 : QString("((V(%1,%2))+(%3))").arg(rail, gnd, vhigh);
        const QString lowExpr = vlow.isEmpty() ? QStringLiteral("-1e308")
                                               : QString("((V(%1,%2))+(%3))").arg(gnd, gnd, vlow);
        const QString voutExpr = QString("V(%1,%2)").arg(out, gnd);
        const QString compliance = QString("(u((%1)-(%2)+(%3))*u((%2)-(%4)+(%3)))")
            .arg(highExpr, voutExpr, epsilon, lowExpr);
        currentExpr = QString("((%1)*(%2))").arg(currentExpr, compliance);
    }

    QStringList lines;
    lines << QString("* OTA_TRANSLATED %1").arg(ref);
    lines << QString("B__OTA_%1 %2 %3 I={%4}").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref), out, gnd, currentExpr);
    if (!rout.isEmpty()) {
        lines << QString("R__OTA_%1 %2 %3 %4").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref), out, gnd, rout);
    }
    if (!cout.isEmpty()) {
        lines << QString("C__OTA_%1 %2 %3 %4").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref), out, gnd, cout);
    }
    return lines.join('\n');
}

} // namespace

QString ModelInjector::normalizeIncludePathForNetlist(const QString& includePath, const QString& projectDir) {
    QString resolvedPath = QDir::fromNativeSeparators(includePath.trimmed());
    if (resolvedPath.isEmpty()) return resolvedPath;

    QFileInfo fi(resolvedPath);
    if (!fi.isAbsolute()) {
        // Prefer project-relative resolution first.
        const QString projectCandidate = QDir(projectDir).absoluteFilePath(resolvedPath);
        if (QFileInfo::exists(projectCandidate)) {
            resolvedPath = QDir::cleanPath(projectCandidate);
        } else {
            // Fall back to known library roots.
            const QStringList roots = ConfigManager::instance().libraryRoots();
            bool found = false;
            for (const QString& root : roots) {
                if (root.isEmpty()) continue;

                // If path already starts with "sub/", resolve directly against the root
                // to avoid a double "sub/sub/" when the root itself ends with /sub.
                if (resolvedPath.startsWith("sub/", Qt::CaseInsensitive)) {
                    QString candidate = QDir(root).absoluteFilePath(resolvedPath);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    // Extension fallback: .lib <-> .sub
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                } else {
                    QString candidate = QDir(root).absoluteFilePath(resolvedPath);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    // Extension fallback
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                }

                // Backwards compatibility: spice/X -> sub/X
                if (resolvedPath.startsWith("spice/")) {
                    const QString compat = "sub/" + resolvedPath.mid(6);
                    QString candidate = QDir(root).absoluteFilePath(compat);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                }

                // Try mod/ subdirectory for model libraries (e.g. LinearTech.lib)
                if (!found) {
                    QString candidate = QDir(root).absoluteFilePath("mod/" + resolvedPath);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    // Extension fallback
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                }
            }
            if (!found && !projectDir.isEmpty()) {
                resolvedPath = QDir::cleanPath(projectCandidate);
            }
        }
    } else if (QFileInfo::exists(resolvedPath)) {
        resolvedPath = QFileInfo(resolvedPath).absoluteFilePath();
    }

    return QDir::fromNativeSeparators(QDir::cleanPath(resolvedPath));
}

QString ModelInjector::sanitizeModelIncludeForNgspice(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return path;

    const QString ext = fi.suffix().toLower();
    const QSet<QString> supportedExt = {"lib", "inc", "sub", "sp", "cir", "cmp", "mod"};
    if (!supportedExt.contains(ext)) return path;

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return path;

    const QByteArray raw = in.readAll();
    in.close();
    if (raw.isEmpty()) return path;

    QString content = QString::fromUtf8(raw);
    if (content.contains(QChar::ReplacementCharacter)) {
        content = QString::fromLatin1(raw);
    }
    QStringList outLines;
    outLines.reserve(content.count('\n') + 1);
    const QStringList lines = content.split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('*') || trimmed.startsWith(';') || trimmed.startsWith('$')) continue;

        const QString otaTranslation = buildNgspiceOtaTranslation(line);
        if (!otaTranslation.isEmpty()) {
            outLines.append(otaTranslation);
            continue;
        }

        QString sanitizedLine = line;
        sanitizedLine = rewriteLtspiceBehavioralFunctions(sanitizedLine, nullptr);
        sanitizedLine.replace(QRegularExpression("\\bnoiseless\\b", QRegularExpression::CaseInsensitiveOption), QString());
        sanitizedLine.replace(QRegularExpression("\\s+;.*$"), QString());
        sanitizedLine.replace(QRegularExpression("\\s{2,}"), QStringLiteral(" "));
        outLines.append(sanitizedLine.trimmed());
    }

    const QString sanitized = outLines.join('\n');
    QByteArray key = fi.absoluteFilePath().toUtf8();
    key += QByteArrayLiteral("|sanitize_v3|");
    key += QByteArray::number(fi.size());
    key += QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());

    const QString cacheDirPath = QDir(QDir::tempPath()).filePath("viospice_model_cache");
    QDir cacheDir(cacheDirPath);
    if (!cacheDir.exists()) cacheDir.mkpath(".");

    const QString outPath = cacheDir.filePath(hash + "_" + fi.fileName());
    QFile out(outPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        out.write(sanitized.toUtf8());
        out.close();
        return outPath;
    }
    return path;
}
