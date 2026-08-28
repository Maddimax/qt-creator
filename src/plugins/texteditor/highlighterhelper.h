// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <KSyntaxHighlighting/Definition>

#include <QFuture>
#include <QTextDocument>

#include <functional>

namespace Utils { class FilePath; }

namespace TextEditor {
class TextDocument;

namespace HighlighterHelper {

using Definition = KSyntaxHighlighting::Definition;
using Definitions = QList<Definition>;

Definition definitionForName(const QString &name);

Definitions definitionsForDocument(const TextDocument *document);
Definitions definitionsForMimeType(const QString &mimeType);
Definitions definitionsForFileName(const Utils::FilePath &filePath);

void rememberDefinitionForDocument(const Definition &definition, const TextDocument *document);
void clearDefinitionForDocumentCache();

// Puts a definition's highlighter on a document. What a view does on top of
// that - which comment markers its shortcuts use, whether it offers folding,
// the info bar about a missing definition - is the view's own business, and a
// view that is not a widget has none of it.
void setDefinitionOn(TextDocument *document, const Definition &definition);

void addCustomHighlighterPath(const Utils::FilePath &path);
// Reports progress through \a logger where one is given, and to the message
// manager otherwise. A callback rather than a label: whoever shows the progress
// need not be a widget.
void downloadDefinitions(const std::function<void(const QString &)> &logger = {});
void reload();

void handleShutdown();

QFuture<QTextDocument *> highlightCode(const QString &code, const QString &mimeType);

} // namespace HighlighterHelper

} // namespace TextEditor
