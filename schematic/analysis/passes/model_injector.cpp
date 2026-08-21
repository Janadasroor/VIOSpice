/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_injector.h"
#include <QFile>
#include <QCryptographicHash>
#include <QRegularExpression>
#include "xspice_block_translator.h"
#include "spice_compat_rewriter.h"
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

        const QString otaTranslation = SpiceCompatRewriter::buildNgspiceOtaTranslation(line);
        if (!otaTranslation.isEmpty()) {
            outLines.append(otaTranslation);
            continue;
        }

        QString sanitizedLine = line;
        sanitizedLine = SpiceCompatRewriter::rewriteLtBehavioralFunctions(sanitizedLine, nullptr);
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
