// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor.h"
#include "textdocument.h"

#include <coreplugin/icontext.h>

#include <QObject>

namespace Core { class IEditor; }
namespace QtcQuick { class ActionModel; class QuickWidget; }

namespace TextEditor {

class CodeSource;
class TextViewport;
class ToolBarChoice;
class ToolBarOutline;

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
// \a contextMenuId is the ActionManager container the language registers its
// own right-click entries in - CppEditor.ContextMenu for a C++ file - which a
// widget subclass used to name in its own contextMenuEvent().
Core::IEditor *createQuickTextEditor(const TextDocumentPtr &document,
                                     const Core::Context &context,
                                     uint optionalActions = OptionalActions::None,
                                     Utils::Id contextMenuId = {});

// A Qt Quick text view over \a source, laid out by the display settings and
// nothing else. The caller owns the widget and decides where it goes: the Qt
// Quick editor makes it its own, and an editor made of more than one pane -
// Markdown's text beside its preview - would put it in a splitter.
//
// Everything that follows from being an *editor* is the caller's to wire: the
// tooltip host, the view's back pointer to the editor, the caret signals. This
// builds a view and stops.
//
// \a contextActions is what the right-click menu is built from, or null for a
// view that offers none.
QtcQuick::QuickWidget *createQuickTextView(CodeSource *source,
                                           QtcQuick::ActionModel *contextActions);

// The view inside a widget createQuickTextView() handed back.
TextViewport *viewportIn(QWidget *host);

// The row above a Qt Quick text view: where the caret is, what the file is
// encoded as, the outline, and whatever the language put there. Takes what it
// draws and nothing else - who keeps those up to date is the caller's, the
// same split createQuickTextView() has.
//
// \a outline and \a choice may be null; a view with neither simply draws
// neither.
QtcQuick::QuickWidget *createQuickTextToolBar(TextViewport *view,
                                              QtcQuick::ActionModel *languageActions,
                                              ToolBarOutline *outline,
                                              ToolBarChoice *choice);

// The Qt Quick code editor. What a plain text file opens in; a language whose
// factory has not said setUsesQuickEditor() still opens in the widget one.
void setupQuickTextEditor();

#ifdef WITH_TESTS
QObject *createQuickTextEditorTest();
#endif

} // namespace Internal
} // namespace TextEditor
