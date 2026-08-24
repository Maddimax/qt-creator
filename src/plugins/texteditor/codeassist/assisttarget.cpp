// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assisttarget.h"

#include <texteditor/texteditor.h>

namespace TextEditor {

AssistTarget::~AssistTarget() = default;

void AssistTarget::setAutoCompleteSkipPosition(const QTextCursor &cursor)
{
    Q_UNUSED(cursor)
}

void AssistTarget::encourageApply() {}

WidgetAssistTarget::WidgetAssistTarget(TextEditorWidget *widget)
    : m_widget(widget)
{}

QTextDocument *WidgetAssistTarget::document() const
{
    return m_widget->document();
}

int WidgetAssistTarget::position() const
{
    return m_widget->position();
}

QChar WidgetAssistTarget::characterAt(int position) const
{
    return m_widget->characterAt(position);
}

QString WidgetAssistTarget::textAt(int position, int length) const
{
    return m_widget->textAt(position, length);
}

QTextCursor WidgetAssistTarget::textCursor() const
{
    return m_widget->textCursor();
}

QTextCursor WidgetAssistTarget::textCursorAt(int position) const
{
    return m_widget->textCursorAt(position);
}

void WidgetAssistTarget::setCursorPosition(int position)
{
    m_widget->setCursorPosition(position);
}

void WidgetAssistTarget::replace(int position, int length, const QString &text)
{
    m_widget->replace(position, length, text);
}

void WidgetAssistTarget::insertCodeSnippet(int basePosition,
                                           const QString &snippet,
                                           const SnippetParser &parse)
{
    m_widget->insertCodeSnippet(basePosition, snippet, parse);
}

void WidgetAssistTarget::setAutoCompleteSkipPosition(const QTextCursor &cursor)
{
    m_widget->setAutoCompleteSkipPosition(cursor);
}

void WidgetAssistTarget::encourageApply()
{
    m_widget->encourageApply();
}

} // namespace TextEditor
