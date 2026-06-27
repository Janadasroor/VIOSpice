/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(const QString &currentVersion, QObject *parent = nullptr);

    void checkAsync();

signals:
    void updateAvailable(const QString &latestVersion, const QString &downloadUrl);

private:
    static int compareVersions(const QString &a, const QString &b);

    QString m_currentVersion;
    QNetworkAccessManager m_net;
};
