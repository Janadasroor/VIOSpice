/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_EVENTS_H
#define EXTENSION_EVENTS_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QVariant>
#include <functional>

namespace IDE {

// Event data structure
struct ExtensionEvent {
    QString name;           // Event name (e.g., "simulation.started")
    QString senderId;       // Extension ID that emitted the event
    QVariant data;          // Event payload
    qint64 timestamp;       // When the event was emitted
};

// Event subscription
struct EventSubscription {
    QString extensionId;    // Extension that subscribed
    QString eventName;      // Event name to listen for
    std::function<void(const ExtensionEvent&)> callback;
};

// Inter-extension event bus
class ExtensionEventBus : public QObject {
    Q_OBJECT
public:
    explicit ExtensionEventBus(QObject* parent = nullptr);

    // Subscribe to an event
    void subscribe(const QString& extensionId, const QString& eventName,
                   std::function<void(const ExtensionEvent&)> callback);

    // Unsubscribe from an event
    void unsubscribe(const QString& extensionId, const QString& eventName);

    // Unsubscribe all callbacks for an extension
    void unsubscribeAll(const QString& extensionId);

    // Emit an event
    void emitEvent(const QString& senderId, const QString& eventName,
                   const QVariant& data = QVariant());

    // Get list of subscribed events for an extension
    QStringList subscribedEvents(const QString& extensionId) const;

    // Get list of all active subscriptions
    int subscriptionCount() const;

    // Clear all subscriptions
    void clearAll();

signals:
    void eventEmitted(const QString& senderId, const QString& eventName, const QVariant& data);

private:
    // eventName -> list of subscriptions
    QMap<QString, QList<EventSubscription>> m_subscriptions;
};

// Global event bus instance
ExtensionEventBus& eventBus();

} // namespace IDE

#endif // EXTENSION_EVENTS_H
