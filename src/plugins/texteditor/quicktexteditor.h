// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor.h"
#include "textdocument.h"

#include <coreplugin/icontext.h>

#include <QObject>

namespace Core { class IEditor; }

namespace TextEditor {

class TextViewport;

namespace Internal {

// The view inside \a editor, when \a editor is the Quick editor at all.
// What the free functions handing out a view's relay objects dispatch on.
TextViewport *viewportForEditor(Core::IEditor *editor);

// The editor \a view is inside, the reverse of the above. What a view owes
// whoever asked it to do something on the reader's behalf.
Core::IEditor *editorForViewport(TextViewport *view);

// Widen what this editor offers, where \a editor is one of ours. The mask a
// factory hands over is what the language says up front; this is what it
// turns out to support once a server has answered.
void addOptionalActionsIn(Core::IEditor *editor, uint optionalActions);

// A Qt Quick view of a document a TextEditorFactory has already built and
// configured: the language's own TextDocument, carrying its indenter, its
// highlighter and its completions. \a context is what the factory would have
// put on a BaseTextEditor, so that the language's own commands reach this view
// too; the editor's own two contexts are kept.
//
// This is how a factory offers the Quick editor of the language it configures
// instead of a TextEditorWidget. See TextEditorFactory::setUsesQuickEditor().
// \a optionalActions is the factory's OptionalActions mask: which of the
// commands that only some languages can answer this one does. What the mask
// does not name is registered disabled, the way the widget editor greys it out
// - a plain text file has no symbol to rename.
Core::IEditor *createQuickTextEditor(const TextDocumentPtr &document,
                                     const Core::Context &context,
                                     uint optionalActions = OptionalActions::None);

// The Qt Quick code editor. What a plain text file opens in; a language whose
// factory has not said setUsesQuickEditor() still opens in the widget one.
void setupQuickTextEditor();

#ifdef WITH_TESTS
QObject *createQuickTextEditorTest();
#endif

} // namespace Internal
} // namespace TextEditor
