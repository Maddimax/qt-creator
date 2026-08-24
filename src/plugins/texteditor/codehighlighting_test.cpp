// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codehighlighting_test.h"

#include "codehighlighting.h"

#include "codedocument.h"
#include "codeassist/assistproposalitem.h"
#include "codeassist/assisttarget.h"
#include "textdocument.h"

#include "codeindenting.h"
#include "codestylepool.h"
#include "fontsettings.h"
#include "icodestylepreferences.h"
#include "snippets/snippetprovider.h"
#include "tabsettings.h"

#include <coreplugin/idocument.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/temporarydirectory.h>

#include <QScopeGuard>

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
    void testEverySnippetGroupSaysWhatItIsWrittenIn();
    void testAQuickEditShowsCreatorsOwnDocument();
    void testEditingThroughTheViewSavesTheFile();
    void testALanguageServerIsToldAboutTheFileOnlyWhenAsked();
    void testACompletionCanBeAppliedWithoutAWidget();
};

// An AssistTarget over a plain document and a cursor, which is what a Qt Quick
// view has. The operations are the ones the proposal items were calling on
// TextEditorWidget; none of them needs a widget to mean something.
class DocumentAssistTarget final : public AssistTarget
{
public:
    explicit DocumentAssistTarget(QTextDocument *document)
        : m_document(document)
        , m_cursor(document)
    {}

    QTextDocument *document() const override { return m_document; }
    int position() const override { return m_cursor.position(); }
    QChar characterAt(int position) const override { return m_document->characterAt(position); }
    QString textAt(int position, int length) const override
    {
        QTextCursor cursor(m_document);
        cursor.setPosition(position);
        cursor.setPosition(position + length, QTextCursor::KeepAnchor);
        return cursor.selectedText();
    }
    QTextCursor textCursor() const override { return m_cursor; }
    QTextCursor textCursorAt(int position) const override
    {
        QTextCursor cursor(m_document);
        cursor.setPosition(position);
        return cursor;
    }
    void setCursorPosition(int position) override { m_cursor.setPosition(position); }
    void replace(int position, int length, const QString &text) override
    {
        QTextCursor cursor(m_document);
        cursor.setPosition(position);
        cursor.setPosition(position + length, QTextCursor::KeepAnchor);
        cursor.insertText(text);
        m_cursor = cursor;
    }
    void insertCodeSnippet(int basePosition, const QString &snippet, const SnippetParser &) override
    {
        replace(basePosition, m_cursor.position() - basePosition, snippet);
    }

private:
    QTextDocument *m_document = nullptr;
    QTextCursor m_cursor;
};

// Stands in for the language client manager, which is reached by object name
// because TextEditor does not depend on the plugin it lives in. Registering one
// of these with that name is what lets a test see what it was told.
class LanguageClientManagerSpy final : public QObject
{
    Q_OBJECT

public:
    LanguageClientManagerSpy() { setObjectName("LanguageClientManager"); }

    QList<Core::IDocument *> opened;
    QList<Core::IDocument *> closed;

public slots:
    void documentOpened(Core::IDocument *document) { opened.append(document); }
    void documentClosed(Core::IDocument *document) { closed.append(document); }
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

// A Qt Quick TextEdit makes its own QTextDocument, which is nobody's file. The
// last settings page that is still on widgets needs one that is: opened from a
// path, saved back to it, and the kind of document a language client can be
// attached to. CodeDocument puts one behind the TextEdit instead.
void CodeHighlightingTest::testAQuickEditShowsCreatorsOwnDocument()
{
    Utils::TemporaryDirectory dir("codedocument-test");
    QVERIFY(dir.isValid());
    const Utils::FilePath file = dir.filePath("sample.cpp");
    QVERIFY(file.writeFileContents("int main() { return 0; }\n"));

    QQmlEngine engine;
    const std::unique_ptr<QQuickItem> edit = textEdit(&engine, "not the file");
    QVERIFY(edit);
    QQuickTextDocument *quickDocument = documentOf(edit.get());
    QVERIFY(quickDocument);
    QTextDocument *ownDocument = quickDocument->textDocument();
    QVERIFY(ownDocument);

    CodeDocument document;
    document.setDocument(quickDocument);
    document.setFilePath(file);
    QVERIFY(document.isOpened());

    // The edit is showing the file, and showing it through the document Qt
    // Creator opened rather than the one the TextEdit made for itself.
    QVERIFY(document.textDocument());
    QCOMPARE(quickDocument->textDocument(), document.textDocument()->document());
    QVERIFY(quickDocument->textDocument() != ownDocument);
    QCOMPARE(quickDocument->textDocument()->toPlainText(), QString("int main() { return 0; }\n"));
}

void CodeHighlightingTest::testEditingThroughTheViewSavesTheFile()
{
    Utils::TemporaryDirectory dir("codedocument-save-test");
    QVERIFY(dir.isValid());
    const Utils::FilePath file = dir.filePath("sample.txt");
    QVERIFY(file.writeFileContents("before\n"));

    QQmlEngine engine;
    const std::unique_ptr<QQuickItem> edit = textEdit(&engine, {});
    QVERIFY(edit);
    CodeDocument document;
    document.setDocument(documentOf(edit.get()));
    document.setFilePath(file);
    QVERIFY(document.isOpened());
    QVERIFY(!document.isModified());

    // What typing in the view comes to: the document is the view's, so the
    // edit is on the file's document and the file knows it has changed.
    QTextCursor cursor(document.textDocument()->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText("after\n");
    QVERIFY(document.isModified());
    QCOMPARE(file.fileContents().value_or(QByteArray()), QByteArray("before\n"));

    QVERIFY(document.save());
    QVERIFY(!document.isModified());
    QCOMPARE(file.fileContents().value_or(QByteArray()), QByteArray("after\n"));
}

// A document opened through the editor manager is announced to a language
// server for free. One opened by a settings page is not, so a page that wants
// completion or diagnostics has to ask - and a page that does not should not be
// starting servers behind the user's back.
void CodeHighlightingTest::testALanguageServerIsToldAboutTheFileOnlyWhenAsked()
{
    Utils::TemporaryDirectory dir("codedocument-lsp-test");
    QVERIFY(dir.isValid());
    const Utils::FilePath file = dir.filePath("sample.cpp");
    QVERIFY(file.writeFileContents("int main() {}\n"));

    // The real manager answers to this name too, and the first object with it
    // wins; taking it out is what makes the spy the one that is found.
    QObject *real = ExtensionSystem::PluginManager::getObjectByName("LanguageClientManager");
    if (real)
        ExtensionSystem::PluginManager::removeObject(real);
    LanguageClientManagerSpy spy;
    ExtensionSystem::PluginManager::addObject(&spy);
    const QScopeGuard restore([real, &spy] {
        ExtensionSystem::PluginManager::removeObject(&spy);
        if (real)
            ExtensionSystem::PluginManager::addObject(real);
    });

    QQmlEngine engine;
    const std::unique_ptr<QQuickItem> edit = textEdit(&engine, {});
    QVERIFY(edit);

    {
        CodeDocument document;
        document.setDocument(documentOf(edit.get()));
        document.setFilePath(file);
        QVERIFY(document.isOpened());

        // Not asked for: nothing was said about it.
        QVERIFY(spy.opened.isEmpty());

        document.setUseLanguageServer(true);
        QCOMPARE(spy.opened.size(), 1);
        QCOMPARE(spy.opened.first(), static_cast<Core::IDocument *>(document.textDocument()));

        // Asking twice does not announce it twice.
        document.setUseLanguageServer(true);
        QCOMPARE(spy.opened.size(), 1);
        QVERIFY(spy.closed.isEmpty());
    }

    // And it is taken back when the document goes, so the server is not left
    // tracking a file nothing is showing.
    QCOMPARE(spy.closed.size(), 1);
}

// Accepting a completion used to take a TextEditorWidget, which is why nothing
// but a widget could ever offer one. What the items actually do is replace a
// range of text, so a target over a plain document is enough.
void CodeHighlightingTest::testACompletionCanBeAppliedWithoutAWidget()
{
    // The word being completed does not start the document, so that replacing
    // from the base position and replacing from the beginning are not the same
    // thing - which is the mistake worth catching.
    QTextDocument document("using QStr");
    DocumentAssistTarget target(&document);
    target.setCursorPosition(10);

    AssistProposalItem item;
    item.setText("QString");

    item.apply(target, 6);

    QCOMPARE(document.toPlainText(), QString("using QString"));
    QCOMPARE(target.position(), 13);
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

void CodeHighlightingTest::testEverySnippetGroupSaysWhatItIsWrittenIn()
{
    // A group's decorator only tells a TextEditorWidget how to highlight
    // itself. A renderer without a widget needs the mime type instead, so a
    // group that does not name one cannot be shown highlighted at all.
    const QList<SnippetProvider> providers = SnippetProvider::snippetProviders();
    QVERIFY(!providers.isEmpty());

    QStringList unnamed;
    for (const SnippetProvider &provider : providers) {
        const QString mimeType = SnippetProvider::mimeTypeForGroup(provider.groupId());
        QCOMPARE(mimeType, provider.mimeType());
        if (mimeType.isEmpty())
            unnamed << provider.groupId();
        else
            QVERIFY2(mimeType.contains('/'), qPrintable(provider.groupId() + ": " + mimeType));
    }
    QVERIFY2(unnamed.isEmpty(),
             qPrintable("snippet groups with no mime type: " + unnamed.join(", ")));

    // And one that was never registered has none rather than something.
    QVERIFY(SnippetProvider::mimeTypeForGroup("NoSuchGroup").isEmpty());
}

QObject *createCodeHighlightingTest()
{
    return new CodeHighlightingTest;
}

} // namespace TextEditor::Internal

#include "codehighlighting_test.moc"
