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
    // A selection changing without the caret moving has no editor-level signal
    // of its own. The widget's is kept where there is one, so that view goes
    // on behaving exactly as it did.
    if (auto * const edit = qobject_cast<PlainTextEdit *>(editor->widget())) {
        connect(edit, &PlainTextEdit::selectionChanged,
                this, &EmacsKeysState::selectionChanged);
    }
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
