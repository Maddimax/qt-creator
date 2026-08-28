// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "blockselection.h"

#include "tabsettings.h"

#include <QTextBlock>
#include <QTextDocument>

namespace TextEditor {

QList<QTextCursor> cursorsForBlockSelection(
    QTextDocument *document, const TabSettingsData &tabSettings, const BlockSelection &selection)
{
    QList<QTextCursor> result;
    if (!document)
        return result;

    QTextBlock block = document->findBlockByNumber(selection.anchorBlockNumber);
    if (!block.isValid())
        return result;
    QTextCursor cursor(block);
    cursor.setPosition(block.position()
                       + tabSettings.positionAtColumn(block.text(), selection.anchorColumn));

    // Which way the rectangle was drawn, so that the cursors come out in that
    // order and the caret ends up under the pointer rather than at the corner
    // it started from.
    const bool forward = selection.blockNumber > selection.anchorBlockNumber
                         || (selection.blockNumber == selection.anchorBlockNumber
                             && selection.column == selection.anchorColumn);

    while (block.isValid()) {
        const QString &blockText = block.text();
        const int columnCount = tabSettings.columnCountForText(blockText);
        if (selection.anchorColumn <= columnCount || selection.column <= columnCount) {
            const int anchor = tabSettings.positionAtColumn(blockText, selection.anchorColumn);
            const int position = tabSettings.positionAtColumn(blockText, selection.column);
            cursor.setPosition(block.position() + anchor);
            cursor.setPosition(block.position() + position, QTextCursor::KeepAnchor);
            result.append(cursor);
        }
        if (block.blockNumber() == selection.blockNumber)
            break;
        block = forward ? block.next() : block.previous();
    }
    return result;
}

} // namespace TextEditor
