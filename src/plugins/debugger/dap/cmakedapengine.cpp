// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "cmakedapengine.h"

#include <debugger/debuggermainwindow.h>

#include <utils/temporarydirectory.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/projecttree.h>

#include <QDebug>
#include <QLocalSocket>
#include <QLoggingCategory>

using namespace Core;
using namespace Utils;
static Q_LOGGING_CATEGORY(dapEngineLog, "qtc.dbg.dapengine", QtWarningMsg)

namespace Debugger::Internal {

class LocalSocketDataProvider : public IDataProvider
{
public:
    LocalSocketDataProvider(const QString &socketName)
        : m_socketName(socketName)
    {
        connect(&m_socket, &QLocalSocket::connected, this, &IDataProvider::started);
        connect(&m_socket, &QLocalSocket::disconnected, this, &IDataProvider::done);
        connect(&m_socket, &QLocalSocket::readyRead, this, &IDataProvider::readyReadStandardOutput);
        connect(&m_socket,
                &QLocalSocket::errorOccurred,
                this,
                &IDataProvider::readyReadStandardError);
    }

    ~LocalSocketDataProvider() { m_socket.disconnectFromServer(); }

    void start() override { m_socket.connectToServer(m_socketName, QIODevice::ReadWrite); }

    bool isRunning() const override { return m_socket.isOpen(); }
    void writeRaw(const QByteArray &data) override { m_socket.write(data); }
    void kill() override
    {
        if (m_socket.isOpen())
            m_socket.disconnectFromServer();
        else {
            m_socket.abort();
            emit done();
        }
    }
    QByteArray readAllStandardOutput() override { return m_socket.readAll(); }
    QString readAllStandardError() override { return QString(); }
    int exitCode() const override { return 0; }
    QString executable() const override { return m_socket.serverName(); }

    QProcess::ExitStatus exitStatus() const override { return QProcess::NormalExit; }
    QProcess::ProcessError error() const override { return QProcess::UnknownError; }
    Utils::ProcessResult result() const override { return ProcessResult::FinishedWithSuccess; }
    QString exitMessage() const override { return QString(); };

private:
    QLocalSocket m_socket;
    const QString m_socketName;
};

CMakeDapEngine::CMakeDapEngine()
    : DapEngine()
{
    setObjectName("CmakeDapEngine");
    setDebuggerName("CmakeDAP");
}

void CMakeDapEngine::handleDapStarted()
{
    QTC_ASSERT(state() == EngineRunRequested, qCDebug(dapEngineLog) << state());

    postDirectCommand({
        {"command", "initialize"},
        {"type", "request"},
        {"arguments", QJsonObject {
            {"clientID",  "QtCreator"}, // The ID of the client using this adapter.
            {"clientName",  "QtCreator"}, //  The human-readable name of the client using this adapter.
            {"adapterID", "cmake"},
            {"pathFormat", "path"}
        }}
    });

    qCDebug(dapEngineLog) << "handleDapStarted";
}

void CMakeDapEngine::setupEngine()
{
    QTC_ASSERT(state() == EngineSetupRequested, qCDebug(dapEngineLog) << state());

    qCDebug(dapEngineLog) << "build system name"
                          << ProjectExplorer::ProjectTree::currentBuildSystem()->name();

    if (TemporaryDirectory::masterDirectoryFilePath().osType() == Utils::OsType::OsTypeWindows) {
        m_dataGenerator = std::make_unique<LocalSocketDataProvider>("\\\\.\\pipe\\cmake-dap");
    } else {
        m_dataGenerator = std::make_unique<LocalSocketDataProvider>(
            TemporaryDirectory::masterDirectoryPath() + "/cmake-dap.sock");
    }
    connectDataGeneratorSignals();

    connect(ProjectExplorer::ProjectTree::currentBuildSystem(),
            &ProjectExplorer::BuildSystem::debuggingStarted,
            this,
            [this] { m_dataGenerator->start(); });

    ProjectExplorer::ProjectTree::currentBuildSystem()->requestDebugging();
    notifyEngineSetupOk();
}

} // namespace Debugger::Internal
