// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor.h"
#include "textdocument.h"

#include <coreplugin/icontext.h>

#include <QObject>
#include <QPointer>

#include <functional>

namespace Core { class IEditor; }
namespace QtcQuick { class ActionModel; class QuickWidget; }

namespace TextEditor {

class CodeSource;
class TextDocument;
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

// The same, over a document somebody else owns - which is what an editor has:
// the editor manager opens the document before the editor is ever shown.
// CodeBuffer holds text of its own and CodeDocument opens a file; neither is
// that. The source pointing at \a document belongs to the widget handed back.
QtcQuick::QuickWidget *createQuickTextViewOver(const TextDocumentPtr &document,
                                               QtcQuick::ActionModel *contextActions);

// Where a jump landed, kept until the reader moves off it. Go Back is meant to
// return to the place a search result or a definition took them to, and the
// manager only learns of it when they leave: recording on arrival would put
// the entry in front of the caret that is still standing on it.
//
// Fifteen lines that are easy to get subtly wrong in a way nobody notices
// until Go Back goes to the wrong place, which is why they are shared rather
// than copied into each host.
class JumpRecorder final : public QObject
{
    Q_OBJECT

public:
    // \a state is the editor's own save format, asked at the moment of the
    // jump - only that editor is ever handed it back.
    JumpRecorder(Core::IEditor *editor, const std::function<QByteArray()> &state);

    // Said by whatever jumps: gotoLine(), and restoring a state.
    void jumped();

    // The caret moved. Connect the view's cursorPositionChanged to this.
    void caretMoved();

private:
    Core::IEditor * const m_editor;
    const std::function<QByteArray()> m_state;
    QByteArray m_stateOfAJumpNotYetLeft;
    bool m_jumpedHereAndStillOnIt = false;
};

// Which of an editor's commands the file's language can actually answer. A
// command is registered either way, so that its menu entry keeps its place and
// its shortcut, and disabled where the language has nothing to answer with -
// which is what the widget editor does with the same mask.
//
// A child of the editor it belongs to, because that is how a language that
// learns later what it can do finds it: addOptionalActionsIn() looks for one
// rather than knowing which editors have one.
class OptionalActionGate final : public QObject
{
    Q_OBJECT

public:
    // \a viewport is asked each time rather than held: two of the gated
    // commands edit, so whether the view is writable is part of the answer,
    // and the view outlives neither this nor the editor reliably.
    OptionalActionGate(QObject *parent, const std::function<TextViewport *()> &viewport);

    // \a needs of OptionalActions::None leaves \a action alone: a command
    // every language can answer is not gated at all.
    void gate(QAction *action, uint needs);

    void setOptionalActions(uint optionalActions);
    void addOptionalActions(uint optionalActions);
    void update();

private:
    struct GatedAction { QPointer<QAction> action; uint needs; };
    QList<GatedAction> m_gated;
    uint m_optionalActions = 0;
    const std::function<TextViewport *()> m_viewport;
};

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
