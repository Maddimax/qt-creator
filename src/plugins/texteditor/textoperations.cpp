// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textoperations.h"

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

} // namespace TextEditor
