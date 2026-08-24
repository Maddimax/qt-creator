// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codeindenting.h"

#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"

#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace TextEditor {

class CodeIndentingPrivate
{
public:
    QPointer<QQuickTextDocument> m_document;
    QString m_languageId;
    QPointer<ICodeStylePreferences> m_codeStyle;
    // Owned here rather than by the document: an Indenter is not a QObject.
    std::unique_ptr<Indenter> m_indenter;
    QList<QMetaObject::Connection> m_styleConnections;
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
    d->m_document = document;
    reattach();
    emit documentChanged();
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
}

bool CodeIndenting::isIndenting() const
{
    return d->m_indenter != nullptr;
}

void CodeIndenting::reindent()
{
    QTextDocument *target = d->m_document ? d->m_document->textDocument() : nullptr;
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

void CodeIndenting::reattach()
{
    const bool was = isIndenting();

    for (const QMetaObject::Connection &connection : std::as_const(d->m_styleConnections))
        disconnect(connection);
    d->m_styleConnections.clear();
    d->m_indenter.reset();

    QTextDocument *target = d->m_document ? d->m_document->textDocument() : nullptr;
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
