// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QList>
#include <QMimeData>

QT_BEGIN_NAMESPACE
class QMimeData;
QT_END_NAMESPACE

namespace TextEditor::Internal {

// The mime type a column selection is copied as, and a copy of some clipboard
// data carrying the parts an editor cares about. Here rather than on the
// widget editor because the clipboard history is not a widget's: a view that
// keeps its own history has to be able to put things into it.
TEXTEDITOR_EXPORT const char *textBlockMimeType();
TEXTEDITOR_EXPORT QMimeData *duplicateMimeData(const QMimeData *source);

class CircularClipboard
{
public:
    static CircularClipboard *instance();

    void collect(const QMimeData *mimeData);
    void collect(const std::shared_ptr<const QMimeData> &mimeData);
    std::shared_ptr<const QMimeData> next() const;
    void toLastCollect();
    int size() const;
    // Everything the program has collected so far. For a test that has to
    // start from a history it knows, which it cannot otherwise: this is one
    // list for the whole program and every copy in every editor adds to it.
    void clear();

private:
    CircularClipboard();
    ~CircularClipboard();
    CircularClipboard &operator=(const CircularClipboard &);

    mutable int m_current = -1;
    QList<std::shared_ptr<const QMimeData>> m_items;
};

} // namespace TextEditor::Internal
