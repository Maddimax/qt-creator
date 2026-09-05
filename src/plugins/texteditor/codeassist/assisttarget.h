// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/texteditor_global.h>
#include <texteditor/snippets/snippetparser.h>

#include <QString>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace TextEditor {

// What applying a completion needs of whatever is showing the text.
//
// It used to be TextEditorWidget, which is why nothing but a widget could ever
// accept a proposal. The operations are the same ones the items were already
// using; naming them here is what lets a Qt Quick view offer completion too.
class TEXTEDITOR_EXPORT AssistTarget
{
public:
    virtual ~AssistTarget();

    virtual QTextDocument *document() const = 0;
    // Where the cursor is, and what is around it.
    virtual int position() const = 0;
    virtual QChar characterAt(int position) const = 0;
    virtual QString textAt(int position, int length) const = 0;
    virtual QTextCursor textCursor() const = 0;
    virtual QTextCursor textCursorAt(int position) const = 0;

    virtual void setCursorPosition(int position) = 0;
    virtual void replace(int position, int length, const QString &text) = 0;
    virtual void insertCodeSnippet(int basePosition,
                                   const QString &snippet,
                                   const SnippetParser &parse) = 0;

    // Where a closing brace the editor added itself may be skipped over. A view
    // that does not add them has nothing to do here.
    virtual void setAutoCompleteSkipPosition(const QTextCursor &cursor);
    // The text was changed from underneath; anything watching it should catch
    // up. A view with nothing watching has nothing to do here either.
    virtual void encourageApply();

    // Puts the clipboard in. Named here because an item that offers a choice
    // of things to paste has to be able to paste one, and until now it could
    // only do that by asking whether the target was a widget - which made the
    // clipboard history a widget feature by accident. The default is what a
    // plain view can do: the text, where the cursor is.
    virtual void paste();
};

// Where a snippet's placeholders ended up once its markup was taken out.
// Positions in the document rather than cursors: whoever keeps them past the
// insertion is the one that has to make them survive editing.
struct SnippetPlaceholder
{
    int start = 0;
    int end = 0;
    int variableIndex = -1;
    bool finalPart = false;
    // How this hole is written once the snippet is done with: the Q_PROPERTY
    // snippet asks for the name it was given with a capital, in WRITE only.
    // Owned by the parser, which keeps one of each for the program's life.
    NameMangler *mangler = nullptr;
};

// An AssistTarget over a plain document and a cursor, which is what a Qt Quick
// view has: the operations are all things a QTextCursor can do.
class TEXTEDITOR_EXPORT DocumentAssistTarget : public AssistTarget
{
public:
    explicit DocumentAssistTarget(QTextDocument *document);

    QTextDocument *document() const override;
    int position() const override;
    QChar characterAt(int position) const override;
    QString textAt(int position, int length) const override;
    QTextCursor textCursor() const override;
    QTextCursor textCursorAt(int position) const override;

    void setCursorPosition(int position) override;
    void replace(int position, int length, const QString &text) override;
    // A snippet's placeholders are the widget editor's; here the text is put in
    // as it stands, which is what a plain view can honour.
    void insertCodeSnippet(int basePosition,
                           const QString &snippet,
                           const SnippetParser &parse) override;

protected:
    // Where the placeholders of the snippet just inserted are. A plain
    // document has nowhere to offer them, so this does nothing; a view that
    // can draw them and tab between them overrides it.
    virtual void snippetInserted(const QList<SnippetPlaceholder> &placeholders);

    // Lay out what was just inserted the way the code style asks. A bare
    // QTextDocument has no indenter - that belongs to the TextDocument - so
    // this does nothing here and a view that has one overrides it.
    virtual void autoIndentRange(int from, int to);

private:
    QTextDocument *m_document = nullptr;
    QTextCursor m_cursor;
};

class TextEditorWidget;

// TextEditorWidget as an AssistTarget, so that the widget path is unchanged by
// the abstraction: every operation is the one the items were calling already.
class TEXTEDITOR_EXPORT WidgetAssistTarget final : public AssistTarget
{
public:
    explicit WidgetAssistTarget(TextEditorWidget *widget);

    QTextDocument *document() const override;
    int position() const override;
    QChar characterAt(int position) const override;
    QString textAt(int position, int length) const override;
    QTextCursor textCursor() const override;
    QTextCursor textCursorAt(int position) const override;

    void setCursorPosition(int position) override;
    void replace(int position, int length, const QString &text) override;
    void insertCodeSnippet(int basePosition,
                           const QString &snippet,
                           const SnippetParser &parse) override;
    void setAutoCompleteSkipPosition(const QTextCursor &cursor) override;
    void encourageApply() override;
    // The widget's own, which knows about rich text and block selections.
    void paste() override;

    // For the few places that still have to reach the widget itself.
    TextEditorWidget *widget() const { return m_widget; }

private:
    TextEditorWidget *m_widget = nullptr;
};

} // namespace TextEditor
