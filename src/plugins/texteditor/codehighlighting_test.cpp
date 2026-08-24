// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codehighlighting_test.h"

#include "codehighlighting.h"

#include "fontsettings.h"

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

QObject *createCodeHighlightingTest()
{
    return new CodeHighlightingTest;
}

} // namespace TextEditor::Internal

#include "codehighlighting_test.moc"
