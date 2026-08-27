// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nimtexteditorwidget.h"
#include "nimconstants.h"
#include "suggest/nimsuggestcache.h"
#include "suggest/nimsuggest.h"

#include <texteditor/textdocument.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>

#include <QTextStream>
#include <QTemporaryFile>
#include <QTextDocument>

using namespace Nim::Suggest;

namespace Nim {

static std::shared_ptr<QTemporaryFile> writeDirtyFile(const TextEditor::TextDocument *doc)
{
    auto result = std::make_shared<QTemporaryFile>("qtcnim.XXXXXX.nim");
    QTC_ASSERT(result->open(), return nullptr);
    QTextStream stream(result.get());
    stream << doc->plainText();
    result->close();
    return result;
}

NimTextEditorWidget::NimTextEditorWidget(QWidget *parent)
    : TextEditorWidget(parent)
{
    setLanguageSettingsId(Nim::Constants::C_NIMLANGUAGE_ID);
}

// nimsuggest is one process answering one question at a time, so a new
// question replaces whatever was still outstanding.
static std::shared_ptr<Suggest::NimSuggestClientRequest> pendingRequest;

// Where the symbol under the cursor is defined, according to nimsuggest.
static void findNimLinkAt(TextEditor::TextDocument *document,
                          const QTextCursor &cursor,
                          const Utils::LinkHandler &processLinkCallback,
                          bool /*resolveTarget*/,
                          bool /*inNextSplit*/)
{
    const Utils::FilePath &path = document->filePath();

    NimSuggest *suggest = Nim::Suggest::getFromCache(path);
    if (!suggest)
        return processLinkCallback(Utils::Link());

    std::shared_ptr<QTemporaryFile> dirtyFile = writeDirtyFile(document);
    if (!dirtyFile)
        return processLinkCallback(Utils::Link());

    int line = 0, column = 0;
    Utils::Text::convertPosition(document->document(), cursor.position(), &line, &column);

    std::shared_ptr<NimSuggestClientRequest> request
        = suggest->def(path.path(), line, column, dirtyFile->fileName());
    if (!request)
        return processLinkCallback(Utils::Link());

    if (pendingRequest)
        QObject::disconnect(pendingRequest.get(), nullptr, nullptr, nullptr);
    pendingRequest = request;

    // The request and the file it reads from both have to outlive this call;
    // the connection holds them until the answer arrives.
    NimSuggestClientRequest *req = request.get();
    QObject::connect(req, &NimSuggestClientRequest::finished, req,
                     [request, dirtyFile, processLinkCallback] {
                         if (request->lines().empty()) {
                             processLinkCallback(Utils::Link());
                             return;
                         }
                         const Line &line = request->lines().front();
                         processLinkCallback(Utils::Link{
                             Utils::FilePath::fromString(line.abs_path), line.row, line.column});
                     });
}

TextEditor::TextEditorFactory::LinkFinder nimLinkFinder()
{
    return &findNimLinkAt;
}

} // Nim
