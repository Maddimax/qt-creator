// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/texteditor_global.h>

#include <QString>

#include <functional>

namespace TextEditor {

class AutoCompleter;
class TextDocument;

class TEXTEDITOR_EXPORT SnippetProvider
{
public:
    SnippetProvider() = default;

    // How a snippet in this group is written, said to the document rather
    // than to a widget: highlighting and indenting are the document's, so
    // either kind of view can show one. It used to take a TextEditorWidget,
    // which is what made the decoration unreachable once the Snippets page
    // stopped being built out of widgets.
    using DocumentDecorator = std::function<void(TextDocument *)>;
    // Auto-completion is the view's, not the document's, so it is made here
    // and installed by whoever is showing the text.
    using AutoCompleterCreator = std::function<AutoCompleter *()>;

    static const QList<SnippetProvider> &snippetProviders();
    static void registerGroup(const QString &groupId, const QString &displayName,
                              DocumentDecorator documentDecorator = {},
                              const QString &mimeType = {},
                              AutoCompleterCreator autoCompleterCreator = {});

    QString groupId() const;
    QString displayName() const;
    // What a snippet in this group is written in, for a view that wants a
    // highlight definition and has no path to guess one from.
    QString mimeType() const;

    static void decorateDocument(TextDocument *document, const QString &groupId);
    // The completer for \a groupId, or nullptr when the group asks for none.
    static AutoCompleter *createAutoCompleter(const QString &groupId);
    static QString mimeTypeForGroup(const QString &groupId);

private:
    QString m_groupId;
    QString m_displayName;
    QString m_mimeType;
    DocumentDecorator m_documentDecorator;
    AutoCompleterCreator m_autoCompleterCreator;
};

} // TextEditor
