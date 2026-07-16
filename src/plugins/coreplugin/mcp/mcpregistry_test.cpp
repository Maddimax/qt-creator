// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcpregistry_test.h"

#include "mcpregistry.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTest>
#include <QTimer>
#include <QUrl>

namespace Core::Internal {

static const char MCP_REGISTRY_URL[]
    = "https://qtccache.qt.io/mcp/registry.json";
static const int MCP_REGISTRY_TIMEOUT_MS = 30000;

class McpRegistryTest final : public QObject
{
    Q_OBJECT

private slots:
    void testReadRegistryFromWeb();
};

void McpRegistryTest::testReadRegistryFromWeb()
{
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(MCP_REGISTRY_URL)));

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(MCP_REGISTRY_TIMEOUT_MS);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(
        &timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.start();
    loop.exec();

    QVERIFY2(
        timeoutTimer.isActive(),
        "Timed out waiting for registry.json from the web");
    timeoutTimer.stop();

    QCOMPARE(reply->error(), QNetworkReply::NoError);

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QVERIFY(!data.isEmpty());

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());

    using McpReg = McpRegistry::McpRegistry;
    const Utils::Result<McpReg> registry
        = McpRegistry::fromJson<McpReg>(doc.object());
    QVERIFY2(registry.has_value(), qPrintable(registry.error()));
    QVERIFY(registry->count() > 0);
    QVERIFY(!registry->servers().isEmpty());
    QVERIFY(!registry->generated_at().isEmpty());
}

QObject *createMcpRegistryTest()
{
    return new McpRegistryTest;
}

} // namespace Core::Internal

#include "mcpregistry_test.moc"
