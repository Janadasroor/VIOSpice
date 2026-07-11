/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_state.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace IDE {

ExtensionState::ExtensionState(const QString& extensionDir, QObject* parent)
    : QObject(parent), m_extensionDir(extensionDir) {
    load();
}

QString ExtensionState::stateFilePath() const {
    return m_extensionDir + "/state.json";
}

QVariant ExtensionState::get(const QString& key, const QVariant& defaultValue) const {
    if (!m_loaded) {
        const_cast<ExtensionState*>(this)->load();
    }
    QJsonValue val = m_state.value(key);
    if (val.isUndefined()) return defaultValue;
    return val.toVariant();
}

void ExtensionState::set(const QString& key, const QVariant& value) {
    m_state[key] = QJsonValue::fromVariant(value);
    m_dirty = true;
    Q_EMIT stateChanged(key, value);
}

QString ExtensionState::getString(const QString& key, const QString& defaultValue) const {
    return get(key, defaultValue).toString();
}

void ExtensionState::setString(const QString& key, const QString& value) {
    set(key, value);
}

int ExtensionState::getInt(const QString& key, int defaultValue) const {
    return get(key, defaultValue).toInt();
}

void ExtensionState::setInt(const QString& key, int value) {
    set(key, value);
}

double ExtensionState::getDouble(const QString& key, double defaultValue) const {
    return get(key, defaultValue).toDouble();
}

void ExtensionState::setDouble(const QString& key, double value) {
    set(key, value);
}

QJsonObject ExtensionState::getAll() const {
    if (!m_loaded) {
        const_cast<ExtensionState*>(this)->load();
    }
    return m_state;
}

void ExtensionState::setAll(const QJsonObject& state) {
    m_state = state;
    m_dirty = true;
}

bool ExtensionState::load() {
    QString path = stateFilePath();
    QFile file(path);
    if (!file.exists()) {
        m_state = QJsonObject();
        m_loaded = true;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ExtState] Cannot read" << path;
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();

    if (parseErr.error != QJsonParseError::NoError) {
        qWarning() << "[ExtState] JSON parse error in" << path << parseErr.errorString();
        return false;
    }

    m_state = doc.object();
    m_loaded = true;
    m_dirty = false;
    return true;
}

bool ExtensionState::save() {
    QString path = stateFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ExtState] Cannot write" << path;
        return false;
    }

    QJsonDocument doc(m_state);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    m_dirty = false;
    return true;
}

void ExtensionState::reset() {
    m_state = QJsonObject();
    m_dirty = true;
    save();
}

bool ExtensionState::contains(const QString& key) const {
    return m_state.contains(key);
}

void ExtensionState::remove(const QString& key) {
    m_state.remove(key);
    m_dirty = true;
}

} // namespace IDE
