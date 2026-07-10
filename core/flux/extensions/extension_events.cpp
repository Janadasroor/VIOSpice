/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_events.h"
#include <QDebug>
#include <QDateTime>

namespace IDE {

ExtensionEventBus& eventBus() {
    static ExtensionEventBus instance;
    return instance;
}

ExtensionEventBus::ExtensionEventBus(QObject* parent)
    : QObject(parent) {
}

void ExtensionEventBus::subscribe(const QString& extensionId, const QString& eventName,
                                  std::function<void(const ExtensionEvent&)> callback) {
    EventSubscription sub;
    sub.extensionId = extensionId;
    sub.eventName = eventName;
    sub.callback = callback;

    m_subscriptions[eventName].append(sub);
    qDebug() << "[EventBus]" << extensionId << "subscribed to" << eventName;
}

void ExtensionEventBus::unsubscribe(const QString& extensionId, const QString& eventName) {
    auto& subs = m_subscriptions[eventName];
    for (int i = subs.size() - 1; i >= 0; --i) {
        if (subs[i].extensionId == extensionId) {
            subs.removeAt(i);
        }
    }
}

void ExtensionEventBus::unsubscribeAll(const QString& extensionId) {
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        auto& subs = it.value();
        for (int i = subs.size() - 1; i >= 0; --i) {
            if (subs[i].extensionId == extensionId) {
                subs.removeAt(i);
            }
        }
    }
}

void ExtensionEventBus::emitEvent(const QString& senderId, const QString& eventName,
                                  const QVariant& data) {
    ExtensionEvent event;
    event.name = eventName;
    event.senderId = senderId;
    event.data = data;
    event.timestamp = QDateTime::currentMSecsSinceEpoch();

    qDebug() << "[EventBus]" << senderId << "emitted" << eventName;

    // Notify all subscribers for this event
    auto it = m_subscriptions.find(eventName);
    if (it != m_subscriptions.end()) {
        for (const auto& sub : it.value()) {
            // Don't send event back to sender
            if (sub.extensionId != senderId) {
                sub.callback(event);
            }
        }
    }

    // Also notify wildcard subscribers (subscribed to "*")
    auto wildcard = m_subscriptions.find("*");
    if (wildcard != m_subscriptions.end()) {
        for (const auto& sub : wildcard.value()) {
            if (sub.extensionId != senderId) {
                sub.callback(event);
            }
        }
    }

    Q_EMIT eventEmitted(senderId, eventName, data);
}

QStringList ExtensionEventBus::subscribedEvents(const QString& extensionId) const {
    QStringList events;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        for (const auto& sub : it.value()) {
            if (sub.extensionId == extensionId) {
                events.append(it.key());
                break;
            }
        }
    }
    return events;
}

int ExtensionEventBus::subscriptionCount() const {
    int count = 0;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        count += it.value().size();
    }
    return count;
}

void ExtensionEventBus::clearAll() {
    m_subscriptions.clear();
}

} // namespace IDE
