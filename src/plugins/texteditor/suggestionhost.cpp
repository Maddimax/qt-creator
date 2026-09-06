// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "suggestionhost.h"

#include "quicktexteditor.h"
#include "texteditor.h"
#include "textviewport.h"

#include <coreplugin/editormanager/editormanager.h>

using namespace Utils;

namespace TextEditor {

namespace {

// Created on demand and parented to the view, which is what makes the handle
// last exactly as long as the view does.
template<class View>
class HostFor : public SuggestionHost
{
public:
    explicit HostFor(View *view)
        : SuggestionHost(view)
        , m_view(view)
    {
        connect(view, &View::cursorPositionChanged, this,
                &SuggestionHost::cursorPositionChanged);
    }

    TextDocument *textDocument() const override { return m_view->textDocument(); }
    QTextDocument *document() const override
    {
        TextDocument * const doc = m_view->textDocument();
        return doc ? doc->document() : nullptr;
    }
    QTextCursor textCursor() const override { return m_view->textCursor(); }
    MultiTextCursor multiTextCursor() const override { return m_view->multiTextCursor(); }
    bool isReadOnly() const override { return m_view->isReadOnly(); }
    TextSuggestion *currentSuggestion() const override { return m_view->currentSuggestion(); }
    void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion) override
    {
        m_view->insertSuggestion(std::move(suggestion));
    }

protected:
    View * const m_view;
};

class WidgetHost final : public HostFor<TextEditorWidget>
{
public:
    using HostFor::HostFor;
    bool suggestionVisible() const override { return m_view->suggestionVisible(); }
};

class ViewportHost final : public HostFor<TextViewport>
{
public:
    using HostFor::HostFor;
    bool suggestionVisible() const override { return m_view->suggestionVisible(); }
};

template<class Host, class View>
SuggestionHost *hostFor(View *view)
{
    if (!view)
        return nullptr;
    // Looked up by the base: a view has at most one, and the handle classes
    // below are local to this file and have no Q_OBJECT of their own.
    if (SuggestionHost * const existing
        = view->template findChild<SuggestionHost *>({}, Qt::FindDirectChildrenOnly)) {
        return existing;
    }
    return new Host(view);
}

} // namespace

SuggestionHost *suggestionHostForEditor(Core::IEditor *editor)
{
    if (TextViewport * const view = Internal::viewportForEditor(editor))
        return hostFor<ViewportHost>(view);
    if (auto * const base = qobject_cast<BaseTextEditor *>(editor))
        return hostFor<WidgetHost>(base->editorWidget());
    return nullptr;
}

SuggestionHost *suggestionHostForView(TextEditorWidget *widget)
{
    return hostFor<WidgetHost>(widget);
}

SuggestionHost *suggestionHostForView(TextViewport *view)
{
    return hostFor<ViewportHost>(view);
}

// Answered here rather than in each view's own file, because hostFor() and
// the handle classes live here.
SuggestionHost *TextEditorWidget::suggestionHost()
{
    return suggestionHostForView(this);
}

SuggestionHost *TextViewport::suggestionHost()
{
    return suggestionHostForView(this);
}

SuggestionHost *currentSuggestionHost()
{
    return suggestionHostForEditor(Core::EditorManager::currentEditor());
}

} // namespace TextEditor
