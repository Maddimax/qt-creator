// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "emacskeysstate.h"

#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>

#include <texteditor/texteditor.h>

#include <utils/plaintextedit/plaintextedit.h>

#include <QTextCursor>

using namespace Utils;

namespace EmacsKeys::Internal {

//---------------------------------------------------------------------------
// EmacsKeysState
//---------------------------------------------------------------------------

EmacsKeysState::EmacsKeysState(Core::IEditor *editor):
    m_ignore3rdParty(false),
    m_mark(-1),
    m_lastAction(KeysAction3rdParty),
    m_editor(editor)
{
    // What "somebody other than us touched this" looks like from outside the
    // view: the caret moved, or the text changed. The editor and the document
    // both say so whichever view is showing them.
    connect(editor, &Core::IEditor::cursorPositionChanged,
            this, &EmacsKeysState::cursorPositionChanged);
    if (Core::IDocument * const document = editor->document()) {
        connect(document, &Core::IDocument::contentsChanged,
                this, &EmacsKeysState::textChanged);
    }
    // A selection can change without the caret moving - something selects the
    // word the caret is already at the end of - and the mark has to be given
    // up then too. Asked of the editor, which says so whichever view it is:
    // this used to reach for a PlainTextEdit inside the widget, and a Qt Quick
    // editor has none, so on a C++ file the mark outlived a selection it
    // should not have.
    connect(editor, &Core::IEditor::selectionChanged,
            this, &EmacsKeysState::selectionChanged);
}

EmacsKeysState::~EmacsKeysState() = default;

void EmacsKeysState::setLastAction(EmacsKeysAction action)
{
    if (m_mark != -1) {
        // this code can be triggered only by 3rd party actions
        beginOwnAction();
        QTextCursor cursor = TextEditor::textCursorOf(m_editor);
        cursor.clearSelection();
        TextEditor::setTextCursorOf(m_editor, cursor);
        m_mark = -1;
        endOwnAction(action);
    } else {
        m_lastAction = action;
    }
}

void EmacsKeysState::cursorPositionChanged() {
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

void EmacsKeysState::textChanged() {
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

void EmacsKeysState::selectionChanged()
{
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

} // namespace EmacsKeys::Internal
