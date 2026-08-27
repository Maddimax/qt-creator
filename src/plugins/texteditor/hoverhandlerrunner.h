// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QList>
#include <QTextCursor>

#include <functional>

namespace TextEditor {

class BaseHoverHandler;
class HoverTarget;

// Asks every hover handler what it would show at a position and runs the one
// that wants it most. Handlers may answer asynchronously, so this is a state
// machine rather than a loop.
class TEXTEDITOR_EXPORT HoverHandlerRunner
{
public:
    using Callback = std::function<void(HoverTarget *, BaseHoverHandler *, int)>;
    using FallbackCallback = std::function<void(HoverTarget *)>;

    HoverHandlerRunner(HoverTarget *target, const QList<BaseHoverHandler *> &handlers);
    ~HoverHandlerRunner();

    void startChecking(const QTextCursor &textCursor,
                       const Callback &callback,
                       const FallbackCallback &fallbackCallback);

    void handlerRemoved(BaseHoverHandler *handler);
    void abortHandlers();

private:
    void restart();
    void checkNext();
    void onHandlerFinished(int documentRevision, int position, int priority);
    bool isCheckRunning(int documentRevision, int position) const;

    HoverTarget *m_target;
    const QList<BaseHoverHandler *> &m_handlers;

    struct LastHandlerInfo
    {
        LastHandlerInfo() = default;
        LastHandlerInfo(BaseHoverHandler *handler, int documentRevision, int cursorPosition)
            : handler(handler)
            , documentRevision(documentRevision)
            , cursorPosition(cursorPosition)
        {}

        bool applies(int documentRevision, int cursorPosition, HoverTarget *target) const;

        BaseHoverHandler *handler = nullptr;
        int documentRevision = -1;
        int cursorPosition = -1;
    } m_lastHandlerInfo;

    // invocation data
    Callback m_callback;
    FallbackCallback m_fallbackCallback;
    int m_position = -1;
    int m_documentRevision = -1;

    // processing data
    int m_currentHandlerIndex = -1;
    int m_highestHandlerPriority = 0;
    BaseHoverHandler *m_bestHandler = nullptr;
};

} // namespace TextEditor
