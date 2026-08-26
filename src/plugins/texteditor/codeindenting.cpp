// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codeindenting.h"

#include "codesource.h"
#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"
#include "tabsettings.h"
#include "textdocument.h"

#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace TextEditor {

class CodeIndentingPrivate
{
public:
    QPointer<QQuickTextDocument> m_document;
    QPointer<CodeSource> m_source;
    QString m_languageId;
    QPointer<ICodeStylePreferences> m_codeStyle;
    // Owned here rather than by the document: an Indenter is not a QObject.
    std::unique_ptr<Indenter> m_indenter;
    QList<QMetaObject::Connection> m_styleConnections;

    // Whichever way it was said. A TextEdit's document and a source are two
    // ways of naming the same kind of thing, and only one of them is ever set.
    QTextDocument *target() const
    {
        if (m_document)
            return m_document->textDocument();
        TextDocument * const document = m_source ? m_source->textDocument() : nullptr;
        return document ? document->document() : nullptr;
    }

    // What an indent is here. A preview measures against the style being
    // edited; an editor with no style of its own uses the global settings, as
    // the widget editors did.
    TabSettingsData tabSettings() const
    {
        return m_codeStyle ? m_codeStyle->currentTabSettings() : globalTabSettings().data();
    }
};

CodeIndenting::CodeIndenting(QObject *parent)
    : QObject(parent)
    , d(new CodeIndentingPrivate)
{}

CodeIndenting::~CodeIndenting()
{
    delete d;
}

QQuickTextDocument *CodeIndenting::document() const
{
    return d->m_document;
}

void CodeIndenting::setDocument(QQuickTextDocument *document)
{
    if (d->m_document == document)
        return;
    if (d->m_document)
        disconnect(d->m_document, nullptr, this, nullptr);
    d->m_document = document;
    // The QTextDocument behind a TextEdit can be swapped for another - see
    // CodeDocument - and what was attached to the old one is attached to
    // nothing the view is showing.
    if (d->m_document) {
        connect(d->m_document, &QQuickTextDocument::textDocumentChanged,
                this, [this] { reattach(); });
    }
    reattach();
    emit documentChanged();
}

CodeSource *CodeIndenting::source() const
{
    return d->m_source;
}

void CodeIndenting::setSource(CodeSource *source)
{
    if (d->m_source == source)
        return;
    if (d->m_source)
        disconnect(d->m_source, nullptr, this, nullptr);
    d->m_source = source;
    // A source hands out a different document when it reopens, and what was
    // attached to the old one is attached to nothing anyone is looking at.
    if (d->m_source) {
        connect(d->m_source, &CodeSource::textDocumentChanged,
                this, [this] { reattach(); });
    }
    reattach();
    emit sourceChanged();
}

QString CodeIndenting::languageId() const
{
    return d->m_languageId;
}

void CodeIndenting::setLanguageId(const QString &languageId)
{
    if (d->m_languageId == languageId)
        return;
    d->m_languageId = languageId;
    reattach();
    emit languageIdChanged();
}

ICodeStylePreferences *CodeIndenting::codeStyle() const
{
    return d->m_codeStyle;
}

void CodeIndenting::setCodeStyle(ICodeStylePreferences *codeStyle)
{
    if (d->m_codeStyle == codeStyle)
        return;
    d->m_codeStyle = codeStyle;
    reattach();
    emit codeStyleChanged();
}

QObject *CodeIndenting::codeStyleObject() const
{
    return d->m_codeStyle;
}

void CodeIndenting::setCodeStyleObject(QObject *codeStyle)
{
    setCodeStyle(qobject_cast<ICodeStylePreferences *>(codeStyle));
}

bool CodeIndenting::isIndenting() const
{
    return d->m_indenter != nullptr;
}

void CodeIndenting::reindent()
{
    QTextDocument * const target = d->target();
    if (!target || !d->m_indenter || !d->m_codeStyle)
        return;

    d->m_indenter->invalidateCache();

    // One edit block, so that the whole re-indent is a single undo step and the
    // document reports one change rather than one per line.
    QTextCursor cursor(target);
    cursor.beginEditBlock();
    const TabSettingsData tabSettings = d->m_codeStyle->currentTabSettings();
    for (QTextBlock block = target->firstBlock(); block.isValid(); block = block.next())
        d->m_indenter->indentBlock(block, QChar::Null, tabSettings);
    cursor.endEditBlock();
}

void CodeIndenting::indentAt(int position)
{
    QTextDocument * const target = d->target();
    if (!target)
        return;

    QTextCursor cursor(target);
    cursor.setPosition(position);
    const TabSettingsData tabs = d->tabSettings();
    const int column = tabs.columnAtCursorPosition(cursor);
    cursor.insertText(tabs.indentationString(column, tabs.indentedColumn(column, true), 0));
}

void CodeIndenting::reattach()
{
    const bool was = isIndenting();

    for (const QMetaObject::Connection &connection : std::as_const(d->m_styleConnections))
        disconnect(connection);
    d->m_styleConnections.clear();
    d->m_indenter.reset();

    QTextDocument * const target = d->target();
    ICodeStylePreferencesFactory *factory
        = d->m_languageId.isEmpty() ? nullptr
                                    : codeStyleFactory(Utils::Id::fromString(d->m_languageId));

    if (target && factory)
        d->m_indenter.reset(factory->createIndenter(target));

    if (d->m_codeStyle) {
        // Everything the preview is meant to react to: the tab settings, the
        // style's own value, and being pointed at a different style.
        d->m_styleConnections
            << connect(d->m_codeStyle, &ICodeStylePreferences::currentTabSettingsChanged,
                       this, &CodeIndenting::reindent)
            << connect(d->m_codeStyle, &ICodeStylePreferences::currentValueChanged,
                       this, &CodeIndenting::reindent)
            << connect(d->m_codeStyle, &ICodeStylePreferences::currentPreferencesChanged,
                       this, &CodeIndenting::reindent);
    }

    reindent();

    if (was != isIndenting())
        emit indentingChanged();
}

} // namespace TextEditor
