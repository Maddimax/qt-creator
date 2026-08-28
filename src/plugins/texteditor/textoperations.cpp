// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textoperations.h"

#include "textdocument.h"

#include <utils/multitextcursor.h>

#include <QRegularExpression>
#include <QTextBlock>

namespace TextEditor {

void joinLines(Utils::MultiTextCursor &cursor)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        QTextCursor start = c;
        QTextCursor end = c;

        start.setPosition(c.selectionStart());
        end.setPosition(c.selectionEnd() - 1);

        int lineCount = qMax(1, end.blockNumber() - start.blockNumber());

        c.setPosition(c.selectionStart());
        while (lineCount--) {
            c.movePosition(QTextCursor::NextBlock);
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QString cutLine = c.selectedText();

            // Collapse leading whitespaces to one or insert whitespace
            static const QRegularExpression regexp("^\\s*");
            cutLine.replace(regexp, QLatin1String(" "));
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            c.removeSelectedText();

            c.movePosition(QTextCursor::PreviousBlock);
            c.movePosition(QTextCursor::EndOfBlock);

            c.insertText(cutLine);
        }
    }
    cursor.endEditBlock();
    cursor.mergeCursors();
}

void transformSelection(Utils::MultiTextCursor &cursor, const TextTransformation &transform)
{
    const bool several = cursor.hasMultipleCursors();
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        const int pos = c.position();
        const int anchor = c.anchor();

        if (!c.hasSelection() && !several)
            c.select(QTextCursor::WordUnderCursor);

        const QString text = c.selectedText();
        const QString transformed = transform(text);
        if (transformed == text)
            continue;

        c.insertText(transformed);

        // Select the changed text again, which assumes the transformation did
        // not change its length.
        c.setPosition(anchor);
        c.setPosition(pos, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

void insertLineAbove(Utils::MultiTextCursor &cursor, TextDocument *document)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        // At the very start of the document there is no previous block to
        // open one after, so the line is opened here and stepped back onto.
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
        c.insertBlock();
        c.movePosition(QTextCursor::PreviousBlock, QTextCursor::MoveAnchor);
        if (document)
            document->autoIndent(c);
    }
    cursor.endEditBlock();
}

void insertLineBelow(Utils::MultiTextCursor &cursor, TextDocument *document)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
        c.insertBlock();
        if (document)
            document->autoIndent(c);
    }
    cursor.endEditBlock();
}

} // namespace TextEditor
