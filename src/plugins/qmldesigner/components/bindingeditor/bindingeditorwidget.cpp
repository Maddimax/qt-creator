// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bindingeditorwidget.h"

#include <indentingtexteditormodifier.h>
#include <qmldesignertr.h>

#include <qmljseditor/qmljsautocompleter.h>
#include <qmljseditor/qmljscompletionassist.h>
#include <qmljseditor/qmljshoverhandler.h>
#include <utils/mimeconstants.h>

#include <QKeyEvent>

namespace QmlDesigner {

BindingDocument::BindingDocument()
    : QmlJSEditor::QmlJSEditorDocument(BINDINGEDITOR_CONTEXT_ID)
    , m_semanticHighlighter(new QmlJSEditor::SemanticHighlighter(this))
{
}

BindingDocument::~BindingDocument()
{
    delete m_semanticHighlighter;
}

void BindingDocument::setDesignDocument(QmlJSEditor::QmlJSEditorDocument *document)
{
    m_designDocument = document;
}

QmlJSEditor::QmlJSEditorDocument *BindingDocument::designDocument() const
{
    return m_designDocument;
}

bool BindingDocument::isMultiline() const
{
    return m_isMultiline;
}

void BindingDocument::setMultiline(bool multiline)
{
    m_isMultiline = multiline;
}

void BindingDocument::setTextWithIndentation(const QString &text)
{
    QTextDocument * const doc = document();
    doc->setPlainText(text);
    // We don't need to indent an empty text, but it is also needed for a safer
    // text.length() - 1 below.
    if (text.isEmpty())
        return;
    auto modifier = std::make_unique<IndentingTextEditModifier>(doc);
    modifier->indent(0, text.size() - 1);
}

std::unique_ptr<TextEditor::AssistInterface> BindingDocument::createAssistInterface(
    const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
    Core::IEditor *editor) const
{
    // Completed against the design document, whose ids and properties a
    // binding refers to, rather than against these few lines.
    if (kind == TextEditor::Completion) {
        return std::make_unique<QmlJSEditor::QmlJSCompletionAssistInterface>(
            cursor, Utils::FilePath(), reason,
            m_designDocument ? m_designDocument->semanticInfo() : QmlJSTools::SemanticInfo());
    }
    return QmlJSEditorDocument::createAssistInterface(cursor, kind, reason, editor);
}

bool BindingDocument::handleKeyPress(QKeyEvent *event, const QTextCursor &cursor)
{
    const bool returnPressed = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (returnPressed) {
        const Qt::KeyboardModifiers mods = event->modifiers();
        constexpr Qt::KeyboardModifier submitModifier = Qt::ControlModifier;
        if ((!m_isMultiline && !mods) || (m_isMultiline && mods.testFlag(submitModifier))) {
            emit returnKeyClicked();
            return true;
        }
    }
    return QmlJSEditorDocument::handleKeyPress(event, cursor);
}

void BindingDocument::applyFontSettings()
{
    TextDocument::applyFontSettings();
    m_semanticHighlighter->updateFontSettings(fontSettings());
    if (!isSemanticInfoOutdated() && semanticInfo().isValid())
        m_semanticHighlighter->rerun(semanticInfo());
}

void BindingDocument::triggerPendingUpdates()
{
    TextDocument::triggerPendingUpdates(); // calls applyFontSettings if necessary
    if (!isSemanticInfoOutdated() && semanticInfo().isValid())
        m_semanticHighlighter->rerun(semanticInfo());
}

BindingEditorFactory::BindingEditorFactory()
{
    setId(BINDINGEDITOR_CONTEXT_ID);
    setDisplayName(Tr::tr("Binding Editor"));
    addMimeType(BINDINGEDITOR_CONTEXT_ID);
    addMimeType(Utils::Constants::QML_MIMETYPE);
    addMimeType(Utils::Constants::QMLTYPES_MIMETYPE);
    addMimeType(Utils::Constants::JS_MIMETYPE);

    setDocumentCreator([]() { return new BindingDocument; });
    setAutoCompleterCreator([]() { return new QmlJSEditor::AutoCompleter; });
    setCommentDefinition(Utils::CommentDefinition::CppStyle);
    setParenthesesMatchingEnabled(true);
    addHoverHandler(&QmlJSEditor::qmlJSHoverHandler());
    setCompletionAssistProvider(new QmlJSEditor::QmlJSCompletionAssistProvider);

    // The Qt Quick editor, a few lines of an expression in a dialog: no line
    // numbers, no marks, nothing to fold, no tool bar. The document brings the
    // QML highlighter and indenter, and completion is the editor's own
    // Ctrl+Space, in its own context.
    setUsesQuickEditor(true);
    setLineNumbersVisible(false);
    setMarksVisible(false);
    setCodeFoldingSupported(false);
    setToolBarVisible(false);
}

} // QmlDesigner namespace

#ifdef WITH_TESTS

#include <coreplugin/editormanager/ieditor.h>

#include <QSignalSpy>
#include <QTest>

namespace QmlDesigner {

// The binding editor was a TextEditorWidget subclass: Return meant "done" in
// an event() override, completion took the design document's scope in a
// createAssistInterface() override, and Ctrl+Space was an action of its own.
// It is the Qt Quick editor over a document that answers the same, so the
// dialog embeds either view alike.
class BindingEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheBindingEditorIsTheQtQuickEditor()
    {
        BindingEditorFactory factory;
        QVERIFY2(factory.usesQuickEditor(), "the binding editor is the widget editor");
        QVERIFY2(!factory.lineNumbersVisible() && !factory.marksVisible(),
                 "an expression of a few lines has a gutter");

        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor.get()),
                 "the binding editor opened as a widget");
        auto * const document = qobject_cast<BindingDocument *>(editor->document());
        QVERIFY2(document, "the binding editor's document is not a BindingDocument");
        QVERIFY2(document->syntaxHighlighter(), "the binding has no highlighter");
        QVERIFY2(document->indenter(), "the binding has no indenter");

        // Return is "done" for one line; for several, Ctrl+Return is and
        // Return is a line.
        document->setPlainText("parent.width");
        QTextCursor cursor(document->document());
        QSignalSpy done(document, &BindingDocument::returnKeyClicked);
        QKeyEvent plainReturn(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QVERIFY(document->handleKeyPress(&plainReturn, cursor));
        QCOMPARE(done.count(), 1);
        document->setMultiline(true);
        QVERIFY(!document->handleKeyPress(&plainReturn, cursor));
        QCOMPARE(done.count(), 1);
        QKeyEvent submit(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(document->handleKeyPress(&submit, cursor));
        QCOMPARE(done.count(), 2);

        // Completion is asked for with the design document's scope, and is
        // still answered with none - the dialog is opened without one only
        // in a test.
        QVERIFY2(document->createAssistInterface(cursor, TextEditor::Completion,
                                                 TextEditor::ExplicitlyInvoked),
                 "no completion interface without a design document");

        // And the text set with indentation is indented as QML.
        document->setTextWithIndentation("if (x) {\ny\n}");
        QCOMPARE(document->plainText(), QString("if (x) {\n    y\n}"));
    }
};

QObject *createBindingEditorTest()
{
    return new BindingEditorTest;
}

} // namespace QmlDesigner

#include "bindingeditorwidget.moc"

#endif // WITH_TESTS
