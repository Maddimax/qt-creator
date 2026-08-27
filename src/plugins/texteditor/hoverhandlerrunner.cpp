// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "hoverhandlerrunner.h"

#include "basehoverhandler.h"

#include <utils/qtcassert.h>
#include <utils/textutils.h>

namespace TextEditor {

bool HoverHandlerRunner::LastHandlerInfo::applies(int documentRevision,
                                                 int cursorPosition,
                                                 HoverTarget *target) const
{
    return handler && handler->lastHelpItemAppliesTo(target)
           && documentRevision == this->documentRevision
           && cursorPosition == this->cursorPosition;
}

HoverHandlerRunner::HoverHandlerRunner(HoverTarget *target,
                                       const QList<BaseHoverHandler *> &handlers)
    : m_target(target)
    , m_handlers(handlers)
{}

HoverHandlerRunner::~HoverHandlerRunner()
{
    abortHandlers();
}

void HoverHandlerRunner::startChecking(const QTextCursor &textCursor,
                                       const Callback &callback,
                                       const FallbackCallback &fallbackCallback)
{
    if (m_handlers.empty()) {
        fallbackCallback(m_target);
        return;
    }

    // Does the last handler still apply?
    const int documentRevision = textCursor.document()->revision();
    const int position = Utils::Text::wordStartCursor(textCursor).position();
    if (m_lastHandlerInfo.applies(documentRevision, position, m_target)) {
        callback(m_target, m_lastHandlerInfo.handler, position);
        return;
    }

    if (isCheckRunning(documentRevision, position))
        return;

    // Update invocation data
    m_documentRevision = documentRevision;
    m_position = position;
    m_callback = callback;
    m_fallbackCallback = fallbackCallback;

    restart();
}

bool HoverHandlerRunner::isCheckRunning(int documentRevision, int position) const
{
    return m_currentHandlerIndex >= 0 && m_documentRevision == documentRevision
           && m_position == position;
}

void HoverHandlerRunner::checkNext()
{
    QTC_ASSERT(m_currentHandlerIndex >= 0, return);
    QTC_ASSERT(m_currentHandlerIndex < m_handlers.size(), return);
    BaseHoverHandler *currentHandler = m_handlers[m_currentHandlerIndex];

    currentHandler->checkPriority(m_target, m_position, [this](int priority) {
        onHandlerFinished(m_documentRevision, m_position, priority);
    });
}

void HoverHandlerRunner::onHandlerFinished(int documentRevision, int position, int priority)
{
    QTC_ASSERT(m_currentHandlerIndex >= 0, return);
    QTC_ASSERT(m_currentHandlerIndex < m_handlers.size(), return);
    QTC_ASSERT(documentRevision == m_documentRevision, return);
    QTC_ASSERT(position == m_position, return);

    BaseHoverHandler *currentHandler = m_handlers[m_currentHandlerIndex];
    if (priority > m_highestHandlerPriority) {
        m_highestHandlerPriority = priority;
        m_bestHandler = currentHandler;
    }

    // There are more, check next
    ++m_currentHandlerIndex;
    if (m_currentHandlerIndex < m_handlers.size()) {
        checkNext();
        return;
    }
    m_currentHandlerIndex = -1;

    // All were queried, run the best
    if (m_bestHandler) {
        m_lastHandlerInfo = LastHandlerInfo(m_bestHandler, m_documentRevision, m_position);
        m_callback(m_target, m_bestHandler, m_position);
    } else {
        m_fallbackCallback(m_target);
    }
}

void HoverHandlerRunner::handlerRemoved(BaseHoverHandler *handler)
{
    if (m_lastHandlerInfo.handler == handler)
        m_lastHandlerInfo = LastHandlerInfo();
    if (m_currentHandlerIndex >= 0)
        restart();
}

void HoverHandlerRunner::abortHandlers()
{
    for (BaseHoverHandler *handler : m_handlers)
        handler->abort();
    m_currentHandlerIndex = -1;
}

void HoverHandlerRunner::restart()
{
    abortHandlers();

    if (m_handlers.empty())
        return;

    // Re-initialize process data
    m_currentHandlerIndex = 0;
    m_bestHandler = nullptr;
    m_highestHandlerPriority = BaseHoverHandler::Priority_None;

    // Start checking
    checkNext();
}

} // namespace TextEditor
