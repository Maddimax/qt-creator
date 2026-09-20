// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef BINDINGEDITORWIDGET_H
#define BINDINGEDITORWIDGET_H

#include <texteditor/texteditor.h>
#include <qmljseditor/qmljseditordocument.h>
#include <qmljseditor/qmljssemantichighlighter.h>

#include <QPointer>

namespace QmlDesigner {

inline constexpr char BINDINGEDITOR_CONTEXT_ID[] = "BindingEditor.BindingEditorContext";

// The text of a binding or a connection being edited in a dialog: a QML/JS
// document of its own, completed against the design document's semantic
// info rather than its own, and told when Return means "done". The dialog
// shows it in the Qt Quick editor; what a widget subclass used to do on key
// presses and for completion is the document's now, so either view has it.
class BindingDocument : public QmlJSEditor::QmlJSEditorDocument
{
    Q_OBJECT

public:
    BindingDocument();
    ~BindingDocument();

    // The document the binding belongs to, whose scope completion offers.
    void setDesignDocument(QmlJSEditor::QmlJSEditorDocument *document);
    QmlJSEditor::QmlJSEditorDocument *designDocument() const;

    // Whether Return inserts a line (Ctrl+Return is then "done") or is "done"
    // by itself.
    bool isMultiline() const;
    void setMultiline(bool multiline);

    // Replaces the text and indents it as the design document would.
    void setTextWithIndentation(const QString &text);

    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
        const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
        Core::IEditor *editor = nullptr) const override;
    bool handleKeyPress(QKeyEvent *event, const QTextCursor &cursor) override;

signals:
    void returnKeyClicked();

protected:
    void applyFontSettings() final;
    void triggerPendingUpdates() final;

private:
    QmlJSEditor::SemanticHighlighter *m_semanticHighlighter = nullptr;
    QPointer<QmlJSEditor::QmlJSEditorDocument> m_designDocument;
    bool m_isMultiline = false;
};

class BindingEditorFactory : public TextEditor::TextEditorFactory
{
public:
    BindingEditorFactory();
};

#ifdef WITH_TESTS
QObject *createBindingEditorTest();
#endif

} // namespace QmlDesigner

#endif //BINDINGEDITORWIDGET_H
