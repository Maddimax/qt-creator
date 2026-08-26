// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QObject>
#include <QtQmlIntegration>

namespace TextEditor {

class TextDocument;

// Something a TextViewport can draw. A file is one - CodeDocument - and text
// held in memory is another - CodeBuffer.
//
// The viewport binds to the source rather than to the document because the
// source owns it: CodeDocument throws its document away and makes a new one
// whenever the path changes, so a pointer to the document is only good until
// then. Asking the source is always good.
class TEXTEDITOR_EXPORT CodeSource : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CodeSource)
    QML_UNCREATABLE("CodeSource is a base class; use CodeDocument or CodeBuffer")

public:
    using QObject::QObject;

    // The document to draw, or null when there is none yet.
    virtual TextDocument *textDocument() const = 0;

signals:
    // A different document from now on - not a change to the one there was.
    void textDocumentChanged();

protected:
    // Gives \a document a highlighter for \a mimeType, and says whether a
    // definition was found for it. Shared because both sources need it and
    // neither gets it for free: TextDocument::open() works out the mime type
    // but leaves the highlighter alone, and it is the editor *widget* that
    // normally puts one on. A document drawn by a TextViewport has no widget.
    static bool applyHighlighting(TextDocument *document, const QString &mimeType);
};

} // namespace TextEditor
