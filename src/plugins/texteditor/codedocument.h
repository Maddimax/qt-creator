// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "codesource.h"

#include <utils/filepath.h>

#include <QQuickTextDocument>

namespace TextEditor {

class TextDocument;
class CodeDocumentPrivate;

// Puts one of Qt Creator's own documents behind a Qt Quick TextEdit, rather
// than letting the TextEdit keep the empty one it makes for itself.
//
// This is what CodeHighlighting cannot be: a highlighter can be attached to any
// QTextDocument, but a file that is opened, saved, indented by the language's
// indenter and known to a language server has to *be* a TextEditor::TextDocument.
// QQuickTextDocument::setTextDocument() is what makes the substitution possible.
class TEXTEDITOR_EXPORT CodeDocument : public CodeSource
{
    Q_OBJECT
    QML_ELEMENT

    // The TextEdit's document, as TextEdit.textDocument. Its own document is
    // replaced by the one this opens.
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // The file to show. Opening it is what gives the editor its contents, its
    // highlighting and its indenter.
    Q_PROPERTY(QString filePath READ filePathString WRITE setFilePathString NOTIFY filePathChanged)
    // Whether the file has been edited since it was opened or last saved.
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)
    // Whether a document was opened at all. Without one the TextEdit keeps its
    // own, and nothing below does anything.
    Q_PROPERTY(bool opened READ isOpened NOTIFY openedChanged)
    // Whether to give the opened document a highlighter for whatever type the
    // path says it is. On by default: a document that opened a file should be
    // coloured, and putting the highlighter on is normally the editor widget's
    // job, which a document drawn by a TextViewport does not have.
    //
    // Off for a page that attaches CodeHighlighting to the same document
    // instead - two highlighters would both write the blocks' formats, and
    // CodeHighlighting is told what the text is rather than guessing from the
    // name, which for a .clang-format file is the only way to know.
    Q_PROPERTY(bool highlight READ highlights WRITE setHighlight NOTIFY highlightChanged)
    // Whether a language server should be told about the file. A document that
    // goes through the editor manager is announced for free; this one does not,
    // so a page that wants diagnostics or completion has to ask.
    Q_PROPERTY(bool useLanguageServer READ usesLanguageServer WRITE setUseLanguageServer
                   NOTIFY useLanguageServerChanged)

public:
    explicit CodeDocument(QObject *parent = nullptr);
    ~CodeDocument() override;

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    QString filePathString() const;
    void setFilePathString(const QString &filePath);

    Utils::FilePath filePath() const;
    void setFilePath(const Utils::FilePath &filePath);

    // The document itself, for the C++ side: a page that wants to attach a
    // language client or read the contents needs the document, not the text.
    TextDocument *textDocument() const override;

    bool isModified() const;
    bool isOpened() const;

    bool highlights() const;
    void setHighlight(bool highlight);

    bool usesLanguageServer() const;
    void setUseLanguageServer(bool use);

    // Writes the file back. Says whether it managed to.
    Q_INVOKABLE bool save();
    // Throws away what was typed and reads the file again.
    Q_INVOKABLE void reload();

signals:
    void documentChanged();
    void filePathChanged();
    void modifiedChanged();
    void openedChanged();
    void highlightChanged();
    void useLanguageServerChanged();
    // The text changed, whoever changed it.
    void contentsChanged();

private:
    void reattach();
    void announceToLanguageServer();
    void withdrawFromLanguageServer();

    CodeDocumentPrivate *d = nullptr;
};

} // namespace TextEditor
