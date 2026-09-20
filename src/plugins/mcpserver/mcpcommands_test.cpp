// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcpcommands_test.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <mcp/server/toolregistry.h>

#include <texteditor/texteditor.h>
#include "mcpcommands.h"
#include <texteditor/textdocument.h>

#include <utils/filepath.h>
#include <utils/temporarydirectory.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTest>
#include <QTextCursor>

using namespace Utils;

namespace Mcp::Internal {

// Invokes a registered tool and returns its structured content. A registry-level
// refusal (unknown tool, schema violation) lands in *error, so a test can tell
// it apart from an error the tool itself reported in "reason".
static QJsonObject callTool(const QString &name, const QJsonObject &arguments, QString *error)
{
    error->clear();
    const Result<Schema::CallToolResult> result = ToolRegistry::callToolForTests(
        name, Schema::CallToolRequestParams{}.arguments(arguments));
    if (!result) {
        *error = result.error();
        return {};
    }
    return result->structuredContentAsObject();
}

// The editor, in whichever view a plain text file opens in today: what the
// tests ask of it - the caret - either view answers.
static Core::IEditor *openText(const TemporaryDirectory &dir, const QByteArray &text)
{
    const FilePath filePath = dir.filePath("selection.txt");
    if (!filePath.writeFileContents(text))
        return nullptr;
    return Core::EditorManager::openEditor(filePath);
}

class McpCommandsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testSelectTextSpansWholeLinesByDefault();
    void testSelectTextTakesOneBasedColumns();
    void testSelectTextRejectsAnInvalidRange();
    void testFindWidgetsReportsATextEditAsAnExcerpt();
    // reformat_file asked the editor for a TextEditorWidget and gave up
    // without one, so the tool failed outright on a C++ file - which opens in
    // the Qt Quick editor.
    void testReformatReachesAViewThatIsNotAWidget()
    {
        Utils::TemporaryDirectory dir("mcp-reformat-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("crooked.cpp");
        QVERIFY(file.writeFileContents("int main()\n{\nreturn 0;\n}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        // The fixture only means anything if the file really is in the view
        // this is about.
        Core::IEditor * const opened = Core::EditorManager::openEditor(file);
        QVERIFY2(opened, "the editor manager opened nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(opened),
                 "the C++ file opened in a widget editor, so this tests nothing");

        McpCommands commands;
        QVERIFY2(commands.reformatFile(file.toUserOutput()),
                 "the tool refused the file it had just been given");

        auto * const document
            = qobject_cast<TextEditor::TextDocument *>(opened->document());
        QVERIFY(document);
        QTRY_VERIFY2(document->plainText().contains("    return 0;"),
                     qPrintable("the body was not indented:\n" + document->plainText()));
    }

    // editor_select_text and editor_get_folds cast the current editor's widget
    // to a TextEditorWidget and answered "no_text_editor" without one - for
    // every file in the Qt Quick editor, which is where a C++ file opens.
    void testSelectTextAndFoldsReachEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testSelectTextAndFoldsReachEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("mcp-editor-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("folds.cpp");
        QVERIFY(file.writeFileContents("int main()\n{\n    return 0;\n}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);

        QString error;
        const QJsonObject selected
            = callTool("editor_select_text", {{"start_line", 2}, {"end_line", 3}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(selected.value("reason").toString(), QString("ok"));
        QCOMPARE(selected.value("text").toString(), QString("{\n    return 0;"));
        // The selection the tool exists for is the editor's own, in whichever
        // view the file is in.
        const QTextCursor cursor = TextEditor::textCursorOf(editor);
        QVERIFY2(cursor.hasSelection(), "the editor has no selection after the tool made one");
        QString own = cursor.selectedText();
        own.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        QCOMPARE(own, QString("{\n    return 0;"));

        // The folds, once the highlighter has said where they are: the body
        // of main() is one level in, and the first line is not.
        const auto foldIndentOfLine = [&error](int line) -> int {
            const QJsonObject folds = callTool("editor_get_folds", {}, &error);
            if (folds.value("reason").toString() != "ok")
                return -1;
            const QJsonArray lines = folds.value("lines").toArray();
            if (lines.size() < line)
                return -1;
            return lines.at(line - 1).toObject().value("fold_indent").toInt(-1);
        };
        QTRY_COMPARE(foldIndentOfLine(3), 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(foldIndentOfLine(1), 0);
    }
};

void McpCommandsTest::testSelectTextSpansWholeLinesByDefault()
{
    TemporaryDirectory dir("qtc-mcpcommands-XXXXXX");
    QVERIFY(dir.isValid());
    const QScopeGuard closeEditors([] { Core::EditorManager::closeAllEditors(false); });
    Core::IEditor * const editor = openText(dir, "alpha one\nbeta two\ngamma three\n");
    QVERIFY(editor);

    QString error;
    const QJsonObject result
        = callTool("editor_select_text", {{"start_line", 2}, {"end_line", 3}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.value("reason").toString(), QString("ok"));

    // Without a column the range runs from the start of the first line to the
    // end of the last one, and the line separators are newlines rather than the
    // U+2029 QTextCursor::selectedText() reports.
    QCOMPARE(result.value("text").toString(), QString("beta two\ngamma three"));
    QVERIFY(!result.value("text").toString().contains(QChar::ParagraphSeparator));

    // The selection the tool exists for is the editor's own, not just the text.
    const QTextCursor cursor = TextEditor::textCursorOf(editor);
    QCOMPARE(cursor.selectionStart(), 10); // Behind "alpha one\n".
    QCOMPARE(cursor.selectionEnd(), 30);   // Behind "gamma three", before its newline.
}

void McpCommandsTest::testSelectTextTakesOneBasedColumns()
{
    TemporaryDirectory dir("qtc-mcpcommands-XXXXXX");
    QVERIFY(dir.isValid());
    const QScopeGuard closeEditors([] { Core::EditorManager::closeAllEditors(false); });
    QVERIFY(openText(dir, "alpha one\nbeta two\n"));

    QString error;
    const QJsonObject result = callTool(
        "editor_select_text",
        {{"start_line", 2}, {"start_column", 6}, {"end_line", 2}, {"end_column", 9}},
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.value("text").toString(), QString("two"));

    // A column past the end of the line selects up to that end.
    const QJsonObject clamped = callTool(
        "editor_select_text",
        {{"start_line", 1}, {"start_column", 7}, {"end_line", 1}, {"end_column", 99}},
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(clamped.value("text").toString(), QString("one"));
}

void McpCommandsTest::testSelectTextRejectsAnInvalidRange()
{
    TemporaryDirectory dir("qtc-mcpcommands-XXXXXX");
    QVERIFY(dir.isValid());
    const QScopeGuard closeEditors([] { Core::EditorManager::closeAllEditors(false); });
    QVERIFY(openText(dir, "alpha one\nbeta two\n"));

    QString error;
    const QJsonObject reversed
        = callTool("editor_select_text", {{"start_line", 2}, {"end_line", 1}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reversed.value("reason").toString(), QString("invalid_range"));

    const QJsonObject pastEnd
        = callTool("editor_select_text", {{"start_line", 1}, {"end_line", 99}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(pastEnd.value("reason").toString(), QString("invalid_range"));
}

void McpCommandsTest::testFindWidgetsReportsATextEditAsAnExcerpt()
{
    TemporaryDirectory dir("qtc-mcpcommands-XXXXXX");
    QVERIFY(dir.isValid());
    const QScopeGuard closeEditors([] { Core::EditorManager::closeAllEditors(false); });
    // The tool finds QWidgets, so this needs the widget editor. A plain text
    // file's default editor is the Qt Quick one - a factory of its own, ahead
    // of the plain text editor's - so the widget editor is asked for by id.
    // The ampersands are content, not accelerator markers, and the document is
    // longer than the excerpt the tool answers with.
    const FilePath filePath = dir.filePath("selection.txt");
    QVERIFY(filePath.writeFileContents("cmake --build . && ctest\n" + QByteArray(600, 'x')));
    Core::IEditor * const editor
        = Core::EditorManager::openEditor(filePath, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
    QVERIFY(editor);
    TextEditor::TextEditorWidget * const widget = TextEditor::TextEditorWidget::fromEditor(editor);
    QVERIFY2(widget, "the plain text file did not open in the widget editor it was asked for");
    widget->setObjectName("mcpCommandsTestEdit");

    QString error;
    const QJsonObject result = callTool(
        "ui_find_widgets",
        {{"object_name", "mcpCommandsTestEdit"}, {"include_invisible", true}},
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.value("count").toInt(), 1);

    const QJsonObject found = result.value("widgets").toArray().first().toObject();
    const QString text = found.value("text").toString();
    QVERIFY(text.startsWith("cmake --build . && ctest"));
    QCOMPARE(text.size(), 400);
    QVERIFY(found.value("text_truncated").toBool());
}

QObject *createMcpCommandsTest()
{
    return new McpCommandsTest;
}

} // namespace Mcp::Internal

#include "mcpcommands_test.moc"
