// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/multitextcursor.h>

#include <QObject>

#include <memory>

namespace Core { class IEditor; }

namespace TextEditor {

class TextDocument;
class TextSuggestion;

// A view something can offer an inline suggestion to: enough of it to decide
// whether asking is worthwhile, and somewhere to put the answer.
//
// The counterpart of SuggestionTarget, which is what *taking* a suggestion
// needs. This is what *making* one needs, and it is a QObject because whoever
// offers has to know when the caret moved away from what it asked about, and
// when the view is gone.
//
// It used to be TextEditorWidget, so Copilot - the only thing that offers
// suggestions - could only offer them to a widget. Nothing it asks for is a
// widget's to answer.
class TEXTEDITOR_EXPORT SuggestionHost : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual TextDocument *textDocument() const = 0;
    virtual QTextDocument *document() const = 0;
    virtual QTextCursor textCursor() const = 0;
    virtual Utils::MultiTextCursor multiTextCursor() const = 0;
    virtual bool isReadOnly() const = 0;
    // Whether one is already being shown, which is a reason not to ask for
    // another.
    virtual bool suggestionVisible() const = 0;
    virtual void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion) = 0;

signals:
    void cursorPositionChanged();
};

// The handle for \a editor, or nullptr when it is not an editor a suggestion
// can be shown in. Owned by the view, so it dies with it - which is the
// notification that the view is gone.
TEXTEDITOR_EXPORT SuggestionHost *suggestionHostForEditor(Core::IEditor *editor);
// For the editor the reader is in, if that is one at all.
TEXTEDITOR_EXPORT SuggestionHost *currentSuggestionHost();

} // namespace TextEditor
