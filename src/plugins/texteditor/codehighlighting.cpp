// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codehighlighting.h"

#include "fontsettings.h"
#include "highlighter.h"
#include "highlighterhelper.h"


#include <QPointer>
#include <QQuickTextDocument>

namespace TextEditor {

class CodeHighlightingPrivate
{
public:
    QPointer<QQuickTextDocument> m_document;
    QString m_mimeType;
    // Parented to the document it highlights, so replacing either takes the
    // old highlighter with it.
    QPointer<Highlighter> m_highlighter;
};

CodeHighlighting::CodeHighlighting(QObject *parent)
    : QObject(parent)
    , d(new CodeHighlightingPrivate)
{
    // Recolouring on a scheme change is what the widget editors do; there is no
    // reason a Quick one should need reopening.
    connect(&globalFontSettings(), &FontSettings::changed, this, [this] {
        if (d->m_highlighter)
            d->m_highlighter->setFontSettings(globalFontSettings().data());
        emit schemeChanged();
    });
}

CodeHighlighting::~CodeHighlighting()
{
    delete d;
}

QQuickTextDocument *CodeHighlighting::document() const
{
    return d->m_document;
}

void CodeHighlighting::setDocument(QQuickTextDocument *document)
{
    if (d->m_document == document)
        return;
    d->m_document = document;
    reattach();
    emit documentChanged();
}

QString CodeHighlighting::mimeType() const
{
    return d->m_mimeType;
}

void CodeHighlighting::setMimeType(const QString &mimeType)
{
    if (d->m_mimeType == mimeType)
        return;
    d->m_mimeType = mimeType;
    reattach();
    emit mimeTypeChanged();
}

bool CodeHighlighting::isHighlighting() const
{
    return !d->m_highlighter.isNull();
}

QFont CodeHighlighting::font() const
{
    return globalFontSettings().data().font();
}

QColor CodeHighlighting::textColor() const
{
    const QBrush brush = globalFontSettings().data().toTextCharFormat(C_TEXT).foreground();
    return brush.style() == Qt::NoBrush ? QColor(Qt::black) : brush.color();
}

QColor CodeHighlighting::backgroundColor() const
{
    const QBrush brush = globalFontSettings().data().toTextCharFormat(C_TEXT).background();
    return brush.style() == Qt::NoBrush ? QColor(Qt::white) : brush.color();
}

void CodeHighlighting::reattach()
{
    const bool was = isHighlighting();
    delete d->m_highlighter;

    QTextDocument *target = d->m_document ? d->m_document->textDocument() : nullptr;
    const HighlighterHelper::Definitions definitions
        = d->m_mimeType.isEmpty() ? HighlighterHelper::Definitions()
                                  : HighlighterHelper::definitionsForMimeType(d->m_mimeType);

    if (target && !definitions.isEmpty()) {
        auto highlighter = new Highlighter;
        highlighter->setDefinition(definitions.first());
        highlighter->setParent(target);
        highlighter->setFontSettings(globalFontSettings().data());
        highlighter->setMimeType(d->m_mimeType);
        highlighter->setDocument(target);
        d->m_highlighter = highlighter;
    }

    if (was != isHighlighting())
        emit highlightingChanged();
}

} // namespace TextEditor
