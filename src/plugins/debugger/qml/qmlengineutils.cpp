// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlengineutils.h"

#include <qmljs/parser/qmljsast_p.h>
#include <debugger/console/console.h>
#include <qmldebug/qdebugmessageclient.h>

#include <coreplugin/editormanager/documentmodel.h>

#include <texteditor/fontsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QTextBlock>

using namespace Core;
using namespace QmlDebug;
using namespace QmlJS;
using namespace QmlJS::AST;
using namespace TextEditor;
using namespace Utils;

namespace Debugger::Internal {

class ASTWalker : public Visitor
{
public:
    void operator()(Node *ast, quint32 *l, quint32 *c)
    {
        done = false;
        line = l;
        column = c;
        Node::accept(ast, this);
    }

    bool preVisit(Node *ast) override
    {
        return !done && ast->lastSourceLocation().startLine >= *line;
    }

    //Case 1: Breakpoint is between sourceStart(exclusive) and
    //        sourceEnd(inclusive) --> End tree walk.
    //Case 2: Breakpoint is on sourceStart --> Check for the start
    //        of the first executable code. Set the line number and
    //        column number. End tree walk.
    //Case 3: Breakpoint is on "unbreakable" code --> Find the next "breakable"
    //        code and check for Case 2. End tree walk.

    //Add more types when suitable.

    bool visit(UiScriptBinding *ast) override
    {
        if (!ast->statement)
            return true;

        quint32 sourceStartLine = ast->firstSourceLocation().startLine;
        quint32 statementStartLine;
        quint32 statementColumn;

        if (ast->statement->kind == Node::Kind_ExpressionStatement) {
            statementStartLine = ast->statement->firstSourceLocation().startLine;
            statementColumn = ast->statement->firstSourceLocation().startColumn;

        } else if (ast->statement->kind == Node::Kind_Block) {
            auto block = static_cast<Block *>(ast->statement);
            if (!block->statements)
                return true;
            statementStartLine = block->statements->firstSourceLocation().startLine;
            statementColumn = block->statements->firstSourceLocation().startColumn;

        } else {
            return true;
        }


        //Case 1
        //Check for possible relocation within the binding statement

        //Rewritten to (function <token>() { { }})
        //The offset 16 is position of inner lbrace without token length.
        const int offset = 16;

        //Case 2
        if (statementStartLine == *line) {
            if (sourceStartLine == *line)
                *column = offset + ast->qualifiedId->identifierToken.length;
            done = true;
        }

        //Case 3
        if (statementStartLine > *line) {
            *line = statementStartLine;
            if (sourceStartLine == *line)
                *column = offset + ast->qualifiedId->identifierToken.length;
            else
                *column = statementColumn;
            done = true;
        }
        return true;
    }

    bool visit(FunctionDeclaration *ast) override {
        quint32 sourceStartLine = ast->firstSourceLocation().startLine;
        quint32 sourceStartColumn = ast->firstSourceLocation().startColumn;
        quint32 statementStartLine = ast->body->firstSourceLocation().startLine;
        quint32 statementColumn = ast->body->firstSourceLocation().startColumn;

        //Case 1
        //Check for possible relocation within the function declaration

        //Case 2
        if (statementStartLine == *line) {
            if (sourceStartLine == *line)
                *column = statementColumn - sourceStartColumn + 1;
            done = true;
        }

        //Case 3
        if (statementStartLine > *line) {
            *line = statementStartLine;
            if (sourceStartLine == *line)
                *column = statementColumn - sourceStartColumn + 1;
            else
                *column = statementColumn;
            done = true;
        }
        return true;
    }

    bool visit(EmptyStatement *ast) override
    {
        *line = ast->lastSourceLocation().startLine + 1;
        return true;
    }

    bool visit(TemplateLiteral *ast) override
    {
        Node::accept(ast->expression, this);
        return true;
    }
    bool visit(VariableStatement *ast) override { test(ast); return true; }
    bool visit(VariableDeclarationList *ast) override { test(ast); return true; }
    bool visit(ExpressionStatement *ast) override { test(ast); return true; }
    bool visit(IfStatement *ast) override { test(ast); return true; }
    bool visit(DoWhileStatement *ast) override { test(ast); return true; }
    bool visit(WhileStatement *ast) override { test(ast); return true; }
    bool visit(ForStatement *ast) override { test(ast); return true; }
    bool visit(ForEachStatement *ast) override { test(ast); return true; }
    bool visit(ContinueStatement *ast) override { test(ast); return true; }
    bool visit(BreakStatement *ast) override { test(ast); return true; }
    bool visit(ReturnStatement *ast) override { test(ast); return true; }
    bool visit(WithStatement *ast) override { test(ast); return true; }
    bool visit(SwitchStatement *ast) override { test(ast); return true; }
    bool visit(CaseBlock *ast) override { test(ast); return true; }
    bool visit(CaseClauses *ast) override { test(ast); return true; }
    bool visit(CaseClause *ast) override { test(ast); return true; }
    bool visit(DefaultClause *ast) override { test(ast); return true; }
    bool visit(LabelledStatement *ast) override { test(ast); return true; }
    bool visit(ThrowStatement *ast) override { test(ast); return true; }
    bool visit(TryStatement *ast) override { test(ast); return true; }
    bool visit(Catch *ast) override { test(ast); return true; }
    bool visit(Finally *ast) override { test(ast); return true; }
    bool visit(FunctionExpression *ast) override { test(ast); return true; }
    bool visit(DebuggerStatement *ast) override { test(ast); return true; }

    void test(Node *ast)
    {
        quint32 statementStartLine = ast->firstSourceLocation().startLine;
        //Case 1/2
        if (statementStartLine <= *line && *line <= ast->lastSourceLocation().startLine)
            done = true;

        //Case 3
        if (statementStartLine > *line) {
            *line = statementStartLine;
            *column = ast->firstSourceLocation().startColumn;
            done = true;
        }
    }

    bool done;
    quint32 *line;
    quint32 *column;
};

void appendDebugOutput(QtMsgType type, const QString &message, const QDebugContextInfo &info)
{
    ConsoleItem::ItemType itemType;
    switch (type) {
    case QtInfoMsg:
    case QtDebugMsg:
        itemType = ConsoleItem::DebugType;
        break;
    case QtWarningMsg:
        itemType = ConsoleItem::WarningType;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        itemType = ConsoleItem::ErrorType;
        break;
    default:
        itemType = ConsoleItem::DefaultType;
        break;
    }

    debuggerConsole()->printItem(new ConsoleItem(itemType, message, info.file, info.line));
}

// The mark is a fact about the document - the exception was thrown on that
// line whoever is looking - and both views draw what the document holds.
// Both of these went to the widget editor alone, so a file in the Qt Quick
// editor showed nothing.
void clearExceptionSelection()
{
    const QList<IDocument *> documents = DocumentModel::openedDocuments();
    for (IDocument *document : documents) {
        if (auto * const textDocument = qobject_cast<TextDocument *>(document))
            textDocument->setExtraSelections(TextEditorWidget::DebuggerExceptionSelection, {});
    }
}

QStringList highlightExceptionCode(int lineNumber, const FilePath &filePath, const QString &errorMessage)
{
    QStringList messages;
    auto * const document
        = qobject_cast<TextDocument *>(DocumentModel::documentForFilePath(filePath));
    if (!document)
        return messages;
    const QTextBlock block = document->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid())
        return messages;

    const TextEditor::FontSettingsData fontSettings = TextEditor::globalFontSettings().data();
    QTextCharFormat errorFormat = fontSettings.toTextCharFormat(TextEditor::C_ERROR);
    errorFormat.setToolTip(errorMessage);

    // From the first non-blank of the line to its end.
    QTextCursor c(block);
    const QString text = block.text();
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isSpace()) {
            c.setPosition(c.position() + i);
            break;
        }
    }
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    document->setExtraSelections(TextEditorWidget::DebuggerExceptionSelection, {{c, errorFormat}});

    messages.append(QString::fromLatin1("%1: %2: %3").arg(filePath.toUserOutput()).arg(lineNumber).arg(errorMessage));
    return messages;
}

} // Debugger::Internal

#ifdef WITH_TESTS

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <texteditor/textviewport.h>
#include <utils/temporarydirectory.h>

#include <QQuickItem>
#include <QQuickWidget>
#include <QScopeGuard>
#include <QTest>

namespace Debugger::Internal {

// The QML debugger marks the line an exception was thrown on - red, the
// message as its tooltip - and clears the mark when the program moves on,
// and tells the console. All of it went to the widget editor alone, so a
// QML file in the Qt Quick editor showed nothing and the console was told
// nothing. The mark is the document's now, and either view draws it.
class QmlExceptionHighlightTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheExceptionLineIsMarkedInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheExceptionLineIsMarkedInEitherView()
    {
        QFETCH(bool, quick);

        TemporaryDirectory dir("debugger-qml-exception");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("main.qml");
        QVERIFY(file.writeFileContents("import QtQuick\n    Item { x: y }\n"));
        const Id editorId = quick ? Id(Core::Constants::K_QUICK_TEXT_EDITOR_ID)
                                  : Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        IEditor * const editor = EditorManager::openEditor(file, editorId);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        QCOMPARE(widget == nullptr, quick);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport *view = nullptr;
        if (quick) {
            auto * const host = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(host && host->rootObject());
            view = host->rootObject()->findChild<TextViewport *>();
            QVERIFY(view);
        }
        const Id kind = TextEditorWidget::DebuggerExceptionSelection;
        const QString error = "ReferenceError: y is not defined";

        const QStringList messages = highlightExceptionCode(2, file, error);
        QCOMPARE(messages.size(), 1);
        QVERIFY2(messages.first().contains(error), qPrintable(messages.first()));
        const QList<TextDocument::ExtraSelection> marked = document->extraSelections(kind);
        QCOMPARE(marked.size(), 1);
        const QTextBlock line = document->document()->findBlockByNumber(1);
        QCOMPARE(marked.first().cursor.selectionStart(), line.position() + 4);
        QCOMPARE(marked.first().cursor.selectionEnd(), line.position() + line.length() - 1);
        QCOMPARE(marked.first().format.toolTip(), error);
        // And whichever view shows the file draws it.
        if (widget)
            QTRY_COMPARE(widget->extraSelections(kind).size(), 1);
        else
            QTRY_COMPARE(view->highlights(kind).size(), 1);

        clearExceptionSelection();
        QVERIFY2(document->extraSelections(kind).isEmpty(),
                 "the mark stayed after the program moved on");
        if (widget)
            QTRY_VERIFY(widget->extraSelections(kind).isEmpty());
        else
            QTRY_VERIFY(view->highlights(kind).isEmpty());
    }
};

QObject *createQmlExceptionHighlightTest()
{
    return new QmlExceptionHighlightTest;
}

} // Debugger::Internal

#include "qmlengineutils.moc"

#endif // WITH_TESTS
