/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_config.h"
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

namespace IDE {

ExtensionConfig::ExtensionConfig(const QString& extensionDir, QObject* parent)
    : QObject(parent), m_extensionDir(extensionDir) {
    load();
}

QString ExtensionConfig::configFilePath() const {
    return m_extensionDir + "/config.json";
}

QVariant ExtensionConfig::get(const QString& key, const QVariant& defaultValue) const {
    if (!m_loaded) {
        const_cast<ExtensionConfig*>(this)->load();
    }
    QJsonValue val = m_settings.value(key);
    if (val.isUndefined()) return defaultValue;
    return val.toVariant();
}

void ExtensionConfig::set(const QString& key, const QVariant& value) {
    m_settings[key] = QJsonValue::fromVariant(value);
    Q_EMIT configChanged(key, value);
}

QJsonObject ExtensionConfig::getAll() const {
    if (!m_loaded) {
        const_cast<ExtensionConfig*>(this)->load();
    }
    return m_settings;
}

void ExtensionConfig::setAll(const QJsonObject& settings) {
    m_settings = settings;
}

bool ExtensionConfig::load() {
    QString path = configFilePath();
    QFile file(path);
    if (!file.exists()) {
        m_settings = QJsonObject();
        m_loaded = true;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ExtConfig] Cannot read" << path;
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();

    if (parseErr.error != QJsonParseError::NoError) {
        qWarning() << "[ExtConfig] JSON error in" << path << parseErr.errorString();
        return false;
    }

    m_settings = doc.object();
    m_loaded = true;
    return true;
}

bool ExtensionConfig::save() {
    QString path = configFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ExtConfig] Cannot write" << path;
        return false;
    }

    QJsonDocument doc(m_settings);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void ExtensionConfig::reset() {
    m_settings = QJsonObject();
    save();
}

bool ExtensionConfig::contains(const QString& key) const {
    return m_settings.contains(key);
}

void ExtensionConfig::remove(const QString& key) {
    m_settings.remove(key);
}

} // namespace IDE
