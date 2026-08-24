// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codehighlighting_test.h"

#include "codehighlighting.h"

#include "codeindenting.h"
#include "codestylepool.h"
#include "fontsettings.h"
#include "icodestylepreferences.h"
#include "tabsettings.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>

namespace TextEditor::Internal {

// A highlighter works on a QTextDocument, and a Qt Quick TextEdit has one, so
// code can be shown highlighted without any of TextEditorWidget. These check
// that it really is highlighted rather than merely built.
class CodeHighlightingTest : public QObject
{
    Q_OBJECT

private slots:
    void testHighlightsAQuickDocument();
    void testUnknownMimeTypeStillShowsTheText();
    void testFollowsTheEditorFont();
    void testIndentsAQuickDocument();
    void testUnknownLanguageLeavesTheTextAlone();
};

// The character formats a highlighter left on the first line, minus the
// document's own default. Empty means nothing was highlighted.
static int formatRunsOnFirstLine(QTextDocument *document)
{
    const QTextBlock block = document->firstBlock();
    return int(block.layout() ? block.layout()->formats().size() : 0);
}

static std::unique_ptr<QQuickItem> textEdit(QQmlEngine *engine, const QString &text)
{
    QQmlComponent component(engine);
    component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }",
                      QUrl("qrc:/codehighlightingtest.qml"));
    auto item = std::unique_ptr<QQuickItem>(qobject_cast<QQuickItem *>(component.create()));
    if (item)
        item->setProperty("text", text);
    return item;
}

static QQuickTextDocument *documentOf(QQuickItem *edit)
{
    return edit->property("textDocument").value<QQuickTextDocument *>();
}

void CodeHighlightingTest::testHighlightsAQuickDocument()
{
    QQmlEngine engine;
    const std::unique_ptr<QQuickItem> edit
        = textEdit(&engine, "int main() { return 0; } // a comment\n");
    QVERIFY(edit);
    QQuickTextDocument *quickDocument = documentOf(edit.get());
    QVERIFY(quickDocument);

    // Nothing has coloured it yet.
    QCOMPARE(formatRunsOnFirstLine(quickDocument->textDocument()), 0);

    CodeHighlighting highlighting;
    highlighting.setDocument(quickDocument);
    highlighting.setMimeType("text/x-c++src");
    QVERIFY2(highlighting.isHighlighting(),
             "no highlight definition for C++ - is KSyntaxHighlighting's data available?");

    // The keyword, the number and the comment are all coloured differently, so
    // the line ends up in several runs.
    QTRY_VERIFY(formatRunsOnFirstLine(quickDocument->textDocument()) > 1);
}

void CodeHighlightingTest::testUnknownMimeTypeStillShowsTheText()
{
    QQmlEngine engine;
    const std::unique_ptr<QQuickItem> edit = textEdit(&engine, "some text\n");
    QVERIFY(edit);

    CodeHighlighting highlighting;
    highlighting.setDocument(documentOf(edit.get()));
    highlighting.setMimeType("application/x-nothing-knows-this");

    // No definition, so no highlighter - and the text is still there, which is
    // the point: a view of code with no definition is plain, not empty.
    QVERIFY(!highlighting.isHighlighting());
    QCOMPARE(edit->property("text").toString(), QString("some text\n"));

    // Naming a type it does know attaches one.
    highlighting.setMimeType("text/x-c++src");
    QVERIFY(highlighting.isHighlighting());

    // And taking the document away detaches it again.
    highlighting.setDocument(nullptr);
    QVERIFY(!highlighting.isHighlighting());
}

void CodeHighlightingTest::testFollowsTheEditorFont()
{
    CodeHighlighting highlighting;

    // A view of code uses the editor's font and the scheme's plain-text
    // colours, not the form's tokens and not a default. Compared against the
    // settings rather than merely checked for being set: a default QFont has a
    // family too, so "not empty" would pass whatever this returned.
    const FontSettingsData &settings = globalFontSettings().data();
    QCOMPARE(highlighting.font(), settings.font());
    QCOMPARE(highlighting.textColor(), settings.toTextCharFormat(C_TEXT).foreground().color());
    QCOMPARE(highlighting.backgroundColor(),
             settings.toTextCharFormat(C_TEXT).background().color());
}

void CodeHighlightingTest::testIndentsAQuickDocument()
{
    ICodeStylePreferences *codeStyle = codeStyleForLanguage("Cpp");
    QVERIFY2(codeStyle, "no C++ code style - is the CppEditor plugin loaded?");

    QQmlEngine engine;
    // Deliberately flat: every line at column zero, so that any indentation at
    // all is the indenter's doing.
    const std::unique_ptr<QQuickItem> edit
        = textEdit(&engine, "int f()\n{\nif (true) {\nreturn 1;\n}\nreturn 0;\n}\n");
    QVERIFY(edit);

    CodeIndenting indenting;
    indenting.setDocument(documentOf(edit.get()));
    indenting.setLanguageId("Cpp");
    indenting.setCodeStyle(codeStyle);
    QVERIFY2(indenting.isIndenting(), "no indenter for C++");

    indenting.reindent();

    // The body of f() and the body of the if are now indented, and by
    // different amounts - which is the whole point of showing a preview.
    const QStringList lines = edit->property("text").toString().split('\n');
    QCOMPARE(lines.size(), 8);
    const auto indentOf = [](const QString &line) {
        return int(line.size() - QStringView(line).trimmed().size());
    };
    QCOMPARE(indentOf(lines.at(0)), 0);          // int f()
    QCOMPARE(indentOf(lines.at(1)), 0);          // {
    QVERIFY(indentOf(lines.at(2)) > 0);          // if (true) {
    QVERIFY(indentOf(lines.at(3)) > indentOf(lines.at(2))); // return 1;
    QCOMPARE(indentOf(lines.at(4)), indentOf(lines.at(2))); // }
    QCOMPARE(indentOf(lines.at(6)), 0);          // }

    // And it is *these* settings it indents by, which is what a preview is
    // for: widening the indent has to widen the preview, by itself. Merely
    // checking that something was indented passes with any settings at all.
    const int wasIndented = indentOf(lines.at(2));
    // The settings that are current, which may be a delegate's rather than
    // this object's own.
    ICodeStylePreferences *current = codeStyle->currentPreferences();
    QVERIFY(current);
    const TabSettingsData original = current->tabSettings();
    TabSettingsData wider = original;
    wider.m_indentSize = original.m_indentSize + 3;
    wider.m_tabSize = wider.m_indentSize;
    current->setTabSettings(wider);

    // No reindent() call: changing the style is what triggers it.
    QTRY_COMPARE(indentOf(edit->property("text").toString().split('\n').at(2)),
                 wasIndented + 3);

    current->setTabSettings(original);
    QTRY_COMPARE(indentOf(edit->property("text").toString().split('\n').at(2)), wasIndented);
}

void CodeHighlightingTest::testUnknownLanguageLeavesTheTextAlone()
{
    QQmlEngine engine;
    const QString code = "int f()\n{\nreturn 0;\n}\n";
    const std::unique_ptr<QQuickItem> edit = textEdit(&engine, code);
    QVERIFY(edit);

    CodeIndenting indenting;
    indenting.setDocument(documentOf(edit.get()));
    indenting.setLanguageId("NoSuchLanguage");
    indenting.setCodeStyle(codeStyleForLanguage("Cpp"));

    // No factory, so no indenter - and the text is left as it was rather than
    // flattened or emptied.
    QVERIFY(!indenting.isIndenting());
    indenting.reindent();
    QCOMPARE(edit->property("text").toString(), code);
}

QObject *createCodeHighlightingTest()
{
    return new CodeHighlightingTest;
}

} // namespace TextEditor::Internal

#include "codehighlighting_test.moc"
