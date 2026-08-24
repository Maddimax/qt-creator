// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codedocument.h"

#include "textdocument.h"

#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>

#include <QPointer>
#include <QTextDocument>

using namespace Utils;

namespace TextEditor {

class CodeDocumentPrivate
{
public:
    QPointer<QQuickTextDocument> m_quickDocument;
    FilePath m_filePath;
    // Owned here: this is a document of the page's own, not one the editor
    // manager is showing, so nothing else will close it.
    std::unique_ptr<TextDocument> m_document;
    QList<QMetaObject::Connection> m_connections;
    bool m_useLanguageServer = false;
    bool m_announced = false;
};

// The language client manager lives in a plugin TextEditor does not depend on,
// and learns about ordinary documents from the editor manager. One opened here
// never goes through it, so it is told by name - which is what the ClangFormat
// page has always done for its own editor.
template<typename... Args>
static void tellLanguageClientManager(const char *method, Args &&...args)
{
    QObject *manager = ExtensionSystem::PluginManager::getObjectByName("LanguageClientManager");
    if (!manager)
        return;
    QMetaObject::invokeMethod(manager, method, args...);
}

CodeDocument::CodeDocument(QObject *parent)
    : QObject(parent)
    , d(new CodeDocumentPrivate)
{}

CodeDocument::~CodeDocument()
{
    withdrawFromLanguageServer();
    delete d;
}

QQuickTextDocument *CodeDocument::document() const
{
    return d->m_quickDocument;
}

void CodeDocument::setDocument(QQuickTextDocument *document)
{
    if (d->m_quickDocument == document)
        return;
    d->m_quickDocument = document;
    reattach();
    emit documentChanged();
}

QString CodeDocument::filePathString() const
{
    return d->m_filePath.toUrlishString();
}

void CodeDocument::setFilePathString(const QString &filePath)
{
    setFilePath(FilePath::fromUserInput(filePath));
}

FilePath CodeDocument::filePath() const
{
    return d->m_filePath;
}

void CodeDocument::setFilePath(const FilePath &filePath)
{
    if (d->m_filePath == filePath)
        return;
    d->m_filePath = filePath;
    reattach();
    emit filePathChanged();
}

TextDocument *CodeDocument::textDocument() const
{
    return d->m_document.get();
}

bool CodeDocument::isModified() const
{
    return d->m_document && d->m_document->isModified();
}

bool CodeDocument::isOpened() const
{
    return d->m_document != nullptr;
}

bool CodeDocument::usesLanguageServer() const
{
    return d->m_useLanguageServer;
}

void CodeDocument::setUseLanguageServer(bool use)
{
    if (d->m_useLanguageServer == use)
        return;
    d->m_useLanguageServer = use;
    if (use)
        announceToLanguageServer();
    else
        withdrawFromLanguageServer();
    emit useLanguageServerChanged();
}

void CodeDocument::announceToLanguageServer()
{
    if (d->m_announced || !d->m_useLanguageServer || !d->m_document)
        return;
    d->m_announced = true;
    tellLanguageClientManager("documentOpened",
                              Q_ARG(Core::IDocument *, d->m_document.get()));
}

void CodeDocument::withdrawFromLanguageServer()
{
    if (!d->m_announced)
        return;
    d->m_announced = false;
    tellLanguageClientManager("documentClosed",
                              Q_ARG(Core::IDocument *, d->m_document.get()));
}

bool CodeDocument::save()
{
    if (!d->m_document)
        return false;
    const Result<> result = d->m_document->save(d->m_filePath);
    return result.has_value();
}

void CodeDocument::reload()
{
    if (!d->m_document)
        return;
    d->m_document->reload();
}

void CodeDocument::reattach()
{
    const bool wasOpened = isOpened();

    // Before the document goes: the manager is told about the one it knows.
    withdrawFromLanguageServer();

    for (const QMetaObject::Connection &connection : std::as_const(d->m_connections))
        disconnect(connection);
    d->m_connections.clear();
    d->m_document.reset();

    if (d->m_filePath.isEmpty() || !d->m_quickDocument) {
        if (wasOpened != isOpened())
            emit openedChanged();
        return;
    }

    auto document = std::make_unique<TextDocument>();
    if (!document->open(d->m_filePath, d->m_filePath)) {
        if (wasOpened != isOpened())
            emit openedChanged();
        return;
    }
    d->m_document = std::move(document);

    // The substitution: the TextEdit shows this document from now on, so the
    // highlighter, the indenter and the marks it carries are all in the view.
    d->m_quickDocument->setTextDocument(d->m_document->document());

    d->m_connections
        << connect(d->m_document.get(), &Core::IDocument::changed,
                   this, &CodeDocument::modifiedChanged)
        << connect(d->m_document->document(), &QTextDocument::contentsChanged,
                   this, &CodeDocument::contentsChanged);

    announceToLanguageServer();

    if (wasOpened != isOpened())
        emit openedChanged();
    emit modifiedChanged();
}

} // namespace TextEditor
