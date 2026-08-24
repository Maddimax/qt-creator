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

    // For the few places that still have to reach the widget itself.
    TextEditorWidget *widget() const { return m_widget; }

private:
    TextEditorWidget *m_widget = nullptr;
};

} // namespace TextEditor
