// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QProcess;
class QTcpServer;
class QTcpSocket;
QT_END_NAMESPACE

namespace Valgrind::Test {

class ValgrindMemcheckParserTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void testMemcheckSample1();
    void testMemcheckSample2();
    void testMemcheckSample3();
    void testMemcheckCharm();
    void testHelgrindSample1();

    void testValgrindCrash();
    void testValgrindGarbage();

    void testParserStop();
    void testRealValgrind();
    void testValgrindStartError_data();
    void testValgrindStartError();

private:
    void initTest(const QString &testfile, const QStringList &otherArgs = {});

    QTcpServer *m_server = nullptr;
    QProcess *m_process = nullptr;
    QTcpSocket *m_socket = nullptr;
};

} // namespace Valgrind::Test
