// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assisttarget.h"

#include <QGuiApplication>

#include <QClipboard>

#include <texteditor/texteditor.h>
#include <texteditor/snippets/snippetparser.h>

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
    // A snippet's placeholders are the widget editor's overlay, and a plain
    // view has none - but the markup around them is not text anybody asked
    // for. Inserting the string as it stands wrote "$var$" into the file for
    // Creator's own snippets and "${1:int}" for a language server's, so the
    // markup has to come out even where the placeholders cannot be offered.
    if (!parse) {
        replace(basePosition, m_cursor.position() - basePosition, snippet);
        return;
    }

    const SnippetParseResult result = parse(snippet);
    // The widget editor shows the error and inserts nothing. There is no
    // window to show it in here, and leaving the text alone is still better
    // than writing markup into it.
    if (!std::holds_alternative<ParsedSnippet>(result))
        return;

    const ParsedSnippet parsed = std::get<ParsedSnippet>(result);

    // Written part by part, recording where each hole landed. The cursors
    // come afterwards: one built while the text is still going in sits exactly
    // where the next part is inserted and gets carried along with it. The
    // widget editor builds its cursors after the same loop, for the same
    // reason.
    QTextCursor writer(m_document);
    writer.setPosition(basePosition);
    writer.setPosition(m_cursor.position(), QTextCursor::KeepAnchor);
    writer.beginEditBlock();
    writer.removeSelectedText();

    const int from = writer.position();

    struct Written
    {
        int start = 0;
        int end = 0;
        int variableIndex = -1;
        bool finalPart = false;
        NameMangler *mangler = nullptr;
    };
    QList<Written> written;

    for (const ParsedSnippet::Part &part : parsed.parts) {
        const int partFrom = writer.position();
        writer.insertText(part.text);
        if (part.variableIndex >= 0) {
            written.append({partFrom, writer.position(), part.variableIndex,
                            part.finalPart, part.mangler});
        }
    }

    // Held over the indent, which moves the text under them.
    QList<QTextCursor> held;
    held.reserve(written.size());
    for (const Written &hole : std::as_const(written)) {
        QTextCursor over(m_document);
        over.setPosition(hole.start);
        over.setPosition(hole.end, QTextCursor::KeepAnchor);
        held.append(over);
    }

    // A snippet is written for column zero and inserted wherever the caret
    // happens to be, so every line after the first lands short without this.
    autoIndentRange(from, writer.position());

    writer.endEditBlock();
    m_cursor = writer;

    QList<SnippetPlaceholder> placeholders;
    for (int i = 0; i < written.size(); ++i) {
        placeholders.append({held.at(i).selectionStart(),
                             held.at(i).selectionEnd(),
                             written.at(i).variableIndex,
                             written.at(i).finalPart,
                             written.at(i).mangler});
    }

    if (placeholders.isEmpty())
        return;

    // Where the reader would type first: the overlay would have selected this
    // placeholder, and the caret standing on it is what is left of that.
    setCursorPosition(placeholders.first().start);
    snippetInserted(placeholders);
}

void DocumentAssistTarget::autoIndentRange(int from, int to)
{
    Q_UNUSED(from)
    Q_UNUSED(to)
}

void DocumentAssistTarget::snippetInserted(const QList<SnippetPlaceholder> &placeholders)
{
    Q_UNUSED(placeholders)
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
