// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assisttarget.h"

#include <QGuiApplication>

#include <QClipboard>

#include <texteditor/texteditor.h>

#include <QTextDocument>

namespace TextEditor {

AssistTarget::~AssistTarget() = default;

void AssistTarget::setAutoCompleteSkipPosition(const QTextCursor &cursor)
{
    Q_UNUSED(cursor)
}

void AssistTarget::encourageApply() {}

void AssistTarget::paste()
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    cursor.insertText(QGuiApplication::clipboard()->text());
    setCursorPosition(cursor.position());
}

DocumentAssistTarget::DocumentAssistTarget(QTextDocument *document)
    : m_document(document)
    , m_cursor(document)
{}

QTextDocument *DocumentAssistTarget::document() const
{
    return m_document;
}

int DocumentAssistTarget::position() const
{
    return m_cursor.position();
}

QChar DocumentAssistTarget::characterAt(int position) const
{
    return m_document->characterAt(position);
}

QString DocumentAssistTarget::textAt(int position, int length) const
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

QTextCursor DocumentAssistTarget::textCursor() const
{
    return m_cursor;
}

QTextCursor DocumentAssistTarget::textCursorAt(int position) const
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    return cursor;
}

void DocumentAssistTarget::setCursorPosition(int position)
{
    m_cursor.setPosition(position);
}

void DocumentAssistTarget::replace(int position, int length, const QString &text)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    m_cursor = cursor;
}

void DocumentAssistTarget::insertCodeSnippet(int basePosition,
                                             const QString &snippet,
                                             const SnippetParser &parse)
{
    Q_UNUSED(parse)
    replace(basePosition, m_cursor.position() - basePosition, snippet);
}

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

void WidgetAssistTarget::paste()
{
    m_widget->paste();
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
