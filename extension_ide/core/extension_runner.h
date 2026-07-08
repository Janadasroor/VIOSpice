/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_RUNNER_H
#define EXTENSION_RUNNER_H

#include <QObject>
#include <QMap>

namespace IDE {

class ExtensionRunner : public QObject {
    Q_OBJECT
public:
    explicit ExtensionRunner(QObject* parent = nullptr);

    bool runSource(const QString& source);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void outputReceived(const QString& message);
    void errorReceived(const QString& message);
    void runStarted();
    void runFinished(bool success);

private:
    void parseAndHighlightErrors(const QString& error);

    bool m_running = false;
};

} // namespace IDE

#endif // EXTENSION_RUNNER_H
