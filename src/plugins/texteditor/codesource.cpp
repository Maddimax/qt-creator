// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codesource.h"

#include "highlighter.h"
#include "highlighterhelper.h"
#include "syntaxhighlighter.h"
#include "textdocument.h"

namespace TextEditor {

AutoCompleter *CodeSource::createAutoCompleter() const
{
    return nullptr;
}

void CodeSource::setTabSettings(const TabSettingsData &tabSettings)
{
    if (TextDocument * const document = textDocument())
        document->setTabSettings(tabSettings);
}

bool CodeSource::applyHighlighting(TextDocument *document, const QString &mimeType)
{
    if (!document)
        return false;

    // Through the document rather than onto its QTextDocument: the document
    // owns the highlighter, hands it the font settings and answers
    // syntaxHighlighter(), which is how a view learns that highlighting has
    // finished. One attached behind the document's back leaves that accessor
    // null, and the colours are never asked for.
    document->setMimeType(mimeType);
    const HighlighterHelper::Definitions definitions
        = mimeType.isEmpty() ? HighlighterHelper::Definitions()
                             : HighlighterHelper::definitionsForMimeType(mimeType);

    if (definitions.isEmpty()) {
        document->resetSyntaxHighlighter([] { return new SyntaxHighlighter; });
        return false;
    }

    const HighlighterHelper::Definition definition = definitions.first();
    document->resetSyntaxHighlighter([definition] {
        auto highlighter = new Highlighter;
        highlighter->setDefinition(definition);
        return highlighter;
    });
    return true;
}

} // namespace TextEditor
