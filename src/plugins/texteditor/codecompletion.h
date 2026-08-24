// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

// Complete rather than forward-declared: it is a Q_PROPERTY type, and moc
// needs the metatype.
#include "codedocument.h"

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration>

namespace TextEditor {

class CodeCompletionPrivate;
class IAssistProposal;

// Asks the document's completion provider what could go at the cursor, and
// hands the answers to QML to draw.
//
// The proposal machinery never needed a widget: a provider takes an
// AssistInterface built from a cursor and a path, and an item is applied to an
// AssistTarget. What it needed a widget for was somewhere to *show* the list,
// which in Qt Quick is a page's own business - this only supplies the words.
class TEXTEDITOR_EXPORT CodeCompletion : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The document to complete in. Its provider is what answers, so a
    // CodeDocument that opened a real file is what makes this work at all.
    Q_PROPERTY(TextEditor::CodeDocument *codeDocument READ codeDocument WRITE setCodeDocument
                   NOTIFY codeDocumentChanged)
    // What could go in, in the order the provider ranked them. Empty until
    // something is asked for, and empty again once it is taken or dropped.
    Q_PROPERTY(QStringList proposals READ proposals NOTIFY proposalsChanged)
    // Whether there is a proposal on offer.
    Q_PROPERTY(bool active READ isActive NOTIFY proposalsChanged)

public:
    explicit CodeCompletion(QObject *parent = nullptr);
    ~CodeCompletion() override;

    CodeDocument *codeDocument() const;
    void setCodeDocument(CodeDocument *document);

    QStringList proposals() const;
    bool isActive() const;

    // Asks what could go at \a position. The answer may arrive later, which is
    // what proposalsChanged() is for.
    Q_INVOKABLE void invoke(int position);
    // Puts the proposal at \a index into the document.
    Q_INVOKABLE void apply(int index);
    // Drops what was on offer without changing anything.
    Q_INVOKABLE void cancel();

signals:
    void codeDocumentChanged();
    void proposalsChanged();

private:
    void takeProposal(IAssistProposal *proposal);

    CodeCompletionPrivate *d = nullptr;
};

} // namespace TextEditor
