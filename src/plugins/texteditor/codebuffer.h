// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "codesource.h"

#include <QString>

namespace TextEditor {

class CodeBufferPrivate;

// Text that was never a file, drawn as code. What a code style preview and a
// snippet editor hold: the text belongs to an aspect or to a snippet, and the
// only reason it needs a TextEditor::TextDocument at all is that a highlighter
// and an indenter work on one.
//
// CodeDocument is the same idea for a file. Neither is the other with a flag:
// opening, saving, reloading and telling a language server are what a file
// needs and are meaningless here, and "which language is this" has to be said
// out loud because there is no path to guess it from.
class TEXTEDITOR_EXPORT CodeBuffer : public CodeSource
{
    Q_OBJECT
    QML_ELEMENT

    // The text itself. Two-way: editing the document writes back here, which is
    // what lets an aspect hold the value and a preview show it.
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // What the text is, for looking up a highlight definition. No path, so
    // nothing else can tell.
    Q_PROPERTY(QString mimeType READ mimeType WRITE setMimeType NOTIFY mimeTypeChanged)
    // Whether a highlight definition was found. Worth reading in a test, and
    // worth knowing before blaming the colours.
    Q_PROPERTY(bool highlighting READ isHighlighting NOTIFY highlightingChanged)

public:
    explicit CodeBuffer(QObject *parent = nullptr);
    ~CodeBuffer() override;

    QString text() const;
    void setText(const QString &text);

    QString mimeType() const;
    void setMimeType(const QString &mimeType);

    bool isHighlighting() const;

    TextDocument *textDocument() const override;
    void setTabSettings(const TabSettingsData &tabSettings) override;

signals:
    void textChanged();
    void mimeTypeChanged();
    void highlightingChanged();

private:
    void applyHighlighting();

    CodeBufferPrivate *d = nullptr;
};

} // namespace TextEditor
