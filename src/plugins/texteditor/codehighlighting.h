// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QQuickTextDocument>
#include <QtQmlIntegration>

namespace TextEditor {

class CodeHighlightingPrivate;

// Syntax highlighting for a Qt Quick TextEdit. A highlighter works on a
// QTextDocument, and a TextEdit has one, so none of TextEditorWidget is needed
// to show highlighted code - only the definition for the mime type and the
// font settings to colour it with.
class TEXTEDITOR_EXPORT CodeHighlighting : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The TextEdit's document, as TextEdit.textDocument.
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // What the code is, so that a definition can be found for it.
    Q_PROPERTY(QString mimeType READ mimeType WRITE setMimeType NOTIFY mimeTypeChanged)
    // Whether a definition was found: without one the text shows unhighlighted
    // rather than not at all.
    Q_PROPERTY(bool highlighting READ isHighlighting NOTIFY highlightingChanged)

    // The editor's own font and the colour scheme's plain-text colours, so that
    // a code view looks like the editor rather than like the rest of the form.
    Q_PROPERTY(QFont font READ font NOTIFY schemeChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY schemeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY schemeChanged)

public:
    explicit CodeHighlighting(QObject *parent = nullptr);
    ~CodeHighlighting() override;

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    QString mimeType() const;
    void setMimeType(const QString &mimeType);

    bool isHighlighting() const;

    QFont font() const;
    QColor textColor() const;
    QColor backgroundColor() const;

signals:
    void documentChanged();
    void mimeTypeChanged();
    void highlightingChanged();
    void schemeChanged();

private:
    void reattach();

    CodeHighlightingPrivate *d = nullptr;
};

} // namespace TextEditor
