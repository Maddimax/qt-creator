// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codebuffer.h"

#include "snippets/snippetprovider.h"
#include "tabsettings.h"
#include "textdocument.h"

#include <QPointer>
#include <QTextDocument>

namespace TextEditor {

// TextDocument::setTabSettings() runs autoDetect() over the text, which is
// right for a file - the indentation already in it is the one to keep - and
// wrong for a preview, where the text is the *demonstration* of a style and
// detecting the style from it would only ever answer "however it looks now".
class BufferDocument : public TextDocument
{
public:
    BufferDocument()
    {
        // Until a code style says otherwise, an indent here is what an indent
        // is everywhere else. A snippet editor has no language and so no style,
        // and typing eight spaces into one because a default said so would be
        // its own kind of wrong.
        m_tabSettings = globalTabSettings().data();
        connect(&globalTabSettings(), &Utils::AspectContainer::changed, this, [this] {
            if (m_followGlobal)
                setTabSettingsVerbatim(globalTabSettings().data());
        });
    }

    TabSettingsData tabSettings() const override { return m_tabSettings; }

    void setTabSettingsVerbatim(const TabSettingsData &tabSettings)
    {
        if (m_tabSettings == tabSettings)
            return;
        m_tabSettings = tabSettings;
        emit tabSettingsChanged();
    }

    // A buffer that has been told what an indent is stops following the global
    // settings: the style it was given is the one it is demonstrating.
    void stopFollowingGlobal() { m_followGlobal = false; }

private:
    TabSettingsData m_tabSettings;
    bool m_followGlobal = true;
};

class CodeBufferPrivate
{
public:
    std::unique_ptr<BufferDocument> m_document;
    QString m_mimeType;
    QString m_snippetGroup;
    // Whether a definition was found for the mime type. The document always has
    // *a* highlighter after the first setMimeType(), so its presence says
    // nothing.
    bool m_hasDefinition = false;
    // Set while the document is being written from text(), so that the
    // contentsChanged it causes is not read back as a change from the user.
    bool m_applying = false;
};

CodeBuffer::CodeBuffer(QObject *parent)
    : CodeSource(parent)
    , d(new CodeBufferPrivate)
{
    d->m_document = std::make_unique<BufferDocument>();
    connect(d->m_document->document(), &QTextDocument::contentsChanged, this, [this] {
        if (!d->m_applying)
            emit textChanged();
    });
    emit textDocumentChanged();
}

CodeBuffer::~CodeBuffer()
{
    delete d;
}

QString CodeBuffer::text() const
{
    return d->m_document->document()->toPlainText();
}

void CodeBuffer::setText(const QString &text)
{
    if (this->text() == text)
        return;

    d->m_applying = true;
    d->m_document->document()->setPlainText(text);
    d->m_applying = false;
    emit textChanged();
}

void CodeBuffer::setTabSettings(const TabSettingsData &tabSettings)
{
    d->m_document->stopFollowingGlobal();
    d->m_document->setTabSettingsVerbatim(tabSettings);
}

QString CodeBuffer::mimeType() const
{
    return d->m_mimeType;
}

void CodeBuffer::setMimeType(const QString &mimeType)
{
    if (d->m_mimeType == mimeType)
        return;
    d->m_mimeType = mimeType;
    applyLanguage();
    emit mimeTypeChanged();
}

QString CodeBuffer::snippetGroup() const
{
    return d->m_snippetGroup;
}

void CodeBuffer::setSnippetGroup(const QString &groupId)
{
    if (d->m_snippetGroup == groupId)
        return;
    d->m_snippetGroup = groupId;
    applyLanguage();
    emit snippetGroupChanged();
}

AutoCompleter *CodeBuffer::createAutoCompleter() const
{
    return SnippetProvider::createAutoCompleter(d->m_snippetGroup);
}

bool CodeBuffer::isHighlighting() const
{
    return d->m_hasDefinition;
}

TextDocument *CodeBuffer::textDocument() const
{
    return d->m_document.get();
}

// What the text is written in, as far as anything here can act on it: the
// colours from the mime type, and then whatever the group's own plugin knows
// on top - which is a better highlighter than a definition file, and the
// indenter, which no definition file has.
void CodeBuffer::applyLanguage()
{
    const bool was = isHighlighting();
    d->m_hasDefinition = CodeSource::applyHighlighting(d->m_document.get(), d->m_mimeType);
    if (!d->m_snippetGroup.isEmpty())
        SnippetProvider::decorateDocument(d->m_document.get(), d->m_snippetGroup);
    if (was != isHighlighting())
        emit highlightingChanged();
    emit languageChanged();
}

} // namespace TextEditor
