// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mcpserver_global.h"

#include "../schemas/schema_2025_11_25.h"
#include "mcpserver.h"

#include <QList>

namespace Mcp {

class MCPSERVER_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool,
        const Server::ToolInterfaceCallback &callback);
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool, const Server::ToolCallback &callback);

    static const ToolRegistry &instance();

    static void enableTool(const QString &toolName, bool enabled);
    static QList<Schema::Tool> registeredTools();

    // Invokes a registered synchronous tool by name and returns its result.
    // Intended for tests; asynchronous (ToolInterface) tools cannot be driven
    // this way and return an error.
    static Utils::Result<Schema::CallToolResult> callToolForTests(
        const QString &name, const Schema::CallToolRequestParams &params);

    // Invokes a registered tool of either kind and hands its result to \a done
    // when it arrives - immediately for a synchronous one, whenever it
    // finishes for an asynchronous one. \a done is not called at all if the
    // tool never finishes; a test has to bound its own wait.
    static void callToolForTests(
        const QString &name,
        const Schema::CallToolRequestParams &params,
        const std::function<void(const Utils::Result<Schema::CallToolResult> &)> &done);

signals:
    void toolRegistered();
    void toolEnabled(const QString &toolName, bool enabled);
};

class MCPSERVER_EXPORT AutoRegisteringServer : public Server, public QObject
{
public:
    AutoRegisteringServer(Generated::Schema::_2025_11_25::Implementation serverInfo);

private:
    std::size_t m_nTools = 0;
};

} // namespace Mcp
