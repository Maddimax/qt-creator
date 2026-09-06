// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nimeditorfactory.h"
#include "nimcompletionassistprovider.h"
#include "nimhighlighter.h"
#include "nimindenter.h"
#include "nimtr.h"

#include "../nimconstants.h"
#include "nimlinkfinder.h"

#include <texteditor/textdocument.h>

using namespace TextEditor;
using namespace Utils;

namespace Nim {

NimEditorFactory::NimEditorFactory()
{
    setId(Constants::C_NIMEDITOR_ID);
    setDisplayName(Tr::tr("Nim Editor"));
    addMimeType(QLatin1String(Nim::Constants::C_NIM_MIMETYPE));
    addMimeType(QLatin1String(Nim::Constants::C_NIM_SCRIPT_MIMETYPE));

    setOptionalActionMask(OptionalActions::Format
                            | OptionalActions::UnCommentSelection
                            | OptionalActions::UnCollapseAll
                            | OptionalActions::FollowSymbolUnderCursor);
    // Its widget subclass named the language whose code style to use, which
    // either view now derives from the mime type - and the link finder it
    // once carried was already a free function on this factory.
    setUsesQuickEditor(true);
    setLinkFinder(nimLinkFinder());

    setDocumentCreator([]() {
        return new TextDocument(Constants::C_NIMEDITOR_ID);
    });
    setIndenterCreator(&createNimIndenter);
    setSyntaxHighlighterCreator(&createNimHighlighter);
    setCompletionAssistProvider(new NimCompletionAssistProvider());
    setCommentDefinition(CommentDefinition::HashStyle);
    setParenthesesMatchingEnabled(true);
    setCodeFoldingSupported(true);
}

void NimEditorFactory::decorateDocument(TextEditor::TextDocument *document)
{
    document->resetSyntaxHighlighter(&createNimHighlighter);
    document->setIndenter(createNimIndenter(document->document()));
}

} // Nim
