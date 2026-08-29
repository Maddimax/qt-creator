// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QObject>
#include <utils/link.h>

#include <QTextCursor>

namespace Core { class IEditor; }

namespace TextEditor {

// What a view asks when the question needs to know the language: where else a
// symbol is used, what it should be called, who calls it. The view does not
// answer any of them; something that understands the file does.
//
// A relay object rather than signals on the view, because the view that has
// them is a QQuickItem and the plugins that answer - the language client
// today - have no Qt Quick of their own and should not grow one to be able to
// connect. The widget editor emits the same three from itself, which is why
// there are two connections in LanguageClientManager rather than one.
class TEXTEDITOR_EXPORT SymbolRequests : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void askForUsages(const QTextCursor &cursor) { emit requestUsages(cursor); }
    void askForRename(const QTextCursor &cursor) { emit requestRename(cursor); }
    void askForCallHierarchy() { emit requestCallHierarchy(); }
    void askForTypeAt(const QTextCursor &cursor, const Utils::LinkHandler &callback,
                      bool resolveTarget, bool inNextSplit)
    {
        emit requestTypeAt(cursor, callback, resolveTarget, inNextSplit);
    }

signals:
    void requestUsages(const QTextCursor &cursor);
    void requestRename(const QTextCursor &cursor);
    void requestCallHierarchy();
    // Where the *type* of the symbol under \a cursor is defined. Unlike the
    // three above this one answers back, so it carries the callback the way
    // the widget editor's signal of the same name does.
    void requestTypeAt(const QTextCursor &cursor, const Utils::LinkHandler &callback,
                       bool resolveTarget, bool inNextSplit);
};

// The relay for \a editor, or nullptr when that editor asks its questions some
// other way - the widget editor emits them itself.
TEXTEDITOR_EXPORT SymbolRequests *symbolRequestsForEditor(Core::IEditor *editor);

} // namespace TextEditor
