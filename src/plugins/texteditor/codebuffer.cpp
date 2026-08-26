// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codebuffer.h"

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
    TabSettingsData tabSettings() const override { return m_tabSettings; }
    void setTabSettingsVerbatim(const TabSettingsData &tabSettings)
    {
        if (m_tabSettings == tabSettings)
            return;
        m_tabSettings = tabSettings;
        emit tabSettingsChanged();
    }

private:
    TabSettingsData m_tabSettings;
};

class CodeBufferPrivate
{
public:
    std::unique_ptr<BufferDocument> m_document;
    QString m_mimeType;
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
    applyHighlighting();
    emit mimeTypeChanged();
}

bool CodeBuffer::isHighlighting() const
{
    return d->m_hasDefinition;
}

TextDocument *CodeBuffer::textDocument() const
{
    return d->m_document.get();
}

void CodeBuffer::applyHighlighting()
{
    const bool was = isHighlighting();
    d->m_hasDefinition = CodeSource::applyHighlighting(d->m_document.get(), d->m_mimeType);
    if (was != isHighlighting())
        emit highlightingChanged();
}

} // namespace TextEditor
