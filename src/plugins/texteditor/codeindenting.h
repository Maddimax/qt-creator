// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/id.h>

#include <QObject>
#include <QQuickTextDocument>
#include <QtQmlIntegration>

namespace TextEditor {

class ICodeStylePreferences;
class CodeIndentingPrivate;

// Re-indents a Qt Quick TextEdit's document with a language's own indenter, so
// that a preview shows what the current code style does to code. An indenter
// works on a QTextDocument like a highlighter does; see CodeHighlighting.
class TEXTEDITOR_EXPORT CodeIndenting : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The TextEdit's document, as TextEdit.textDocument.
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // Which language's indenter to use, as the factory is registered under.
    Q_PROPERTY(QString languageId READ languageId WRITE setLanguageId NOTIFY languageIdChanged)
    // Whether an indenter was found. Without one the text is left as typed.
    Q_PROPERTY(bool indenting READ isIndenting NOTIFY indentingChanged)
    // What the indenting is measured against: a page hands over its own
    // editable copy of the preferences, so that the preview shows the edits
    // being made rather than what is saved. A plain QObject because QML has no
    // use for the type beyond passing it along.
    Q_PROPERTY(QObject *codeStyle READ codeStyleObject WRITE setCodeStyleObject
                   NOTIFY codeStyleChanged)

public:
    explicit CodeIndenting(QObject *parent = nullptr);
    ~CodeIndenting() override;

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    QString languageId() const;
    void setLanguageId(const QString &languageId);

    ICodeStylePreferences *codeStyle() const;
    void setCodeStyle(ICodeStylePreferences *codeStyle);

    QObject *codeStyleObject() const;
    void setCodeStyleObject(QObject *codeStyle);

    bool isIndenting() const;

    // Re-indents every line. Happens by itself whenever the code style changes;
    // call it after replacing the text.
    Q_INVOKABLE void reindent();

    // What pressing Tab does in an editor: one indent's worth of spaces or a
    // tab character, as the code style's tab settings say - the global ones
    // where there is no code style. A Qt Quick TextEdit moves the focus on Tab
    // instead, which is right for a form field and wrong for an editor, so
    // every editor has to say so itself.
    Q_INVOKABLE void indentAt(int position);

signals:
    void documentChanged();
    void languageIdChanged();
    void indentingChanged();
    void codeStyleChanged();

private:
    void reattach();

    CodeIndentingPrivate *d = nullptr;
};

} // namespace TextEditor
