/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "update_checker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

UpdateChecker::UpdateChecker(const QString &currentVersion, QObject *parent)
    : QObject(parent)
    , m_currentVersion(currentVersion)
{
}

void UpdateChecker::checkAsync()
{
    QNetworkRequest req(QUrl("https://api.github.com/repos/Janadasroor/VioraEDA/releases/latest"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "VioraEDA/" + m_currentVersion);

    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        QString tag = obj["tag_name"].toString();

        if (tag.isEmpty())
            return;

        QString version = tag;
        if (version.startsWith('v'))
            version = version.mid(1);

        if (compareVersions(version, m_currentVersion) > 0) {
            QString url = obj["html_url"].toString();
            if (url.isEmpty())
                url = "https://github.com/Janadasroor/VioraEDA/releases/tag/" + tag;
            emit updateAvailable(version, url);
        }
    });
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    QStringList pa = a.split('.');
    QStringList pb = b.split('.');
    int len = qMax(pa.size(), pb.size());

    for (int i = 0; i < len; ++i) {
        int na = (i < pa.size()) ? pa[i].toInt() : 0;
        int nb = (i < pb.size()) ? pb[i].toInt() : 0;
        if (na != nb)
            return na - nb;
    }
    return 0;
}
