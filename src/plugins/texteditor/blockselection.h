// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QList>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace TextEditor {

class TabSettingsData;

// A rectangle of text, as the two corners it was dragged between. Columns
// rather than positions: a tab is one character and several columns wide, and
// what is selected is what lines up on screen.
class TEXTEDITOR_EXPORT BlockSelection
{
public:
    int blockNumber = -1;
    int column = -1;
    int anchorBlockNumber = -1;
    int anchorColumn = -1;
};

// One cursor per line of the rectangle, in the order the selection was made,
// so that the last one is where the caret ended up. Lines too short to reach
// either column carry no cursor at all rather than an empty one.
TEXTEDITOR_EXPORT QList<QTextCursor> cursorsForBlockSelection(
    QTextDocument *document, const TabSettingsData &tabSettings,
    const BlockSelection &selection);

} // namespace TextEditor
