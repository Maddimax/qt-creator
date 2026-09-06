// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport_test.h"

#include "codebuffer.h"
#include "inlinediffdecorator.h"
#include "displaysettings.h"
#include "codedocument.h"
#include "codeindenting.h"
#include "codestylepool.h"
#include "icodestylepreferences.h"
#include "snippets/snippetprovider.h"
#include "icodestylepreferencesfactory.h"
#include "autocompleter.h"
#include "texteditor.h"
#include "refactoroverlay.h"
#include "behaviorsettings.h"
#include "codeassist/documentcontentcompletion.h"
#include "completionsettings.h"
#include "fontsettings.h"
#include "syntaxhighlighter.h"
#include "tabsettings.h"
#include "textdocument.h"
#include "typingsettings.h"
#include "textdocumentlayout.h"
#include "texteditorconstants.h"
#include "marginsettings.h"
#include "codeassist/assistproposalitem.h"
#include "codeassist/genericproposal.h"
#include "codeassist/genericproposalmodel.h"
#include "codeassist/iassistprocessor.h"
#include "codeassist/iassistprovider.h"
#include "codeassist/assistinterface.h"
#include "codeassist/completionassistprovider.h"
#include "codeassist/ifunctionhintproposalmodel.h"
#include "codeassist/functionhintproposal.h"
#include "codeassist/assisttarget.h"
#include "highlighterhelper.h"
#include "textindenter.h"
#include "textoperations.h"
#include "textsuggestion.h"
#include "textviewport.h"
#include "circularclipboard.h"
#include "textmark.h"
#include "bookmarkmanager.h"

#include <qtcquick/qtciconprovider.h>

#include <qtcquick/actionmodel.h>
#include <qtcquick/qtcquickengine.h>

#include <utils/utilsicons.h>

#include <utils/aspects.h>
#include <utils/plaintextedit/texteditorlayout.h>
#include <utils/temporarydirectory.h>
#include <utils/hostosinfo.h>
#include <utils/theme/theme.h>

#include <QQuickStyle>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QClipboard>
#include <QFile>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QMenu>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QApplication>
#include <QStyleHints>
#include <QWheelEvent>
#include <QSet>
#include <QTest>

using namespace Utils;

namespace TextEditor::Internal {

// The chord this platform uses for a standard move. End is the end of the line
// on Windows and the end of the *file* on a Mac, so a test that types Key_End
// is asserting the platform rather than the editor.
static void keyMove(QQuickView &view, QKeySequence::StandardKey key)
{
    QTest::keySequence(&view, QKeySequence(key));
}

// Creator's icons reach QML as image://qtcreator/... URLs, which resolve only
// if the provider is on the engine. The shared engine has it; a view made here
// has its own, so without this an icon fails to load and the test cannot tell
// a wrong URL from a missing provider.
static void installIconProvider(QQuickView &view)
{
    view.engine()->addImageProvider(QLatin1String(QtcQuick::IconProvider::name()),
                                    new QtcQuick::IconProvider);
}

// A viewport in a window, because polish only happens for an item in a scene:
// without one nothing is ever laid out and every assertion below reads zero.
class ViewportFixture
{
public:
    explicit ViewportFixture(const FilePath &file, int width = 400, int height = 200)
        : ViewportFixture(nullptr, width, height)
    {
        if (viewport) {
            document.setFilePath(file);
            viewport->setDocument(&document);
        }
    }

    // Anything else a viewport can draw. A file goes through the constructor
    // above, which is the common case; a CodeBuffer comes in here.
    explicit ViewportFixture(CodeSource *source, int width = 400, int height = 200)
    {
        view.resize(width, height);
        installIconProvider(view);
        component.reset(new QQmlComponent(view.engine()));
        component->setData(QByteArray("import QtCreator.TextEditor\n"
                                      "TextViewport { width: %1; height: %2 }")
                               .replace("%1", QByteArray::number(width))
                               .replace("%2", QByteArray::number(height)),
                           QUrl("qrc:/test/TextViewportTest.qml"));
        viewport = qobject_cast<TextViewport *>(component->create());
        if (viewport) {
            viewport->setParentItem(view.contentItem());
            if (source)
                viewport->setDocument(source);
        }
        view.show();
        // Keys go to the scene's active focus item, and an item that was never
        // given focus is not it - every key assertion below would otherwise
        // fail as a wrong value rather than as a missing focus.
        if (viewport)
            viewport->forceActiveFocus();
    }

    bool isReady() const { return viewport != nullptr; }
    bool hasFocus() const { return viewport && viewport->hasActiveFocus(); }
    QString error() const { return component->errorString(); }

    QQuickView view;
    std::unique_ptr<QQmlComponent> component;
    CodeDocument document;
    TextViewport *viewport = nullptr;
};

// The form around the viewport, for the parts that are QML: a bare TextViewport
// has no drop area and nothing to drag a selection out with.
class CodeViewportFixture
{
public:
    // \a extra is for whatever the test needs the form to be showing - the
    // gutter is not on by default, and an item inside a hidden one is not
    // visible however right its own geometry is.
    explicit CodeViewportFixture(const FilePath &file, int width = 400, int height = 200,
                                 const QVariantMap &extra = {})
    {
        view.resize(width, height);
        installIconProvider(view);
        document.setFilePath(file);
        component.reset(new QQmlComponent(view.engine(),
                                          QUrl("qrc:/qt/qml/QtCreator/TextEditor/"
                                               "CodeViewport.qml")));
        QVariantMap properties{{"source", QVariant::fromValue(&document)},
                               {"readOnly", false}};
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
            properties.insert(it.key(), it.value());
        root = qobject_cast<QQuickItem *>(component->createWithInitialProperties(properties));
        if (root) {
            root->setParentItem(view.contentItem());
            root->setWidth(width);
            root->setHeight(height);
            viewport = root->findChild<TextViewport *>();
        }
        view.show();
    }

    bool isReady() const { return root && viewport; }
    QString error() const { return component->errorString(); }

    QQuickView view;
    std::unique_ptr<QQmlComponent> component;
    CodeDocument document;
    QQuickItem *root = nullptr;
    TextViewport *viewport = nullptr;
};

// A language that says "a dot means you are about to name something", which is
// what an activation sequence is. The base provider has none, so nothing is
// offered until the user asks.
class DotActivatesCompletion : public DocumentContentCompletionProvider
{
public:
    int activationCharSequenceLength() const override { return 1; }
    bool isActivationCharSequence(const QString &sequence) const override
    {
        return sequence == ".";
    }
};

// What a language's AutoCompleter subclass does and the base one does not:
// close a bracket as it is typed. The base refuses in contextAllowsAutoBrackets,
// so overriding that is what makes the pairing happen at all.
class ClosingAutoCompleter : public AutoCompleter
{
public:
    bool contextAllowsAutoBrackets(const QTextCursor &, const QString &) const override
    {
        return true;
    }

    QString insertMatchingBrace(const QTextCursor &, const QString &text, QChar, bool,
                                int *skippedChars) const override
    {
        if (skippedChars)
            *skippedChars = 0;
        return text == "[" ? QString("]") : QString();
    }
};

// Every item in the visual tree below \a root, itself included.
static QList<QQuickItem *> allItems(QQuickItem *root)
{
    QList<QQuickItem *> found{root};
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found += allItems(child);
    return found;
}

// The numbers a gutter is showing, top to bottom. Walks the visual tree
// because a Repeater's delegates are visual children of the item and QObject
// children of somewhere else.
static QStringList gutterNumbers(QQuickItem *item)
{
    QStringList numbers;
    if (item->metaObject()->indexOfProperty("text") >= 0
        && QString::fromLatin1(item->metaObject()->className()).contains("Text")) {
        numbers << item->property("text").toString();
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        numbers += gutterNumbers(child);
    return numbers;
}

static FilePath writeLines(const TemporaryDirectory &dir, const QString &name, int count)
{
    const FilePath file = dir.filePath(name);
    QString contents;
    for (int i = 0; i < count; ++i)
        contents += QString("line %1\n").arg(i);
    file.writeFileContents(contents.toUtf8());
    return file;
}

// The base AutoCompleter allows brackets nowhere and inserts nothing - both
// are what a language's subclass adds. The tests below are about what an
// editor does once one has, not about which languages do, so they bring the
// smallest completer that answers "(" with ")".
class ClosesParentheses final : public AutoCompleter
{
public:
    bool contextAllowsAutoBrackets(const QTextCursor &, const QString &) const final
    {
        return true;
    }
    QString insertMatchingBrace(const QTextCursor &, const QString &text, QChar, bool,
                                int *) const final
    {
        return text == "(" ? QString(")") : QString();
    }
};

// Whatever QML said while a component was alive. A binding loop is a warning
// and nothing else - the component still builds and still draws - so the only
// way a test sees one is by listening.
//
// From the engine rather than from a message handler. A handler catches every
// warning the process makes, so unrelated Qt noise - a window elsewhere being
// torn down - failed this as if a binding had gone wrong. Filtering the
// handler by category does not work either: QML's runtime warnings are logged
// with no category at all, so the obvious filter silences exactly the thing
// being listened for.
class QmlComplaints
{
public:
    explicit QmlComplaints(QQmlEngine *engine)
    {
        QObject::connect(engine, &QQmlEngine::warnings, &m_lifetime,
                         [this](const QList<QQmlError> &errors) {
                             for (const QQmlError &error : errors)
                                 m_messages.append(error.toString());
                         });
    }

    QStringList messages() const { return m_messages; }

private:
    QObject m_lifetime;
    QStringList m_messages;
};


// A quick fix that knows one thing to do: replace what it was asked about with
// "fixed". What a real one does is ask a code model; what it hands back is a
// proposal, and applying it is the item's own business - which is the part
// this checks can happen without a widget.
class OneFixItem final : public TextEditor::AssistProposalItem
{
public:
    void apply(TextEditor::AssistTarget &target, int basePosition) const override
    {
        target.replace(basePosition, target.position() - basePosition, "fixed");
    }
};

class OneFixProcessor final : public TextEditor::IAssistProcessor
{
public:
    TextEditor::IAssistProposal *perform() override
    {
        auto * const item = new OneFixItem;
        item->setText("Replace with fixed");
        QSharedPointer<TextEditor::GenericProposalModel> model(
            new TextEditor::GenericProposalModel);
        model->loadContent({item});
        // From the start of the line, so that applying it replaces the word
        // rather than adding to it - a fix that replaced nothing would look
        // the same as one that was never applied.
        return new TextEditor::GenericProposal(0, model);
    }
};

class OneFixProvider final : public TextEditor::IAssistProvider
{
public:
    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *) const override
    {
        return new OneFixProcessor;
    }
};


// A hint for a call that takes two arguments. Which argument the caret is in
// is worked out from the text typed since the call started, the way a real
// model does it - and once the closing bracket is there the call is over,
// which is what -1 means.
class TwoArgumentHintModel final : public TextEditor::IFunctionHintProposalModel
{
public:
    void reset() override {}
    int size() const override { return 1; }
    QString text(int) const override { return QString("f(int a, int b)"); }

    int activeArgument(const QString &prefix) const override
    {
        if (prefix.contains(QLatin1Char(')')))
            return -1;
        return prefix.count(QLatin1Char(','));
    }
};

class TwoArgumentHintProcessor final : public TextEditor::IAssistProcessor
{
public:
    TextEditor::IAssistProposal *perform() override
    {
        TextEditor::FunctionHintProposalModelPtr model(new TwoArgumentHintModel);
        return new TextEditor::FunctionHintProposal(interface()->position(), model);
    }
};

class TwoArgumentHintProvider final : public TextEditor::CompletionAssistProvider
{
public:
    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *) const override
    {
        return new TwoArgumentHintProcessor;
    }
};


// A completion whose label is not what it puts in. Real ones do this whenever
// they expand a snippet or add the brackets of a call; the point here is only
// that the item decides, not the view.
class ExpandingItem final : public TextEditor::AssistProposalItem
{
public:
    void apply(TextEditor::AssistTarget &target, int basePosition) const override
    {
        target.replace(basePosition, target.position() - basePosition, "expanded()");
    }
};

class ExpandingProcessor final : public TextEditor::IAssistProcessor
{
public:
    TextEditor::IAssistProposal *perform() override
    {
        auto * const item = new ExpandingItem;
        item->setText("expand me");
        QSharedPointer<TextEditor::GenericProposalModel> model(
            new TextEditor::GenericProposalModel);
        model->loadContent({item});
        return new TextEditor::GenericProposal(interface()->position() - 2, model);
    }
};

class ExpandingProvider final : public TextEditor::CompletionAssistProvider
{
public:
    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *) const override
    {
        return new ExpandingProcessor;
    }
};


// A processor that is still working, and says so. Real ones are on a thread
// pool; this only has to answer running() the way one of those would, because
// that is the whole of the contract the view has to honour.
struct StillWorking
{
    bool running = false;
    bool cancelled = false;
    bool deleted = false;
};

class StillWorkingProcessor final : public TextEditor::IAssistProcessor
{
public:
    explicit StillWorkingProcessor(StillWorking *state) : m_state(state) {}
    ~StillWorkingProcessor() override { m_state->deleted = true; }

    TextEditor::IAssistProposal *perform() override { return nullptr; }
    // Nothing here changes running(): AsyncProcessor::cancel() does not stop
    // the thread, it only arranges for the answer to be dropped and for the
    // processor to see itself out once it lands.
    void cancel() override { m_state->cancelled = true; }
    bool running() override { return m_state->running; }

private:
    StillWorking * const m_state;
};

class StillWorkingProvider final : public TextEditor::CompletionAssistProvider
{
public:
    explicit StillWorkingProvider(StillWorking *state) : m_state(state) {}
    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *) const override
    {
        return new StillWorkingProcessor(m_state);
    }

private:
    StillWorking * const m_state;
};


// An indenter that puts every line it is given four spaces in. Real ones ask
// the language; this is here so that a test can tell whether the command
// reached the indenter at all, which is the only thing the view decides.
class FourSpaceIndenter final : public TextEditor::TextIndenter
{
public:
    using TextEditor::TextIndenter::TextIndenter;

    void indentBlock(const QTextBlock &block,
                     const QChar &,
                     const TextEditor::TabSettingsData &,
                     int = -1) override
    {
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.insertText("    ");
    }
};

class TextViewportTest final : public QObject
{
    Q_OBJECT

private slots:
    // The fixtures build their own QQuickViews and CodeViewport.qml imports
    // Qt Quick Controls, so these tests only show what the product shows if
    // the style was set before any of it loaded. QtcQuick does that when the
    // application starts; this is here to notice if it stops.
    void initTestCase()
    {
        QCOMPARE(QQuickStyle::name(), QString("QtCreatorStyle"));
    }

    // Room kept clear at an edge, so something else can sit there without
    // covering the text - FakeVim's in-editor command line does exactly this
    // at the bottom, and the widget editor reserves it with
    // PlainTextEdit::setEditorTextMargin().
    void testTextIsKeptOutOfAReservedStrip()
    {
        TemporaryDirectory dir("textviewport-text-inset");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(viewport->lineHeight() > 0);

        const qreal fullHeight = viewport->textAreaHeight();
        QCOMPARE(fullHeight, viewport->height());
        const int rowsBefore = viewport->visibleLineCount();
        const std::pair<int, int> before = viewport->visibleBlockRange();

        // A strip three lines deep at the bottom.
        const int strip = int(viewport->lineHeight() * 3);
        viewport->setTextInset("Test.Strip", Qt::BottomEdge, strip);

        QCOMPARE(viewport->textAreaHeight(), fullHeight - strip);
        // And the text really is laid out into what is left: fewer rows, and
        // the last line on screen is one that was on screen before.
        QTRY_VERIFY2(viewport->visibleLineCount() < rowsBefore,
                     "the same rows were laid out, so the strip reserved nothing");
        QCOMPARE(viewport->visibleBlockRange().first, before.first);
        QVERIFY2(viewport->visibleBlockRange().second < before.second,
                 "the bottom line did not move up out of the strip");

        // Taking it back gives the room back.
        viewport->setTextInset("Test.Strip", Qt::BottomEdge, 0);
        QCOMPARE(viewport->textAreaHeight(), fullHeight);
        QTRY_COMPARE(viewport->visibleLineCount(), rowsBefore);
    }

    // Which positions are on screen, which is what a command that scrolls
    // past the caret needs in order to bring it back. Only the rows the view
    // has laid out are searched, so this answers no for anything scrolled off.
    void testAPositionScrolledOffScreenIsNotVisible()
    {
        TemporaryDirectory dir("textviewport-visible-position");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY2(viewport->rowsPerPage() > 0, "the view has no page to scroll by");

        // The start of the file is on screen to begin with, and the far end
        // is not - so the fixture is showing a window onto the file rather
        // than all of it.
        QVERIFY2(viewport->isPositionVisible(0), "the first line is not on screen");
        const std::pair<int, int> before = viewport->visibleBlockRange();
        QVERIFY2(before.second < 399, "the whole file is on screen, so nothing can scroll off");

        // Half a page down, which is what Emacs's Ctrl+V asks for.
        viewport->scrollByRows(viewport->rowsPerPage() / 2);
        QTRY_VERIFY2(viewport->visibleBlockRange().first > before.first,
                     "scrolling changed nothing the view reports");

        // And now the first line is behind us.
        QVERIFY2(!viewport->isPositionVisible(0),
                 "the first line is still reported on screen after scrolling past it");
    }

    // What a view is showing is lines of the *file*, not rows of the screen.
    // A wrapped line covers several rows, so a caller that counted rows would
    // report lines the file does not have - and "first and last visible line"
    // is exactly what an agent asking about the editor is told.
    void testTheVisibleRangeCountsLinesNotRows()
    {
        TemporaryDirectory dir("textviewport-visible-range");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("wrapped.txt");
        // One line far wider than the view, then two short ones.
        QVERIFY(file.writeFileContents(QByteArray(4000, 'x') + "\nsecond\nthird\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        viewport->setWrapping(true);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // The trailing newline makes a fourth, empty block, and it is on
        // screen too - so the count comes from the document rather than from
        // counting the lines written above.
        const int lines = viewport->textDocument()->document()->blockCount();

        // The fixture only means anything if that first line really did wrap
        // over more rows than the file has lines.
        QTRY_VERIFY2(viewport->visibleLineCount() > lines,
                     "the long line did not wrap, so rows and lines still agree");

        const std::pair<int, int> range = viewport->visibleBlockRange();
        QCOMPARE(range.first, 0);
        QVERIFY2(range.second < lines,
                 qPrintable(QString("the range names line %1 of a %2-line file")
                                .arg(range.second).arg(lines)));
    }

    void testItDrawsOnlyWhatIsOnScreen()
    {
        // The whole reason this is worth building: what it costs to show a file
        // is what is on screen, not what is in the file. Five thousand lines in
        // a two-hundred-pixel window is a handful of layouts.
        TemporaryDirectory dir("textviewport-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const qreal lineHeight = viewport->lineHeight();
        QVERIFY(lineHeight > 0);

        // Every line laid out starts on screen, and one fewer would leave a
        // gap at the bottom. Stated as the property rather than as a formula,
        // which would only be the implementation's arithmetic written twice.
        const int expected = viewport->visibleLineCount();
        QVERIFY((expected - 1) * lineHeight < 200);
        QVERIFY(expected * lineHeight >= 200);
        QVERIFY2(expected < 100,
                 "a two-hundred-pixel window laid out more than a hundred lines");
        QCOMPARE(viewport->firstVisibleLine(), 0);
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("line 0"));

        // The document is as tall as its lines, so a scroll bar has something
        // to size itself against.
        QCOMPARE(viewport->contentHeight(), lineHeight * 5001);

        // The same background the widget editor would paint. It fills with the
        // brush, so a scheme that sets none shows the palette through; a colour
        // cannot say "none" and QBrush().color() is black, so the viewport has
        // to ask the theme rather than hand QML a black rectangle.
        const QBrush brush
            = fixture.document.textDocument()->fontSettings().toTextCharFormat(C_TEXT).background();
        const QColor expectedBackground
            = brush.style() == Qt::NoBrush
                  ? Utils::creatorColor(Utils::Theme::BackgroundColorNormal)
                  : brush.color();
        QCOMPARE(viewport->backgroundColor(), expectedBackground);

        // Scrolled a thousand lines down: a different thousandth line, and not
        // one layout more than before.
        viewport->setScrollY(lineHeight * 1000);
        QTRY_COMPARE(viewport->firstVisibleLine(), 1000);
        QCOMPARE(viewport->visibleLineCount(), expected);
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("line 1000"));

        // Past the end is held at the end rather than showing nothing.
        viewport->setScrollY(lineHeight * 100000);
        QTRY_VERIFY(viewport->firstVisibleLine() < 5001);
        QVERIFY(viewport->visibleLineCount() > 0);
    }

    void testAScreenPositionAndADocumentPositionAgree()
    {
        // A caret and a mouse are the same question asked in two directions,
        // and they have to give the same answer or a click lands one character
        // off - the kind of thing nobody notices until they are editing.
        TemporaryDirectory dir("textviewport-mapping");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // Every position on the first two lines, including the one at the end
        // of each line where the caret sits on the newline. "alpha\n" is
        // positions 0-5, "beta\n" is 6-10.
        for (int position = 0; position <= 10; ++position) {
            const QRectF caret = viewport->rectangleAt(position);
            QVERIFY2(!caret.isEmpty(),
                     qPrintable(QString("no caret for position %1").arg(position)));
            QCOMPARE(viewport->positionAt(caret.x(), caret.center().y()), position);
        }

        // The caret advances across a line and drops a line at the newline.
        QVERIFY(viewport->rectangleAt(1).x() > viewport->rectangleAt(0).x());
        QCOMPARE(viewport->rectangleAt(6).x(), viewport->rectangleAt(0).x());
        QVERIFY(viewport->rectangleAt(6).y() > viewport->rectangleAt(0).y());

        // Clicking past the end of a line lands on its end, not on the next one.
        const QRectF firstLine = viewport->rectangleAt(0);
        QCOMPARE(viewport->positionAt(10000, firstLine.center().y()), 5);

        // Clicking below everything laid out lands on the last line rather than
        // nowhere, which is what a drag out of the viewport does.
        QVERIFY(viewport->positionAt(0, 100000) >= 0);
    }

    void testAPositionThatIsNotOnScreenHasNoCaret()
    {
        // cursorRectangle() answers in item coordinates, so it can only answer
        // for what is on screen. Saying so with an empty rect is what stops a
        // caret being drawn at the top of the viewport for a position that
        // scrolled off it.
        TemporaryDirectory dir("textviewport-offscreen");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(!viewport->rectangleAt(0).isEmpty());

        viewport->setScrollY(viewport->lineHeight() * 1000);
        QTRY_COMPARE(viewport->firstVisibleLine(), 1000);
        QVERIFY(viewport->rectangleAt(0).isEmpty());
    }

    void testTheComponentTurnsAClickIntoACaretAndADragIntoASelection()
    {
        // CodeViewport is where the parts that are not text live - the
        // background, the caret, the scroll bar and the mouse. None of it is
        // exercised by driving TextViewport directly, and a binding loop in it
        // is a warning rather than a failure, so this both drives it and
        // listens to what QML says while it does.
        TemporaryDirectory dir("codeviewport-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        QStringList complaints;
        int position = -1;
        int caretAfterClick = -1;
        int selectionAfterDrag = -1;
        QString typedAfterClick;
        {
            QQuickView view;
            installIconProvider(view);
            view.resize(400, 200);
            QmlComplaints listener(view.engine());
            QQmlComponent component(view.engine());
            component.setData(QByteArray("import QtCreator.TextEditor\n"
                                         "CodeViewport {\n"
                                         "    width: 400; height: 200\n"
                                         "    source: CodeDocument { filePath: path }\n"
                                         "    property string path\n"
                                         "}"),
                              QUrl("qrc:/test/CodeViewportTest.qml"));
            std::unique_ptr<QObject> created(component.createWithInitialProperties(
                {{"path", file.toUrlishString()}}));
            QVERIFY2(created != nullptr, qPrintable(component.errorString()));

            auto * const item = qobject_cast<QQuickItem *>(created.get());
            QVERIFY(item);
            item->setParentItem(view.contentItem());
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));

            auto * const viewport = item->findChild<TextViewport *>("codeViewport");
            QVERIFY(viewport);
            QTRY_VERIFY(viewport->visibleLineCount() > 3);

            // A click three lines down and a little way in. What position that
            // is, is the viewport's own answer - the point here is that the
            // component asks it and keeps what it says, not what the arithmetic
            // is.
            const QPoint at = viewport->mapToScene(QPointF(20, viewport->lineHeight() * 3 + 2))
                                  .toPoint();
            position = viewport->positionAt(20, viewport->lineHeight() * 3 + 2);
            QVERIFY(position > 0);

            QTest::mousePress(&view, Qt::LeftButton, {}, at);
            caretAfterClick = viewport->cursorPosition();

            // Dragging down a line grows the selection rather than starting a
            // new one, so the anchor from the press has to have survived.
            const QPoint to = viewport->mapToScene(QPointF(20, viewport->lineHeight() * 5 + 2))
                                  .toPoint();
            QTest::mouseMove(&view, to);
            QTest::mouseRelease(&view, Qt::LeftButton, {}, to);
            selectionAfterDrag = viewport->selectionEnd();

            // Typing reaches the viewport through the component, which means
            // the focus arrived where the keys are handled - focusing the root
            // stops one level short of a focus scope, and nothing says so.
            item->setProperty("readOnly", false);
            QTest::keyClick(&view, 'Q');
            typedAfterClick = viewport->document()->textDocument()->document()->toPlainText();

            complaints = listener.messages();
        }

        QCOMPARE(caretAfterClick, position);
        QVERIFY2(selectionAfterDrag > position,
                 "dragging down did not extend the selection past where it started");
        // The drag left a selection, so typing replaced it: the character lands
        // where the selection began, which is where the press was.
        QVERIFY(position < typedAfterClick.size());
        QCOMPARE(typedAfterClick.at(position), QChar('Q'));
        QVERIFY2(complaints.isEmpty(), qPrintable("QML complained: " + complaints.join("; ")));
    }

    void testTheWheelAndTheTrackpadBothScroll()
    {
        // A WheelHandler accepts an actual mouse wheel and nothing else unless
        // it is told otherwise, so the editor did not scroll from a trackpad at
        // all. A trackpad also says how far in pixels rather than in wheel
        // notches, and following the fingers means using that.
        TemporaryDirectory dir("codeviewport-trackpad");
        QVERIFY(dir.isValid());
        // Long lines as well as many of them: there is nothing to scroll
        // sideways to in a document narrower than the viewport, and the
        // sideways half of this would pass for the wrong reason.
        const FilePath file = dir.filePath("wide.txt");
        QString content;
        for (int i = 0; i < 2000; ++i)
            content += QString(200, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QCOMPARE(viewport->scrollY(), 0.0);

        // Not the default device: that one is a mouse, and a mouse was never
        // the problem.
        const QPointingDevice trackpad("test trackpad",
                                       4242,
                                       QInputDevice::DeviceType::TouchPad,
                                       QPointingDevice::PointerType::Finger,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Scroll,
                                       1,
                                       0);
        const QPointF centre(viewport->width() / 2, viewport->height() / 2);
        QWheelEvent wheel(centre,
                          fixture.view.mapToGlobal(centre.toPoint()),
                          QPoint(0, -60),
                          QPoint(0, -60),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::ScrollUpdate,
                          false,
                          Qt::MouseEventNotSynthesized,
                          &trackpad);
        QCoreApplication::sendEvent(&fixture.view, &wheel);

        // The pixels it reported, not a number of lines derived from them.
        QTRY_COMPARE(viewport->scrollY(), 60.0);

        // A wheel, which reports notches rather than pixels, goes as far as
        // the reader's own setting says - the same number the widget editor
        // scrolls by, through a scroll bar whose step is one line.
        viewport->setScrollY(0);
        QTRY_COMPARE(viewport->scrollY(), 0.0);
        // Set rather than read: the default is three on most machines, which is
        // exactly the number this used to be hard-coded to, so reading it
        // would let the old behaviour pass.
        const int wasNotchLines = QApplication::wheelScrollLines();
        QApplication::setWheelScrollLines(7);
        const QScopeGuard restoreNotch([wasNotchLines] {
            QApplication::setWheelScrollLines(wasNotchLines);
        });
        const int notchLines = QGuiApplication::styleHints()->wheelScrollLines();
        QCOMPARE(notchLines, 7);
        QWheelEvent notch(centre,
                          fixture.view.mapToGlobal(centre.toPoint()),
                          QPoint(0, 0),
                          QPoint(0, -120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(&fixture.view, &notch);
        QTRY_COMPARE(viewport->scrollY(), viewport->lineHeight() * notchLines);

        // And sideways, which is the only way to follow a long line with the
        // hands rather than the caret: the handler used to read the vertical
        // delta and nothing else.
        QWheelEvent sideways(centre,
                             fixture.view.mapToGlobal(centre.toPoint()),
                             QPoint(-30, 0),
                             QPoint(-30, 0),
                             Qt::NoButton,
                             Qt::NoModifier,
                             Qt::ScrollUpdate,
                             false,
                             Qt::MouseEventNotSynthesized,
                             &trackpad);
        QCoreApplication::sendEvent(&fixture.view, &sideways);
        QTRY_COMPARE(viewport->scrollX(), 30.0);
    }

    void testTheHorizontalScrollBarSaysHowFarAcrossTheLineIs()
    {
        // With wrapping off a line runs past the right edge, and until there
        // was a bar there was nothing saying so and nothing to drag: the line
        // could only be followed by the caret or the wheel.
        TemporaryDirectory dir("codeviewport-hbar");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("wide.txt");
        QString content;
        for (int i = 0; i < 50; ++i)
            content += QString(300, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QQuickItem * const bar = fixture.root->findChild<QQuickItem *>("horizontalScrollBar");
        QVERIFY2(bar, "the viewport has no horizontal scroll bar");
        const auto barSize = [bar] { return bar->property("size").toReal(); };
        const auto barPosition = [bar] { return bar->property("position").toReal(); };

        // There is more line than viewport, so the handle is shorter than the
        // groove and the bar is worth showing.
        QTRY_VERIFY(viewport->contentWidth() > viewport->width());
        QTRY_VERIFY(barSize() < 1.0);
        QCOMPARE(barPosition(), 0.0);

        // The viewport moves, the handle follows.
        viewport->setScrollX(200);
        QTRY_COMPARE(viewport->scrollX(), 200.0);
        QTRY_VERIFY(barPosition() > 0);
        QVERIFY(qFuzzyCompare(barPosition() + 1,
                              viewport->scrollX() / viewport->contentWidth() + 1));

        // And the handle is dragged, the viewport follows.
        const QPointF handle = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2));
        const qreal before = viewport->scrollX();
        QTest::mousePress(&fixture.view, Qt::LeftButton, {}, handle.toPoint());
        QTest::mouseMove(&fixture.view, handle.toPoint() + QPoint(60, 0));
        QTest::mouseRelease(&fixture.view, Qt::LeftButton, {}, handle.toPoint() + QPoint(60, 0));
        QTRY_VERIFY2(viewport->scrollX() > before,
                     qPrintable(QString("dragging the handle right left scrollX at %1")
                                    .arg(viewport->scrollX())));

        // And with wrapping on there is nothing to scroll sideways to, so the
        // bar has to go away rather than offer a groove that does nothing.
        viewport->setWrapping(true);
        QTRY_VERIFY2(barSize() >= 1.0,
                     qPrintable(QString("wrapped, but the bar still shows a handle of %1 "
                                        "(contentWidth %2, width %3)")
                                    .arg(barSize())
                                    .arg(viewport->contentWidth())
                                    .arg(viewport->width())));
        QTRY_COMPARE(viewport->scrollX(), 0.0);
    }

    void testTheScrollBarAndTheViewportKeepFollowingEachOther()
    {
        // Both directions, and both of them again after a drag. Two-way
        // bindings between a control and what it controls are where the bugs
        // are, and the failure this guards against is the quiet one: a bar
        // that stops following the viewport once the user has touched it, so
        // it looks right until the next wheel scroll.
        TemporaryDirectory dir("codeviewport-scrollbar");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 400; height: 200\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "    property string path\n"
                                     "}"),
                          QUrl("qrc:/test/CodeViewportScrollTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QQuickItem * const bar = item->findChild<QQuickItem *>("verticalScrollBar");
        QVERIFY(bar);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const auto barPosition = [bar] { return bar->property("position").toReal(); };
        // The handle travels over the track that is left beside it, so where
        // it sits is a fraction of how far the document can be scrolled - not
        // of the document. The two only agree while the handle is its true
        // proportion, which a long file's is not.
        const auto expectedPosition = [viewport, bar] {
            const qreal scrollable = viewport->contentHeight() - viewport->height();
            if (scrollable <= 0)
                return 0.0;
            return (viewport->scrollY() / scrollable) * (1 - bar->property("size").toReal());
        };

        QCOMPARE(barPosition(), 0.0);

        // The wheel scrolls, and the bar notices. One notch is three lines,
        // the same as everywhere else.
        const QPointF centre(viewport->width() / 2, viewport->height() / 2);
        QWheelEvent wheel(centre,
                          view.mapToGlobal(centre.toPoint()),
                          QPoint(0, 0),
                          QPoint(0, -120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(&view, &wheel);
        QTRY_COMPARE(viewport->firstVisibleLine(), 3);
        QVERIFY(barPosition() > 0);

        // However long the document, the handle stays big enough to grab. The
        // true proportion here is a couple of pixels; QScrollBar floors the
        // slider the same way.
        const qreal handleHeight = bar->property("size").toReal() * bar->height();
        QVERIFY2(handleHeight >= 24 - 0.5,
                 qPrintable(QString("handle is %1 pixels for a %2 line file")
                                .arg(handleHeight).arg(viewport->lineCount())));
        QVERIFY2(bar->property("size").toReal() > bar->property("shown").toReal(),
                 "this file is not long enough for the floor to apply");

        // The viewport moves programmatically, the handle follows that too.
        viewport->setScrollY(viewport->lineHeight() * 1000);
        QTRY_COMPARE(viewport->firstVisibleLine(), 1000);
        QVERIFY(qFuzzyCompare(barPosition() + 1, expectedPosition() + 1));

        // The handle is dragged, the viewport follows.
        const QPointF handle = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2));
        QTest::mousePress(&view, Qt::LeftButton, {}, handle.toPoint());
        QTest::mouseMove(&view, handle.toPoint() + QPoint(0, 40));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, handle.toPoint() + QPoint(0, 40));
        QTRY_VERIFY(viewport->firstVisibleLine() > 1000);

        // And after all that, the viewport still leads.
        const int before = viewport->firstVisibleLine();
        viewport->setScrollY(viewport->lineHeight() * 4000);
        QTRY_VERIFY(viewport->firstVisibleLine() != before);
        QVERIFY2(qFuzzyCompare(barPosition() + 1, expectedPosition() + 1),
                 qPrintable(QString("bar at %1, viewport says %2")
                                .arg(barPosition())
                                .arg(expectedPosition())));
    }

    void testTypingGoesThroughTheDocumentsOwnCursor()
    {
        // Edits go through a QTextCursor rather than through the text, so the
        // document's undo stack, its layout and the marks on it all see them.
        // Writing the text out and back would lose every one of those, and the
        // page would look right while the undo stack was ruined.
        TemporaryDirectory dir("textviewport-typing");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        QTextDocument * const text = fixture.document.textDocument()->document();
        QVERIFY(text);

        // A viewport is a view until told otherwise, so this does nothing yet.
        QVERIFY(viewport->isReadOnly());
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText().left(5), QString("alpha"));

        viewport->setReadOnly(false);
        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText().left(6), QString("Xalpha"));
        QCOMPARE(viewport->cursorPosition(), 1);
        QVERIFY2(text->isUndoAvailable(),
                 "the edit did not reach the document's undo stack");

        // The line on screen was laid out again, not just the document.
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("Xalpha"));

        // Backspace takes it back out.
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText().left(5), QString("alpha"));
        QCOMPARE(viewport->cursorPosition(), 0);

        // Typing over a selection replaces it rather than inserting beside it.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QTest::keyClick(&fixture.view, 'Y');
        QCOMPARE(text->toPlainText().left(2), QString("Y\n"));

        // Return splits the line, so the document has one more of them.
        const int before = text->blockCount();
        QTest::keyClick(&fixture.view, Qt::Key_Return);
        QCOMPARE(text->blockCount(), before + 1);
    }

    // Reported from a real page: selecting text in a Code Style preview and
    // pressing Delete left it there.
    void testDeleteAndBackspaceRemoveTheWholeSelection()
    {
        TemporaryDirectory dir("textviewport-delete");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTextDocument * const text = fixture.document.textDocument()->document();
        QVERIFY(text);
        viewport->setReadOnly(false);

        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QTest::keyClick(&fixture.view, Qt::Key_Delete);
        QCOMPARE(text->toPlainText(), QString("\nbeta\ngamma\n"));

        // And Backspace does the same to a selection rather than taking one
        // more character out in front of it.
        viewport->setSelectionStart(1);
        viewport->setSelectionEnd(5);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText(), QString("\n\ngamma\n"));
    }

    // The gutter is the first thing that makes a viewport look like an editor
    // rather than a text box, and the only thing holding its rows against the
    // text's is that both work the line's position out the same way.
    void testTheGutterNumbersTheLinesThatAreOnScreen()
    {
        TemporaryDirectory dir("gutter-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "Row {\n"
                                     "    property alias viewport: v\n"
                                     "    property alias gutter: g\n"
                                     "    property string path\n"
                                     "    EditorGutter { id: g; viewport: v; height: 200 }\n"
                                     "    TextViewport {\n"
                                     "        id: v; objectName: \"gutterViewport\"\n"
                                     "        width: 300; height: 200\n"
                                     "        document: CodeDocument { filePath: path }\n"
                                     "    }\n"
                                     "}"),
                          QUrl("qrc:/test/GutterTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("gutterViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const gutter = item->property("gutter").value<QQuickItem *>();
        QVERIFY(gutter);

        // One number per line on screen, and they are the lines on screen -
        // counting from one, the way an editor does and the document does not.
        QTRY_COMPARE(gutterNumbers(gutter).size(), viewport->visibleLineCount());
        QStringList numbers = gutterNumbers(gutter);
        QCOMPARE(numbers.first(), QString::number(viewport->firstVisibleLine() + 1));
        QCOMPARE(numbers.last(),
                 QString::number(viewport->firstVisibleLine() + viewport->visibleLineCount()));

        // And they follow the text rather than the frame: scrolling by a whole
        // number of lines moves the numbers by the same amount.
        const int wasFirst = viewport->firstVisibleLine();
        viewport->setScrollY(viewport->lineHeight() * 1000);
        QTRY_COMPARE(viewport->firstVisibleLine(), 1000);
        QVERIFY(viewport->firstVisibleLine() != wasFirst);
        QTRY_COMPARE(gutterNumbers(gutter).first(), QString("1001"));

        // Wide enough for the highest number in the file, not for the ones
        // being shown: a gutter that sized itself to what is on screen would
        // change width while scrolling.
        QCOMPARE(viewport->lineCount(), 5001);
        const qreal wide = gutter->implicitWidth();
        viewport->setScrollY(0);
        QTRY_COMPARE(viewport->firstVisibleLine(), 0);
        QCOMPARE(gutter->implicitWidth(), wide);
    }

    // Folding takes lines off the screen without taking them out of the file.
    // The rows close up over what is hidden; the numbers beside them keep
    // counting the document, so they jump.
    void testFoldingClosesTheGapWithoutRenumberingTheFile()
    {
        TemporaryDirectory dir("qtc-viewport-fold");
        const FilePath file = writeLines(dir, "folded.txt", 40);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "Row {\n"
                                     "    property alias viewport: v\n"
                                     "    property alias gutter: g\n"
                                     "    property string path\n"
                                     "    EditorGutter { id: g; viewport: v; height: 200 }\n"
                                     "    TextViewport {\n"
                                     "        id: v; objectName: \"foldViewport\"\n"
                                     "        width: 300; height: 200\n"
                                     "        document: CodeDocument { filePath: path }\n"
                                     "    }\n"
                                     "}"),
                          QUrl("qrc:/test/FoldTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("foldViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 6);
        auto * const gutter = item->property("gutter").value<QQuickItem *>();
        QVERIFY(gutter);

        QTextDocument * const text = viewport->document()->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);

        const qreal fullHeight = viewport->contentHeight();
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 1"));

        // Lines 1 to 4 belong to line 0, which is what makes line 0 foldable.
        // A highlighter is what normally says so; saying it here keeps the
        // test about the viewport.
        for (int i = 1; i <= 4; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        const QTextBlock first = text->findBlockByNumber(0);
        QVERIFY(TextBlockUserData::canFold(first));

        // Folded the way the widget editor folds: the document hides the
        // blocks, and the layout is what tells anyone showing it.
        TextBlockUserData::doFoldOrUnfold(first, /*unfold=*/false);
        layout->requestUpdate();

        // Four lines' worth of document has gone, and the row under the first
        // one is now the line after the fold.
        QTRY_COMPARE(viewport->contentHeight(), fullHeight - 4 * viewport->lineHeight());
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 5"));
        // The file has not lost any lines, so the gutter is still as wide as
        // its highest number.
        QCOMPARE(viewport->lineCount(), 41);

        // Which is exactly what the numbers say: 1, then 6.
        QStringList numbers = gutterNumbers(gutter);
        QCOMPARE(numbers.value(0), QString("1"));
        QCOMPARE(numbers.value(1), QString("6"));

        // Scrolling past the fold still lands on the right line: the row at an
        // offset is counted in rows, and the fold is not one.
        viewport->setScrollY(viewport->lineHeight() * 10);
        QTRY_COMPARE(viewport->firstVisibleLine(), 10);
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("line 14"));
        QCOMPARE(gutterNumbers(gutter).value(0), QString("15"));

        // And unfolding puts them back.
        viewport->setScrollY(0);
        TextBlockUserData::doFoldOrUnfold(first, /*unfold=*/true);
        layout->requestUpdate();
        QTRY_COMPARE(viewport->contentHeight(), fullHeight);
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 1"));
        QTRY_COMPARE(gutterNumbers(gutter).value(1), QString("2"));
    }

    // The gutter offers folding, and clicking what it offers folds. This is
    // the half a C++ assertion cannot reach: the marker is a delegate, and the
    // line it acts on is the one the viewport named, not the row it sits on.
    void testClickingTheGutterMarkerFoldsAndUnfoldsTheLineItSitsOn()
    {
        TemporaryDirectory dir("qtc-viewport-foldclick");
        const FilePath file = writeLines(dir, "clickable.txt", 40);

        // Tall enough that both folds are on screen at once, which is the
        // whole point of having two of them.
        QQuickView view;
        installIconProvider(view);
        view.resize(400, 500);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 500\n"
                                     "    showLineNumbers: true\n"
                                     "    showFoldMarkers: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/FoldClickTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 16);

        QTextDocument * const text = viewport->document()->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);

        // Two folds, one under line 1 and one under line 10. Two, because a
        // single fold at the top of the file is the one case where a line's
        // number and the row it is drawn on agree - and this test is about
        // the marker acting on the line rather than on the row.
        for (int i : {1, 2, 3, 4, 10, 11, 12, 13, 14})
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        layout->requestUpdate();

        // Only the two lines that start a fold are offered one.
        QTRY_VERIFY(viewport->visibleLine(0).value("foldable").toBool());
        QVERIFY(!viewport->visibleLine(0).value("folded").toBool());
        QVERIFY(!viewport->visibleLine(1).value("foldable").toBool());
        QVERIFY(viewport->visibleLine(9).value("foldable").toBool());

        // Every image the gutter is actually showing. Nothing has a text mark
        // here, so the only ones that can be visible are fold markers.
        auto markers = [item] {
            QList<QQuickItem *> found;
            const QList<QQuickItem *> items = allItems(item);
            for (QQuickItem *candidate : items) {
                if (candidate->inherits("QQuickImage") && candidate->isVisible()
                    && !candidate->property("source").toUrl().isEmpty()) {
                    found << candidate;
                }
            }
            std::sort(found.begin(), found.end(),
                      [](QQuickItem *a, QQuickItem *b) { return a->y() < b->y(); });
            return found;
        };
        QTRY_COMPARE(markers().size(), 2);
        // Image.Ready. A URL that no provider answers still leaves the item
        // visible with a source set, so without this the markers could be
        // pointing at nothing and every other assertion here would pass.
        QTRY_COMPARE(markers().first()->property("status").toInt(), 1);

        auto click = [&view](QQuickItem *marker) {
            const QPoint at = view.contentItem()
                                  ->mapFromItem(marker, QPointF(marker->width() / 2,
                                                                marker->height() / 2))
                                  .toPoint();
            QTest::mouseClick(&view, Qt::LeftButton, {}, at);
        };

        // Close the first fold, which moves the second one up five rows. Line
        // 10 is now drawn on row 5, so anything that folds "the row it sits
        // on" from here folds the wrong thing.
        const qreal fullHeight = viewport->contentHeight();
        click(markers().first());
        QTRY_COMPARE(viewport->contentHeight(), fullHeight - 4 * viewport->lineHeight());
        QVERIFY(viewport->visibleLine(0).value("folded").toBool());
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 5"));
        QTRY_COMPARE(viewport->visibleLine(5).value("lineNumber").toInt(), 10);
        QVERIFY(viewport->visibleLine(5).value("foldable").toBool());

        // Now the second one, by its marker rather than by its number.
        QTRY_COMPARE(markers().size(), 2);
        click(markers().last());
        QTRY_COMPARE(viewport->contentHeight(), fullHeight - 9 * viewport->lineHeight());
        QVERIFY(viewport->visibleLine(5).value("folded").toBool());
        QCOMPARE(viewport->visibleLine(6).value("text").toString(), QString("line 15"));

        // The same click again puts them back, which is what makes it a
        // toggle rather than a fold button.
        QTRY_COMPARE(markers().size(), 2);
        click(markers().last());
        QTRY_COMPARE(viewport->contentHeight(), fullHeight - 4 * viewport->lineHeight());
        click(markers().first());
        QTRY_COMPARE(viewport->contentHeight(), fullHeight);
        QVERIFY(!viewport->visibleLine(0).value("folded").toBool());
    }

    // Hovering the folding column dims everything outside the scope the
    // pointer is in, leaving that scope on the plain page colour. The widget
    // editor does this whether or not the highlightBlocks setting is on - that
    // setting widens what counts as a hover rather than switching the
    // highlight on - so nothing here turns anything on.
    void testHoveringTheFoldColumnLightsUpTheScopeAroundTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-scope");
        const FilePath file = dir.filePath("scope.txt");
        // Two nested scopes at different indentation, so that a band ignoring
        // the indent lands in the wrong place rather than merely in the wrong
        // colour.
        QVERIFY(file.writeFileContents("a\n"
                                       "    b\n"
                                       "        c\n"
                                       "        d\n"
                                       "    e\n"
                                       "f\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "Row {\n"
                                     "    property alias viewport: v\n"
                                     "    property alias gutter: g\n"
                                     "    property string path\n"
                                     "    EditorGutter {\n"
                                     "        id: g; viewport: v; height: 200\n"
                                     "        showFoldMarkers: true\n"
                                     "    }\n"
                                     "    TextViewport {\n"
                                     "        id: v; objectName: \"scopeViewport\"\n"
                                     "        width: 300; height: 200\n"
                                     "        document: CodeDocument { filePath: path }\n"
                                     "    }\n"
                                     "}"),
                          QUrl("qrc:/test/ScopeTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("scopeViewport");
        QVERIFY(viewport);
        // Seven: the file ends with a newline, so there is an empty block
        // after "f".
        QTRY_COMPARE(viewport->visibleLineCount(), 7);
        auto * const gutter = item->property("gutter").value<QQuickItem *>();
        QVERIFY(gutter);

        // What a highlighter would have worked out. Saying it here keeps the
        // test about the viewport.
        QTextDocument * const text = viewport->document()->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        const QList<int> indents = {0, 1, 2, 2, 1, 0};
        for (int i = 0; i < indents.size(); ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), indents.at(i));
        layout->requestUpdate();

        const auto bands = [viewport](int row) {
            return viewport->visibleLine(row).value("scopeBands").toList();
        };
        // A band of a given colour, whichever piece of it the right margin
        // left behind.
        const auto bandColoured = [](const QVariantList &list, const QColor &colour) {
            for (const QVariant &entry : list) {
                const QVariantMap band = entry.toMap();
                if (band.value("colour").value<QColor>() == colour)
                    return band;
            }
            return QVariantMap();
        };

        // Nothing is hovered, so nothing is dimmed.
        for (int row = 0; row < 7; ++row)
            QVERIFY2(bands(row).isEmpty(), "a scope was highlighted before anything was hovered");

        // Hover the folding column beside "        d", the innermost scope.
        const qreal foldX = gutter->property("foldX").toReal();
        const qreal foldWidth = gutter->property("foldWidth").toReal();
        QVERIFY(foldWidth > 0);
        const QPointF onD(foldX + foldWidth / 2, 3.5 * viewport->lineHeight());
        QTest::mouseMove(&view, gutter->mapToScene(onD).toPoint());
        QTRY_VERIFY2(!bands(3).isEmpty(), "hovering the folding column highlighted nothing");

        // The scope the pointer is in keeps the page colour, and starts where
        // the scope is indented rather than at the edge.
        const QColor page = viewport->backgroundColor();
        const qreal spaceWidth = QFontMetricsF(viewport->font()).horizontalAdvance(QLatin1Char(' '));
        const QVariantMap inner = bandColoured(bands(3), page);
        QVERIFY2(!inner.isEmpty(), "the scope under the pointer was dimmed along with the rest");
        QCOMPARE(qRound(inner.value("x").toReal()), qRound(4 * spaceWidth));

        // The line outside it is dimmed: none of its bands is the page.
        QVERIFY(!bands(5).isEmpty());
        QVERIFY2(bandColoured(bands(5), page).isEmpty(),
                 "a line outside the scope was left undimmed");

        // Moving off the column puts the page back.
        QTest::mouseMove(&view, viewport->mapToScene(QPointF(viewport->width() / 2,
                                                             viewport->height() / 2)).toPoint());
        QTRY_VERIFY2(bands(3).isEmpty(), "the highlight outlived the hover");
    }

    // What the "Highlight blocks" setting adds on top: the scope around the
    // caret lights up as it moves, with nothing hovered. Off by default,
    // unlike the folding column's own highlight, which is why the two are
    // tested apart.
    void testHighlightBlocksFollowsTheCaret()
    {
        const bool was = displaySettings().highlightBlocks();
        const QScopeGuard restore([was] { displaySettings().highlightBlocks.setValue(was); });
        displaySettings().highlightBlocks.setValue(false);

        TemporaryDirectory dir("qtc-viewport-caretscope");
        const FilePath file = dir.filePath("scope.txt");
        QVERIFY(file.writeFileContents("a\n"
                                       "    b\n"
                                       "        c\n"
                                       "        d\n"
                                       "    e\n"
                                       "f\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_COMPARE(viewport->visibleLineCount(), 7);

        QTextDocument * const text = viewport->document()->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        const QList<int> indents = {0, 1, 2, 2, 1, 0};
        for (int i = 0; i < indents.size(); ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), indents.at(i));
        layout->requestUpdate();

        const auto bands = [viewport](int row) {
            return viewport->visibleLine(row).value("scopeBands").toList();
        };

        // Off: the caret moves into the innermost scope and nothing lights up.
        viewport->setCursorPosition(text->findBlockByNumber(3).position());
        QVERIFY2(bands(3).isEmpty(), "a scope was highlighted with the setting off");

        // On: the scope the caret is already in lights up.
        displaySettings().highlightBlocks.setValue(true);
        QTRY_VERIFY2(!bands(3).isEmpty(), "turning the setting on highlighted nothing");

        // And it follows the caret out. The last line is at the top level,
        // where there is no enclosing fold to light up at all.
        viewport->setCursorPosition(text->findBlockByNumber(5).position());
        QTRY_VERIFY2(bands(3).isEmpty(), "the highlight stayed where the caret had been");

        // Back in, and the scope under the caret is the one left undimmed.
        viewport->setCursorPosition(text->findBlockByNumber(2).position());
        QTRY_VERIFY(!bands(2).isEmpty());
        const QColor page = viewport->backgroundColor();
        bool undimmed = false;
        for (const QVariant &entry : bands(2))
            undimmed = undimmed || entry.toMap().value("colour").value<QColor>() == page;
        QVERIFY2(undimmed, "the scope the caret is in was dimmed along with the rest");

        // Turning it off puts the page back.
        displaySettings().highlightBlocks.setValue(false);
        QTRY_VERIFY2(bands(2).isEmpty(), "the highlight outlived the setting");
    }

    // "Enable mouse navigation" turns Ctrl+click link following off. The Quick
    // editor followed links whatever the setting said: the QML restated the
    // widget editor's modifier rule and left the setting out of it, so a
    // preference that is on by default could not be turned off.
    void testMouseNavigationCanBeTurnedOff()
    {
        const bool was = globalBehaviorSettings().mouseNavigation();
        const QScopeGuard restore(
            [was] { globalBehaviorSettings().mouseNavigation.setValue(was); });

        ViewportFixture fixture(nullptr);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;

        globalBehaviorSettings().mouseNavigation.setValue(true);
        QVERIFY(viewport->isMouseNavigation(Qt::ControlModifier));
        // Shift makes it a selection gesture rather than a navigation one.
        QVERIFY(!viewport->isMouseNavigation(Qt::ControlModifier | Qt::ShiftModifier));
        // And a plain click was never navigation.
        QVERIFY(!viewport->isMouseNavigation(Qt::NoModifier));

        // Turned off, Ctrl+click is an ordinary click again.
        globalBehaviorSettings().mouseNavigation.setValue(false);
        QVERIFY2(!viewport->isMouseNavigation(Qt::ControlModifier),
                 "a link was followed with mouse navigation turned off");
    }

    // "Always open links in another split" swaps what Alt means, rather than
    // being a second way of asking for the other split. The Quick editor
    // passed Alt straight through, so the setting did nothing at all.
    void testAlwaysOpeningLinksInAnotherSplitSwapsWhatAltMeans()
    {
        const bool was = displaySettings().openLinksInNextSplit();
        const QScopeGuard restore(
            [was] { displaySettings().openLinksInNextSplit.setValue(was); });

        ViewportFixture fixture(nullptr);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;

        // Off: Alt asks for the other split and nothing else does.
        displaySettings().openLinksInNextSplit.setValue(false);
        QVERIFY(!viewport->opensInNextSplit(false));
        QVERIFY(viewport->opensInNextSplit(true));

        // On: the other split is where a link goes by default, and Alt is
        // what asks for this one.
        displaySettings().openLinksInNextSplit.setValue(true);
        QVERIFY2(viewport->opensInNextSplit(false),
                 "a link went to this split with the setting asking for the other one");
        QVERIFY2(!viewport->opensInNextSplit(true),
                 "Alt did not ask for this split once the setting had swapped them");
    }

    // A link under the pointer is underlined in the scheme's link colour and
    // turns the cursor into a hand, which is how the widget editor says a
    // Control-click will go somewhere. Finding the link needs a language with
    // a link finder registered; drawing one that has been found does not, and
    // that is the half tested here.
    void testALinkUnderThePointerIsUnderlined()
    {
        TemporaryDirectory dir("qtc-viewport-link");
        const FilePath file = dir.filePath("link.txt");
        QVERIFY(file.writeFileContents("alpha beta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const auto underlined = [viewport] {
            QVariantList found;
            const QVariantList ranges = viewport->visibleLine(0).value("formats").toList();
            for (const QVariant &range : ranges) {
                if (range.toMap().value("underline").toBool())
                    found << range;
            }
            return found;
        };

        QVERIFY2(underlined().isEmpty(), "text was underlined before any link was shown");
        const Qt::CursorShape plain = viewport->cursor().shape();

        // Held rather than only let go of: QTest tracks modifier state for the
        // whole process, so a release with no press leaves every later test
        // being told Control is down.
        QTest::keyPress(&fixture.view, Qt::Key_Control);

        Utils::Link link;
        link.linkTextStart = 0;
        link.linkTextEnd = 5; // "alpha"
        viewport->showLink(link);

        QTRY_COMPARE(underlined().size(), 1);
        const QVariantMap range = underlined().first().toMap();
        QCOMPARE(range.value("start").toInt(), 0);
        QCOMPARE(range.value("length").toInt(), 5);
        QCOMPARE(viewport->cursor().shape(), Qt::PointingHandCursor);

        // Letting go of Control puts it away again.
        QTest::keyRelease(&fixture.view, Qt::Key_Control);
        QTRY_VERIFY2(underlined().isEmpty(), "letting go of Control left the link underlined");
        QCOMPARE(viewport->cursor().shape(), plain);
    }

    // Backspace inside a line's indentation takes a whole level back rather
    // than one space. That is the default - the Quick editor deleted a
    // character whatever the typing settings said - and the other two
    // behaviours are here because the setting has three.
    void testBackspaceInTheIndentationFollowsTheTypingSettings()
    {
        TemporaryDirectory dir("qtc-viewport-backspace");
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents("    alpha\n        beta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QVERIFY(fixture.hasFocus());

        // A viewport is a view until told otherwise, and a read-only one
        // never reaches the key at all.
        viewport->setReadOnly(false);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);
        // Said here rather than taken from whatever the global code style is,
        // so that what one level costs is not a shared setting's to change.
        TabSettingsData tabs = doc->tabSettings();
        tabs.m_indentSize = 4;
        tabs.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        // Or setTabSettings() would read the sample text and answer with what
        // *it* is indented by instead - see the test beside this one.
        tabs.m_autoDetect = false;
        doc->setTabSettings(tabs);
        QCOMPARE(doc->tabSettings().m_indentSize, 4);

        QTextDocument * const text = doc->document();
        const auto secondLine = [text] { return text->findBlockByNumber(1).text(); };
        // Each case starts from the same line rather than from what the one
        // before it left, so that none of them depends on the others.
        const auto startFrom = [text](const QString &line) {
            QTextCursor c(text->findBlockByNumber(1));
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.insertText(line);
        };
        // Just before the first non-space, which counts as inside the
        // indentation rather than after it.
        const auto putCaretInTheIndent = [viewport, text] {
            const QTextBlock block = text->findBlockByNumber(1);
            const QString line = block.text();
            int nonSpace = 0;
            while (nonSpace < line.size() && line.at(nonSpace).isSpace())
                ++nonSpace;
            viewport->setCursorPosition(block.position() + nonSpace);
        };

        QCOMPARE(secondLine(), QString("        beta"));

        // The default: a whole level, not one space.
        TypingSettingsData typing = doc->typingSettings();
        QCOMPARE(typing.m_smartBackspaceBehavior, TypingSettingsData::BackspaceUnindents);
        putCaretInTheIndent();
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(secondLine(), QString("    beta"));

        // Turned off, it is one character again.
        typing.m_smartBackspaceBehavior = TypingSettingsData::BackspaceNeverIndents;
        doc->setTypingSettings(typing);
        startFrom("        beta");
        putCaretInTheIndent();
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(secondLine(), QString("       beta"));

        // Following the previous indents goes to the indentation of the
        // nearest line above that is indented less than this one - "    alpha"
        // at four, rather than one level back from eight.
        typing.m_smartBackspaceBehavior = TypingSettingsData::BackspaceFollowsPreviousIndents;
        doc->setTypingSettings(typing);
        startFrom("        beta");
        putCaretInTheIndent();
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(secondLine(), QString("    beta"));

        // Past the indentation there is nothing to unindent, so it is one
        // character again even with the default behaviour.
        typing.m_smartBackspaceBehavior = TypingSettingsData::BackspaceUnindents;
        doc->setTypingSettings(typing);
        startFrom("        beta");
        const QTextBlock second = text->findBlockByNumber(1);
        viewport->setCursorPosition(second.position() + second.text().size());
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(secondLine(), QString("        bet"));
    }

    // Backspace between the two halves of a bracket pair takes both, which is
    // right for a pair the editor put in and wrong for one that was already in
    // the file: AutoCompleter::autoBackspace() reads the text and cannot tell
    // them apart, so the widget editor asks it only where it knows it inserted
    // something, and only when the reader has not turned that off. This view
    // asked it every time.
    // setTabSettings() does not store what it is handed: it runs the
    // indentation detector over the document first, and answers with what the
    // text says unless the reader has turned that off. A fixture that sets its
    // tab settings after putting text in silently runs at whatever that text
    // is indented by - which is how a difference between the two editors was
    // reported in entry 131 that turned out to be two different indent sizes.
    void testTabSettingsAreDetectedFromTheTextUnlessTurnedOff()
    {
        TemporaryDirectory dir("qtc-viewport-tabdetect");
        const FilePath file = dir.filePath("twos.txt");
        // Indented by two throughout, which is what the detector will say.
        QVERIFY(file.writeFileContents("a\n  b\n    c\n  d\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);

        TabSettingsData asked = doc->tabSettings();
        asked.m_indentSize = 4;
        asked.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        QVERIFY2(asked.m_autoDetect, "detection is off by default, so this tests nothing");
        doc->setTabSettings(asked);
        QCOMPARE(doc->tabSettings().m_indentSize, 2);

        // And turned off, the answer is the question.
        asked.m_autoDetect = false;
        doc->setTabSettings(asked);
        QCOMPARE(doc->tabSettings().m_indentSize, 4);
    }

    // "Skip automatically inserted character if re-typed manually after
    // completion or by pressing tab." Two triggers, and this view had the
    // first: Tab between the brackets of "f(|)" indented instead of stepping
    // out of them, which is an indent in the middle of a call.
    void testTabStepsOverWhatThisViewInserted()
    {
        TemporaryDirectory dir("qtc-viewport-tabskip");
        const FilePath file = dir.filePath("call.txt");
        QVERIFY(file.writeFileContents("f\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(fixture.hasFocus());
        viewport->setReadOnly(false);
        viewport->setAutoCompleter(new ClosesParentheses);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);
        TabSettingsData tabs = doc->tabSettings();
        tabs.m_indentSize = 4;
        tabs.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        // Or setTabSettings() would read the sample text and answer with what
        // *it* is indented by instead - see the test beside this one.
        tabs.m_autoDetect = false;
        doc->setTabSettings(tabs);
        QCOMPARE(doc->tabSettings().m_indentSize, 4);
        const auto firstLine = [doc] { return doc->document()->firstBlock().text(); };

        QVERIFY2(globalCompletionSettings().skipAutoCompletedText(),
                 "the preference under test starts turned off");

        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, '(');
        QCOMPARE(firstLine(), QString("f()"));
        QCOMPARE(viewport->cursorPosition(), 2);

        // Out of the brackets, and the text is exactly as it was.
        QTest::keyClick(&fixture.view, Qt::Key_Tab);
        QCOMPARE(firstLine(), QString("f()"));
        QCOMPARE(viewport->cursorPosition(), 3);

        // Once stepped over, it is no longer pending: a second Tab indents,
        // because there is nothing left to step out of.
        QTest::keyClick(&fixture.view, Qt::Key_Tab);
        // One space, not four: an indent goes to the next multiple of the
        // indent size, and the caret was at column three.
        QCOMPARE(firstLine(), QString("f() "));

        // And the preference turns the stepping off, leaving the indent.
        const bool was = globalCompletionSettings().skipAutoCompletedText();
        const QScopeGuard restore(
            [was] { globalCompletionSettings().skipAutoCompletedText.setValue(was); });
        globalCompletionSettings().skipAutoCompletedText.setValue(false);

        QTextCursor all(doc->document());
        all.select(QTextCursor::Document);
        all.insertText("f\n");
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, '(');
        QCOMPARE(firstLine(), QString("f()"));
        QTest::keyClick(&fixture.view, Qt::Key_Tab);
        QCOMPARE(firstLine(), QString("f(  )"));
        QCOMPARE(viewport->cursorPosition(), 4);
    }

    void testTabFollowsTheTabKeyBehaviourSetting_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<int>("behavior");
        QTest::addColumn<int>("caret");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("caretAfter");

        const QString twoLines = "    alpha\n        beta\n";
        // A line whose auto-indent is deeper than the white space in front of
        // the caret, which is the only shape where moving the caret past that
        // white space first changes where it ends up.
        const QString shallower = "        alpha\n  beta\n";

        // Measured against a real TextEditorWidget rather than reasoned about:
        // these are the answers the widget editor gives, key for key.
        const QString start = "    alpha\n        beta\n";
        Q_UNUSED(start)
        // In the leading white space of the first line.
        QTest::newRow("never, in the indent")
            << twoLines << int(TypingSettingsData::TabNeverIndents) << 2
            << QString("      alpha\n        beta\n") << 4;
        QTest::newRow("always, in the indent")
            << twoLines << int(TypingSettingsData::TabAlwaysIndents) << 2
            << QString("alpha\n        beta\n") << 0;
        QTest::newRow("leading, in the indent")
            << twoLines << int(TypingSettingsData::TabLeadingWhitespaceIndents) << 2
            << QString("alpha\n        beta\n") << 0;
        // Inside the word on the second line, which is what tells Always from
        // In-Leading-White-Space apart.
        QTest::newRow("never, in the word")
            << twoLines << int(TypingSettingsData::TabNeverIndents) << 20
            << QString("    alpha\n        be  ta\n") << 22;
        QTest::newRow("always, in the word")
            << twoLines << int(TypingSettingsData::TabAlwaysIndents) << 20
            << QString("    alpha\n    beta\n") << 16;
        QTest::newRow("leading, in the word")
            << twoLines << int(TypingSettingsData::TabLeadingWhitespaceIndents) << 20
            << QString("    alpha\n        be  ta\n") << 22;
        // And where auto-indent makes the line deeper: the caret ends up at
        // the first non-space rather than adrift in the white space it was in.
        QTest::newRow("never, shallower line")
            << shallower << int(TypingSettingsData::TabNeverIndents) << 15
            << QString("        alpha\n     beta\n") << 18;
        QTest::newRow("always, shallower line")
            << shallower << int(TypingSettingsData::TabAlwaysIndents) << 15
            << QString("        alpha\n        beta\n") << 22;
        QTest::newRow("leading, shallower line")
            << shallower << int(TypingSettingsData::TabLeadingWhitespaceIndents) << 15
            << QString("        alpha\n        beta\n") << 22;
    }

    // "Tab key performs auto-indent: Never / Always / In Leading White Space."
    // This view always inserted one indent's worth, so the two settings that
    // are not the default did nothing at all.
    void testTabFollowsTheTabKeyBehaviourSetting()
    {
        QFETCH(QString, before);
        QFETCH(int, behavior);
        QFETCH(int, caret);
        QFETCH(QString, text);
        QFETCH(int, caretAfter);

        TemporaryDirectory dir("qtc-viewport-tabbehavior");
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents(before.toUtf8()));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QVERIFY(fixture.hasFocus());
        viewport->setReadOnly(false);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);
        // Said here rather than taken from the global code style, so that what
        // one level costs is not a shared setting's to change.
        TabSettingsData tabs = doc->tabSettings();
        tabs.m_indentSize = 4;
        tabs.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        // Or setTabSettings() would read the sample text and answer with what
        // *it* is indented by instead - see the test beside this one.
        tabs.m_autoDetect = false;
        doc->setTabSettings(tabs);
        QCOMPARE(doc->tabSettings().m_indentSize, 4);

        TypingSettingsData typing = doc->typingSettings();
        typing.m_tabKeyBehavior = TypingSettingsData::TabKeyBehavior(behavior);
        doc->setTypingSettings(typing);

        viewport->setCursorPosition(caret);
        QTest::keyClick(&fixture.view, Qt::Key_Tab);
        QCOMPARE(doc->document()->toPlainText(), text);
        QCOMPARE(viewport->cursorPosition(), caretAfter);
    }

    void testBackspaceOnlyTakesAPairThisViewPutIn()
    {
        TemporaryDirectory dir("qtc-viewport-autobackspace");
        const FilePath file = dir.filePath("brackets.txt");
        QVERIFY(file.writeFileContents("f()\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(fixture.hasFocus());

        // A viewport is a view until told otherwise, and a read-only one
        // never reaches the key at all.
        viewport->setReadOnly(false);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);
        const auto firstLine = [doc] { return doc->document()->firstBlock().text(); };
        const auto startFrom = [doc, viewport](const QString &line) {
            QTextCursor c(doc->document());
            c.select(QTextCursor::Document);
            c.insertText(line + "\n");
            viewport->setCursorPosition(0);
        };

        viewport->setAutoCompleter(new ClosesParentheses);

        // The fixture is only worth anything if brackets are inserted at all.
        QVERIFY2(globalCompletionSettings().autoInsertBrackets(),
                 "brackets are not auto-inserted, so nothing here is about them");
        QVERIFY2(globalCompletionSettings().autoRemove(),
                 "the preference under test starts turned off");

        // A pair that was already in the file. Backspace over the "(" takes
        // the "(" - the ")" belongs to whoever wrote it.
        QCOMPARE(firstLine(), QString("f()"));
        viewport->setCursorPosition(2);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(firstLine(), QString("f)"));

        // And a pair this view put in: typing "(" answers with "()", and
        // Backspace takes both, or the ")" is left orphaned.
        startFrom("f");
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, '(');
        QCOMPARE(firstLine(), QString("f()"));
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(firstLine(), QString("f"));

        // Which is what the preference turns off: the trigger goes and what
        // was inserted for it stays.
        const bool was = globalCompletionSettings().autoRemove();
        const QScopeGuard restore(
            [was] { globalCompletionSettings().autoRemove.setValue(was); });
        globalCompletionSettings().autoRemove.setValue(false);

        startFrom("f");
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, '(');
        QCOMPARE(firstLine(), QString("f()"));
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(firstLine(), QString("f)"));

        // And away from a pair it is an ordinary Backspace, taking one
        // character and leaving what is in front of the caret alone.
        startFrom("ab()");
        viewport->setCursorPosition(2);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(firstLine(), QString("a()"));

        // And automatic indentation turns it off as well, which is what the
        // widget editor gates it on: an editor that does not indent for the
        // reader does not take away what it inserted for them either.
        globalCompletionSettings().autoRemove.setValue(true);
        const TypingSettingsData wasTyping = doc->typingSettings();
        const QScopeGuard restoreTyping([doc, wasTyping] { doc->setTypingSettings(wasTyping); });
        TypingSettingsData typing = wasTyping;
        typing.m_autoIndent = false;
        doc->setTypingSettings(typing);

        startFrom("f");
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, '(');
        QCOMPARE(firstLine(), QString("f()"));
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(firstLine(), QString("f)"));
    }

    // A closed fold says what it swallowed. The widget editor draws "{...};"
    // after the line rather than leaving a gap, and puts back the brackets the
    // hidden text opened and closed - which is the part that has to come from
    // the document rather than from a constant.
    void testTheCaretStepsOverAFoldRatherThanIntoIt()
    {
        TemporaryDirectory dir("qtc-viewport-foldcaret");
        const FilePath file = writeLines(dir, "caret.txt", 20);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 6);
        QVERIFY(viewport->hasActiveFocus());

        QTextDocument * const text = fixture.document.textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        for (int i = 1; i <= 4; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(0), false);
        layout->requestUpdate();
        QTRY_COMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 5"));

        // Down from the line that owns the fold lands after it, not in it.
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_Down);
        const QTextBlock below = text->findBlock(viewport->cursorPosition());
        QCOMPARE(below.blockNumber(), 5);
        QVERIFY2(below.isVisible(), "the caret is on a line nobody can see");

        // And back up over it again.
        QTest::keyClick(&fixture.view, Qt::Key_Up);
        const QTextBlock above = text->findBlock(viewport->cursorPosition());
        QCOMPARE(above.blockNumber(), 0);
        QVERIFY(above.isVisible());

        // A page is not one of the moves the shared table handles - it depends
        // on how tall the view is - so it has to skip the fold on its own. The
        // fold has to be deeper than a page for this to be able to fail: a
        // page that clears it lands on a visible line whether it counted the
        // hidden ones or not.
        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(0), true);
        for (int i = 1; i <= 15; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(0), false);
        layout->requestUpdate();
        QTRY_COMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 16"));

        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_PageDown);
        const QTextBlock paged = text->findBlock(viewport->cursorPosition());
        QVERIFY2(paged.isVisible(),
                 qPrintable(QString("a page down left the caret on hidden line %1")
                                .arg(paged.blockNumber() + 1)));
    }

    void testAClosedFoldSaysWhatItSwallowed()
    {
        TemporaryDirectory dir("qtc-viewport-replacement");
        const FilePath file = dir.filePath("braces.txt");
        file.writeFileContents("struct S\n{\n    int a;\n};\nafter\n");

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 200\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/FoldReplacementTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        QTextDocument * const text = viewport->document()->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);

        // Line 1 owns lines 2 to 4, and the braces on the edges of the region
        // belong to the fold rather than to what is left showing.
        for (int i = 1; i <= 3; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        TextBlockUserData::setFoldingStartIncluded(text->findBlockByNumber(1), true);
        TextBlockUserData::setFoldingEndIncluded(text->findBlockByNumber(3), true);

        const QTextBlock first = text->findBlockByNumber(0);
        QVERIFY(TextBlockUserData::canFold(first));

        // What is on screen with the given text, if anything.
        auto shown = [item](const QString &wanted) -> QQuickItem * {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->isVisible() && candidate->property("text").toString() == wanted)
                    return candidate;
            }
            return nullptr;
        };

        // Nothing is folded, so nothing stands in for anything.
        QTRY_COMPARE(viewport->visibleLine(0).value("foldReplacement").toString(), QString());
        QVERIFY(!shown("{...};"));

        TextBlockUserData::doFoldOrUnfold(first, /*unfold=*/false);
        layout->requestUpdate();

        // The opening brace comes from the first hidden line and the closing
        // one from the last, semicolon included.
        QTRY_COMPARE(viewport->visibleLine(0).value("foldReplacement").toString(),
                     QString("{...};"));
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("after"));
        // Only the folded line has one.
        QCOMPARE(viewport->visibleLine(1).value("foldReplacement").toString(), QString());

        // And it is on screen, after the text of the line it stands for.
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = shown("{...};")) != nullptr);
        const QPointF at = box->mapToItem(viewport, QPointF(0, 0));
        const qreal lineWidth = viewport->visibleLine(0).value("width").toReal();
        QVERIFY2(lineWidth > 0, "the line has no width, so 'after it' means nothing");
        QVERIFY2(at.x() >= lineWidth,
                 qPrintable(QString("replacement at %1 overlaps text %2 wide")
                                .arg(at.x()).arg(lineWidth)));
        QCOMPARE(qRound(at.y() / viewport->lineHeight()), 0);

        // And clicking the box is the other way to open the fold: it is where
        // the reader is already looking, and the widget editor opens on it too.
        const QPointF middle = box->mapToItem(view.contentItem(),
                                              QPointF(box->width() / 2, box->height() / 2));
        QTest::mouseClick(&view, Qt::LeftButton, {}, middle.toPoint());
        QTRY_COMPARE(viewport->visibleLine(0).value("foldReplacement").toString(), QString());
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("{"));
        QTRY_VERIFY(!shown("{...};"));
    }

    void testCodeViewportNumbersItsLinesOnlyWhenAsked()
    {
        TemporaryDirectory dir("codeviewport-gutter");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 200);

        for (const bool numbered : {false, true}) {
            QQuickView view;
            installIconProvider(view);
            view.resize(400, 200);
            QQmlComponent component(view.engine());
            component.setData(QByteArray("import QtCreator.TextEditor\n"
                                         "CodeViewport {\n"
                                         "    width: 400; height: 200\n"
                                         "    property string path\n"
                                         "    property bool numbered\n"
                                         "    showLineNumbers: numbered\n"
                                         "    source: CodeDocument { filePath: path }\n"
                                         "}"),
                              QUrl("qrc:/test/CodeViewportGutterTest.qml"));
            std::unique_ptr<QObject> created(component.createWithInitialProperties(
                {{"path", file.toUrlishString()}, {"numbered", numbered}}));
            QVERIFY2(created != nullptr, qPrintable(component.errorString()));

            auto * const item = qobject_cast<QQuickItem *>(created.get());
            QVERIFY(item);
            item->setParentItem(view.contentItem());
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));

            auto * const viewport = item->findChild<TextViewport *>("codeViewport");
            QVERIFY(viewport);
            QTRY_VERIFY(viewport->visibleLineCount() > 3);
            auto * const gutter = item->findChild<QQuickItem *>("codeGutter");
            QVERIFY2(gutter, "the viewport has no gutter to show or hide");

            if (!numbered) {
                QVERIFY2(!gutter->isVisible(), "a preview numbered its lines");
                // And it takes no room: the text starts where it would have
                // without a gutter, not indented by an invisible one.
                QCOMPARE(gutter->width(), 0.0);
                continue;
            }

            QVERIFY(gutter->isVisible());
            QVERIFY2(gutter->width() > 0, "the gutter is shown and has no width");

            // A mark is drawn, not merely reported: the viewport's own answer
            // is checked elsewhere, and a delegate that read it once would
            // still be showing nothing.
            auto * const source = viewport->document();
            QVERIFY(source && source->textDocument());
            TextMark mark(source->textDocument(), 2,
                          TextMarkCategory{"Test", "TextEditor.Test.Mark"});
            mark.setIcon(Utils::Icons::WARNING.icon());
            QQuickItem *icon = nullptr;
            QTRY_VERIFY([&] {
                for (QQuickItem * const candidate : allItems(gutter)) {
                    if (!candidate->property("source").toUrl().isEmpty()
                        && candidate->isVisible()) {
                        icon = candidate;
                        return true;
                    }
                }
                return false;
            }());
            QCOMPARE(qRound(icon->y() / viewport->lineHeight()), 1);
            QTRY_COMPARE(gutterNumbers(gutter).size(), viewport->visibleLineCount());
            QCOMPARE(gutterNumbers(gutter).first(), QString("1"));
            // The text was moved over to make room rather than drawn under it.
            QVERIFY2(viewport->x() >= gutter->width(),
                     "the text is drawn on top of the line numbers");
        }
    }

    // The line the caret is on. An editor without it is hard to read, and the
    // bar has to be on the caret's line rather than near it - the viewport is
    // inset, so a highlight that forgets the inset sits a margin too high.
    void testTheCurrentLineIsHighlightedWhereTheCaretIs()
    {
        TemporaryDirectory dir("currentline-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 200);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        // With the numbers on: the band must not reach back over them, and a
        // gutter of no width could not tell whether it did.
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 400; height: 200\n"
                                     "    showLineNumbers: true\n"
                                     "    highlightCurrentLine: true\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/CurrentLineTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const highlight = item->findChild<QQuickItem *>("currentLineHighlight");
        QVERIFY2(highlight, "no current-line highlight");
        // Asked for above. Without this the assertions below still pass on a
        // hidden band, because an invisible item has geometry like any other.
        QTRY_VERIFY2(highlight->isVisible(), "the highlight was asked for and is not shown");

        // On the caret's line, in the same coordinates the caret is drawn in.
        const auto lineOf = [viewport, highlight] {
            return qRound((highlight->y() - viewport->y()) / viewport->lineHeight());
        };
        viewport->setCursorPosition(0);
        QTRY_COMPARE(lineOf(), 0);
        QCOMPARE(highlight->height(), viewport->lineHeight());

        // The text and all of it, and none of the gutter beside it. The widget
        // editor draws this inside its viewport; a band that reaches back over
        // the line numbers reads as a selection rather than as "you are here",
        // and hides the current line number's own colour.
        QCOMPARE(highlight->width(), viewport->width());
        QCOMPARE(highlight->x(), viewport->x());
        auto * const gutter = item->findChild<QQuickItem *>("codeGutter");
        QVERIFY2(gutter, "no gutter, so there is nothing for the band to spill over");
        QVERIFY2(gutter->width() > 0, "the gutter is not showing, so this asserts nothing");
        QVERIFY2(highlight->x() >= gutter->x() + gutter->width(),
                 "the current-line highlight reached over the line numbers");

        // And it follows the caret rather than staying where it started.
        viewport->forceActiveFocus();
        QVERIFY2(viewport->hasActiveFocus(), "the viewport never took focus, so no key arrives");
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_Down);
        QTRY_VERIFY2(viewport->cursorPosition() > 0, "the caret did not move, so this says nothing");
        QTRY_COMPARE(lineOf(), 2);
        QCOMPARE(qRound(highlight->y() - viewport->y()),
                 qRound(viewport->cursorRectangle().y()));
    }

    // Errors, warnings and breakpoints appear beside the line they are about.
    // They arrive and go while the file sits there, so the gutter has to hear
    // about them without the text changing.
    void testTheGutterShowsAMarkAndForgetsItAgain()
    {
        TemporaryDirectory dir("marks-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 200);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // Nothing is marked to begin with.
        QVERIFY(viewport->visibleLine(2).value("markIcon").toString().isEmpty());

        {
            // Line 3, which is index 2 on screen: marks count from one.
            TextMark mark(fixture.document.textDocument(), 3,
                          TextMarkCategory{"Test", "TextEditor.Test.Mark"});
            mark.setIcon(Utils::Icons::WARNING.icon());
            mark.setLineAnnotation("something is wrong here");
            QVERIFY(!mark.icon().isNull());

            QTRY_VERIFY2(!viewport->visibleLine(2).value("markIcon").toString().isEmpty(),
                         "the mark never reached the gutter");
            QCOMPARE(viewport->visibleLine(2).value("annotation").toString(),
                     QString("something is wrong here"));
            // And only that line.
            QVERIFY(viewport->visibleLine(1).value("markIcon").toString().isEmpty());
            QVERIFY(viewport->visibleLine(3).value("markIcon").toString().isEmpty());
        }

        // The mark is gone with its object, and so is what the gutter shows.
        QTRY_VERIFY2(viewport->visibleLine(2).value("markIcon").toString().isEmpty(),
                     "the gutter still shows a mark that no longer exists");
    }

    // Clicking the gutter's mark column asks for a mark on the line that was
    // clicked - which is how a breakpoint gets set. Nothing inside TextEditor
    // answers the request, so what is asserted is the request itself and the
    // line it names: that is exactly what the debugger connects to.
    void testClickingTheMarkColumnAsksForAMarkOnThatLine()
    {
        TemporaryDirectory dir("qtc-viewport-markclick");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "clickable.txt", 40);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 500);
        QQmlComponent component(view.engine());
        // requestMarks, because a viewport is a preview until told otherwise
        // and a preview must not offer to put a breakpoint in a file that
        // does not exist.
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 500\n"
                                     "    showLineNumbers: true\n"
                                     "    requestMarks: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/MarkClickTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 16);

        auto * const markColumn = item->findChild<QQuickItem *>("gutterMarkColumn");
        QVERIFY2(markColumn, "the gutter offers nothing in its mark column to click");
        QVERIFY(markColumn->width() > 0);

        TextDocument * const document = viewport->document()->textDocument();
        QVERIFY(document);

        // Where a line is, as a point inside the mark column. The gutter and
        // the viewport are laid out separately and only happen to line up, so
        // the y is mapped through both rather than assumed to agree.
        auto pointForLine = [&](int line) {
            const qreal y = (line - 1 + 0.5) * viewport->lineHeight() - viewport->scrollY();
            const QPointF inColumn(markColumn->width() / 2,
                                   markColumn->mapFromItem(viewport, QPointF(0, y)).y());
            return view.contentItem()->mapFromItem(markColumn, inColumn).toPoint();
        };

        QSignalSpy requested(document, &TextDocument::markRequested);
        QVERIFY(requested.isValid());

        QTest::mouseClick(&view, Qt::LeftButton, {}, pointForLine(7));
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first().at(1).toInt(), 7);
        // BreakpointRequest is zero, and so is what an argument the spy could
        // not record answers - hence the validity check beside the value.
        QVERIFY(requested.first().at(2).isValid());
        QCOMPARE(requested.first().at(2).toInt(), int(BreakpointRequest));

        // A second line, so that a mapping which always lands on the same row
        // cannot pass this.
        QTest::mouseClick(&view, Qt::LeftButton, {}, pointForLine(12));
        QTRY_COMPARE(requested.size(), 2);
        QCOMPARE(requested.last().at(1).toInt(), 12);

        // A mark already on the line takes the click instead, which is how
        // clicking a breakpoint's own icon acts on it rather than asking for
        // a second one.
        class ClickCountingMark final : public TextMark
        {
        public:
            using TextMark::TextMark;
            bool isClickable() const override { return true; }
            void clicked() override { ++clicks; }
            int clicks = 0;
        };
        ClickCountingMark mark(document, 9,
                               TextMarkCategory{"Test", "TextEditor.Test.MarkClick"});
        QTest::mouseClick(&view, Qt::LeftButton, {}, pointForLine(9));
        QTRY_COMPARE(mark.clicks, 1);
        QCOMPARE(requested.size(), 2);

        // Shift makes it a bookmark rather than a request - the same key the
        // widget editor's column reads.
        BookmarkManager &bookmarks = bookmarkManager();
        QVERIFY(!bookmarks.hasBookmarkInPosition(file, 5));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ShiftModifier, pointForLine(5));
        QTRY_VERIFY(bookmarks.hasBookmarkInPosition(file, 5));
        QCOMPARE(requested.size(), 2);

        // Bookmarks are global state that outlives this test, so put it back.
        bookmarks.toggleBookmark(file, 5);
        QVERIFY(!bookmarks.hasBookmarkInPosition(file, 5));
    }

    // The same column, right-clicked. What is on offer for the line is
    // assembled into a QMenu the way the widget editor assembles it, and
    // listed as the model the gutter's menu draws - so the bridge between the
    // two is what this covers, along with the line everyone is asked about.
    // A mark is moved by dragging it to another line - that is how a
    // breakpoint is moved, and how the current-location marker jumps the
    // debugger. The gutter could only click, so every TextMark::dragToLine()
    // override in the tree was unreachable from a Quick view.
    void testDraggingAMarkInTheGutterMovesItToTheDroppedLine()
    {
        TemporaryDirectory dir("qtc-viewport-markdrag");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "draggable.txt", 40);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 500);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 500\n"
                                     "    showLineNumbers: true\n"
                                     "    requestMarks: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/MarkDragTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 16);

        auto * const markColumn = item->findChild<QQuickItem *>("gutterMarkColumn");
        QVERIFY(markColumn);
        TextDocument * const document = viewport->document()->textDocument();
        QVERIFY(document);

        auto pointForLine = [&](int line) {
            const qreal y = (line - 1 + 0.5) * viewport->lineHeight() - viewport->scrollY();
            const QPointF inColumn(markColumn->width() / 2,
                                   markColumn->mapFromItem(viewport, QPointF(0, y)).y());
            return view.contentItem()->mapFromItem(markColumn, inColumn).toPoint();
        };

        class DraggableMark final : public TextMark
        {
        public:
            DraggableMark(TextDocument *document, int line)
                : TextMark(document, line, {"Draggable", "TextEditor.Test.Draggable"})
            {}
            bool isDraggable() const override { return true; }
            void dragToLine(int line) override { droppedOn = line; }
            int droppedOn = -1;
        };

        DraggableMark mark(document, 7);
        QVERIFY2(document->marks().contains(&mark), "the fixture never attached the mark");

        // A click is not a drag. Without this the test would pass on a gutter
        // that moved a mark whenever the column was touched.
        QTest::mouseClick(&view, Qt::LeftButton, {}, pointForLine(7));
        QCOMPARE(mark.droppedOn, -1);
        QVERIFY2(mark.isVisible(), "a click left the mark hidden");

        // Press on it, carry it down the gutter, drop it.
        QTest::mousePress(&view, Qt::LeftButton, {}, pointForLine(7));
        QTest::mouseMove(&view, pointForLine(12));
        // Hidden while it is carried, or it appears to be in two places.
        QTRY_VERIFY2(!mark.isVisible(), "the mark stayed put while being dragged");
        QTest::mouseRelease(&view, Qt::LeftButton, {}, pointForLine(12));

        QTRY_COMPARE(mark.droppedOn, 12);
        QVERIFY2(mark.isVisible(), "the mark was left hidden after the drop");
    }

    void testTheMarkColumnOffersWhatThereIsToSayAboutTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-markmenu");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "menu.txt", 40);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 8);

        TextDocument * const document = viewport->document()->textDocument();
        QVERIFY(document);

        int askedAbout = 0;
        connect(document, &TextDocument::markContextMenuRequested, this,
                [&](TextDocument *, int line, QMenu *menu) {
                    askedAbout = line;
                    menu->addAction("Set Breakpoint");
                });

        QtcQuick::ActionModel * const actions = viewport->markActions();
        QVERIFY(actions);
        QCOMPARE(actions->rowCount(), 0);

        QVERIFY(viewport->prepareMarkMenu((6 - 1 + 0.5) * viewport->lineHeight()));
        QCOMPARE(askedAbout, 6);

        // What the bookmark manager put in, and what the handler above did.
        QVERIFY(actions->rowCount() >= 2);
        const QString last = actions->index(actions->rowCount() - 1, 0)
                                 .data(QtcQuick::ActionModel::TextRole).toString();
        QCOMPARE(last, QString("Set Breakpoint"));

        // A different line, so that a lookup answering a fixed one fails here.
        // On screen, and asserted to be: positionAt() clamps to what is laid
        // out, so a line below the fold would quietly resolve to the last row
        // and this would be measuring the clamp.
        QVERIFY(viewport->visibleLineCount() > 3);
        QVERIFY(viewport->prepareMarkMenu((3 - 1 + 0.5) * viewport->lineHeight()));
        QCOMPARE(askedAbout, 3);
    }

    // The other half of a diagnostic: what it says, beside the line it is
    // about, after the text rather than at a fixed column.
    void testAMarksMessageIsDrawnAfterTheLine()
    {
        TemporaryDirectory dir("annotation-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 200);

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/AnnotationTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());

        const QString message = "expected ';' after expression";
        TextMark mark(source->textDocument(), 3, TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setIcon(Utils::Icons::WARNING.icon());
        mark.setLineAnnotation(message);

        // The viewport knows what the mark says...
        QTRY_COMPARE(viewport->visibleLine(2).value("annotation").toString(), message);

        // ...and it is drawn somewhere in the viewport.
        QQuickItem *drawn = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->property("text").toString() == message && candidate->isVisible()) {
                    drawn = candidate;
                    return true;
                }
            }
            return false;
        }());

        // After the text on that line, not before it and not at a fixed column.
        // In the viewport's coordinates rather than the item's own: what is
        // between the text and the annotation is a layout detail, and asking
        // for a local x quietly measures a different thing when it changes.
        const QPointF at = drawn->mapToItem(viewport, QPointF(0, 0));
        const qreal lineWidth = viewport->visibleLine(2).value("width").toReal();
        QVERIFY2(lineWidth > 0, "the line has no width, so 'after it' means nothing");
        QVERIFY2(at.x() >= lineWidth,
                 qPrintable(QString("annotation at %1 overlaps text %2 wide")
                                .arg(at.x()).arg(lineWidth)));
        // And on that line.
        QCOMPARE(qRound(at.y() / viewport->lineHeight()), 2);
    }

    // Where that message goes is a setting, and its default is the right
    // side - not after the text. The test above only asks that the message
    // does not overlap the line, which every alignment satisfies, so it never
    // said where the message actually is.
    void testWhereAMarksMessageGoesIsASetting()
    {
        const AnnotationAlignment was = displaySettings().annotationAlignment();
        const QScopeGuard restore(
            [was] { displaySettings().annotationAlignment.setValue(was); });

        TemporaryDirectory dir("annotation-alignment");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("big.txt");
        // Line 5 is long enough that the right edge is left of where the text
        // ends, which is the case that decides whether a right-aligned
        // message is allowed to sit on top of the line.
        QString contents;
        for (int i = 0; i < 200; ++i)
            contents += (i == 4 ? QString(80, 'x') : QString("line %1").arg(i)) + '\n';
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/AnnotationAlignmentTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());

        const QString message = "expected ';' after expression";
        TextMark mark(source->textDocument(), 3, TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setIcon(Utils::Icons::WARNING.icon());
        mark.setLineAnnotation(message);
        QTRY_COMPARE(viewport->visibleLine(2).value("annotation").toString(), message);

        const auto annotationX = [viewport] {
            return viewport->visibleLine(2).value("annotationX").toReal();
        };
        const qreal messageWidth
            = QFontMetricsF(viewport->font()).horizontalAdvance(message);
        const qreal lineWidth = viewport->visibleLine(2).value("width").toReal();
        QVERIFY(lineWidth > 0);

        // The default: the message ends at the right edge of the view.
        QCOMPARE(displaySettings().annotationAlignment(), AnnotationAlignment::RightSide);
        QTRY_COMPARE(qRound(annotationX() + messageWidth), qRound(viewport->width()));

        // And what is drawn is where the viewport said, rather than wherever
        // the row happens to put it. Looked up again each time: the delegates
        // are rebuilt when the rows change, so a pointer kept from before is
        // an item that is no longer the one on screen.
        const auto drawnX = [&]() -> qreal {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->property("text").toString() == message && candidate->isVisible())
                    return candidate->mapToItem(viewport, QPointF(0, 0)).x();
            }
            return -1;
        };
        QTRY_COMPARE(qRound(drawnX()), qRound(annotationX()));

        // Next to the content instead: just after the text, nowhere near the
        // right edge.
        displaySettings().annotationAlignment.setValue(AnnotationAlignment::NextToContent);
        QTRY_VERIFY2(annotationX() < viewport->width() - messageWidth,
                     "the message stayed at the right edge after being told to follow the text");
        QVERIFY2(annotationX() > lineWidth, "the message was drawn over the line it is about");
        QTRY_COMPARE(qRound(drawnX()), qRound(annotationX()));

        // Back to the right side, on a line whose text reaches past where the
        // right edge would put the message. The edge is a minimum, not a
        // position: the message goes after the text instead of over it.
        displaySettings().annotationAlignment.setValue(AnnotationAlignment::RightSide);
        const QString longMessage = "unused variable";
        TextMark onLongLine(source->textDocument(), 5,
                            TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        onLongLine.setIcon(Utils::Icons::WARNING.icon());
        onLongLine.setLineAnnotation(longMessage);
        QTRY_COMPARE(viewport->visibleLine(4).value("annotation").toString(), longMessage);

        const qreal longLineWidth = viewport->visibleLine(4).value("width").toReal();
        const qreal longMessageWidth
            = QFontMetricsF(viewport->font()).horizontalAdvance(longMessage);
        QVERIFY2(longLineWidth > viewport->width() - longMessageWidth,
                 "the line is not long enough for the right edge to fall inside it");
        QTRY_VERIFY2(viewport->visibleLine(4).value("annotationX").toReal() > longLineWidth,
                     "a right-aligned message was drawn over the line it is about");
    }

    // "Editor content width" narrows the text from both sides. It is what Zen
    // mode changes to make a file readable, and the Quick editor read it
    // nowhere - so the preference and Zen mode both did nothing to it.
    void testTheContentCanBeNarrowedFromBothSides()
    {
        const int was = marginSettings().centerEditorContentWidthPercent();
        const QScopeGuard restore(
            [was] { marginSettings().centerEditorContentWidthPercent.setValue(was); });
        marginSettings().centerEditorContentWidthPercent.setValue(100);

        TemporaryDirectory dir("qtc-viewport-contentwidth");
        const FilePath file = dir.filePath("wide.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/ContentWidthTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const qreal fullWidth = viewport->width();
        const qreal leftAtFullWidth = viewport->x();
        QVERIFY(fullWidth > 0);

        // Half the width means a quarter of the view taken off each side.
        marginSettings().centerEditorContentWidthPercent.setValue(50);
        QTRY_COMPARE(qRound(viewport->width()), qRound(fullWidth - item->width() / 2));
        QCOMPARE(qRound(viewport->x()), qRound(leftAtFullWidth + item->width() / 4));

        // And back: the whole width means no inset at all.
        marginSettings().centerEditorContentWidthPercent.setValue(100);
        QTRY_COMPARE(qRound(viewport->width()), qRound(fullWidth));
        QCOMPARE(qRound(viewport->x()), qRound(leftAtFullWidth));
    }

    // The document drawn small beside it, and dragging the marked part
    // scrolls the text. Off by default, so it takes no room until asked for.
    void testTheMinimapShowsTheDocumentAndScrollsIt()
    {
        const bool was = displaySettings().displayMinimap();
        const QScopeGuard restore([was] { displaySettings().displayMinimap.setValue(was); });
        displaySettings().displayMinimap.setValue(false);

        TemporaryDirectory dir("qtc-viewport-minimap");
        const FilePath file = writeLines(dir, "long.txt", 400);

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/MinimapTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const minimap = item->findChild<QQuickItem *>("minimap");
        QVERIFY(minimap);

        // Off: no room taken, nothing drawn.
        QVERIFY(!minimap->isVisible());
        QCOMPARE(minimap->width(), qreal(0));
        const qreal fullWidth = viewport->width();

        // On: it appears and the text makes room for it.
        displaySettings().displayMinimap.setValue(true);
        QTRY_VERIFY(minimap->isVisible());
        QVERIFY(minimap->width() > 0);
        QTRY_COMPARE(viewport->width(), fullWidth - minimap->width());

        // The marked part is at the top of a document scrolled to the top,
        // and is shorter than the whole picture because the file is longer
        // than the screen.
        QVERIFY(viewport->contentHeight() > viewport->height());
        QCOMPARE(viewport->scrollY(), qreal(0));

        // Dragging it down scrolls the text down with it.
        const QPoint onThumb = minimap->mapToScene(QPointF(minimap->width() / 2, 3)).toPoint();
        const QPoint lower = minimap->mapToScene(QPointF(minimap->width() / 2, 60)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, onThumb);
        QTest::mouseMove(&view, lower);
        QTRY_VERIFY2(viewport->scrollY() > 0,
                     "dragging the marked part did not scroll the text");
        QTest::mouseRelease(&view, Qt::LeftButton, {}, lower);

        // A press away from the marked part is not a handle. The widget
        // editor ignores it rather than jumping there, and the drag above
        // is what proves these events arrive at all.
        viewport->setScrollY(0);
        QTRY_COMPARE(viewport->scrollY(), qreal(0));
        const QPoint belowThumb
            = minimap->mapToScene(QPointF(minimap->width() / 2, minimap->height() - 10)).toPoint();
        const QPoint higher
            = minimap->mapToScene(QPointF(minimap->width() / 2, minimap->height() - 60)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, belowThumb);
        QTest::mouseMove(&view, higher);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, higher);
        QCOMPARE(viewport->scrollY(), qreal(0));
    }

    // The picture is of the document, so it is drawn again when the document
    // changes and not when the view merely scrolls. Redrawing a whole file
    // because the caret moved is a repaint nobody asked for.
    void testTheMinimapIsRedrawnForTheDocumentAndNotForScrolling()
    {
        const bool was = displaySettings().displayMinimap();
        const QScopeGuard restore([was] { displaySettings().displayMinimap.setValue(was); });
        displaySettings().displayMinimap.setValue(true);

        TemporaryDirectory dir("qtc-viewport-minimap-redraw");
        const FilePath file = writeLines(dir, "long.txt", 400);

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/MinimapRedrawTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const minimap = item->findChild<QQuickItem *>("minimap");
        QVERIFY(minimap);
        QTRY_VERIFY(minimap->isVisible());

        // Drawn at least once, and as tall as the file is long.
        QTRY_VERIFY(minimap->property("pictureHeight").toInt() > 0);
        const int tall = minimap->property("pictureHeight").toInt();

        QSignalSpy redraws(minimap, SIGNAL(pictureChanged()));

        // Scrolling moves the marked part and nothing else.
        viewport->setScrollY(viewport->lineHeight() * 100);
        QTRY_COMPARE(viewport->firstVisibleLine(), 100);
        QCOMPARE(minimap->property("pictureHeight").toInt(), tall);
        QCOMPARE(redraws.size(), 0);

        // Adding lines is a different document, so it is drawn again - and
        // the picture grows, which is what proves it was drawn from the new
        // text rather than merely announced.
        QTextDocument * const text = viewport->textDocument()->document();
        QTextCursor cursor(text);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QString(50, QChar('\n')));
        QTRY_VERIFY2(redraws.size() > 0, "the picture was not drawn again for a longer document");
        QTRY_VERIFY2(minimap->property("pictureHeight").toInt() > tall,
                     "the picture did not grow with the document");
    }

    // Marks show up on the scroll bar, which is how a file too long to see
    // says where its errors are. On by default, and the Quick editor had
    // none of it.
    void testMarksAreShownOnTheScrollBar()
    {
        const bool was = displaySettings().scrollBarHighlights();
        const QScopeGuard restore(
            [was] { displaySettings().scrollBarHighlights.setValue(was); });
        displaySettings().scrollBarHighlights.setValue(true);

        TemporaryDirectory dir("qtc-viewport-scrollmarks");
        const FilePath file = writeLines(dir, "long.txt", 400);

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/ScrollMarkTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());

        // The caret's line is on the bar whether or not anything is marked,
        // so a mark has to be told apart by its colour.
        const int before = viewport->scrollBarHighlights().size();

        TextMark mark(source->textDocument(), 200,
                      TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setColor(Utils::Theme::TextColorError);
        const QColor marked = Utils::creatorColor(Utils::Theme::TextColorError);

        const auto positionOfMark = [viewport, marked]() -> qreal {
            const QVariantList highlights = viewport->scrollBarHighlights();
            for (const QVariant &entry : highlights) {
                if (entry.toMap().value("color").value<QColor>() == marked)
                    return entry.toMap().value("position").toReal();
            }
            return -1;
        };

        QTRY_COMPARE(viewport->scrollBarHighlights().size(), before + 1);
        // Line 200 of 401 is about halfway down.
        const qreal at = positionOfMark();
        QVERIFY2(at > 0.45 && at < 0.55,
                 qPrintable(QString("a mark halfway down the file is at %1").arg(at)));

        // And it is drawn there, on the bar rather than somewhere in the text.
        QQuickItem * const overlay = item->findChild<QQuickItem *>("scrollBarHighlights");
        QVERIFY(overlay);
        // The whole visual tree under it: a Repeater's delegates are children
        // of the item the Repeater is in, not of the Repeater.
        const auto drawnAt = [overlay, marked]() -> qreal {
            for (QQuickItem * const rect : allItems(overlay)) {
                if (rect->property("color").value<QColor>() == marked)
                    return rect->y();
            }
            return -1;
        };
        QTRY_COMPARE(qRound(drawnAt()), qRound(at * overlay->height()));

        // Turned off, the bar carries nothing at all - not even the caret.
        displaySettings().scrollBarHighlights.setValue(false);
        QTRY_VERIFY2(viewport->scrollBarHighlights().isEmpty(),
                     "the bar still carried marks with the setting turned off");
    }

    // "Highlight selection" promises two things - the preference says so in as
    // many words: "Adds a colored background and a marker to the scrollbar to
    // occurrences of the selected text." The Qt Quick view tinted the
    // occurrences on the rows it had laid out and put nothing on the bar, so
    // the ones below the fold - which are the reason to look at the bar at all
    // - were invisible. On by default, both of them.
    void testTheSelectionsOtherOccurrencesAreShownOnTheScrollBar()
    {
        const bool wasBar = displaySettings().scrollBarHighlights();
        const bool wasSelection = displaySettings().highlightSelection();
        const QScopeGuard restore([wasBar, wasSelection] {
            displaySettings().scrollBarHighlights.setValue(wasBar);
            displaySettings().highlightSelection.setValue(wasSelection);
        });
        displaySettings().scrollBarHighlights.setValue(true);
        displaySettings().highlightSelection.setValue(true);

        TemporaryDirectory dir("qtc-viewport-selection-marks");
        const FilePath file = dir.filePath("occurrences.txt");
        // "alpha" on lines 0, 100, 200 and 300 of 400. Line 0 is on screen and
        // the other three are not, which is the case under test.
        QString contents;
        for (int i = 0; i < 400; ++i)
            contents += (i % 100 == 0) ? QString("alpha\n") : QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/SelectionMarkTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        QVERIFY2(viewport->visibleLineCount() < 100,
                 "the whole file is on screen, so nothing is below the fold to miss");

        const QColor selectionColour
            = Utils::creatorColor(Utils::Theme::TextEditor_Selection_ScrollBarColor);
        const auto marks = [viewport, selectionColour] {
            QList<qreal> at;
            const QVariantList highlights = viewport->scrollBarHighlights();
            for (const QVariant &entry : highlights) {
                if (entry.toMap().value("color").value<QColor>() == selectionColour)
                    at << entry.toMap().value("position").toReal();
            }
            std::sort(at.begin(), at.end());
            return at;
        };

        // The caret's own line is on the bar from the start, so the marks
        // under test have to be told apart by their colour - and there are
        // none of them before anything is selected.
        QVERIFY2(marks().isEmpty(), "the bar carried selection marks with nothing selected");

        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QCOMPARE(viewport->selectedText(), QString("alpha"));

        QTRY_COMPARE(marks().size(), 4);
        // Lines 0, 100, 200 and 300 of 401: a quarter of the file apart.
        const QList<qreal> where = marks();
        for (int i = 0; i < where.size(); ++i) {
            const qreal wanted = qreal(i * 100) / 401;
            QVERIFY2(qAbs(where.at(i) - wanted) < 0.02,
                     qPrintable(QString("mark %1 is at %2, not near %3")
                                    .arg(i).arg(where.at(i)).arg(wanted)));
        }

        // An edit moves every match that follows it, and the selected text is
        // still the selected text - so nothing about the *needle* says the
        // answer is stale. Appended at the end, past every mark and past the
        // selection, so what changes is the file rather than what is selected.
        QTextDocument * const text = viewport->document()->textDocument()->document();
        QTextCursor atEnd(text);
        atEnd.movePosition(QTextCursor::End);
        atEnd.insertText("alpha\n");
        QTRY_COMPARE(marks().size(), 5);

        // A selection that spans lines is a passage, not a name: the widget
        // editor shows nothing for it and neither does this. One character
        // past the end of the line rather than a hundred, because that is the
        // case the rule has to be there for - trimming takes the line break
        // off again, so what is left looks exactly like the single-line
        // selection above and would be searched for as one.
        viewport->setSelectionEnd(6);
        QTRY_VERIFY2(marks().isEmpty(),
                     "a selection reaching into the next line was searched for as a word");

        // And the preference turns them off on their own, with the bar itself
        // still on: turned off while marks are showing, so what is waited for
        // is them going away rather than their continued absence.
        viewport->setSelectionEnd(5);
        QTRY_COMPARE(marks().size(), 5);
        displaySettings().highlightSelection.setValue(false);
        QTRY_VERIFY2(marks().isEmpty(),
                     "the occurrences stayed on the bar with the preference turned off");
    }

    // Scrolling moves the rows up: what row five said is what row four says
    // now, word for word. Telling the view they all changed makes it read
    // every one again, so a scroll is reported as the rows that left the top
    // and the ones that arrived at the bottom.
    void testAScrollIsReportedAsRowsLeavingAndArriving()
    {
        TemporaryDirectory dir("qtc-viewport-shift");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        QAbstractItemModel * const rows = viewport->visibleRows();
        QVERIFY(rows);

        // Settled first: a view still finding its width lays out differently,
        // and those rows really have all changed.
        viewport->setScrollY(viewport->lineHeight());
        QTRY_COMPARE(viewport->firstVisibleLine(), 1);

        QSignalSpy changed(rows, &QAbstractItemModel::dataChanged);
        QSignalSpy removed(rows, &QAbstractItemModel::rowsRemoved);

        viewport->setScrollY(viewport->lineHeight() * 2);
        QTRY_COMPARE(viewport->firstVisibleLine(), 2);

        QCOMPARE(removed.size(), 1);
        QVERIFY2(changed.isEmpty(),
                 qPrintable(QString("the rows that stayed were announced as changed %1 times")
                                .arg(changed.size())));

        // And what is on screen is still right, which is the thing the
        // shortcut must not cost.
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("line 2"));
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("line 3"));

        // Asked of the model rather than of the view: the view's own rows are
        // right whatever the model was told, so reading them would not notice
        // a row the model kept when it should not have. By role name, which
        // is what a delegate binds to.
        const auto rowFromModel = [rows](int row, const QByteArray &role) {
            const int id = rows->roleNames().key(role, -1);
            return rows->data(rows->index(row, 0), id);
        };

        // A scroll that happens in the same layout as something else: the
        // rows still line up one for one, but one of them says something new.
        // Matching the first row is not enough to conclude the rest are
        // unchanged, which is why the whole overlap is compared.
        QTextDocument * const text = viewport->textDocument()->document();
        // Line 6 is the fifth row at this scroll and the fourth at the next
        // one. How wide it is is read by the form, so widening it is a change
        // a kept row would be caught still denying.
        const qreal before = rowFromModel(4, "width").toReal();
        QVERIFY(before > 0);

        QTextCursor edit(text);
        edit.setPosition(text->findBlockByNumber(6).position());
        edit.insertText("wider wider wider");
        viewport->setScrollY(viewport->lineHeight() * 3);
        QTRY_COMPARE(viewport->firstVisibleLine(), 3);

        QTRY_VERIFY2(rowFromModel(3, "width").toReal() > before,
                     "the widened row was kept from before it was widened");
    }

    // Typing changes the row it was typed on and leaves the others saying
    // exactly what they said. Announcing all of them makes every delegate on
    // screen evaluate every binding it has, which is the cost a role per
    // value was meant to avoid.
    void testAnEditAnnouncesTheRowItChangedAndNoOthers()
    {
        TemporaryDirectory dir("qtc-viewport-onerow");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        QAbstractItemModel * const rows = viewport->visibleRows();
        QVERIFY(rows);

        // Settled first: a view still finding its width lays out differently,
        // and those rows really have all changed.
        viewport->setScrollY(viewport->lineHeight());
        QTRY_COMPARE(viewport->firstVisibleLine(), 1);

        QSignalSpy changed(rows, &QAbstractItemModel::dataChanged);

        QTextDocument * const text = viewport->textDocument()->document();
        QTextCursor edit(text);
        edit.setPosition(text->findBlockByNumber(3).position());
        edit.insertText("!");

        // Line 3 is the third row at this scroll, and it is the only one that
        // says anything new.
        QTRY_VERIFY(!changed.isEmpty());
        for (const QList<QVariant> &announcement : std::as_const(changed)) {
            const int from = qvariant_cast<QModelIndex>(announcement.at(0)).row();
            const int to = qvariant_cast<QModelIndex>(announcement.at(1)).row();
            QCOMPARE(from, 2);
            QCOMPARE(to, 2);
        }
    }

    // Scrolling brings back text that has already been shaped, and shaping is
    // most of what a layout costs. A row whose text and formats are what they
    // were is kept rather than built again.
    void testScrollingKeepsTheRowsItAlreadyShaped()
    {
        TemporaryDirectory dir("qtc-viewport-reshape");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // The first layout has nothing to keep, so it shapes what it shows.
        const int onScreen = viewport->visibleLineCount();
        QVERIFY(onScreen > 3);

        // Two scrolls, and the second is the one measured: a view still
        // settling its geometry lays out at a different width, and a row
        // shaped at another width cannot be kept.
        viewport->setScrollY(viewport->lineHeight());
        QTRY_COMPARE(viewport->firstVisibleLine(), 1);
        viewport->setScrollY(viewport->lineHeight() * 2);
        QTRY_COMPARE(viewport->firstVisibleLine(), 2);
        QVERIFY2(viewport->rowsShapedInLastLayout() < onScreen,
                 qPrintable(QString("shaped %1 of %2 rows again after scrolling one line")
                                .arg(viewport->rowsShapedInLastLayout()).arg(onScreen)));

        // Editing a line changes its text without changing the formats over
        // it, so what is shown has to be the new text: keeping the old row
        // because its formats still match would leave the edit invisible.
        QTextDocument * const text = viewport->textDocument()->document();
        QTextCursor cursor(text->findBlockByNumber(5));
        cursor.insertText("x");
        // Line 5 is the fourth row on screen once the top two are scrolled off.
        QTRY_COMPARE(viewport->visibleLine(3).value("text").toString(), QString("xline 5"));
    }

    // What the bar carries changes when the document's marks do, and not when
    // the view merely scrolls. A bar told to look again rebuilds every mark
    // on it, which for a search with a few thousand hits is a lot of nothing.
    void testTheScrollBarIsToldOnlyWhenItsMarksChange()
    {
        const bool was = displaySettings().scrollBarHighlights();
        const QScopeGuard restore(
            [was] { displaySettings().scrollBarHighlights.setValue(was); });
        displaySettings().scrollBarHighlights.setValue(true);

        TemporaryDirectory dir("qtc-viewport-barnotify");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // The caret's line is on the bar from the start.
        QTRY_VERIFY(!viewport->scrollBarHighlights().isEmpty());
        const int carried = viewport->scrollBarHighlights().size();

        QSignalSpy told(viewport, &TextViewport::scrollBarHighlightsChanged);

        // Scrolling moves what is on screen, not what is marked.
        viewport->setScrollY(viewport->lineHeight() * 100);
        QTRY_COMPARE(viewport->firstVisibleLine(), 100);
        QCOMPARE(viewport->scrollBarHighlights().size(), carried);
        QCOMPARE(told.size(), 0);

        // A mark is a change, and the bar is told once.
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());
        TextMark mark(source->textDocument(), 200,
                      TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setColor(Utils::Theme::TextColorError);
        QTRY_VERIFY2(told.size() > 0, "the bar was never told about a new mark");
        QCOMPARE(viewport->scrollBarHighlights().size(), carried + 1);
    }

    // A jump within the file can be scrolled to rather than snapped to, so
    // that the reader can see which way it went. Off by default, and the
    // Quick editor always snapped.
    void testAJumpWithinTheFileCanBeScrolledTo()
    {
        const bool was = displaySettings().animateNavigationWithinFile();
        const QScopeGuard restore(
            [was] { displaySettings().animateNavigationWithinFile.setValue(was); });
        displaySettings().animateNavigationWithinFile.setValue(false);

        TemporaryDirectory dir("qtc-viewport-navanim");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // Off: the jump is a jump, and where it lands is what the animated
        // one has to reach.
        viewport->gotoLine(200);
        const qreal target = viewport->scrollY();
        QVERIFY2(target > 0, "the jump did not scroll at all, so there is nothing to animate");

        viewport->setScrollY(0);
        QCOMPARE(viewport->scrollY(), qreal(0));

        // On: the same jump sets off rather than arriving.
        displaySettings().animateNavigationWithinFile.setValue(true);
        viewport->gotoLine(200);
        QVERIFY2(viewport->scrollY() < target,
                 "the jump arrived at once rather than scrolling to it");
        QTRY_COMPARE(viewport->scrollY(), target);
    }

    // The bracket that matches the one the caret arrives beside is pulsed
    // once. On by default, and the Quick editor did not do it - nor did it
    // even look for a match unless the highlight was on as well.
    void testTheMatchingBracketIsPulsedOnce()
    {
        const bool wasAnimate = displaySettings().animateMatchingParentheses();
        const bool wasHighlight = displaySettings().highlightMatchingParentheses();
        const QScopeGuard restore([wasAnimate, wasHighlight] {
            displaySettings().animateMatchingParentheses.setValue(wasAnimate);
            displaySettings().highlightMatchingParentheses.setValue(wasHighlight);
        });
        displaySettings().animateMatchingParentheses.setValue(true);
        // Off, so that what is tested is the animation rather than the pair
        // being drawn: the widget editor looks for a match on either setting.
        displaySettings().highlightMatchingParentheses.setValue(false);

        TemporaryDirectory dir("qtc-viewport-brackets");
        const FilePath file = dir.filePath("brackets.txt");
        // A second pair, never arrived at until the last case: two
        // setCursorPosition calls in a row are one layout, so "move away and
        // come back" does not clear what was marked, and a pair that was
        // already marked is not pulsed again whatever the setting says.
        QVERIFY(file.writeFileContents("(alpha)\n[beta]\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        // A highlighter is what records these; saying it here keeps the test
        // about the viewport.
        QTextDocument * const text = viewport->textDocument()->document();
        const QTextBlock first = text->findBlockByNumber(0);
        TextBlockUserData::setParentheses(
            first,
            Parentheses{Parenthesis(Parenthesis::Opened, '(', 0),
                        Parenthesis(Parenthesis::Closed, ')', 6)});
        const QTextBlock second = text->findBlockByNumber(1);
        TextBlockUserData::setParentheses(
            second,
            Parentheses{Parenthesis(Parenthesis::Opened, '[', 0),
                        Parenthesis(Parenthesis::Closed, ']', 5)});

        QSignalSpy pulses(viewport, &TextViewport::animateCharacter);

        // With only the animation on, the pair is still looked for: the
        // highlight is not what decides whether to match.
        viewport->setCursorPosition(3);
        viewport->setCursorPosition(0);
        QTRY_COMPARE(pulses.size(), 1);
        QCOMPARE(pulses.first().at(1).toString(), QString(")"));
        const QRectF at = pulses.first().at(0).toRectF();
        QVERIFY2(at.width() > 0 && at.height() > 0, "the bracket was pulsed with no size");

        // The rest needs the highlight on, because what stops a second pulse
        // for the same pair is whether that bracket was marked last time -
        // and nothing is marked while the highlight is off.
        displaySettings().highlightMatchingParentheses.setValue(true);
        pulses.clear();
        viewport->setCursorPosition(4);
        viewport->setCursorPosition(0);
        QTRY_COMPARE(pulses.size(), 1);

        // Round to the other end of the same pair: still the same pair, so it
        // is not pulsed again.
        viewport->setCursorPosition(7);
        QTRY_VERIFY(viewport->cursorPosition() == 7);
        QCOMPARE(pulses.size(), 1);

        // With the animation off and the highlight still on, arriving at a
        // pair for the first time marks it and pulses nothing.
        displaySettings().animateMatchingParentheses.setValue(false);
        pulses.clear();
        viewport->setCursorPosition(second.position());
        // Waiting for the pair to be *marked* rather than for the caret to
        // move: the caret moves at once and the pulse would come with the
        // next layout, so asking straight away asks before the answer exists.
        QTRY_VERIFY2(!viewport->visibleLine(1).value("formats").toList().isEmpty(),
                     "the second pair was never marked, so nothing was laid out to pulse");
        QCOMPARE(pulses.size(), 0);
    }

    void testAnEditTheViewportDidNotMakeStillShows()
    {
        // The indenter, another view, a refactoring: nothing tells the viewport
        // that the document changed except the document. Its own edits polish
        // themselves, so this only holds if it listens.
        TemporaryDirectory dir("textviewport-elsewhere");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("alpha"));

        QTextDocument * const text = fixture.document.textDocument()->document();
        QVERIFY(text);
        QTextCursor elsewhere(text);
        elsewhere.setPosition(0);
        elsewhere.insertText("Z");
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("Zalpha"));

        // The second edit is the one that proves the connection, and it is not
        // redundant: the first made the document modified, and CodeDocument
        // reports that separately, so the check above passes either way. Only
        // an edit that changes nothing else has the document's own signal to
        // arrive by. Adding a line also makes the document taller, which is
        // what a scroll bar reads - a viewport that redrew without remeasuring
        // would keep the old height.
        const qreal before = viewport->contentHeight();
        elsewhere.insertText("\n");
        QTRY_COMPARE(viewport->contentHeight(), before + viewport->lineHeight());
    }

    void testTheCaretMovesByKeyAndShiftSelectsWithIt()
    {
        // Moving the caret is allowed in a read-only viewport - reading a file
        // means moving through it - so this deliberately does not enable
        // editing.
        TemporaryDirectory dir("textviewport-keys");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setCursorPosition(0);

        QTest::keyClick(&fixture.view, Qt::Key_Right);
        QCOMPARE(viewport->cursorPosition(), 1);
        keyMove(fixture.view, QKeySequence::MoveToEndOfLine);
        QCOMPARE(viewport->cursorPosition(), 5);
        QTest::keyClick(&fixture.view, Qt::Key_Down);
        QCOMPARE(viewport->cursorPosition(), 10); // end of "beta"
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 6);

        // Moving without Shift leaves nothing selected.
        QCOMPARE(viewport->selectionStart(), -1);

        // Shift takes the caret's old place as the anchor and grows from it.
        viewport->setCursorPosition(6);
        QTest::keyClick(&fixture.view, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(&fixture.view, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(viewport->selectionStart(), 6);
        QCOMPARE(viewport->selectionEnd(), 8);
        QCOMPARE(viewport->cursorPosition(), 8);


        // And a plain move collapses it again.
        QTest::keyClick(&fixture.view, Qt::Key_Right);
        QCOMPARE(viewport->selectionStart(), -1);
        QCOMPARE(viewport->selectionEnd(), -1);
    }

    // A word at a time and a file at a time. Neither is something a plain text
    // box offers, and both are the difference between reading code and
    // scrolling through it.
    // A search result, an error, the other places a symbol is used: ranges
    // somebody else asked to have drawn differently. They go through the same
    // format list the selection does, so this is the only way to see that one
    // arrived at all.
    // A line too long for the width is broken across rows. Off by default,
    // because with it on the viewport has to know where every block starts and
    // nothing knows that without laying out the whole file.
    void testALongLineIsBrokenAcrossRowsWhenAsked()
    {
        TemporaryDirectory dir("qtc-viewport-wrap");
        const FilePath file = dir.filePath("long.txt");
        const QString longLine = QString("word ").repeated(60).trimmed();
        QVERIFY(file.writeFileContents((longLine + "\nshort\n").toUtf8()));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Unwrapped: one row per line, and the long one runs off the edge.
        QVERIFY(!viewport->isWrapping());
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), longLine);
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("short"));
        const qreal unwrappedHeight = viewport->contentHeight();

        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        // Against the count before it, so this cannot pass on a document that
        // already had more rows than the number would have asked for.
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     qPrintable(QString("still %1 rows after wrapping was turned on")
                                    .arg(viewport->visibleLineCount())));

        // The long line now covers several rows, and they spell it out again.
        QString rebuilt;
        int rowsOfFirstLine = 0;
        for (int row = 0; row < viewport->visibleLineCount(); ++row) {
            const QVariantMap line = viewport->visibleLine(row);
            if (line.value("lineNumber").toInt() != 1)
                break;
            rebuilt += line.value("text").toString();
            ++rowsOfFirstLine;
        }
        QVERIFY2(rowsOfFirstLine > 1, "the long line still occupies one row");
        QCOMPARE(rebuilt, longLine);

        // Numbered once: the continuation rows carry the same line number but
        // are not where the line starts, so the gutter leaves them blank.
        QVERIFY(viewport->visibleLine(0).value("firstRowOfLine").toBool());
        for (int row = 1; row < rowsOfFirstLine; ++row) {
            QCOMPARE(viewport->visibleLine(row).value("lineNumber").toInt(), 1);
            QVERIFY2(!viewport->visibleLine(row).value("firstRowOfLine").toBool(),
                     "a continuation row claims to start the line");
        }
        // And the next line begins on the row after them.
        QCOMPARE(viewport->visibleLine(rowsOfFirstLine).value("lineNumber").toInt(), 2);
        QCOMPARE(viewport->visibleLine(rowsOfFirstLine).value("text").toString(), QString("short"));

        // A taller document: the rows are real rows, not a relabelling.
        QVERIFY2(viewport->contentHeight() > unwrappedHeight,
                 "the document is no taller although a line now covers several rows");

        // Clicking on the second row lands inside the line rather than at its
        // end - which is the whole point of the row being its own row.
        const qreal lineHeight = viewport->lineHeight();
        const int onSecondRow = viewport->positionAt(1, lineHeight * 1.5);
        const int firstRowLength = viewport->visibleLine(0).value("text").toString().size();
        QVERIFY2(onSecondRow >= firstRowLength,
                 qPrintable(QString("a click on row 2 gave position %1, inside row 1 (%2 long)")
                                .arg(onSecondRow).arg(firstRowLength)));
        QVERIFY(onSecondRow < longLine.size());

        // And the caret for that position is drawn on that row.
        const QRectF caret = viewport->rectangleAt(onSecondRow);
        QCOMPARE(qRound(caret.y() / lineHeight), 1);

        // Turning it off puts the line back on one row.
        viewport->setWrapping(false);
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), longLine);
        QCOMPARE(viewport->contentHeight(), unwrappedHeight);
    }

    // Scrolling through a wrapped document, and what the gutter does with the
    // rows a wrapped line covers. Scrolling is the part that needs the row
    // index: which line is at an offset is no longer the offset divided by the
    // line height.
    void testScrollingAWrappedDocumentLandsOnTheRightLine()
    {
        TemporaryDirectory dir("qtc-viewport-wrapscroll");
        const FilePath file = dir.filePath("wrapped.txt");
        // Every line long enough to wrap, so a row number and a line number
        // part company immediately and stay apart.
        QString contents;
        for (int i = 0; i < 40; ++i)
            contents += QString("line %1 ").arg(i) + QString("filler ").repeated(30).trimmed() + "\n";
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "Row {\n"
                                     "    property alias viewport: v\n"
                                     "    property alias gutter: g\n"
                                     "    property string path\n"
                                     "    EditorGutter { id: g; viewport: v; height: 200 }\n"
                                     "    TextViewport {\n"
                                     "        id: v; objectName: \"wrapViewport\"\n"
                                     "        width: 300; height: 200\n"
                                     "        wrapping: true\n"
                                     "        document: CodeDocument { filePath: path }\n"
                                     "    }\n"
                                     "}"),
                          QUrl("qrc:/test/WrapScrollTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("wrapViewport");
        QVERIFY(viewport);
        QVERIFY(viewport->isWrapping());
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // More rows than lines, or the document is not wrapping at all.
        const int rowsInAll = qRound(viewport->contentHeight() / viewport->lineHeight());
        QVERIFY2(rowsInAll > viewport->lineCount(),
                 qPrintable(QString("%1 rows for %2 lines - nothing wrapped")
                                .arg(rowsInAll).arg(viewport->lineCount())));

        // Scroll a long way down and ask what is at the top. The row is not
        // the line, so this can only be right if the index is consulted.
        viewport->setScrollY(viewport->lineHeight() * 50);
        QTRY_COMPARE(viewport->firstVisibleLine(), 50);
        const int lineAtTop = viewport->visibleLine(0).value("lineNumber").toInt();
        QVERIFY2(lineAtTop > 1 && lineAtTop < 50,
                 qPrintable(QString("row 50 says it is line %1; with every line wrapped it "
                                    "should be well before line 50").arg(lineAtTop)));

        // The rows on screen belong to consecutive lines, each starting once.
        int previous = 0;
        for (int row = 0; row < viewport->visibleLineCount(); ++row) {
            const QVariantMap line = viewport->visibleLine(row);
            const int number = line.value("lineNumber").toInt();
            if (line.value("firstRowOfLine").toBool()) {
                if (previous != 0)
                    QCOMPARE(number, previous + 1);
                previous = number;
            } else if (previous != 0) {
                QCOMPARE(number, previous);
            }
        }

        // And the gutter numbers a line once, not once per row.
        auto * const gutter = item->property("gutter").value<QQuickItem *>();
        QVERIFY(gutter);
        QStringList numbers = gutterNumbers(gutter);
        QTRY_COMPARE(gutterNumbers(gutter).size(), viewport->visibleLineCount());
        numbers = gutterNumbers(gutter);
        const int blank = int(std::count(numbers.cbegin(), numbers.cend(), QString()));
        QVERIFY2(blank > 0, "every row is numbered, so continuation rows are numbered too");
        const int numbered = numbers.size() - blank;
        QCOMPARE(numbered, int(std::count_if(numbers.cbegin(), numbers.cend(),
                                             [](const QString &n) { return !n.isEmpty(); })));
        // No number appears twice: that is what "once per line" means.
        QStringList shown;
        for (const QString &n : std::as_const(numbers)) {
            if (!n.isEmpty())
                shown << n;
        }
        QCOMPARE(shown.size(), QSet<QString>(shown.cbegin(), shown.cend()).size());
    }

    // A diagnostic is a fact about the document, not about one view of it, so
    // the document is where it lives and any view can draw it. This is the
    // mechanism the language clients' warnings and the code model's unused
    // symbols travel on.
    void testWhatTheDocumentSaysIsDrawnDifferentlyIsDrawn()
    {
        TemporaryDirectory dir("qtc-viewport-diagnostics");
        const FilePath file = writeLines(dir, "warned.txt", 20);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        TextDocument * const document = fixture.document.textDocument();
        QTextDocument * const text = document->document();

        const auto backgrounds = [viewport](int row) {
            QList<QColor> colours;
            const QVariantList ranges = viewport->visibleLine(row).value("formats").toList();
            for (const QVariant &range : ranges)
                colours << range.toMap().value("background").value<QColor>();
            return colours;
        };
        QVERIFY(!backgrounds(2).contains(QColor(Qt::cyan)));

        // A warning across the third line, set the way a diagnostic producer
        // sets one - on the document, with a cursor rather than offsets so an
        // edit above carries it along.
        QTextCharFormat warning;
        warning.setBackground(QColor(Qt::cyan));
        const QTextBlock third = text->findBlockByNumber(2);
        QTextCursor over(text);
        over.setPosition(third.position());
        over.setPosition(third.position() + 4, QTextCursor::KeepAnchor);
        document->setExtraSelections("Test.Warnings", {{over, warning}});

        QTRY_VERIFY2(backgrounds(2).contains(QColor(Qt::cyan)),
                     "the document says line 3 is warned about and nothing shows it");
        QVERIFY(!backgrounds(1).contains(QColor(Qt::cyan)));

        // Inserting a line above carries it with them: that is what a cursor
        // buys over a pair of offsets.
        QTextCursor top(text);
        top.setPosition(0);
        top.insertText("inserted\n");
        QTRY_VERIFY2(backgrounds(3).contains(QColor(Qt::cyan)),
                     "the warning stayed on the row instead of following its line");

        // And emptying the kind takes it away - the document keeps the key so
        // that a view knows there is nothing left to draw for it.
        document->setExtraSelections("Test.Warnings", {});
        QTRY_VERIFY(!backgrounds(3).contains(QColor(Qt::cyan)));
    }

    // Selecting a word shows where else it appears. That is usually why it
    // was selected, and the widget editor does it, so an editor that does not
    // reads as having lost the feature.
    void testSelectingAWordShowsWhereElseItAppears()
    {
        const bool wasOn = displaySettings().highlightSelection();
        const QScopeGuard restore(
            [wasOn] { displaySettings().highlightSelection.setValue(wasOn); });
        displaySettings().highlightSelection.setValue(true);

        TemporaryDirectory dir("qtc-viewport-occurrences");
        const FilePath file = dir.filePath("repeats.txt");
        //                              0         10        20
        QVERIFY(file.writeFileContents("alpha beta\ngamma alpha\nbeta only\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // The occurrence marker is the only *translucent* background the
        // viewport draws - the selection and the scheme's own colours are
        // opaque - so counting all the formats on a row would also count the
        // highlighter's whitespace marks.
        const auto occurrences = [viewport](int row) {
            QVariantList found;
            const QVariantList ranges = viewport->visibleLine(row).value("formats").toList();
            for (const QVariant &range : ranges) {
                if (range.toMap().value("background").value<QColor>().alpha() < 255)
                    found << range;
            }
            return found;
        };

        // Nothing selected, nothing marked.
        QVERIFY(occurrences(1).isEmpty());

        // Select "alpha" on the first line. The other "alpha" is on line 2.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QTRY_COMPARE(occurrences(1).size(), 1);

        // Exactly the other "alpha", not the whole line.
        const QVariantMap marked = occurrences(1).first().toMap();
        QCOMPARE(marked.value("start").toInt(), 6);
        QCOMPARE(marked.value("length").toInt(), 5);
        // And nowhere it does not appear.
        QVERIFY(occurrences(2).isEmpty());

        // A selection spanning two lines marks nothing: the rule is one
        // line's worth, as it is for the widget.
        //
        // From the newline before "gamma" to just past it. Trimming takes the
        // paragraph separator with the whitespace - QChar::isSpace() is true
        // for it - so what is searched for would be exactly "gamma" and would
        // be found on the line below. It is the *two blocks* that disqualify
        // it, and a selection with the separator still inside it could not
        // tell the two rules apart, because no block contains one.
        viewport->setSelectionStart(10);
        viewport->setSelectionEnd(16);
        QTRY_VERIFY2(occurrences(1).isEmpty(),
                     "a selection spanning two lines marked something anyway");

        // And the display setting turns it off while the selection stands.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QTRY_COMPARE(occurrences(1).size(), 1);
        displaySettings().highlightSelection.setValue(false);
        QTRY_VERIFY(occurrences(1).isEmpty());
    }

    // Every Code Style page shows a preview of the language it configures, and
    // the preview is only useful if it is coloured like the language. Whether
    // it is comes down to whether a highlight definition exists for the mime
    // type the page's factory names - which nothing was checking, and which is
    // what "highlighting does not work" in a preview looks like.
    void testEveryCodeStylePreviewFindsItsLanguage()
    {
        const QMap<Utils::Id, ICodeStylePreferencesFactory *> factories = codeStyleFactories();
        QVERIFY2(!factories.isEmpty(), "no code style pages exist, so this checks nothing");

        QStringList unhighlighted;
        int checked = 0;
        for (auto it = factories.cbegin(); it != factories.cend(); ++it) {
            const QString mimeType = SnippetProvider::mimeTypeForGroup(it.value()->snippetGroupId());
            if (mimeType.isEmpty())
                continue; // a language with no snippet group names no mime type
            ++checked;

            CodeBuffer buffer;
            buffer.setMimeType(mimeType);
            buffer.setText("// comment\nint value = 42;\n");
            if (!buffer.isHighlighting())
                unhighlighted << it.key().toString() + " (" + mimeType + ")";
        }

        QVERIFY2(checked > 0, "no code style page named a mime type, so this checks nothing");
        QVERIFY2(unhighlighted.isEmpty(),
                 qPrintable("code style previews with no highlight definition: "
                            + unhighlighted.join(", ")));
    }

    // Utils::TextEditorLayout is the per-view index of which row each block
    // starts on - what a wrapped view needs and what this viewport would use.
    // firstLineNumberOf() carries a FIXME saying its cache is not recalculated
    // on a width change; this asks whether that is still true, because a Qt
    // Quick item changes width constantly.
    void testTheEditorLayoutFollowsAWidthChange()
    {
        QTextDocument document;
        document.setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        auto * const documentLayout = new Utils::PlainTextDocumentLayout(&document);
        document.setDocumentLayout(documentLayout);

        QString contents;
        for (int i = 0; i < 20; ++i)
            contents += QString("word ").repeated(40).trimmed() + "\n";
        document.setPlainText(contents);
        QCOMPARE(document.blockCount(), 21);

        // setTextWidth() is protected on PlainTextDocumentLayout, so anything
        // that is not a PlainTextEdit has to subclass to reach it - the first
        // thing a non-widget view has to work around.
        struct WidthSettable : Utils::TextEditorLayout
        {
            using Utils::TextEditorLayout::TextEditorLayout;
            using Utils::TextEditorLayout::setTextWidth;
        };
        WidthSettable layout(documentLayout);

        // Wide enough that nothing wraps: every block is one row, so the last
        // block starts on row 20.
        layout.setTextWidth(100000);
        for (QTextBlock b = document.firstBlock(); b.isValid(); b = b.next())
            layout.blockBoundingRect(b); // force this width's layout
        const int wideFirstLine = layout.firstLineNumberOf(document.lastBlock());
        QCOMPARE(wideFirstLine, 20);

        // Narrow enough that every line wraps several times, so the last block
        // must start much further down.
        layout.setTextWidth(120);
        for (QTextBlock b = document.firstBlock(); b.isValid(); b = b.next())
            layout.blockBoundingRect(b);
        const int narrowFirstLine = layout.firstLineNumberOf(document.lastBlock());

        QVERIFY2(narrowFirstLine > wideFirstLine,
                 qPrintable(QString("the last block still starts on row %1 after the width "
                                    "went from 100000 to 120; it started on %2 before")
                                .arg(narrowFirstLine)
                                .arg(wideFirstLine)));

        // The part that decides how a view may use this. Above, every block
        // was laid out again after the width changed. Without that, the index
        // answers from line counts that are still 1 - so a view cannot ask
        // where a block starts until the blocks above it have been laid out at
        // the current width, and that is the O(file) cost wrapping carries.
        WidthSettable lazy(documentLayout);
        lazy.setTextWidth(120);
        const int withoutLayout = lazy.firstLineNumberOf(document.lastBlock());
        QVERIFY2(withoutLayout < narrowFirstLine,
                 qPrintable(QString("asked without laying the blocks out first, the index "
                                    "already answered %1 - the same as after layout (%2), so "
                                    "this test says nothing about the cost")
                                .arg(withoutLayout)
                                .arg(narrowFirstLine)));
    }

    // Changing the font in Preferences, or zooming, has to reach a viewport
    // that is already showing a file. Nothing else makes it lay out again, so
    // without a connection the editor keeps the size it opened with.
    // Double click takes the word, triple click takes the line. Through the
    // component, because what tells a third click from a second one is in the
    // QML rather than in the viewport.
    void testDoubleClickTakesTheWordAndTripleClickTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-clicks");
        const FilePath file = dir.filePath("words.txt");
        // Two spaces between the first two words, and a second line: a single
        // space, or a click on the first line, cannot tell the whitespace and
        // newline handling below from doing nothing at all.
        //                              0  3 5   9      15  19
        QVERIFY(file.writeFileContents("one  two three\nfour five\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 200\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/ClickTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        const auto pointAt = [&view, viewport](int position) {
            const QRectF caret = viewport->rectangleAt(position);
            return view.contentItem()->mapFromItem(viewport, caret.center()).toPoint();
        };
        QVERIFY2(viewport->rectangleAt(6).height() > 0,
                 "the caret has no place, so neither does a click");

        // Inside "two".
        QTest::mouseDClick(&view, Qt::LeftButton, {}, pointAt(6));
        QCOMPARE(viewport->selectionStart(), 5);
        QCOMPARE(viewport->selectionEnd(), 8);

        // A third click, inside the double-click interval, takes the line -
        // and the *second* line, so that a selection reaching back over the
        // newline above would show up as starting one character early.
        QTest::mouseDClick(&view, Qt::LeftButton, {}, pointAt(17));
        QTest::mouseClick(&view, Qt::LeftButton, {}, pointAt(17));
        QCOMPARE(viewport->selectionStart(), 15);
        QCOMPARE(viewport->selectionEnd(), 24);

        // Between two spaces there is no word under the cursor, and the word
        // *before* them is not what was clicked. The whitespace itself is.
        QTest::mouseDClick(&view, Qt::LeftButton, {}, pointAt(4));
        QCOMPARE(viewport->selectionStart(), 3);
        QCOMPARE(viewport->selectionEnd(), 5);
    }

    // A drag that leaves the bottom edge is asking for the lines below it.
    // Without this a selection stops at whatever happened to be on screen when
    // the drag started, which is most of a file away from what was wanted.
    void testDraggingPastTheEdgeKeepsScrollingAndSelecting()
    {
        TemporaryDirectory dir("qtc-viewport-autoscroll");
        const FilePath file = writeLines(dir, "long.txt", 2000);

        // The window is taller than the editor in it, so that a drag can go
        // below the editor and still be a mouse event Qt will deliver.
        QQuickView view;
        installIconProvider(view);
        view.resize(400, 400);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "Item {\n"
                                     "    property string path\n"
                                     "    property alias editor: e\n"
                                     "    width: 400; height: 400\n"
                                     "    CodeViewport {\n"
                                     "        id: e\n"
                                     "        width: 400; height: 200\n"
                                     "        source: CodeDocument { filePath: path }\n"
                                     "    }\n"
                                     "}"),
                          QUrl("qrc:/test/AutoScrollTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        const int onScreen = viewport->visibleLineCount();

        // Press on the first line, then drag well below the window.
        const QPointF top = viewport->rectangleAt(0).center();
        const QPoint from = view.contentItem()->mapFromItem(viewport, top).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        QCOMPARE(viewport->selectionStart(), 0);

        const QPoint below(from.x(), 260); // below the editor, inside the window
        QTest::mouseMove(&view, below);

        // It keeps going on its own: the pointer is not moving any more.
        QTRY_VERIFY2(viewport->firstVisibleLine() > onScreen,
                     qPrintable(QString("the view stopped at line %1 after %2 were on screen")
                                    .arg(viewport->firstVisibleLine())
                                    .arg(onScreen)));
        const int scrolledTo = viewport->firstVisibleLine();

        // And the selection follows it rather than stopping where the visible
        // text used to end.
        QTextDocument * const text = viewport->document()->textDocument()->document();
        QVERIFY2(text->findBlock(viewport->selectionEnd()).blockNumber() >= scrolledTo,
                 "the view scrolled away from the selection instead of extending it");

        // Letting go stops it: a released drag that kept scrolling would run
        // to the end of the file on its own. Asked of the timer rather than of
        // the scroll position - "it did not move" needs an event to wait for
        // and there is none, whereas "it is not running" is true immediately.
        QObject * const ticker = item->findChild<QObject *>("editorAutoScroll");
        QVERIFY(ticker);
        QVERIFY2(ticker->property("running").toBool(),
                 "nothing was scrolling, so stopping it proves nothing");
        QTest::mouseRelease(&view, Qt::LeftButton, {}, below);
        QVERIFY2(!ticker->property("running").toBool(), "the drag went on scrolling after release");
    }

    // The right-click menu, from the QML side: that the entries carry what the
    // model said is the half no C++ assertion about the model can reach.
    void testTheContextMenuShowsWhatItWasGiven()
    {
        TemporaryDirectory dir("qtc-viewport-menu");
        const FilePath file = writeLines(dir, "menu.txt", 20);

        QAction alpha("Alpha");
        QAction separator;
        separator.setSeparator(true);
        QAction beta("Beta");
        beta.setEnabled(false);

        QtcQuick::ActionModel actions;
        actions.setActions({&alpha, &separator, &beta});

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.Ui\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 200\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/MenuTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()},
             {"contextActions", QVariant::fromValue(&actions)}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        // A Menu is a popup: its entries are not in the item tree under the
        // page, so it has to be asked directly.
        QObject * const menu = item->findChild<QObject *>("editorContextMenu");
        QVERIFY(menu);
        QVERIFY(!menu->property("opened").toBool());

        const QRectF caret = viewport->rectangleAt(3);
        const QPoint at = view.contentItem()->mapFromItem(viewport, caret.center()).toPoint();
        QTest::mouseClick(&view, Qt::RightButton, {}, at);
        QTRY_VERIFY2(menu->property("opened").toBool(), "a right click opened no menu");

        QCOMPARE(menu->property("count").toInt(), 3);
        const auto entry = [menu](int index) {
            QQuickItem *found = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, found),
                                      Q_ARG(int, index));
            return found;
        };
        QVERIFY(entry(0));
        QCOMPARE(entry(0)->property("text").toString(), QString("Alpha"));
        QVERIFY(entry(0)->property("enabled").toBool());
        // The separator carries no text and cannot be chosen.
        QVERIFY(entry(1));
        QCOMPARE(entry(1)->property("text").toString(), QString());
        QVERIFY(!entry(1)->property("enabled").toBool());
        // And a disabled action arrives disabled rather than missing.
        QVERIFY(entry(2));
        QCOMPARE(entry(2)->property("text").toString(), QString("Beta"));
        QVERIFY(!entry(2)->property("enabled").toBool());

        QMetaObject::invokeMethod(menu, "close");
        QTRY_VERIFY(!menu->property("opened").toBool());
    }

    // Zoom is global, so a file opened after one has to open at that size
    // rather than at the size the settings had when Creator started.
    void testAFileOpenedAfterAZoomOpensZoomed()
    {
        const int wasZoom = globalFontSettings().fontZoom();
        const QScopeGuard restore([wasZoom] { globalFontSettings().setFontZoom(wasZoom); });

        TemporaryDirectory dir("qtc-viewport-latezoom");
        const FilePath file = writeLines(dir, "late.txt", 100);

        globalFontSettings().setFontZoom(wasZoom * 3);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QCOMPARE(viewport->font().pointSize(),
                 std::max(globalFontSettings().data().fontSize() * wasZoom * 3 / 100, 1));
    }

    // Ctrl and the wheel zoom rather than scroll - and only when the user has
    // said so, which is a setting the widget editor honours too.
    void testCtrlWheelZoomsAndOnlyWhenAllowed()
    {
        const int wasZoom = globalFontSettings().fontZoom();
        const bool wasAllowed = globalBehaviorSettings().scrollWheelZooming();
        const QScopeGuard restore([wasZoom, wasAllowed] {
            globalFontSettings().setFontZoom(wasZoom);
            globalBehaviorSettings().scrollWheelZooming.setValue(wasAllowed);
        });

        TemporaryDirectory dir("qtc-viewport-zoom");
        const FilePath file = writeLines(dir, "zoomed.txt", 100);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        globalBehaviorSettings().scrollWheelZooming.setValue(true);
        viewport->zoomBy(1);
        QVERIFY2(globalFontSettings().fontZoom() > wasZoom,
                 qPrintable(QString("zoom stayed at %1").arg(globalFontSettings().fontZoom())));
        const int zoomed = globalFontSettings().fontZoom();

        // And the text on screen actually got bigger.
        QTRY_VERIFY(viewport->font().pointSize() > 0);

        // Turned off, the wheel does nothing at all - not even a little.
        globalBehaviorSettings().scrollWheelZooming.setValue(false);
        viewport->zoomBy(1);
        QCOMPARE(globalFontSettings().fontZoom(), zoomed);
    }

    void testChangingTheFontRedrawsWhatIsAlreadyOpen()
    {
        TemporaryDirectory dir("qtc-viewport-fontchange");
        const FilePath file = writeLines(dir, "sized.txt", 200);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal wasLineHeight = viewport->lineHeight();
        const int wasOnScreen = viewport->visibleLineCount();
        QVERIFY(wasLineHeight > 0);

        const int wasZoom = globalFontSettings().fontZoom();
        const QScopeGuard restore([wasZoom] { globalFontSettings().setFontZoom(wasZoom); });
        globalFontSettings().setFontZoom(wasZoom * 2);

        // Taller lines, so fewer of them fit - which is the whole observable
        // consequence of a zoom.
        QTRY_VERIFY2(viewport->lineHeight() > wasLineHeight,
                     qPrintable(QString("line height stayed at %1 after zooming to %2%")
                                    .arg(viewport->lineHeight())
                                    .arg(globalFontSettings().fontZoom())));
        QVERIFY(viewport->visibleLineCount() < wasOnScreen);
        QCOMPARE(viewport->font().pointSize(),
                 std::max(globalFontSettings().data().fontSize()
                              * globalFontSettings().data().fontZoom() / 100,
                          1));
    }

    void testAHighlightIsDrawnOnTheLineItCovers()
    {
        TemporaryDirectory dir("qtc-viewport-highlights");
        const FilePath file = writeLines(dir, "marked.txt", 5000);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        QTextCharFormat wanted;
        wanted.setBackground(QColor(Qt::magenta));

        // "line 2", on the third line, which is on screen.
        const QTextBlock third
            = fixture.document.textDocument()->document()->findBlockByNumber(2);
        const int from = third.position();

        const auto backgrounds = [viewport](int index) {
            QList<QColor> colours;
            const QVariantList ranges = viewport->visibleLine(index).value("formats").toList();
            for (const QVariant &range : ranges)
                colours << range.toMap().value("background").value<QColor>();
            return colours;
        };

        QVERIFY(!backgrounds(2).contains(QColor(Qt::magenta)));

        viewport->setHighlights("Test.Highlights", {{from, from + 4, wanted}});
        QTRY_VERIFY(backgrounds(2).contains(QColor(Qt::magenta)));
        // And only on that line.
        QVERIFY(!backgrounds(1).contains(QColor(Qt::magenta)));
        QVERIFY(!backgrounds(3).contains(QColor(Qt::magenta)));

        // A highlight far below the screen costs nothing and shows nothing.
        const QTextBlock deep
            = fixture.document.textDocument()->document()->findBlockByNumber(4000);
        viewport->setHighlights("Test.Highlights",
                                {{from, from + 4, wanted},
                                 {deep.position(), deep.position() + 4, wanted}});
        QTRY_VERIFY(backgrounds(2).contains(QColor(Qt::magenta)));
        QCOMPARE(viewport->highlights("Test.Highlights").size(), 2);

        // Clearing a kind takes its ranges and nobody else's.
        viewport->setHighlights("Test.Highlights", {});
        QTRY_VERIFY(!backgrounds(2).contains(QColor(Qt::magenta)));
        QVERIFY(viewport->highlights("Test.Highlights").isEmpty());
    }

    void testASelectionEndingInsideAHighlightLeavesTheRestOfIt()
    {
        // Selecting up to the middle of a coloured token must not take the
        // colour off the rest of it. The selection is one format range over
        // the top of the highlighter's; the two overlap, and what the layout
        // is given has to keep both.
        TemporaryDirectory dir("qtc-viewport-select-highlight");
        const FilePath file = writeLines(dir, "marked.txt", 20);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        QTextCharFormat wanted;
        wanted.setForeground(QColor(Qt::magenta));

        const QTextBlock third
            = fixture.document.textDocument()->document()->findBlockByNumber(2);
        const int from = third.position();

        // Six characters of the third line coloured, which is more than the
        // selection below will cover.
        viewport->setHighlights("Test.Highlights", {{from, from + 6, wanted}});

        // Which characters are magenta. Not which range says so: a boundary
        // anywhere in the token splits it into several runs that all still say
        // magenta, and that is not a difference the reader can see.
        const auto magentaChars = [viewport] {
            QSet<int> chars;
            const QVariantList ranges = viewport->visibleLine(2).value("formats").toList();
            for (const QVariant &range : ranges) {
                const QVariantMap map = range.toMap();
                if (map.value("foreground").value<QColor>() != QColor(Qt::magenta))
                    continue;
                const int start = map.value("start").toInt();
                for (int i = 0; i < map.value("length").toInt(); ++i)
                    chars.insert(start + i);
            }
            return chars;
        };
        const QSet<int> wholeToken = {0, 1, 2, 3, 4, 5};

        QTRY_COMPARE(magentaChars(), wholeToken);

        // Now select the first three characters of that line - ending inside
        // the coloured token.
        QTextCursor cursor(fixture.document.textDocument()->document());
        cursor.setPosition(from);
        cursor.setPosition(from + 3, QTextCursor::KeepAnchor);
        viewport->setTextCursor(cursor);
        QTRY_COMPARE(viewport->selectionEnd(), from + 3);

        // The colour still covers the whole token, not just the selected part.
        QCOMPARE(magentaChars(), wholeToken);

        // And the ranges handed to the layout do not overlap. QTextLayout
        // tolerates overlapping ones and QTextLayout::draw() merges them, but
        // a QSGTextNode splits the text at every boundary and keeps only one
        // format per run - so an overlap loses whatever the other range said,
        // which is what took the colour off the unselected half of the token.
        const auto overlapping = [viewport] {
            QList<QPair<int, int>> spans;
            const QVariantList ranges = viewport->visibleLine(2).value("formats").toList();
            for (const QVariant &range : ranges) {
                const QVariantMap map = range.toMap();
                spans.append({map.value("start").toInt(), map.value("length").toInt()});
            }
            std::sort(spans.begin(), spans.end());
            for (int i = 1; i < spans.size(); ++i) {
                if (spans.at(i).first < spans.at(i - 1).first + spans.at(i - 1).second)
                    return true;
            }
            return false;
        };
        QVERIFY2(!overlapping(), "the layout was given overlapping format ranges");
    }

    void testTheCaretMovesAWordAndAFileAtATime()
    {
        TemporaryDirectory dir("qtc-viewport-words");
        const FilePath file = dir.filePath("words.txt");
        //                                     0   4   8       14   19
        QVERIFY(file.writeFileContents("one two three\nfour five\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        viewport->setCursorPosition(0);

        keyMove(fixture.view, QKeySequence::MoveToNextWord);
        QCOMPARE(viewport->cursorPosition(), 4); // "two"
        keyMove(fixture.view, QKeySequence::MoveToNextWord);
        QCOMPARE(viewport->cursorPosition(), 8); // "three"
        keyMove(fixture.view, QKeySequence::MoveToPreviousWord);
        QCOMPARE(viewport->cursorPosition(), 4);

        // The whole file, which Home and End alone cannot reach.
        keyMove(fixture.view, QKeySequence::MoveToEndOfDocument);
        QCOMPARE(viewport->cursorPosition(), 24);
        keyMove(fixture.view, QKeySequence::MoveToStartOfDocument);
        QCOMPARE(viewport->cursorPosition(), 0);

        // And selecting by word takes exactly the word.
        keyMove(fixture.view, QKeySequence::SelectNextWord);
        QCOMPARE(viewport->selectionStart(), 0);
        QCOMPARE(viewport->selectionEnd(), 4);
    }

    // Home means the first thing on the line, not column zero - and column
    // zero only once the caret is already there. Indented code is unreadable
    // otherwise: every Home would land the caret in the margin.
    void testTheCaretStaysVisibleAcrossALongLine()
    {
        // Wrapping is off for code, so a long line runs past the right edge.
        // Putting the caret out there has to bring it back into view, the way
        // moving it below the last row scrolls down.
        TemporaryDirectory dir("qtc-viewport-hscroll");
        const FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents(QByteArray(400, 'x') + "\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(!viewport->isWrapping());
        QCOMPARE(viewport->scrollX(), 0.0);

        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        // Driven by a key, which is how a caret actually gets there: the
        // property setter is the low-level one and does not scroll, the same
        // as it does not scroll vertically.
        keyMove(fixture.view, QKeySequence::MoveToEndOfLine);
        QCOMPARE(viewport->cursorPosition(), 400);
        // The caret is far to the right of a 400 pixel viewport, so it can
        // only be on screen if the view has followed it.
        QTRY_VERIFY2(viewport->cursorRectangle().right() <= viewport->width(),
                     qPrintable(QString("caret at x=%1 in a viewport %2 wide, scrollX=%3")
                                    .arg(viewport->cursorRectangle().right())
                                    .arg(viewport->width())
                                    .arg(viewport->scrollX())));
        QVERIFY(viewport->cursorRectangle().left() >= 0);

        // The rule is to centre the caret, which the end of the longest line
        // cannot show: there is nothing to its right to scroll to, so the
        // clamp leaves it against the edge - as it does in the widget editor.
        // In the middle of the line there is room on both sides.
        viewport->setCursorPosition(200);
        keyMove(fixture.view, QKeySequence::MoveToNextChar);
        QTRY_VERIFY(viewport->scrollX() > 0);
        const auto offCentre = [viewport] {
            const QRectF caret = viewport->cursorRectangle();
            return caret.isNull() ? viewport->width()
                                  : qAbs(caret.center().x() - viewport->width() / 2);
        };
        QTRY_VERIFY2(offCentre() < viewport->width() / 10,
                     qPrintable(QString("caret %1 pixels off centre in a view %2 wide")
                                    .arg(offCentre())
                                    .arg(viewport->width())));

        // And back again.
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QTRY_COMPARE(viewport->scrollX(), 0.0);
    }

    void testWideningTheViewportDoesNotLeaveItScrolledPastTheEnd()
    {
        // Both offsets are clamped against how big the content is, and both
        // are left alone when the viewport changes size - so a view scrolled
        // to the end and then widened stays where it was, showing blank space
        // past the end of the longest line.
        TemporaryDirectory dir("qtc-viewport-resize");
        const FilePath file = dir.filePath("wide.txt");
        QString content;
        for (int i = 0; i < 200; ++i)
            content += QString(200, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QTRY_VERIFY(viewport->contentWidth() > viewport->width());

        // All the way to the right.
        viewport->setScrollX(100000);
        QTRY_VERIFY(viewport->scrollX() > 0);
        const qreal atNarrow = viewport->scrollX();
        QCOMPARE(atNarrow, viewport->contentWidth() - viewport->width());

        // Now there is far less to scroll through, so the offset has to come
        // back with it rather than leaving the text off to the left.
        viewport->setWidth(1200);
        QTRY_COMPARE(viewport->width(), 1200.0);
        // Wait for the offset to come back, not for the bound to hold: the
        // bound holds the instant the width changes, because the content size
        // has not been recomputed yet, so waiting on it waits for nothing.
        QTRY_VERIFY2(viewport->scrollX() < atNarrow,
                     qPrintable(QString("scrollX stayed at %1").arg(viewport->scrollX())));
        QVERIFY2(viewport->scrollX()
                     <= qMax(0.0, viewport->contentWidth() - viewport->width()) + 0.5,
                 qPrintable(QString("scrollX %1 but only %2 to scroll through")
                                .arg(viewport->scrollX())
                                .arg(viewport->contentWidth() - viewport->width())));

        // The same thing downwards. With wrapping on the same text needs fewer
        // rows in a wider viewport, so widening leaves less to scroll through
        // vertically as well.
        viewport->setWidth(400);
        QTRY_COMPARE(viewport->width(), 400.0);
        viewport->setWrapping(true);
        // Wait for the wrapped height to settle before scrolling to the end of
        // it: setScrollY clamps against the height it knows now, so scrolling
        // too early lands somewhere short of the bottom and the widening below
        // has nothing to bring back.
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        // More rows than the 200 lines in the file, which is what says the
        // wrapping has actually been applied. lineCount() counts rows, so
        // comparing it against the height proves nothing.
        QTRY_VERIFY(viewport->contentHeight() / viewport->lineHeight() > 300);
        viewport->setScrollY(1000000);
        const qreal deepAtNarrow = viewport->scrollY();
        QCOMPARE(deepAtNarrow, viewport->contentHeight() - viewport->height());

        viewport->setWidth(1200);
        QTRY_COMPARE(viewport->width(), 1200.0);
        QTRY_VERIFY2(viewport->scrollY() < deepAtNarrow,
                     qPrintable(QString("scrollY stayed at %1").arg(viewport->scrollY())));
        QVERIFY2(viewport->scrollY()
                     <= qMax(0.0, viewport->contentHeight() - viewport->height()) + 0.5,
                 qPrintable(QString("scrollY %1 but only %2 to scroll through")
                                .arg(viewport->scrollY())
                                .arg(viewport->contentHeight() - viewport->height())));
    }

    void testAPageIsAScreenOfRowsNotOfLines()
    {
        // A page used to be counted in document lines. With wrapping on a line
        // is several rows, so a page of thirteen rows moved thirteen lines -
        // about four screens. It is taken by scrolling a screen and putting
        // the caret back where it was on screen.
        TemporaryDirectory dir("qtc-viewport-page");
        const FilePath file = dir.filePath("wrapped.txt");
        QString content;
        for (int i = 0; i < 100; ++i)
            content += QString(200, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const auto rowsInDocument = [viewport] {
            return int(viewport->contentHeight() / viewport->lineHeight());
        };
        // Unwrapped, a row is a line: 100 lines plus the empty one the
        // trailing newline leaves behind.
        QVERIFY(!viewport->isWrapping());
        QTRY_COMPARE(rowsInDocument(), 101);

        viewport->setWrapping(true);
        // Now several rows per line, which is what makes the two counts differ.
        QTRY_VERIFY2(rowsInDocument() > 150,
                     qPrintable(QString("only %1 rows for 100 lines, so nothing wrapped")
                                    .arg(rowsInDocument())));

        const int pageRows = qMax(1, int(viewport->height() / viewport->lineHeight()) - 1);
        QCOMPARE(viewport->cursorLine(), 1);
        QCOMPARE(viewport->scrollY(), 0.0);

        QTest::keyClick(&fixture.view, Qt::Key_PageDown);

        // A screen was scrolled - within a row of one, since the caret is put
        // back on the row it was on and the view then settles around it.
        QTRY_VERIFY(viewport->scrollY() > 0);
        QTRY_VERIFY2(viewport->scrollY() >= (pageRows - 1) * viewport->lineHeight()
                         && viewport->scrollY() <= (pageRows + 1) * viewport->lineHeight(),
                     qPrintable(QString("scrolled %1, wanted about %2")
                                    .arg(viewport->scrollY())
                                    .arg(pageRows * viewport->lineHeight())));
        // ...and the caret went with it, by rows rather than by lines: it has
        // moved, but nothing like the pageRows lines it used to.
        QTRY_VERIFY(viewport->cursorLine() > 1);
        QVERIFY2(viewport->cursorLine() < pageRows,
                 qPrintable(QString("a page of %1 rows moved the caret to line %2")
                                .arg(pageRows)
                                .arg(viewport->cursorLine())));

        // And back up again lands where it started.
        QTest::keyClick(&fixture.view, Qt::Key_PageUp);
        QTRY_COMPARE(viewport->scrollY(), 0.0);
        QTRY_COMPARE(viewport->cursorLine(), 1);
    }

    void testGoingToALineLandsOnItWhenWrapping()
    {
        // What a search result or a compiler message does. It scrolled to the
        // line's number times the line height, which is a count of lines - so
        // with wrapping on, where a line is several rows, it landed short by
        // as many rows as the lines above it took.
        TemporaryDirectory dir("qtc-viewport-goto");
        const FilePath file = dir.filePath("wrapped.txt");
        QString content;
        for (int i = 0; i < 100; ++i)
            content += QString(200, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->contentHeight() / viewport->lineHeight() > 150,
                     "nothing wrapped, so a row and a line are still the same");

        // Somewhere well down the file, centred.
        viewport->gotoLine(60, 0, true);
        QTRY_COMPARE(viewport->cursorLine(), 60);

        // Wherever the view ended up, line 60 has to be on screen - that is
        // the whole point of going to it.
        QTRY_VERIFY2(!viewport->cursorRectangle().isNull(),
                     "the caret is not laid out, so line 60 is not on screen");
        const QRectF caret = viewport->cursorRectangle();
        QVERIFY2(caret.top() >= 0 && caret.bottom() <= viewport->height(),
                 qPrintable(QString("went to line 60 and it is at y=%1 in a view %2 tall")
                                .arg(caret.top())
                                .arg(viewport->height())));
    }

    void testDownMovesToTheNextRowWhenWrapping()
    {
        // The most used key there is. A wrapped line is several rows, and Down
        // means the next row - not the next line, which would skip everything
        // the line wrapped onto.
        TemporaryDirectory dir("qtc-viewport-down");
        const FilePath file = dir.filePath("wrapped.txt");
        QString content;
        for (int i = 0; i < 20; ++i)
            content += QString(200, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->contentHeight() / viewport->lineHeight() > 40,
                     "nothing wrapped, so a row and a line are still the same");

        viewport->setCursorPosition(0);
        QCOMPARE(viewport->cursorLine(), 1);
        const QRectF first = viewport->cursorRectangle();
        QVERIFY(!first.isNull());

        QTest::keyClick(&fixture.view, Qt::Key_Down);

        // Still on line one - it takes several rows, and Down went to the next
        // of them rather than over the whole line.
        QTRY_VERIFY(viewport->cursorPosition() > 0);
        QCOMPARE(viewport->cursorLine(), 1);
        // And exactly one row further down the screen. Waited for, not read
        // straight away: where the caret is on screen is worked out when the
        // rows are laid out again, which is a polish later.
        // The caret lands where the row on screen starts, which is what says
        // the layout the cursor moved through and the rows drawn agree.
        QTRY_COMPARE(viewport->cursorPosition(),
                     viewport->visibleLine(0).value("text").toString().size());
        const auto caretDrop = [viewport, first] {
            const QRectF now = viewport->cursorRectangle();
            return now.isNull() ? -1.0 : now.top() - first.top();
        };
        QTRY_COMPARE(caretDrop(), viewport->lineHeight());
    }

    void testScrollingToTheBottomShowsTheLastLineWhenWrapping()
    {
        // How far there is to scroll comes from the layout's row count, and
        // the rows on screen are shaped by the viewport. If the two wrap
        // differently the range is wrong: scrolled all the way down, the file
        // either ends early or runs on past the bottom.
        TemporaryDirectory dir("qtc-viewport-bottom");
        const FilePath file = dir.filePath("wrapped.txt");
        QString content;
        for (int i = 1; i <= 60; ++i)
            content += QString(200, QLatin1Char('x')) + QString::number(i)
                       + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->contentHeight() / viewport->lineHeight() > 100,
                     "nothing wrapped, so a row and a line are still the same");

        // All the way down, and let the clamp settle it.
        viewport->setScrollY(1000000);
        QTRY_COMPARE(viewport->scrollY(), viewport->contentHeight() - viewport->height());

        // The last row on screen has to be the end of the file. A row count
        // that is too high leaves blank space below the text instead.
        // Waited for, not read once: the scroll asks for a polish, and until
        // it runs the rows are still the ones from before it. The assertion
        // above holds either way, so it does not wait for this on its own.
        const auto lastRowLine = [viewport] {
            const int rows = viewport->visibleLineCount();
            return rows > 0 ? viewport->visibleLine(rows - 1).value("lineNumber").toInt() : -1;
        };
        // 60 lines of text and the empty one the trailing newline leaves.
        QTRY_COMPARE(lastRowLine(), 61);
    }

    void testClickingALaterRowOfAWrappedLineLandsOnIt()
    {
        // Clicking is how the caret is placed most of the time. On a wrapped
        // line the row under the pointer is not the line's first, so the
        // character under it is somewhere in the middle of the block.
        TemporaryDirectory dir("qtc-viewport-click");
        const FilePath file = dir.filePath("wrapped.txt");
        QVERIFY(file.writeFileContents(QByteArray(400, 'x') + "\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Measured against what it was, not against a number: a threshold the
        // document already meets is not a wait, and this one is three rows
        // away from being one.
        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        // The long line takes several rows, so there is a later row to click.
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     qPrintable(QString("still %1 rows, so nothing wrapped")
                                    .arg(viewport->visibleLineCount())));
        QCOMPARE(viewport->visibleLine(2).value("lineNumber").toInt(), 1);
        QVERIFY(!viewport->visibleLine(2).value("firstRowOfLine").toBool());

        // The third row, a little way in.
        const int rowLength = viewport->visibleLine(0).value("text").toString().size();
        const qreal y = 2 * viewport->lineHeight() + viewport->lineHeight() / 2;
        const int position = viewport->positionAt(0, y);

        // Where the third row starts: two rows of the same line before it.
        QCOMPARE(position, 2 * rowLength);
        // And it is still the first line - the click did not land on line two.
        QCOMPARE(viewport->cursorLine(), 1);
    }

    void testGoingToALineStillWorksAfterTheWidthChanges()
    {
        // Where a line starts, in rows, is cached by the layout, which warns in
        // a FIXME that the cache is not reset when the width changes. It
        // nonetheless comes out right, and what makes it so is not settled:
        // neither dropping the cache when a block's line count changes nor the
        // per-block walk in updatePolish() can be taken away and made to fail
        // this. So it is here as a fact about the behaviour, not as a claim
        // about the reason - if it ever goes red, the FIXME is where to look.
        TemporaryDirectory dir("qtc-viewport-rewrap");
        const FilePath file = dir.filePath("wrapped.txt");
        QString content;
        for (int i = 0; i < 80; ++i)
            content += QString("word ").repeated(40).trimmed() + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setWrapping(true);
        QTRY_VERIFY(viewport->contentHeight() / viewport->lineHeight() > 150);

        const auto goTo = [viewport](int line) {
            viewport->gotoLine(line, 0, true);
            return viewport->cursorLine();
        };
        const auto caretIsOnScreen = [viewport] {
            const QRectF caret = viewport->cursorRectangle();
            return !caret.isNull() && caret.top() >= 0 && caret.bottom() <= viewport->height();
        };

        QCOMPARE(goTo(50), 50);
        QTRY_VERIFY2(caretIsOnScreen(), "line 50 is not on screen before the resize");

        // Half as wide, so every line takes about twice the rows and every
        // line below the first starts somewhere new.
        // Half as wide, and waited for by the row length: the row count was
        // already past any threshold worth naming, so waiting on that waits
        // for nothing and the rewrap never happens before the assertions.
        const auto rowLength = [viewport] {
            return viewport->visibleLine(0).value("text").toString().size();
        };
        const int wideRow = rowLength();
        viewport->setWidth(200);
        QTRY_COMPARE(viewport->width(), 200.0);
        QTRY_VERIFY2(rowLength() < wideRow,
                     qPrintable(QString("rows are still %1 characters at half the width")
                                    .arg(rowLength())));

        QCOMPARE(goTo(50), 50);
        QTRY_VERIFY2(caretIsOnScreen(),
                     qPrintable(QString("after rewrapping, line 50 is at y=%1 in a view "
                                        "%2 tall, scrollY %3")
                                    .arg(viewport->cursorRectangle().top())
                                    .arg(viewport->height())
                                    .arg(viewport->scrollY())));

    }

    void testScrollingPastShortLinesKeepsThePlaceAcrossTheLongOnes()
    {
        TemporaryDirectory dir("qtc-viewport-cw");
        const FilePath file = dir.filePath("ragged.txt");
        QString content;
        for (int i = 0; i < 40; ++i)
            content += QString("short\n");
        for (int i = 0; i < 40; ++i)
            content += QString(300, QLatin1Char('x')) + QLatin1Char('\n');
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(!viewport->isWrapping());

        // Only the rows on screen are laid out, so how wide the content is can
        // only be answered from them - and at the top of this file they are
        // all short.
        const qreal atTop = viewport->contentWidth();

        // Down among the long lines, where there is something to scroll to.
        viewport->setScrollY(60 * viewport->lineHeight());
        QTRY_COMPARE(viewport->visibleLine(0).value("lineNumber").toInt(), 61);
        QTRY_VERIFY2(viewport->contentWidth() > atTop,
                     "the long lines did not make the content any wider");

        viewport->setScrollX(800);
        QTRY_COMPARE(viewport->scrollX(), 800.0);

        // Back up among the short ones. The reader has not asked to go
        // anywhere sideways, so they must not be moved: a width that shrank to
        // fit what is on screen would clamp this to nothing.
        viewport->setScrollY(0);
        QTRY_COMPARE(viewport->visibleLine(0).value("lineNumber").toInt(), 1);
        QCOMPARE(viewport->scrollX(), 800.0);
    }

    void testWhitespaceIsDrawnWhenTheSettingAsksForIt()
    {
        // The setting reaches the document's text option, and there is a test
        // for that - but QSGTextNode draws glyph runs and nothing else, so the
        // dots and arrows QTextLine::draw() would add have to be drawn here.
        // Counted rather than looked at.
        TemporaryDirectory dir("qtc-viewport-ws");
        const FilePath file = dir.filePath("spaces.txt");
        QVERIFY(file.writeFileContents("alpha      beta\n"));

        const bool was = displaySettings().visualizeWhitespace();
        const QScopeGuard restore(
            [was] { displaySettings().visualizeWhitespace.setValue(was); });
        displaySettings().visualizeWhitespace.setValue(false);

        // The marks are drawn by CodeViewport.qml, so this needs the form
        // rather than a bare viewport - a TextViewport on its own paints
        // glyphs and nothing around them.
        CodeViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Pixels of the colour the marks are drawn in, over the whole band the
        // first row occupies - a dot two pixels high is easy to miss with one
        // scanline, and counting "not the background" would drown six dots in
        // the text beside them.
        const auto inkOnFirstRow = [&fixture, viewport] {
            const QImage shot = fixture.view.grabWindow();
            const qreal dpr = fixture.view.devicePixelRatio();
            const int bottom = qMin(int(viewport->lineHeight() * 2 * dpr), shot.height());
            const QColor mark = viewport->property("indentGuideColor").value<QColor>();
            int ink = 0;
            for (int y = 0; y < bottom; ++y) {
                for (int x = 0; x < shot.width(); ++x) {
                    if (shot.pixelColor(x, y) == mark)
                        ++ink;
                }
            }
            return ink;
        };

        // The test line has no leading indentation, so the guides - which are
        // drawn in this same colour - cannot be mistaken for the marks.
        QVERIFY(!viewport->visibleLine(0).value("text").toString().startsWith(' '));
        QTRY_COMPARE(inkOnFirstRow(), 0);

        displaySettings().visualizeWhitespace.setValue(true);
        QTRY_VERIFY2(inkOnFirstRow() > 0,
                     "the setting is on and not one pixel of the six spaces is marked");
    }

    // Typing into a wrapped line makes it wrap over more rows. An edit is
    // handled by the lazy path rather than by the walk that lays every block
    // out, so this does not guard that walk - it covers the wrapped edit
    // itself, which nothing else did.
    void testTypingMoreTextWrapsOverMoreRows()
    {
        TemporaryDirectory dir("qtc-viewport-editwrap");
        const FilePath file = dir.filePath("one.txt");
        QVERIFY(file.writeFileContents(QByteArray(300, 'x') + "\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setWrapping(true);
        QTRY_VERIFY(viewport->rowCount() > 1);

        const int before = viewport->rowCount();

        QTextCursor edit(viewport->textDocument()->document());
        edit.movePosition(QTextCursor::End);
        edit.insertText(QString(600, 'x'));

        QTRY_VERIFY2(viewport->rowCount() > before,
                     qPrintable(QString("still %1 rows after tripling the line, was %2")
                                    .arg(viewport->rowCount())
                                    .arg(before)));
    }

    // Dragging with Alt takes a rectangle of text: a caret on every line it
    // covers, each selecting the same columns, which is what makes it
    // typeable over. The widget editor does this on Alt+drag too.
    void testAltDraggingTakesARectangleOfText()
    {
        TemporaryDirectory dir("qtc-viewport-blocksel");
        const FilePath file = dir.filePath("cols.txt");
        // Four lines long enough that the rectangle lands inside all of them.
        QVERIFY(file.writeFileContents("abcdefgh\nabcdefgh\nabcdefgh\nabcdefgh\n"));

        CodeViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal columnWidth = viewport->rectangleAt(1).x() - viewport->rectangleAt(0).x();
        QVERIFY2(columnWidth > 0, "a character was expected to have a width");

        const auto scenePoint = [viewport](qreal x, int row) {
            return viewport->mapToScene(QPointF(x, viewport->lineHeight() * row + 2)).toPoint();
        };

        // From column 2 of the first line to column 5 of the fourth.
        QTest::mousePress(&fixture.view, Qt::LeftButton, Qt::AltModifier,
                          scenePoint(columnWidth * 2, 0));
        QTest::mouseMove(&fixture.view, scenePoint(columnWidth * 5, 3));
        QTest::mouseRelease(&fixture.view, Qt::LeftButton, Qt::AltModifier,
                            scenePoint(columnWidth * 5, 3));

        QTRY_COMPARE(viewport->multiTextCursor().cursorCount(), 4);
        const QList<QTextCursor> carets = viewport->multiTextCursor().cursors();
        for (int i = 0; i < carets.size(); ++i) {
            const QTextCursor &caret = carets.at(i);
            QCOMPARE(caret.blockNumber(), i);
            QVERIFY2(caret.hasSelection(),
                     qPrintable(QString("line %1 selected nothing").arg(i)));
            QCOMPARE(caret.selectedText(), QString("cde"));
        }
    }

    // Alt+Shift+click takes the rectangle between where the caret already is
    // and where the click landed, which is how a column selection is made
    // without holding the button down across it.
    void testAltShiftClickingTakesTheRectangleFromTheCaret()
    {
        TemporaryDirectory dir("qtc-viewport-blockclick");
        const FilePath file = dir.filePath("cols.txt");
        QVERIFY(file.writeFileContents("abcdefgh\nabcdefgh\nabcdefgh\nabcdefgh\n"));

        CodeViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal columnWidth = viewport->rectangleAt(1).x() - viewport->rectangleAt(0).x();
        QVERIFY(columnWidth > 0);

        const auto scenePoint = [viewport](qreal x, int row) {
            return viewport->mapToScene(QPointF(x, viewport->lineHeight() * row + 2)).toPoint();
        };

        // The caret goes to column 2 of the first line the ordinary way, so
        // that the rectangle has a corner to be measured from.
        QTest::mouseClick(&fixture.view, Qt::LeftButton, {}, scenePoint(columnWidth * 2, 0));
        QTRY_COMPARE(viewport->multiTextCursor().cursorCount(), 1);

        QTest::mouseClick(&fixture.view, Qt::LeftButton,
                          Qt::AltModifier | Qt::ShiftModifier, scenePoint(columnWidth * 5, 3));

        QTRY_COMPARE(viewport->multiTextCursor().cursorCount(), 4);
        const QList<QTextCursor> carets = viewport->multiTextCursor().cursors();
        for (int i = 0; i < carets.size(); ++i) {
            QCOMPARE(carets.at(i).blockNumber(), i);
            QCOMPARE(carets.at(i).selectedText(), QString("cde"));
        }
    }

    // Join Lines pulls the next line onto the caret's, collapsing the leading
    // whitespace of what arrives to one space. The menu entry is the widget
    // editor's, so the Quick editor has to answer the same command or it is
    // dead whenever a Quick editor is the current one.
    void testJoiningLinesPullsTheNextOneUp()
    {
        TemporaryDirectory dir("qtc-viewport-join");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("first\n      second\nthird\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        viewport->setCursorPosition(0);
        viewport->joinLines();

        QTextDocument * const text = viewport->textDocument()->document();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("first second"));
        // Three, not two: the trailing newline of the file is a block of its
        // own, so "first second", "third" and the empty one.
        QCOMPARE(text->blockCount(), 3);

        // One undo step, not one per line, so that the command can be taken
        // back the way it was given.
        text->undo();
        QCOMPARE(text->blockCount(), 4);
        QCOMPARE(text->findBlockByNumber(0).text(), QString("first"));
    }

    // A read only buffer is not edited by a command any more than by a key.
    void testJoiningLinesDoesNothingWhenReadOnly()
    {
        TemporaryDirectory dir("qtc-viewport-join-ro");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("first\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QVERIFY(viewport->isReadOnly());

        viewport->setCursorPosition(0);
        viewport->joinLines();

        QCOMPARE(viewport->textDocument()->document()->blockCount(), 3);
    }

    // Upper Case Selection with nothing selected takes the word the caret is
    // in. That is only right while there is one caret: several carets each
    // swallowing a word is not what one key press meant.
    void testUppercasingWithNoSelectionTakesTheWordUnderTheCaret()
    {
        TemporaryDirectory dir("qtc-viewport-upper");
        const FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma delta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();

        // One caret, inside "beta", nothing selected.
        viewport->setCursorPosition(7);
        viewport->uppercaseSelection();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("alpha BETA"));

        // Two carets, neither with a selection, both sitting in a lower case
        // word: no word is swallowed, so upper casing changes nothing. Asked
        // with uppercase rather than lowercase on purpose - lowercasing words
        // that are already lower case cannot tell the two behaviours apart.
        viewport->setCursorPosition(1);
        viewport->addCaretAt(text->findBlockByNumber(1).position() + 1);
        QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);
        viewport->uppercaseSelection();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("alpha BETA"));
        QCOMPARE(text->findBlockByNumber(1).text(), QString("gamma delta"));
    }

    // Several carets are made to be typed into together, and moving them is
    // half of that. The Qt Quick view moved only the main one and dropped the
    // rest: a caret on each of two lines, one press of Right, and there was
    // one caret left. The widget editor hands every cursor to the move and
    // writes them all back.
    void testEveryCaretMovesWithTheKeys()
    {
        TemporaryDirectory dir("qtc-viewport-multimove");
        const FilePath file = dir.filePath("three.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma delta\nepsilon zeta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        QVERIFY(fixture.hasFocus());

        QTextDocument * const text = viewport->textDocument()->document();
        const auto positions = [viewport] {
            QList<int> at;
            for (const QTextCursor &c : viewport->multiTextCursor().cursors())
                at << c.position();
            std::sort(at.begin(), at.end());
            return at;
        };
        const auto twoCaretsAt = [&](int first, int second) {
            viewport->setCursorPosition(first);
            viewport->addCaretAt(second);
            QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);
        };

        // The answers below are a real TextEditorWidget's, key for key.
        twoCaretsAt(1, 12);
        QTest::keyClick(&fixture.view, Qt::Key_Right);
        QCOMPARE(positions(), QList<int>({2, 13}));

        twoCaretsAt(1, 12);
        QTest::keyClick(&fixture.view, Qt::Key_Down);
        QCOMPARE(positions(), QList<int>({12, 24}));

        // Start of line rather than Home: on a Mac Home is the top of the
        // file, which collapses to one caret in both editors and would have
        // said this works when it did not.
        twoCaretsAt(3, 14);
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(positions(), QList<int>({0, 11}));

        // And the same move asked for directly, which is what the form and
        // the command do rather than sending a key.
        twoCaretsAt(3, 14);
        viewport->gotoLineStart();
        QCOMPARE(positions(), QList<int>({0, 11}));

        Q_UNUSED(text)
    }

    // An edit can bring two carets together - Delete with one just behind the
    // other does - and two carets in one place make the next key happen twice
    // there. The widget editor merges after Delete; this view merged after
    // nothing.
    //
    // Note what is *not* asserted below: the number of carets. It already
    // reads one, because MultiTextCursor is keyed by position and collapses
    // the duplicate as it is built - while m_extraCursors still holds it and
    // is what the edit walks. Counting carets says this works. Only typing
    // says it does not.
    void testCaretsThatHaveMetBecomeOneCaret()
    {
        TemporaryDirectory dir("qtc-viewport-caretsmeet");
        const FilePath file = dir.filePath("letters.txt");
        QVERIFY(file.writeFileContents("abcdef\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(fixture.hasFocus());

        QTextDocument * const text = viewport->textDocument()->document();
        const auto startOver = [&](int first, int second) {
            QTextCursor all(text);
            all.select(QTextCursor::Document);
            all.insertText("abcdef\n");
            viewport->setCursorPosition(first);
            viewport->addCaretAt(second);
            QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);
        };

        // Delete: the caret at 1 takes "b", the one at 2 takes "c", and they
        // are both at 1 afterwards. One "x", not two - which is the widget
        // editor's answer, measured.
        startOver(1, 2);
        QTest::keyClick(&fixture.view, Qt::Key_Delete);
        QCOMPARE(text->toPlainText().trimmed(), QString("adef"));
        QTest::keyClick(&fixture.view, 'x');
        QCOMPARE(text->toPlainText().trimmed(), QString("axdef"));

        // Backspace brings them together the same way. The widget editor does
        // *not* merge here - it types "yy" - and this is the one place the two
        // views are meant to differ: merging is what its own Delete does, so
        // not doing it after every edit is an oversight rather than a rule.
        startOver(2, 3);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText().trimmed(), QString("adef"));
        QTest::keyClick(&fixture.view, 'y');
        QCOMPARE(text->toPlainText().trimmed(), QString("aydef"));
    }

    void testCtrlWheelZoomsByLessThanAWholeNotch_data()
    {
        QTest::addColumn<qreal>("delta");
        QTest::addColumn<int>("zoom");
        // A real TextEditorWidget's answers to zoomF(), from 100%.
        QTest::newRow("a whole notch") << 1.0 << 110;
        QTest::newRow("a quarter of one") << 0.25 << 102;
        QTest::newRow("a twentieth, which rounds to nothing") << 0.05 << 101;
        QTest::newRow("a quarter backwards") << -0.25 << 98;
    }

    // Ctrl and the wheel zooms. A mouse notch reports 120 eighths of a degree
    // and a trackpad reports whatever the fingers did, so the delta a wheel
    // event carries is a fraction of a notch far more often than it is one -
    // and zoomBy() took an int, so every fraction truncated to zero and did
    // nothing at all. The widget editor takes a float and keeps a minimum of
    // one, so it always zooms by something.
    void testCtrlWheelZoomsByLessThanAWholeNotch()
    {
        QFETCH(qreal, delta);
        QFETCH(int, zoom);

        TemporaryDirectory dir("qtc-viewport-zoom");
        const FilePath file = writeLines(dir, "zoom.txt", 20);
        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const bool wasZooming = globalBehaviorSettings().scrollWheelZooming();
        const int wasZoom = globalFontSettings().fontZoom();
        const QScopeGuard restore([wasZooming, wasZoom] {
            globalBehaviorSettings().scrollWheelZooming.setValue(wasZooming);
            globalFontSettings().setFontZoom(wasZoom);
        });
        globalBehaviorSettings().scrollWheelZooming.setValue(true);

        globalFontSettings().setFontZoom(100);
        viewport->zoomBy(delta);
        QCOMPARE(globalFontSettings().fontZoom(), zoom);

        // And the preference still turns the whole thing off.
        globalBehaviorSettings().scrollWheelZooming.setValue(false);
        globalFontSettings().setFontZoom(100);
        viewport->zoomBy(delta);
        QCOMPARE(globalFontSettings().fontZoom(), 100);
    }

    // A lamp at the end of a line is something to click, and the widget editor
    // says so by putting a pointing hand under the mouse - the same as over a
    // collapsed fold's "{...}". The Qt Quick form sets a cursor shape in one
    // place only, on the text area, so both of those stayed an I-beam and gave
    // the reader no reason to believe there was anything there.
    void testAClickableMarkerSaysSoWithThePointer()
    {
        TemporaryDirectory dir("qtc-viewport-handcursor");
        const FilePath file = writeLines(dir, "marked.txt", 20);

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 400);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 400\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/HandCursorTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY(doc);
        QTextCursor at(doc->document());
        at.setPosition(doc->document()->findBlockByNumber(1).position() + 3);
        RefactorMarker marker;
        marker.cursor = at;
        marker.tooltip = "Something to click";
        marker.type = Utils::Id("ViewportHandCursorTest");
        doc->setRefactorMarkers(marker.type, {marker});

        QQuickItem *lamp = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->objectName() == "refactorMarker" && candidate->isVisible()) {
                    lamp = candidate;
                    return true;
                }
            }
            return false;
        }());

        // Over the lamp: a hand, because there is something there to press.
        const QPoint onLamp = view.contentItem()
                                  ->mapFromItem(lamp, QPointF(lamp->width() / 2,
                                                              lamp->height() / 2))
                                  .toPoint();
        QTest::mouseMove(&view, onLamp);
        QTRY_COMPARE(view.cursor().shape(), Qt::PointingHandCursor);

        // And over plain text an I-beam again, or the assertion above would
        // pass for a window that shows a hand everywhere.
        const QRectF text = viewport->rectangleAt(0);
        QVERIFY(!text.isNull());
        QTest::mouseMove(&view, view.contentItem()
                                    ->mapFromItem(viewport, text.center()).toPoint());
        QTRY_COMPARE(view.cursor().shape(), Qt::IBeamCursor);

        // The other thing the widget editor puts a hand under: the box drawn
        // after a collapsed line, which opens the fold again.
        QTextDocument * const plain = doc->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(plain->documentLayout());
        QVERIFY(layout);
        // A highlighter is what normally says a line is foldable; saying it
        // here keeps the test about the pointer.
        for (int i = 1; i <= 4; ++i)
            TextBlockUserData::setFoldingIndent(plain->findBlockByNumber(i), 1);
        const QTextBlock first = plain->findBlockByNumber(0);
        QVERIFY(TextBlockUserData::canFold(first));
        TextBlockUserData::doFoldOrUnfold(first, /*unfold=*/false);
        layout->requestUpdate();

        QQuickItem *box = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->inherits("QQuickText") && candidate->isVisible()
                    && candidate->property("text").toString().contains("...")) {
                    box = candidate->parentItem();
                    return box != nullptr && box->isVisible();
                }
            }
            return false;
        }());

        QTest::mouseMove(&view, view.contentItem()
                                    ->mapFromItem(box, QPointF(box->width() / 2,
                                                               box->height() / 2))
                                    .toPoint());
        QTRY_COMPARE(view.cursor().shape(), Qt::PointingHandCursor);
    }

    // Clicking a line number selects that whole line, and dragging down them
    // takes the lines in between - the anchor line staying whole whichever way
    // the drag goes. The Qt Quick gutter answered the mark column and nothing
    // else, so the numbers were decoration.
    void testClickingALineNumberSelectsTheLine()
    {
        TemporaryDirectory dir("qtc-gutter-lineselect");
        const FilePath file = dir.filePath("lines.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(500, 300);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 500; height: 300\n"
                                     "    showLineNumbers: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/GutterSelectTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        viewport->setReadOnly(false);
        QQuickItem * const gutter = item->findChild<QQuickItem *>("codeGutter");
        QVERIFY(gutter);
        QTRY_VERIFY(gutter->width() > 0);

        QTextDocument * const text = viewport->textDocument()->document();
        // Two pixels in from the gutter's right edge: past the mark column,
        // and there are no fold markers in this fixture.
        const auto onNumberOf = [&](int line) {
            const QRectF row = viewport->rectangleAt(text->findBlockByNumber(line).position());
            const qreal y = gutter->mapFromItem(viewport, QPointF(0, row.center().y())).y();
            return view.contentItem()
                ->mapFromItem(gutter, QPointF(gutter->width() - 2, y))
                .toPoint();
        };
        const auto selected = [viewport] {
            return QString(viewport->selectedText())
                .replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        };

        // The answers below are a real TextEditorWidget's, event for event.
        QTest::mousePress(&view, Qt::LeftButton, {}, onNumberOf(1));
        QTRY_COMPARE(selected(), QString("beta\n"));

        QTest::mouseMove(&view, onNumberOf(2));
        QTRY_COMPARE(selected(), QString("beta\ngamma\n"));

        QTest::mouseRelease(&view, Qt::LeftButton, {}, onNumberOf(2));
        QCOMPARE(selected(), QString("beta\ngamma\n"));

        // Upwards, the line the drag started on stays whole.
        QTest::mousePress(&view, Qt::LeftButton, {}, onNumberOf(2));
        QTRY_COMPARE(selected(), QString("gamma\n"));
        QTest::mouseMove(&view, onNumberOf(0));
        QTRY_COMPARE(selected(), QString("alpha\nbeta\ngamma\n"));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, onNumberOf(0));

        // And the mark column beside the numbers is not part of this: a press
        // there is a press on the marks, whether or not anything answers it.
        // The two areas are placed by arithmetic, which is the thing to get
        // wrong.
        const QRectF row = viewport->rectangleAt(text->findBlockByNumber(3).position());
        const qreal y = gutter->mapFromItem(viewport, QPointF(0, row.center().y())).y();
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          view.contentItem()->mapFromItem(gutter, QPointF(1, y)).toPoint());
        QCOMPARE(selected(), QString("alpha\nbeta\ngamma\n"));
    }

    // A gutter drag that leaves the bottom is asking for lines that are not on
    // screen, so the view has to go and get them - and keep going while the
    // pointer stays out there. The widget editor runs a timer for exactly this
    // and the Qt Quick gutter had none, so a selection could never be longer
    // than the window.
    void testDraggingPastTheGutterKeepsSelectingLines()
    {
        TemporaryDirectory dir("qtc-gutter-autoscroll");
        const FilePath file = writeLines(dir, "many.txt", 200);

        QQuickView view;
        installIconProvider(view);
        view.resize(500, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 500; height: 200\n"
                                     "    showLineNumbers: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/GutterScrollTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        QQuickItem * const gutter = item->findChild<QQuickItem *>("codeGutter");
        QVERIFY(gutter);
        QTRY_VERIFY(gutter->width() > 0);

        const int onScreen = viewport->visibleLineCount();
        QVERIFY2(onScreen < 100, "the whole file is on screen, so there is nothing to scroll for");

        // Line breaks reach here as either a paragraph separator or a newline
        // depending on who built the string; count both rather than guess.
        const auto selectedLines = [viewport] {
            const QString text = viewport->selectedText();
            return text.count(QChar::ParagraphSeparator) + text.count(QLatin1Char('\n'));
        };
        const auto inGutterAt = [&](qreal yInGutter) {
            return view.contentItem()
                ->mapFromItem(gutter, QPointF(gutter->width() - 2, yInGutter))
                .toPoint();
        };
        QTextDocument * const text = viewport->textDocument()->document();
        const auto onNumberOf = [&](int line) {
            const QRectF row = viewport->rectangleAt(text->findBlockByNumber(line).position());
            return inGutterAt(gutter->mapFromItem(viewport, QPointF(0, row.center().y())).y());
        };

        // Start on the first line and drag below the gutter's bottom edge.
        QTest::mousePress(&view, Qt::LeftButton, {}, onNumberOf(0));
        QTRY_COMPARE(selectedLines(), 1);
        QTest::mouseMove(&view, inGutterAt(gutter->height() + 40));

        // It keeps going while the pointer is out there: more lines than the
        // window ever showed.
        QTRY_VERIFY2(selectedLines() > onScreen,
                     "the selection stopped at the bottom of the window");
        QVERIFY2(viewport->scrollY() > 0, "the view never went to fetch the lines");

        // And stops when the button comes up. Asked of the timer rather than
        // by waiting to see whether the selection grows: a selection that has
        // stopped growing is an absence, and there is no event to wait for.
        // A Timer is a QObject and not a QQuickItem, so this cannot be the
        // usual findChild<QQuickItem *>.
        QObject * const ticker = gutter->findChild<QObject *>("gutterAutoScroll");
        QVERIFY2(ticker, "the gutter has nothing that would keep the drag going");
        QTest::mouseRelease(&view, Qt::LeftButton, {}, inGutterAt(gutter->height() + 40));
        QTRY_VERIFY2(!ticker->property("running").toBool(),
                     "the drag went on fetching lines after the button came up");
    }

    // A drag carrying text over the editor is being asked where to put it, and
    // the widget editor answers by drawing a caret at the drop point and
    // hiding the one the reader left behind. The Qt Quick view drew nothing at
    // all, so a drag gave no idea where the text would land.
    void testADragOverTheEditorShowsWhereItWouldLand()
    {
        TemporaryDirectory dir("qtc-viewport-dropcaret");
        const FilePath file = dir.filePath("target.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(400, 300);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 400; height: 300\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/DropCaretTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);

        QTextDocument * const text = viewport->textDocument()->document();
        // The reader's caret on the first line, and the drag over the third:
        // one caret drawn either way, so where it is drawn is the question.
        viewport->setCursorPosition(0);
        const QRectF home = viewport->rectangleAt(0);
        const int overThird = text->findBlockByNumber(2).position() + 2;
        const QRectF target = viewport->rectangleAt(overThird);
        QVERIFY(!home.isNull() && !target.isNull());
        QVERIFY2(!qFuzzyCompare(home.y() + 1, target.y() + 1),
                 "the two places are on the same row, so this cannot tell them apart");

        const auto drawnCarets = [viewport] {
            QList<QPointF> at;
            const QVariantList rects = viewport->caretRectangles();
            for (const QVariant &entry : rects)
                at << entry.toRectF().topLeft();
            return at;
        };
        QCOMPARE(drawnCarets(), QList<QPointF>({home.topLeft()}));

        // A drag arriving and moving over the third line.
        QMimeData mime;
        mime.setText("dropped");
        const QPoint inWindow = view.contentItem()
                                    ->mapFromItem(viewport, target.center()).toPoint();
        QDragEnterEvent entering(inWindow, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(&view, &entering);
        QDragMoveEvent moving(inWindow, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(&view, &moving);

        QTRY_COMPARE(drawnCarets(), QList<QPointF>({target.topLeft()}));

        // And it follows: a second move puts it on the second line instead.
        // Two positions rather than one, so that the caret arriving with the
        // drag and the caret following it are separate claims.
        const int overSecond = text->findBlockByNumber(1).position() + 2;
        const QRectF second = viewport->rectangleAt(overSecond);
        QVERIFY(!second.isNull());
        QDragMoveEvent movingAgain(view.contentItem()
                                       ->mapFromItem(viewport, second.center()).toPoint(),
                                   Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(&view, &movingAgain);
        QTRY_COMPARE(drawnCarets(), QList<QPointF>({second.topLeft()}));

        // And when the drag goes away again, the reader's own caret is back.
        QDragLeaveEvent leaving;
        QCoreApplication::sendEvent(&view, &leaving);
        QTRY_COMPARE(drawnCarets(), QList<QPointF>({home.topLeft()}));

        // A drag that ends in a drop rather than by leaving: the drop caret
        // has to go then too, and a DropArea does not promise an exit after
        // one. Asserted against wherever the caret ended up rather than
        // against a position, because the drop moves it - a stuck drop caret
        // would still be at the point the text landed on, not after it.
        QDragEnterEvent arriving(inWindow, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(&view, &arriving);
        QDropEvent dropping(inWindow, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(&view, &dropping);
        QTRY_COMPARE(text->toPlainText().contains("dropped"), true);
        QCOMPARE(drawnCarets(),
                 QList<QPointF>({viewport->rectangleAt(viewport->cursorPosition()).topLeft()}));
    }

    void testTheSameAnswersToAnInputMethod_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("anchor");
        QTest::addColumn<int>("position");

        const QString two = "alpha beta\ngamma delta\n";
        QTest::newRow("a selection mid-document") << two << 11 << 14;
        QTest::newRow("the very start") << two << 0 << 0;
        QTest::newRow("the very end") << two << 23 << 23;
        QTest::newRow("no selection, mid-word") << two << 14 << 14;
        // Longer than the thousand characters an input method is given, so
        // that whatever caps the answer has to cap it the same way in both.
        QString many;
        for (int i = 0; i < 300; ++i)
            many += QString("line %1\n").arg(i);
        QTest::newRow("longer than the cap") << many << 1500 << 1500;
    }

    // An input method asks the editor where the caret is, what is around it
    // and what is selected, and everything it offers - predicting, correcting,
    // capitalising, reconverting - is built on those answers. The widget
    // editor inherits them from QWidgetTextControl; this view answers them by
    // hand, and nothing had ever compared the two. Two of the nine came back
    // empty.
    void testTheSameAnswersToAnInputMethod()
    {
        QFETCH(QString, content);
        QFETCH(int, anchor);
        QFETCH(int, position);

        const QList<std::pair<Qt::InputMethodQuery, QString>> queries{
            {Qt::ImCursorPosition, "ImCursorPosition"},
            {Qt::ImAnchorPosition, "ImAnchorPosition"},
            {Qt::ImSurroundingText, "ImSurroundingText"},
            {Qt::ImCurrentSelection, "ImCurrentSelection"},
            {Qt::ImAbsolutePosition, "ImAbsolutePosition"},
            {Qt::ImTextBeforeCursor, "ImTextBeforeCursor"},
            {Qt::ImTextAfterCursor, "ImTextAfterCursor"},
            {Qt::ImHints, "ImHints"},
            {Qt::ImEnabled, "ImEnabled"},
        };

        QMap<QString, QString> fromWidget;
        {
            TextEditorWidget widget;
            QSharedPointer<TextDocument> doc(new TextDocument);
            widget.setTextDocument(doc);
            widget.resize(400, 200);
            widget.show();
            QVERIFY(QTest::qWaitForWindowExposed(widget.window()));
            widget.setFocus();
            doc->setPlainText(content);
            QTextCursor c(doc->document());
            c.setPosition(anchor);
            c.setPosition(position, QTextCursor::KeepAnchor);
            widget.setTextCursor(c);
            for (const auto &[query, name] : queries)
                fromWidget.insert(name, widget.inputMethodQuery(query).toString());
        }

        TemporaryDirectory dir("qtc-viewport-imquery");
        const FilePath file = dir.filePath("asked.txt");
        QVERIFY(file.writeFileContents(content.toUtf8()));
        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        viewport->setReadOnly(false);
        if (anchor == position) {
            viewport->setCursorPosition(position);
        } else {
            viewport->setSelectionStart(anchor);
            viewport->setSelectionEnd(position);
            viewport->setCursorPosition(position);
        }

        QStringList differ;
        for (const auto &[query, name] : queries) {
            const QString mine
                = static_cast<QQuickItem *>(viewport)->inputMethodQuery(query).toString();
            if (mine != fromWidget.value(name)) {
                differ << QString("%1: %2 rather than %3")
                              .arg(name, mine, fromWidget.value(name));
            }
        }
        QVERIFY2(differ.isEmpty(), qPrintable(differ.join("; ")));
        // The comparison is only worth anything if the widget answered
        // something. Not a particular query: at the very start there is no
        // text before the caret and at the very end no surrounding line, and
        // both are correct answers.
        QVERIFY2(!fromWidget.value("ImTextBeforeCursor").isEmpty()
                     || !fromWidget.value("ImTextAfterCursor").isEmpty(),
                 "the widget answered nothing anywhere, so this compared blanks");
    }

    // Resting on a collapsed line's "{...}" shows what it swallowed, so a
    // folded function can be read without opening it. The widget editor draws
    // those lines in a box - drawCollapsedBlockPopup() - and the Qt Quick view
    // drew nothing, so a fold was a wall.
    void testRestingOnAFoldShowsWhatItHides()
    {
        TemporaryDirectory dir("qtc-viewport-foldpeek");
        const FilePath file = dir.filePath("peek.txt");
        QVERIFY(file.writeFileContents("head\n  alpha\n  beta\n  gamma\ntail\n"));

        QQuickView view;
        installIconProvider(view);
        view.resize(500, 300);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtQuick\n"
                                     "import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    property string path\n"
                                     "    width: 500; height: 300\n"
                                     "    showFoldMarkers: true\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/FoldPeekTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        QTextDocument * const text = viewport->textDocument()->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        // Lines 1 to 3 belong to line 0. A highlighter is what normally says
        // so; saying it here keeps the test about what is shown.
        for (int i = 1; i <= 3; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        const QTextBlock first = text->findBlockByNumber(0);
        QVERIFY(TextBlockUserData::canFold(first));

        // What it hides, asked of the viewport: three lines, in order, and
        // nothing when the line is not folded.
        QVERIFY2(viewport->foldedLinesAt(1).isEmpty(),
                 "an open line was said to be hiding something");
        TextBlockUserData::doFoldOrUnfold(first, /*unfold=*/false);
        layout->requestUpdate();
        const QStringList hidden = viewport->foldedLinesAt(1);
        QCOMPARE(hidden.size(), 3);
        QVERIFY2(hidden.at(0).contains("alpha") && hidden.at(2).contains("gamma"),
                 qPrintable("what the fold hides came back as: " + hidden.join(" / ")));

        // And the form shows them when the pointer rests on the box. The box
        // only exists once the row is drawn folded.
        QQuickItem *box = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->inherits("QQuickText") && candidate->isVisible()
                    && candidate->property("text").toString().contains("...")) {
                    box = candidate->parentItem();
                    return box != nullptr && box->isVisible();
                }
            }
            return false;
        }());

        QQuickItem * const peek = item->findChild<QQuickItem *>("foldPeek");
        QVERIFY2(peek, "the form has nothing to show a fold's contents in");
        QVERIFY2(!peek->isVisible(), "the contents were showing before anything was hovered");

        QTest::mouseMove(&view, view.contentItem()
                                    ->mapFromItem(box, QPointF(box->width() / 2,
                                                               box->height() / 2))
                                    .toPoint());
        QTRY_VERIFY2(peek->isVisible(), "resting on the fold showed nothing");
        QCOMPARE(peek->property("lines").toStringList().size(), 3);

        // And it goes when the pointer does.
        const QRectF away = viewport->rectangleAt(0);
        QTest::mouseMove(&view, view.contentItem()
                                    ->mapFromItem(viewport, away.center()).toPoint());
        QTRY_VERIFY2(!peek->isVisible(), "the contents stayed up after the pointer left");
    }

    // Home on a wrapped line means the start of the *row* the caret is on, and
    // only on the first row of a line does it mean the first thing on that
    // line. The widget editor makes the distinction with handleHomeKey()'s
    // `block` argument; this view computed the same flag and then ignored it,
    // so Home from the middle of a wrapped line jumped to the top of it.
    //
    // Measured against a real TextEditorWidget, which from position 200 of a
    // wrapped line answers 174 for the line-wise Home and 4 for the block-wise
    // one. The numbers themselves are not shared - the two views wrap at
    // different widths - so what is asserted below is the shape.
    void testHomeOnAWrappedLineGoesToTheRowNotTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-wraphome");
        const FilePath file = dir.filePath("wrapped.txt");
        // Four spaces of indent, then far more words than fit on a row.
        const QString content = QString("    ") + QString("word ").repeated(60).trimmed() + "\n";
        QVERIFY(file.writeFileContents(content.toUtf8()));

        ViewportFixture fixture(file, 300, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(fixture.hasFocus());

        // Per view rather than read from the display settings by a bare
        // viewport, and the rows only appear once it has laid out again -
        // without both of these there is no later row to be on and the test
        // passes for the wrong reason.
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->contentHeight() > 3 * viewport->lineHeight(),
                     "the line did not wrap, so there is no second row");

        const int inTheMiddle = 200;
        const QRectF onItsRow = viewport->rectangleAt(inTheMiddle);
        QVERIFY(!onItsRow.isNull());
        // The first non-space of the line, which is where the block-wise Home
        // goes and where the line-wise one must not.
        const QRectF firstThing = viewport->rectangleAt(4);
        QVERIFY2(!qFuzzyCompare(onItsRow.y() + 1, firstThing.y() + 1),
                 "the caret is on the line's first row, so the two Homes cannot differ");

        // Line-wise: the start of the row the caret is on.
        viewport->setCursorPosition(inTheMiddle);
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        const int afterLineHome = viewport->cursorPosition();
        QVERIFY2(afterLineHome > 4 && afterLineHome < inTheMiddle,
                 qPrintable(QString("the line-wise Home went to %1, not into the row")
                                .arg(afterLineHome)));
        QCOMPARE(viewport->rectangleAt(afterLineHome).y(), onItsRow.y());
        QVERIFY2(viewport->rectangleAt(afterLineHome).x() < onItsRow.x(),
                 "it did not go to the left of the row");

        // Block-wise: the first thing on the whole line, from the same place.
        viewport->setCursorPosition(inTheMiddle);
        keyMove(fixture.view, QKeySequence::MoveToStartOfBlock);
        QCOMPARE(viewport->cursorPosition(), 4);

        // And with nothing wrapped, the line-wise Home is the first thing on
        // the line again - which is what it does for every unwrapped file.
        viewport->setWrapping(false);
        QTRY_VERIFY(viewport->contentHeight() < 3 * viewport->lineHeight());
        viewport->setCursorPosition(inTheMiddle);
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 4);
    }

    // Sort Lines with nothing selected takes the run of lines around the
    // caret that share its indentation, and stops at one that does not.
    void testSortingTakesTheIndentedRunAroundTheCaret()
    {
        TemporaryDirectory dir("qtc-viewport-sort");
        const FilePath file = dir.filePath("list.txt");
        // The indented run is delta/beta/charlie; "outside" is at column 0
        // and must be left where it is, above and below.
        QVERIFY(file.writeFileContents("outside\n  delta\n  beta\n  charlie\nzoutside\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(text->findBlockByNumber(2).position() + 3);
        viewport->sortLines();

        QCOMPARE(text->findBlockByNumber(0).text(), QString("outside"));
        QCOMPARE(text->findBlockByNumber(1).text(), QString("  beta"));
        QCOMPARE(text->findBlockByNumber(2).text(), QString("  charlie"));
        QCOMPARE(text->findBlockByNumber(3).text(), QString("  delta"));
        // The line below the run is at another indent, so it was not in it.
        QCOMPARE(text->findBlockByNumber(4).text(), QString("zoutside"));
    }

    // Duplicate Selection with nothing selected copies the whole line, and
    // leaves the caret where it was rather than on the copy.
    void testDuplicatingWithNoSelectionCopiesTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-dup");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("first\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        const int at = text->findBlockByNumber(1).position() + 2;
        viewport->setCursorPosition(at);
        viewport->duplicateSelection();

        QCOMPARE(text->findBlockByNumber(1).text(), QString("second"));
        QCOMPARE(text->findBlockByNumber(2).text(), QString("second"));
        QCOMPARE(text->blockCount(), 4);

        // Two carets and no selection: a line each is not what one key press
        // asked for, so nothing is duplicated at all.
        const QString before = text->toPlainText();
        viewport->setCursorPosition(1);
        viewport->addCaretAt(text->findBlockByNumber(2).position() + 1);
        QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);
        viewport->duplicateSelection();
        QCOMPARE(text->toPlainText(), before);
    }

    // Comment and uncomment use the markers of the file's own language, which
    // the view reads off the definition its highlighter was built from. That
    // is the piece the Quick editor was missing, and without it the command
    // silently did nothing.
    void testCommentingUsesTheLanguagesOwnMarkers()
    {
        TemporaryDirectory dir("qtc-viewport-comment");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int x;\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Nothing puts a definition on a bare viewport's document, so the
        // test puts one there itself - which is also the path being tested.
        TextDocument * const doc = viewport->textDocument();
        const HighlighterHelper::Definition definition
            = HighlighterHelper::definitionForName("C++");
        QVERIFY2(definition.isValid(), "no C++ syntax definition is installed");
        HighlighterHelper::setDefinitionOn(doc, definition);
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());

        QTextDocument * const text = doc->document();
        const QString before = text->toPlainText();

        viewport->setCursorPosition(0);
        viewport->unCommentSelection();
        QVERIFY2(text->findBlockByNumber(0).text().contains("//"),
                 qPrintable("the line was not commented: " + text->findBlockByNumber(0).text()));

        // And again takes it back off, which is what one command doing both
        // means.
        viewport->unCommentSelection();
        QCOMPARE(text->toPlainText(), before);
    }

    // Duplicate and Comment wraps the copy in the language's block comment.
    // Without a comment definition it has nowhere to put the markers and does
    // nothing at all, which is what it did here before.
    void testDuplicateAndCommentWrapsTheCopy()
    {
        TemporaryDirectory dir("qtc-viewport-dupcomment");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int x;\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());

        QTextDocument * const text = doc->document();
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(6);
        QTRY_VERIFY(viewport->textCursor().hasSelection());

        viewport->duplicateSelectionAndComment();

        const QString all = text->toPlainText();
        QVERIFY2(all.contains("/*") && all.contains("*/"),
                 qPrintable("the copy was not commented: " + all));
        QVERIFY2(all.count("int x;") == 2,
                 qPrintable("the selection was not duplicated: " + all));
    }

    // Delete Line and Cut Line take the whole line when nothing is selected,
    // newline and all, so the lines below move up rather than a blank one
    // being left behind.
    void testDeletingAndCuttingTakeTheWholeLine()
    {
        TemporaryDirectory dir("qtc-viewport-deleteline");
        const FilePath file = dir.filePath("three.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        QTextDocument * const text = viewport->textDocument()->document();

        // Nothing selected, caret on "two".
        viewport->setCursorPosition(text->findBlockByNumber(1).position() + 1);
        viewport->deleteLine();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("one"));
        QCOMPARE(text->findBlockByNumber(1).text(), QString("three"));

        QGuiApplication::clipboard()->clear();
        viewport->setCursorPosition(text->findBlockByNumber(0).position() + 1);
        viewport->cutLine();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("three"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("one\n"));
    }

    // The last line of a file that does not end in a newline has none of its
    // own to swallow, so deleting it takes the newline of the line before
    // instead - otherwise the line above is left with a trailing blank.
    void testDeletingTheLastLineWithNoNewlineAfterIt()
    {
        TemporaryDirectory dir("qtc-viewport-deletelast");
        const FilePath file = dir.filePath("nonewline.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        QTextDocument * const text = viewport->textDocument()->document();
        QCOMPARE(text->blockCount(), 3);

        viewport->setCursorPosition(text->findBlockByNumber(2).position() + 1);
        viewport->deleteLine();

        QCOMPARE(text->toPlainText(), QString("one\ntwo"));
        QCOMPARE(text->blockCount(), 2);
    }

    // Copy Line does not change the text, so it works on a buffer that cannot
    // be edited - which is the difference between it and Cut Line.
    void testCopyingALineWorksOnAReadOnlyBuffer()
    {
        TemporaryDirectory dir("qtc-viewport-copyline");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QVERIFY(viewport->isReadOnly());

        QTextDocument * const text = viewport->textDocument()->document();
        const QString before = text->toPlainText();

        QGuiApplication::clipboard()->clear();
        viewport->setCursorPosition(text->findBlockByNumber(1).position() + 1);
        viewport->copyLine();

        QCOMPARE(QGuiApplication::clipboard()->text(), QString("beta\n"));
        QCOMPARE(text->toPlainText(), before);
    }

    // Copy Line Down puts a copy under the line and leaves the caret on the
    // copy, so that typing changes the new line and not the original.
    void testCopyingALineDownLeavesTheCaretOnTheCopy()
    {
        TemporaryDirectory dir("qtc-viewport-copylinedown");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(text->findBlockByNumber(0).position() + 1);
        viewport->copyLineDown();

        QCOMPARE(text->findBlockByNumber(0).text(), QString("alpha"));
        QCOMPARE(text->findBlockByNumber(1).text(), QString("alpha"));
        QCOMPARE(text->findBlockByNumber(2).text(), QString("beta"));
        QCOMPARE(viewport->textCursor().blockNumber(), 1);
    }

    // Move Line Down swaps the line with the one under it and keeps the caret
    // on the line that moved, so that moving it twice moves the same line.
    void testMovingALineDownTakesTheCaretWithIt()
    {
        TemporaryDirectory dir("qtc-viewport-moveline");
        const FilePath file = dir.filePath("three.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(text->findBlockByNumber(0).position() + 1);
        viewport->moveLineDown();

        QCOMPARE(text->findBlockByNumber(0).text(), QString("two"));
        QCOMPARE(text->findBlockByNumber(1).text(), QString("one"));
        // At the start of the line it moved to, not at its end: the block
        // number alone cannot tell those apart, because they are the same
        // line.
        QCOMPARE(viewport->cursorPosition(), text->findBlockByNumber(1).position());

        // Again, and the same line keeps going rather than the caret being
        // left behind on the one that took its place.
        viewport->moveLineDown();
        QCOMPARE(text->findBlockByNumber(2).text(), QString("one"));
        QCOMPARE(viewport->cursorPosition(), text->findBlockByNumber(2).position());
    }

    // Two moves in a row are one thing the reader did, so one undo puts both
    // back. A key pressed between them ends the run.
    void testMovingALineTwiceUndoesInOneStep()
    {
        TemporaryDirectory dir("qtc-viewport-moveundo");
        const FilePath file = dir.filePath("three.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        QTextDocument * const text = viewport->textDocument()->document();
        const QString before = text->toPlainText();

        viewport->setCursorPosition(text->findBlockByNumber(0).position() + 1);
        viewport->moveLineDown();
        viewport->moveLineDown();
        QCOMPARE(text->findBlockByNumber(2).text(), QString("one"));

        text->undo();
        QCOMPARE(text->toPlainText(), before);
    }

    // Rewrap Paragraph reflows the lines around the caret to the margin
    // column, and keeps the prefix its lines share - which is what stops it
    // eating the stars of a doxygen comment.
    void testRewrappingKeepsTheSharedPrefix()
    {
        TemporaryDirectory dir("qtc-viewport-rewrap");
        const FilePath file = dir.filePath("comment.txt");
        // Three short comment lines that fit on one at 80 columns.
        QVERIFY(file.writeFileContents(" * alpha beta\n * gamma delta\n * epsilon\n"));

        const int wasMargin = marginSettings().data().m_marginColumn;
        const QScopeGuard restore([wasMargin] {
            MarginSettingsData data = marginSettings().data();
            data.m_marginColumn = wasMargin;
            marginSettings().setData(data);
        });
        MarginSettingsData wide = marginSettings().data();
        wide.m_marginColumn = 80;
        marginSettings().setData(wide);

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(text->findBlockByNumber(1).position() + 3);
        viewport->rewrapParagraph();

        // One line now, and it still starts with the prefix rather than
        // having had it folded into the middle of the text.
        QCOMPARE(text->findBlockByNumber(0).text(),
                 QString(" * alpha beta gamma delta epsilon"));
        QCOMPARE(text->blockCount(), 2);
    }

    // A file the filesystem will not take back is not edited either, which
    // is a different question from the view being told to be read only -
    // nothing sets one from the other, and typing checks both.
    void testTheLineCommandsLeaveAFileThatIsReadOnlyOnDiskAlone()
    {
        TemporaryDirectory dir("qtc-viewport-ro-file");
        const FilePath file = dir.filePath("locked.txt");
        QVERIFY(file.writeFileContents("zebra\napple\n"));
        QVERIFY(QFile::setPermissions(file.toFSPathString(), QFile::ReadOwner));
        const QScopeGuard restore([file] {
            QFile::setPermissions(file.toFSPathString(), QFile::ReadOwner | QFile::WriteOwner);
        });

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        TextDocument * const doc = viewport->textDocument();
        QVERIFY2(doc->isFileReadOnly(), "the file was expected to be read only on disk");
        // The view itself was not told to be read only, which is the whole
        // point: the two are separate and both have to be honoured.
        viewport->setReadOnly(false);

        QTextDocument * const text = doc->document();
        const QString before = text->toPlainText();

        viewport->setCursorPosition(2);
        viewport->joinLines();
        viewport->sortLines();
        viewport->insertLineAbove();
        viewport->duplicateSelection();
        viewport->moveLineDown();
        viewport->uppercaseSelection();

        QCOMPARE(text->toPlainText(), before);
    }

    // Fold at the caret folds the block the caret is inside, not the line it
    // is on - standing in the middle of a function and asking to fold means
    // the function. Unfold opens it again.
    void testFoldingAtTheCaretTakesTheBlockAroundIt()
    {
        TemporaryDirectory dir("qtc-viewport-fold");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int f()\n{\n    int a = 1;\n    int b = 2;\n}\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());
        QTextDocument * const text = doc->document();
        // Folding indents are the highlighter's, so there is nothing to fold
        // until it has been over the file - and folding while it is still
        // going defers until it has finished, which is not something a test
        // can assert against. Wait for it to be done, not merely started.
        QTRY_VERIFY(TextEditor::hasUnfoldedBlocks(text));
        QTRY_VERIFY(doc->syntaxHighlighter()
                    && doc->syntaxHighlighter()->syntaxHighlighterUpToDate());

        // The caret inside the body, on a line that folds nothing itself.
        viewport->setCursorPosition(text->findBlockByNumber(2).position() + 4);
        viewport->foldCurrentBlock();

        // The body is hidden and the line that owns the fold is not - which
        // is "int f()", not the brace: the brace is inside what was folded.
        QVERIFY2(!text->findBlockByNumber(2).isVisible(), "the body was not folded away");
        QVERIFY2(text->findBlockByNumber(0).isVisible(), "the line owning the fold went too");
        QVERIFY(!text->findBlockByNumber(1).isVisible());

        viewport->unfoldCurrentBlock();
        QVERIFY2(text->findBlockByNumber(2).isVisible(), "unfolding did not open it again");
    }

    // Fold All closes everything while anything is still open, and only opens
    // everything once nothing is left open.
    void testFoldingAllClosesFirstAndOpensAfterwards()
    {
        TemporaryDirectory dir("qtc-viewport-foldall");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int f()\n{\n    int a = 1;\n}\n"
                                       "int g()\n{\n    int b = 2;\n}\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());
        QTextDocument * const text = doc->document();
        QTRY_VERIFY(TextEditor::hasUnfoldedBlocks(text));
        QTRY_VERIFY(doc->syntaxHighlighter()
                    && doc->syntaxHighlighter()->syntaxHighlighterUpToDate());

        viewport->toggleFoldAll();
        QVERIFY2(!TextEditor::hasUnfoldedBlocks(text), "something was left open");

        viewport->toggleFoldAll();
        QVERIFY2(TextEditor::hasUnfoldedBlocks(text), "nothing was opened again");
    }

    // Home goes to the first thing on the line, and only from there to column
    // zero. On an unindented line the two are the same place, so the test
    // uses an indented one - otherwise it would pass either way.
    void testGoingToTheLineStartStopsAtTheFirstCharacter()
    {
        TemporaryDirectory dir("qtc-viewport-home");
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents("    indented line\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // In the middle of the word.
        viewport->setCursorPosition(8);
        viewport->gotoLineStart();
        QCOMPARE(viewport->cursorPosition(), 4);

        // And only now to the margin.
        viewport->gotoLineStart();
        QCOMPARE(viewport->cursorPosition(), 0);

        // From the margin it goes back to the text, which is what makes it a
        // toggle rather than a one way trip.
        viewport->gotoLineStart();
        QCOMPARE(viewport->cursorPosition(), 4);
    }

    // The View commands move what is shown without moving the caret, which is
    // the whole difference between them and Page Up.
    void testViewScrollingLeavesTheCaretWhereItIs()
    {
        TemporaryDirectory dir("qtc-viewport-viewscroll");
        const FilePath file = writeLines(dir, "long.txt", 400);

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        viewport->setCursorPosition(0);
        const int caret = viewport->cursorPosition();
        QCOMPARE(viewport->scrollY(), 0.0);

        viewport->viewLineDown();
        QTRY_COMPARE(viewport->scrollY(), viewport->lineHeight());
        QCOMPARE(viewport->cursorPosition(), caret);

        const qreal afterLine = viewport->scrollY();
        viewport->viewPageDown();
        QTRY_VERIFY2(viewport->scrollY() > afterLine + viewport->lineHeight(),
                     "a page scrolled no further than a line");
        QCOMPARE(viewport->cursorPosition(), caret);

        viewport->viewPageUp();
        QTRY_COMPARE(viewport->scrollY(), afterLine);
        QCOMPARE(viewport->cursorPosition(), caret);
    }

    // Select Word gives every caret the word it stands in; Clear Selection
    // takes the selections away and leaves the carets.
    void testSelectingAndClearingWordsAtEveryCaret()
    {
        TemporaryDirectory dir("qtc-viewport-selectword");
        const FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma delta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        // One caret with a selection of its own that is wider than a word, so
        // that leaving it alone and re-selecting it look different. The other
        // has none and should be given the word it stands in.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(10);
        viewport->addCaretAt(text->findBlockByNumber(1).position() + 1);
        QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);

        viewport->selectWordUnderCursor();
        const QList<QTextCursor> carets = viewport->multiTextCursor().cursors();
        QCOMPARE(carets.size(), 2);
        // Sorted, because which of the two is the main one is not what this
        // is asking: adding a caret makes the added one main, so the order
        // here is the reverse of the order they were made in.
        QStringList selected;
        for (const QTextCursor &caret : carets)
            selected << caret.selectedText();
        selected.sort();
        QCOMPARE(selected, QStringList({"alpha beta", "gamma"}));

        viewport->clearSelection();
        for (const QTextCursor &caret : viewport->multiTextCursor().cursors())
            QVERIFY2(!caret.hasSelection(), "a selection survived Clear Selection");
        QCOMPARE(viewport->multiTextCursor().cursorCount(), 2);
    }

    // Select Block Up grows the selection to the brackets around it, and
    // again to the ones around those. Select Block Down shrinks it back,
    // which needs it to have remembered where the growing started.
    void testSelectingABlockGrowsAndShrinksAgain()
    {
        TemporaryDirectory dir("qtc-viewport-block");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int f()\n{\n    if (a) {\n        b();\n    }\n}\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());
        QTRY_VERIFY(doc->syntaxHighlighter()
                    && doc->syntaxHighlighter()->syntaxHighlighterUpToDate());

        // Inside the inner braces, on "b();".
        QTextDocument * const text = doc->document();
        viewport->setCursorPosition(text->findBlockByNumber(3).position() + 9);

        QVERIFY2(viewport->selectBlockUp(), "the inner block was not found");
        const QString inner = viewport->textCursor().selectedText();
        QVERIFY2(inner.contains("b();"), qPrintable("inner selection was: " + inner));
        QVERIFY2(!inner.contains("if"), qPrintable("it reached too far: " + inner));

        QVERIFY2(viewport->selectBlockUp(), "the outer block was not found");
        const QString outer = viewport->textCursor().selectedText();
        QVERIFY2(outer.length() > inner.length(), "growing did not take more");
        QVERIFY2(outer.contains("if"), qPrintable("outer selection was: " + outer));

        // And back in again to what it had before.
        QVERIFY2(viewport->selectBlockDown(), "shrinking found no way back");
        QCOMPARE(viewport->textCursor().selectedText(), inner);
    }

    // Go to Block End moves to the bracket that closes the one the caret is
    // in, and the selecting variant takes the text on the way there.
    void testGoingToTheBlockEndFindsTheClosingBracket()
    {
        TemporaryDirectory dir("qtc-viewport-blockend");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int f()\n{\n    b();\n}\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());
        QTRY_VERIFY(doc->syntaxHighlighter()
                    && doc->syntaxHighlighter()->syntaxHighlighterUpToDate());

        QTextDocument * const text = doc->document();
        const int inside = text->findBlockByNumber(2).position() + 4;
        viewport->setCursorPosition(inside);

        viewport->gotoBlockEnd();
        // The closing brace is on the last line, so the caret left the line it
        // was on rather than moving within it.
        QCOMPARE(viewport->textCursor().blockNumber(), 3);
        QVERIFY(!viewport->textCursor().hasSelection());

        viewport->setCursorPosition(inside);
        viewport->gotoBlockEnd(true);
        QVERIFY2(viewport->textCursor().hasSelection(), "the selecting variant selected nothing");
        QVERIFY(viewport->textCursor().selectedText().contains("b();"));
    }

    // A quick fix rewrites the file, so applying one cannot be a matter of
    // putting text in where the caret is: the item does it, and all the view
    // supplies is somewhere for it to happen. That is what lets a view with
    // no widget behind it offer fixes at all.
    void testAQuickFixIsAppliedByTheProposalItself()
    {
        TemporaryDirectory dir("qtc-viewport-quickfix");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("broken\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        OneFixProvider provider;
        viewport->textDocument()->setQuickFixAssistProvider(&provider);

        QSignalSpy offered(viewport, &TextViewport::quickFixesAvailable);
        viewport->setCursorPosition(6);
        viewport->requestQuickFixes();

        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(offered.at(0).at(0).toStringList(), QStringList({"Replace with fixed"}));

        viewport->applyQuickFix(0);
        QCOMPARE(viewport->textDocument()->document()->findBlockByNumber(0).text(),
                 QString("fixed"));

        // What was on offer was for the text as it stood, so it is withdrawn.
        QTRY_COMPARE(offered.size(), 2);
        QVERIFY(offered.at(1).at(0).toStringList().isEmpty());
    }

    // The right-click menu wants the same fixes the popup does, and asking for
    // them must not answer the popup's question: routed to one signal for both,
    // opening the menu opened the popup over it.
    void testAskingForTheMenusFixesLeavesThePopupShut()
    {
        TemporaryDirectory dir("qtc-viewport-menufix");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("broken\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        OneFixProvider provider;
        viewport->textDocument()->setQuickFixAssistProvider(&provider);

        QSignalSpy popup(viewport, &TextViewport::quickFixesAvailable);
        QSignalSpy menu(viewport, &TextViewport::contextFixesAvailable);
        viewport->setCursorPosition(6);
        viewport->requestContextFixes();

        QTRY_COMPARE(menu.size(), 1);
        QCOMPARE(menu.at(0).at(0).toStringList(), QStringList({"Replace with fixed"}));

        // Not a bare check that nothing happened: both signals are emitted from
        // the same delivery, so the answer above having arrived is what says the
        // other one is not still on its way.
        QVERIFY2(popup.isEmpty(), "asking for the menu's fixes also opened the popup");

        // And they are the same fixes, so the menu applies them by index the
        // way the popup does.
        viewport->applyQuickFix(0);
        QCOMPARE(viewport->textDocument()->document()->findBlockByNumber(0).text(),
                 QString("fixed"));
    }

    // Whoever wants first refusal at a tooltip is asked before the view's own
    // hover handlers - the debugger answers with the value of an expression
    // while it is stopped. Both views ask through the document, so the thing
    // answering does not have to know which one is showing the file.
    void testTheDocumentIsAskedBeforeTheHoverHandlers()
    {
        class CountingHandler final : public TextEditor::BaseHoverHandler
        {
        public:
            int asked = 0;

        protected:
            void identifyMatch(HoverTarget *, int, ReportPriority report) override
            {
                ++asked;
                report(Priority_None);
            }
        };

        TemporaryDirectory dir("qtc-viewport-tooltipseam");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("alpha beta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Without somewhere to put a tooltip the view does not ask for one. In
        // the editor that is the widget the QQuickWidget lives in; here it
        // only has to exist, since nothing is placed.
        QWidget host;
        viewport->setTooltipHost(&host);

        CountingHandler handler;
        viewport->addHoverHandler(&handler);

        TextDocument * const document = viewport->textDocument();
        QVERIFY(document);

        // Alt on its own asks for a tooltip at the caret when it is let go,
        // which is the way in that does not need a mouse.
        auto &keyboardTooltips = globalBehaviorSettings().keyboardTooltips;
        const bool wasAsking = keyboardTooltips();
        const QScopeGuard restore(
            [&keyboardTooltips, wasAsking] { keyboardTooltips.setValue(wasAsking); });
        keyboardTooltips.setValue(true);

        viewport->forceActiveFocus();
        viewport->setCursorPosition(6);
        const auto tapAlt = [viewport] {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &press);
            QCoreApplication::sendEvent(viewport, &release);
        };

        // Declined: the question was asked, and the hover handlers still ran.
        int askedAt = -1;
        auto declining = connect(document, &TextDocument::tooltipOverrideRequested, document,
                                 [&askedAt](Core::IEditor *, const QPoint &, int position, bool *) {
                                     askedAt = position;
                                 });
        tapAlt();
        QCOMPARE(askedAt, 6);
        QTRY_COMPARE(handler.asked, 1);
        disconnect(declining);

        // And taken: the hover handlers are not asked at all, which is what
        // stops two tooltips racing for the same spot.
        connect(document, &TextDocument::tooltipOverrideRequested, document,
                [](Core::IEditor *, const QPoint &, int, bool *handled) { *handled = true; });
        tapAlt();
        QCOMPARE(handler.asked, 1);
    }

    // A buffer that cannot be edited is not fixed either.
    void testQuickFixesAreNotOfferedForAReadOnlyBuffer()
    {
        TemporaryDirectory dir("qtc-viewport-quickfix-ro");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("broken\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QVERIFY(viewport->isReadOnly());

        OneFixProvider provider;
        viewport->textDocument()->setQuickFixAssistProvider(&provider);

        QSignalSpy offered(viewport, &TextViewport::quickFixesAvailable);
        viewport->setCursorPosition(6);
        viewport->requestQuickFixes();

        QTRY_COMPARE(offered.size(), 1);
        QVERIFY2(offered.at(0).at(0).toStringList().isEmpty(),
                 "a read only buffer was offered a fix");
    }

    // Asking again while the language is still answering must not delete the
    // processor that is answering. IAssistProcessor::cancel() does not stop
    // anything - an AsyncProcessor is already on a thread pool - so all it can
    // do is arrange for the answer to be dropped and for the processor to
    // delete itself when it lands. Deleting it here frees what a worker thread
    // is still reading, and then frees it a second time.
    //
    // The ASan repro is
    // testTypingALineOfCppLeavesTheSameFileInEitherView: eight crashes out of
    // eight before this. That needs a sanitizer to say anything, so this says
    // the same thing without one.
    void testAskingAgainDoesNotDeleteWhatIsStillAnswering()
    {
        TemporaryDirectory dir("qtc-viewport-stillworking");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        StillWorking working;
        StillWorkingProvider provider(&working);
        viewport->textDocument()->setCompletionAssistProvider(&provider);

        viewport->setCursorPosition(5);
        viewport->requestCompletions();
        QVERIFY2(!working.deleted, "the first request was thrown away before it answered");

        // Still answering, so asking again has to let go of it rather than
        // delete it - and has to say so, because a processor nobody cancelled
        // never sees itself out and the letting go is a leak.
        working.running = true;
        viewport->requestCompletions();
        QVERIFY2(working.cancelled, "a request nobody is waiting for was not cancelled");
        QVERIFY2(!working.deleted,
                 "a processor that was still answering was deleted under its own thread");

        // And one that has finished is this view's to delete, or every
        // abandoned request would be a leak. A fresh state, because the one
        // above is deliberately still alive.
        StillWorking done;
        StillWorkingProvider finished(&done);
        viewport->textDocument()->setCompletionAssistProvider(&finished);
        viewport->requestCompletions();
        QVERIFY2(!done.deleted, "the finished-processor fixture starts out already deleted");
        viewport->requestCompletions();
        QVERIFY2(done.deleted, "a processor that had finished was leaked instead of deleted");
    }

    // The same rule when the view goes away rather than when it asks again,
    // which is what a reader closing a file just after typing "(" does. The
    // members are unique_ptrs and would simply have deleted what was still
    // being worked on.
    //
    // Built by hand rather than through ViewportFixture, because that one does
    // not own the viewport it makes - after the fixture goes out of scope the
    // item is still alive, so a destructor is exactly what it cannot test.
    void testClosingTheViewDoesNotDeleteWhatIsStillAnswering()
    {
        TemporaryDirectory dir("qtc-viewport-closing");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        // Outlives the view on purpose: what is being asked is what the view's
        // destructor did to it.
        StillWorking working;
        {
            CodeDocument source;
            source.setFilePath(file);
            StillWorkingProvider provider(&working);

            const std::unique_ptr<TextViewport> viewport(new TextViewport);
            viewport->setDocument(&source);
            QTRY_VERIFY(viewport->textDocument());
            viewport->textDocument()->setCompletionAssistProvider(&provider);
            viewport->setCursorPosition(1);
            viewport->requestCompletions();
            QVERIFY2(!working.deleted, "the request was thrown away before it answered");
            working.running = true;
        }
        QVERIFY2(working.cancelled, "the view went without cancelling what it had asked for");
        QVERIFY2(!working.deleted,
                 "closing the view deleted a processor its own thread was still in");
    }

    // A function hint is not a list to choose from: it says what the call
    // takes while the arguments are being typed, so it has to follow the
    // caret and go away when the call is finished.
    void testTheFunctionHintFollowsTheCaretAndEndsWithTheCall()
    {
        TemporaryDirectory dir("qtc-viewport-hint");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("f(\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        TwoArgumentHintProvider provider;
        viewport->textDocument()->setFunctionHintAssistProvider(&provider);

        QSignalSpy hinted(viewport, &TextViewport::functionHintAvailable);
        // Just after the opening bracket, which is where the call starts.
        viewport->setCursorPosition(2);
        viewport->requestFunctionHint();

        QTRY_COMPARE(hinted.size(), 1);
        QCOMPARE(hinted.at(0).at(0).toStringList(), QStringList({"f(int a, int b)"}));
        QCOMPARE(hinted.at(0).at(1).toInt(), 0);

        // Typing the first argument and a comma puts the caret in the second,
        // which the hint has to notice without being asked again.
        QTextCursor edit(viewport->textDocument()->document());
        edit.setPosition(2);
        edit.insertText("1,");
        viewport->setCursorPosition(4);

        QTRY_VERIFY(hinted.size() >= 2);
        QCOMPARE(hinted.last().at(1).toInt(), 1);

        // And closing the call ends the hint rather than leaving it up.
        edit.setPosition(4);
        edit.insertText("2)");
        viewport->setCursorPosition(6);

        QTRY_VERIFY(hinted.last().at(0).toStringList().isEmpty());
        QCOMPARE(hinted.last().at(1).toInt(), -1);
    }

    // Taking a completion asks the item to put itself in, rather than the
    // view inserting the word it was showing. Most items do put their own
    // text in, which is why inserting it looked right for so long - but one
    // that expands a snippet or adds brackets does not, and only the item
    // knows the difference.
    void testTakingACompletionAsksTheItemToApplyItself()
    {
        TemporaryDirectory dir("qtc-viewport-applyitem");
        const FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("ex\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        ExpandingProvider provider;
        viewport->textDocument()->setCompletionAssistProvider(&provider);

        QSignalSpy offered(viewport, &TextViewport::completionsAvailable);
        viewport->setCursorPosition(2);
        viewport->requestCompletions();
        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(offered.at(0).at(0).toStringList(), QStringList({"expand me"}));

        viewport->applyCompletion("expand me");

        // What the item does, not what the list said.
        QCOMPARE(viewport->textDocument()->document()->findBlockByNumber(0).text(),
                 QString("expanded()"));
    }

    // Deleting to the end of the line takes what is after the caret and
    // leaves the line itself; deleting to the start takes what is before it.
    // Neither joins the line to its neighbour, which is the difference
    // between these and Delete or Backspace at the ends.
    void testDeletingToTheEndsOfTheLineLeavesTheLine()
    {
        TemporaryDirectory dir("qtc-viewport-deleteends");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("alpha beta\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(6);
        viewport->deleteEndOfLine();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("alpha "));
        QCOMPARE(text->blockCount(), 3);

        viewport->deleteStartOfLine();
        QCOMPARE(text->findBlockByNumber(0).text(), QString());
        // Still its own line: the one below did not come up to meet it.
        QCOMPARE(text->findBlockByNumber(1).text(), QString("second"));
    }

    // Deleting a word takes the word, and the camel case variant stops inside
    // one - which is the whole reason they are separate commands.
    void testDeletingAWordStopsWhereTheWordDoes()
    {
        TemporaryDirectory dir("qtc-viewport-deleteword");
        const FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alpha beta\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(0);
        viewport->deleteEndOfWord();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("beta"));

        viewport->setCursorPosition(4);
        viewport->deleteStartOfWord();
        QCOMPARE(text->findBlockByNumber(0).text(), QString());
    }

    void testDeletingACamelCaseWordStopsInsideTheWord()
    {
        TemporaryDirectory dir("qtc-viewport-deletecamel");
        const FilePath file = dir.filePath("camel.txt");
        QVERIFY(file.writeFileContents("oneTwoThree\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(0);
        viewport->deleteEndOfWordCamelCase();

        // "one" and not the whole of "oneTwoThree", which is what the plain
        // word command would have taken.
        QCOMPARE(text->findBlockByNumber(0).text(), QString("TwoThree"));
    }

    // Indent and Unindent ask the document's indenter, which is what knows
    // how wide a step is here. Asserted as a round trip and as a change: two
    // commands that both did nothing would pass a round trip on their own.
    void testIndentingAndUnindentingGoBackAndForth()
    {
        TemporaryDirectory dir("qtc-viewport-indent");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("text\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QTextDocument * const text = viewport->textDocument()->document();
        const QString before = text->findBlockByNumber(0).text();
        // The whole line selected: with nothing selected these commands put a
        // step in at the caret, which is what Tab does and is not what is
        // being asked about here.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(before.length());

        viewport->indent();
        const QString indented = text->findBlockByNumber(0).text();
        QVERIFY2(indented != before, "indenting changed nothing");
        QVERIFY2(indented.endsWith(before),
                 qPrintable("the text itself changed: " + indented));

        viewport->unindent();
        QCOMPARE(text->findBlockByNumber(0).text(), before);
    }

    // Auto-indent puts a line where the indenter says it belongs, which for a
    // file with no language of its own is where the line above sits.
    void testAutoIndentFollowsTheLineAbove()
    {
        TemporaryDirectory dir("qtc-viewport-autoindent");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("    first\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        // A file with no language of its own gets an indenter that leaves a
        // line where it is, so one that does something is brought along -
        // what is being asked is whether the command reaches the indenter.
        viewport->textDocument()->setIndenter(
            new FourSpaceIndenter(viewport->textDocument()->document()));

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(text->findBlockByNumber(1).position() + 1);
        viewport->autoIndent();

        QCOMPARE(text->findBlockByNumber(1).text(), QString("    second"));
        // Only the line the caret was on: the one above kept its own.
        QCOMPARE(text->findBlockByNumber(0).text(), QString("    first"));
    }

    // Showing whitespace is the view's own answer, not the setting's: turning
    // it on for the file being read must not turn it on everywhere. Until the
    // view is told, the setting is what answers.
    void testShowingWhitespaceIsPerViewAndFallsBackToTheSetting()
    {
        TemporaryDirectory dir("qtc-viewport-whitespace");
        const FilePath file = dir.filePath("spaces.txt");
        QVERIFY(file.writeFileContents("a b\n"));

        const bool was = displaySettings().visualizeWhitespace();
        const QScopeGuard restore(
            [was] { displaySettings().visualizeWhitespace.setValue(was); });
        displaySettings().visualizeWhitespace.setValue(false);

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Nothing said yet, so the setting answers.
        QVERIFY2(!viewport->visualizesWhitespace(), "the view disagreed with the setting");
        QTRY_VERIFY(viewport->visibleLine(0).value("whitespace").toList().isEmpty());

        // Told, and now it draws them without the setting having moved.
        viewport->setVisualizeWhitespace(true);
        QVERIFY(viewport->visualizesWhitespace());
        QTRY_VERIFY2(!viewport->visibleLine(0).value("whitespace").toList().isEmpty(),
                     "the space between the words was not marked");
        QVERIFY2(!displaySettings().visualizeWhitespace(),
                 "turning it on for this view turned it on for every view");
    }

    // Zooming changes the font every editor shows, which is what the widget
    // editor's zoom does; the scroll wheel setting gates the wheel and not a
    // command asked for by name.
    void testZoomingChangesTheFontAndIsNotGatedByTheWheelSetting()
    {
        TemporaryDirectory dir("qtc-viewport-zoom");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("text\n"));

        const int wasZoom = globalFontSettings().fontZoom();
        const bool wasWheel = globalBehaviorSettings().scrollWheelZooming();
        const QScopeGuard restore([wasZoom, wasWheel] {
            globalFontSettings().setFontZoom(wasZoom);
            globalBehaviorSettings().scrollWheelZooming.setValue(wasWheel);
        });
        globalBehaviorSettings().scrollWheelZooming.setValue(false);

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const int before = globalFontSettings().fontZoom();
        viewport->increaseFontZoom();
        QVERIFY2(globalFontSettings().fontZoom() > before,
                 "the command was refused because the wheel is turned off");

        viewport->resetFontZoom();
        QCOMPARE(globalFontSettings().fontZoom(), 100);
    }

    // Clean Whitespace takes the trailing spaces off, which is the document's
    // work; what is asked here is whether the command reaches it and honours
    // a buffer that cannot be edited.
    void testCleaningWhitespaceTakesTheTrailingSpacesOff()
    {
        TemporaryDirectory dir("qtc-viewport-clean");
        const FilePath file = dir.filePath("trailing.txt");
        QVERIFY(file.writeFileContents("text   \nmore\t\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("text   "));

        viewport->setCursorPosition(0);
        viewport->cleanWhitespace();

        QCOMPARE(text->findBlockByNumber(0).text(), QString("text"));
    }

    // Asking for the context menu from the keyboard asks the form, which is
    // what has one - and at the caret, because that is where the reader is.
    void testAskingForTheContextMenuAsksTheForm()
    {
        TemporaryDirectory dir("qtc-viewport-menu");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("text\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QSignalSpy asked(viewport, &TextViewport::contextMenuRequested);
        viewport->showContextMenu();
        QCOMPARE(asked.size(), 1);
    }

    // Circular Paste with nothing else in the history is an ordinary paste;
    // with a history it offers what is in it, through the list quick fixes
    // are offered in - and taking one pastes it, which is what used to need
    // the target to be a widget.
    void testCircularPasteOffersTheHistoryAndPastesTheChoice()
    {
        TemporaryDirectory dir("qtc-viewport-circular");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QSignalSpy offered(viewport, &TextViewport::quickFixesAvailable);
        QTextDocument * const text = viewport->textDocument()->document();

        // The history is the program's, not this view's: anything copied
        // anywhere earlier in the run is in it, and "nothing else in the
        // history" below is a precondition this test has to make true rather
        // than hope for.
        Internal::CircularClipboard::instance()->clear();

        // One thing copied, so there is nothing to choose between and it is
        // an ordinary paste.
        // Bounded on the clipboard actually holding it: setText() goes to a
        // system pasteboard, and reading it back straight away can find it
        // empty - which pastes nothing and reads as the paste being broken.
        QGuiApplication::clipboard()->setText("first");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("first"));
        viewport->setCursorPosition(0);
        viewport->circularPaste();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("first"));

        // A second thing copied, and now there is a choice.
        QGuiApplication::clipboard()->setText("second");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second"));
        viewport->circularPaste();
        QTRY_VERIFY2(!offered.isEmpty(), "the history was not offered");
        const QStringList fixes = offered.last().at(0).toStringList();
        QVERIFY2(fixes.size() > 1,
                 qPrintable(QString("only %1 offered").arg(fixes.size())));
        QVERIFY2(fixes.contains("first"), qPrintable("what was offered: " + fixes.join(", ")));

        // Taking the older one puts it in, which is the step that used to
        // need the view to be a widget.
        const int older = fixes.indexOf("first");
        const int before = text->characterCount();
        viewport->applyQuickFix(older);
        QVERIFY2(text->characterCount() > before, "choosing from the history pasted nothing");
        QVERIFY2(text->toPlainText().contains("firstfirst")
                     || text->toPlainText().count("first") >= 2,
                 qPrintable("the document says: " + text->toPlainText()));
    }

    // Whether the file carries a byte order mark is the document's to keep;
    // the command turns it over and the document remembers, which is what
    // makes the file save differently afterwards.
    void testSwitchingTheByteOrderMarkTurnsItOver()
    {
        TemporaryDirectory dir("qtc-viewport-bom");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("text\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        TextDocument * const doc = viewport->textDocument();
        const bool before = doc->format().hasUtf8Bom;

        viewport->switchUtf8Bom();
        QCOMPARE(doc->format().hasUtf8Bom, !before);

        // And back, so the command is a toggle rather than a one way trip.
        viewport->switchUtf8Bom();
        QCOMPARE(doc->format().hasUtf8Bom, before);
    }

    // Copy with HTML puts both on the clipboard: the plain text, and the same
    // text as HTML carrying the colours it is shown in. The colours come from
    // the layout that drew it, which is why a view has to be asked rather
    // than the document alone.
    void testCopyingWithHtmlCarriesTheHighlighting()
    {
        TemporaryDirectory dir("qtc-viewport-copyhtml");
        const FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int value = 1;\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // A language, so that there is highlighting to carry in the first
        // place - without one the HTML would be right and prove nothing.
        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());
        QTRY_VERIFY(doc->syntaxHighlighter()
                    && doc->syntaxHighlighter()->syntaxHighlighterUpToDate());

        QGuiApplication::clipboard()->clear();
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(13);
        QTRY_VERIFY(viewport->textCursor().hasSelection());

        viewport->copyWithHtml();

        const QMimeData * const mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime);
        QCOMPARE(mime->text(), QString("int value = 1"));
        QVERIFY2(mime->hasHtml(), "nothing was copied as HTML");
        const QString html = mime->html();
        // With the tags taken out, because the point of the exercise is that
        // the words are *not* contiguous: each is wrapped in a span of its
        // own carrying the colour it is drawn in.
        static const QRegularExpression tag("<[^>]*>");
        const QString shown = QString(html).remove(tag).simplified();
        QVERIFY2(shown.contains("int value = 1"), qPrintable("the HTML read: " + shown));
        // And a colour of some sort, which is the whole difference between
        // this and an ordinary copy.
        QVERIFY2(html.contains("color:"), qPrintable("no colours in: " + html));
    }

    // A suggestion is grey text the reader can take a word at a time. This
    // view cannot draw one yet, but everything else a suggestion needs of a
    // view is here - so putting one on a block by hand and taking it proves
    // the half that is done, and says exactly what the other half is.
    void testASuggestionCanBeTakenWithoutAWidget()
    {
        TemporaryDirectory dir("qtc-viewport-suggestion");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QTextDocument * const text = viewport->textDocument()->document();
        QVERIFY2(!viewport->currentSuggestion(), "there is nothing to take yet");

        // "ret" with "return value;" offered from the start of the line, put
        // there the way something offering one does it.
        viewport->setCursorPosition(3);
        const Utils::Text::Range range{{1, 0}, {1, 3}};
        TextSuggestion::Data offered{range, {1, 3}, "return value;"};
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{offered}, text, 0);
        suggestion->setCurrentPosition(3);
        viewport->insertSuggestion(std::move(suggestion));
        QVERIFY2(viewport->currentSuggestion(), "the suggestion was not found at the caret");

        // One word of it, which is the case that needed a widget: it asks
        // where the caret is and puts back what is left over.
        viewport->applySuggestionWord();
        const QString afterWord = text->findBlockByNumber(0).text();
        QVERIFY2(afterWord.startsWith("return"),
                 qPrintable("one word gave: " + afterWord));
        QVERIFY2(!afterWord.contains("value"),
                 qPrintable("it took more than a word: " + afterWord));
    }

    // A block with a suggestion on it is drawn as the suggestion says the
    // line could read, so the reader sees where taking it would land. The
    // text underneath is unchanged - it is a picture of what would happen,
    // not what has happened.
    void testASuggestionIsShownOnTheLineItWouldChange()
    {
        TemporaryDirectory dir("qtc-viewport-showsuggestion");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("ret"));

        const Utils::Text::Range range{{1, 0}, {1, 3}};
        const TextSuggestion::Data offered{range, {1, 3}, "return value;"};
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{offered}, text, 0);
        suggestion->setCurrentPosition(3);
        viewport->insertSuggestion(std::move(suggestion));

        // The row now reads what the suggestion offers.
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(),
                     QString("return value;"));
        // Drawn the way a suggestion is drawn: the three characters that are
        // really there and the ten being offered are in different colours,
        // which is the whole of what tells the reader which is which. Asking
        // only whether the row has any formatting would not say that - an
        // unstyled row carries format ranges too.
        const auto colourAt = [&](int index) {
            QColor colour;
            for (const QVariant &range : viewport->visibleLine(0).value("formats").toList()) {
                const QVariantMap format = range.toMap();
                const int start = format.value("start").toInt();
                if (index >= start && index < start + format.value("length").toInt())
                    colour = format.value("foreground").value<QColor>();
            }
            return colour;
        };
        QVERIFY2(colourAt(0) != colourAt(6),
                 "the suggestion was drawn in the same colour as the real text");

        // And the line below is where it was: showing a one line suggestion
        // does not move anything.
        QCOMPARE(viewport->visibleLine(1).value("text").toString(), QString("second"));
        // The document still says what it said - this is a picture, not an edit.
        QCOMPARE(text->findBlockByNumber(0).text(), QString("ret"));
    }

    // A suggestion describes what would happen on one line. Leave that line
    // and it describes nothing, so it goes - otherwise the grey text sits
    // there offering something the reader can no longer take.
    void testASuggestionGoesWhenTheCaretLeavesItsLine()
    {
        TemporaryDirectory dir("qtc-viewport-suggestiongone");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        const Utils::Text::Range range{{1, 0}, {1, 3}};
        const auto offer = [&] {
            auto suggestion = std::make_unique<CyclicSuggestion>(
                QList<TextSuggestion::Data>{{range, {1, 3}, "return value;"}}, text, 0);
            suggestion->setCurrentPosition(3);
            viewport->insertSuggestion(std::move(suggestion));
        };

        viewport->setCursorPosition(3);
        offer();
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return value;"));

        QSignalSpy changed(viewport, &TextViewport::suggestionChanged);
        viewport->setCursorPosition(text->findBlockByNumber(1).position());
        QVERIFY2(!viewport->currentSuggestion(), "the suggestion outlived the line it was about");
        QCOMPARE(changed.size(), 1);
        // And it stops being drawn there, which is the half a reader sees.
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("ret"));

        viewport->setCursorPosition(3);
        offer();
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return value;"));
        viewport->setCursorPosition(2);
        QVERIFY2(!viewport->currentSuggestion(),
                 "the suggestion survived the caret moving back before it");

        // And typing something the suggestion does not begin with ends it:
        // what is on the line can no longer become what it offers.
        viewport->setCursorPosition(3);
        offer();
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return value;"));
        QTest::keyClick(&fixture.view, 'x');
        QCOMPARE(text->findBlockByNumber(0).text(), QString("retx"));
        QVERIFY2(!viewport->currentSuggestion(),
                 "the suggestion survived text it cannot be reached from");
    }

    // Escape is the way to be rid of a suggestion without taking it: the one
    // thing on screen the reader never asked for, so the key that dismisses
    // things dismisses it first.
    //
    // It cannot be shown beside several carets - moving to a second one
    // leaves the line the suggestion is about, which ends it - so there is no
    // arrangement in which the two orders differ, and this says only that
    // Escape reaches the suggestion at all.
    void testEscapeTakesTheSuggestionAway()
    {
        TemporaryDirectory dir("qtc-viewport-suggestionescape");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(3);
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "return value;"}}, text, 0);
        suggestion->setCurrentPosition(3);
        viewport->insertSuggestion(std::move(suggestion));
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return value;"));

        QTest::keyClick(&fixture.view, Qt::Key_Escape);
        QVERIFY2(!viewport->currentSuggestion(), "Escape left the suggestion up");
        // Dismissed, not taken: the line says what it said, and stops being
        // drawn as anything else.
        QCOMPARE(text->findBlockByNumber(0).text(), QString("ret"));
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("ret"));
    }

    // The text can also change without this view's caret moving - another
    // view editing the same document, or an undo. The suggestion is about
    // that text, so it is looked at again then too.
    void testASuggestionIsLookedAtAgainWhenTheTextChangesUnderIt()
    {
        TemporaryDirectory dir("qtc-viewport-suggestionunder");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        viewport->setCursorPosition(3);
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "return value;"}}, text, 0);
        suggestion->setCurrentPosition(3);
        viewport->insertSuggestion(std::move(suggestion));
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return value;"));

        // Written through the document rather than through this view. The
        // caret is carried along by text arriving in front of it, the way a
        // cursor in the document would be, and what the suggestion has to be
        // looked at against is the text - the caret is still on the same
        // character it was on.
        const int caret = viewport->cursorPosition();
        QTextCursor elsewhere(text);
        elsewhere.setPosition(0);
        elsewhere.insertText("x");
        QCOMPARE(text->findBlockByNumber(0).text(), QString("xret"));
        QCOMPARE(viewport->cursorPosition(), caret + 1);

        QVERIFY2(!viewport->currentSuggestion(),
                 "the suggestion survived text it can no longer be reached from");
    }

    // A suggestion of several lines is shown on several: its first line on
    // the line it would change, and the rest on rows between the file's own -
    // the same ghost rows an inline diff shows a removed line with. They are
    // in no document, so nothing can be typed into them and nothing maps a
    // click onto them.
    void testASuggestionOfSeveralLinesIsShownOnSeveral()
    {
        TemporaryDirectory dir("qtc-viewport-multiline");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        const Utils::Text::Range range{{1, 0}, {1, 3}};
        const auto offer = [&](const QString &suggested) {
            auto suggestion = std::make_unique<CyclicSuggestion>(
                QList<TextSuggestion::Data>{{range, {1, 3}, suggested}}, text, 0);
            suggestion->setCurrentPosition(3);
            viewport->insertSuggestion(std::move(suggestion));
        };

        // Where the line below sits with nothing offered, to compare against.
        const qreal secondLineY = viewport->visibleLine(1).value("y").toReal();

        offer("return one;\nreturn two;\nreturn three;");
        // The first line goes where the line it would change is.
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("return one;"));
        // The other two are drawn under it, in order.
        QTRY_COMPARE(viewport->ghostTextOnScreen(),
                     QStringList({"return two;", "return three;"}));
        // And they take up room: the file's next line is two rows further
        // down than it was, rather than being drawn over.
        QTRY_VERIFY2(viewport->visibleLine(1).value("y").toReal() > secondLineY,
                     "the line below did not move, so the extra rows are drawn over it");
        QCOMPARE(viewport->ghostRectanglesOnScreen().size(), 2);

        // Drawn as the rest of the same offer: the colour the offered part of
        // the line above is in. A ghost row is also what a diff shows a
        // removed line with, and that is a quite different colour on a band
        // of its own - a suggestion drawn that way reads as a deletion.
        const auto colourAt = [&](int index) {
            QColor colour;
            for (const QVariant &range : viewport->visibleLine(0).value("formats").toList()) {
                const QVariantMap format = range.toMap();
                const int start = format.value("start").toInt();
                if (index >= start && index < start + format.value("length").toInt())
                    colour = format.value("foreground").value<QColor>();
            }
            return colour;
        };
        const QColor offered = colourAt(6);
        QVERIFY(offered.isValid());
        QCOMPARE(viewport->ghostForegroundsOnScreen(), QList<QColor>({offered, offered}));
        // The document still says what it said: this is a picture of what
        // taking the suggestion would do.
        QCOMPARE(text->findBlockByNumber(0).text(), QString("ret"));
        QCOMPARE(text->blockCount(), 3);

        // One line again, and the extra rows go with it.
        offer("one line;");
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("one line;"));
        QVERIFY(viewport->ghostTextOnScreen().isEmpty());
        QTRY_COMPARE(viewport->visibleLine(1).value("y").toReal(), secondLineY);
    }

    // A view that wraps works out its own rows from the width it has, so a
    // suggestion cannot be given rows in it: showing part of one would be a
    // lie about where the rest lands.
    //
    // "Nothing happens" is not something to wait for. So a suggestion is
    // shown first and waited for, and wrapping is turned on after: going back
    // to the file's own text is an event, and it cannot arrive before the
    // layout that would have shown the suggestion had it been going to.
    void testASuggestionIsNotDrawnInAViewThatWraps()
    {
        TemporaryDirectory dir("qtc-viewport-wrapping-suggestion");
        const FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "one line;\nand another;"}},
            text, 0);
        suggestion->setCurrentPosition(3);
        viewport->insertSuggestion(std::move(suggestion));
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("one line;"));
        QCOMPARE(viewport->ghostTextOnScreen(), QStringList({"and another;"}));

        viewport->setWrapping(true);
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("ret"));
        QVERIFY2(viewport->ghostTextOnScreen().isEmpty(),
                 "the rest of the suggestion is still drawn in a wrapping view");
    }

    // None of the line commands edits a buffer that is read only, the same
    // as a key press does not. One test for all of them: each has its own
    // guard, and a guard that is missing on one of them is exactly the kind
    // of thing a per-command test would be written for and then not written.
    void testTheLineCommandsLeaveAReadOnlyBufferAlone()
    {
        TemporaryDirectory dir("qtc-viewport-ro-commands");
        // A source file, not a plain one, and with a definition put on it
        // below: the comment commands have no markers without a language and
        // would do nothing whether or not they were guarded.
        const FilePath file = dir.filePath("code.cpp");
        // Out of alphabetical order, in lower case, and with trailing spaces
        // on purpose: content that every one of these commands would visibly
        // change, so that a missing guard cannot hide behind a command that
        // had nothing to do.
        QVERIFY(file.writeFileContents("zebra;  \napple;\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QVERIFY(viewport->isReadOnly());

        TextDocument * const doc = viewport->textDocument();
        HighlighterHelper::setDefinitionOn(doc, HighlighterHelper::definitionForName("C++"));
        QTRY_VERIFY(HighlighterHelper::definitionForDocument(doc).isValid());

        QTextDocument * const text = doc->document();
        const QString before = text->toPlainText();
        // Not everything these commands could change is text: switching
        // the byte order mark changes how the file is written and nothing
        // else, so comparing the text alone would not see it happen.
        const bool bomBefore = doc->format().hasUtf8Bom;

        viewport->setCursorPosition(2);
        viewport->joinLines();
        viewport->lowercaseSelection();
        viewport->insertLineAbove();
        viewport->insertLineBelow();
        viewport->duplicateSelection();
        viewport->sortLines();
        viewport->unCommentSelection();
        viewport->duplicateSelectionAndComment();
        viewport->deleteLine();
        viewport->cutLine();
        viewport->copyLineUp();
        viewport->copyLineDown();
        viewport->moveLineUp();
        viewport->moveLineDown();
        viewport->rewrapParagraph();
        viewport->deleteEndOfLine();
        viewport->deleteStartOfWord();
        viewport->deleteEndOfWordCamelCase();
        viewport->indent();
        viewport->unindent();
        viewport->autoIndent();
        viewport->autoFormat();
        viewport->cleanWhitespace();
        viewport->pasteWithoutFormat();
        viewport->switchUtf8Bom();
        // Last, and after the lower case one: both go through the same guard,
        // so with the guard gone they would run one after the other and put
        // the text back exactly as it was between them.
        viewport->uppercaseSelection();

        QCOMPARE(text->toPlainText(), before);
        QCOMPARE(doc->format().hasUtf8Bom, bomBefore);
    }

    // Insert Line Above and Below open a line and leave the caret on it,
    // which is the whole point of them over pressing Return.
    void testInsertingALineLeavesTheCaretOnIt()
    {
        TemporaryDirectory dir("qtc-viewport-insertline");
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("first\nsecond\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QTextDocument * const text = viewport->textDocument()->document();

        // On "second", open a line above it.
        viewport->setCursorPosition(text->findBlockByNumber(1).position() + 2);
        viewport->insertLineAbove();
        QCOMPARE(text->findBlockByNumber(1).text(), QString());
        QCOMPARE(text->findBlockByNumber(2).text(), QString("second"));
        QCOMPARE(viewport->textCursor().blockNumber(), 1);

        viewport->insertLineBelow();
        QCOMPARE(text->findBlockByNumber(2).text(), QString());
        QCOMPARE(viewport->textCursor().blockNumber(), 2);
        QCOMPARE(text->findBlockByNumber(3).text(), QString("second"));
    }

    void testWrappedRowsSitUnderTheTextTheyContinue()
    {
        // vim's 'breakindent': a wrapped row starts under its line's own
        // indent rather than hard against the margin, so the continuation
        // reads as part of the line. The widget editor gets this from
        // PlainTextDocumentLayout; this one shapes its own rows.
        TemporaryDirectory dir("qtc-viewport-breakindent");
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents(QByteArray(8, ' ') + QByteArray(300, 'x') + "\n"));

        const bool was = displaySettings().breakindent();
        const QScopeGuard restore(
            [was] { displaySettings().breakindent.setValue(was); });
        displaySettings().breakindent.setValue(false);

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     "the long line did not wrap, so there is no continuation row");

        // The caret at the first character of the second row: that row starts
        // where the first one broke.
        const int firstRowLength = viewport->visibleLine(0).value("text").toString().size();
        QVERIFY(firstRowLength > 0);
        viewport->setCursorPosition(firstRowLength);

        // Off: the continuation starts at the left margin like any other row.
        QTRY_VERIFY(!viewport->cursorRectangle().isNull());
        QCOMPARE(viewport->cursorRectangle().left(), 0.0);

        // On: it starts under the eight spaces the line begins with.
        viewport->setCursorPosition(firstRowLength);
        displaySettings().breakindent.setValue(true);
        QTRY_VERIFY2(viewport->cursorRectangle().left() > 0,
                     "the wrapped row is still hard against the margin");

        // The layout that counts rows is told the same indent, so that it and
        // the rows drawn break in the same places. Not asserted by moving the
        // caret two rows and checking where it lands: that disagrees with the
        // rows drawn whether or not break indent is on, which is a separate
        // fault and is written up in the migration notes.
    }

    void testHoldingDownWalksTheRowsOneAtATime()
    {
        // The caret keeps the column it started from while it moves up and
        // down - a QTextCursor carries that, and this view builds a fresh one
        // for every key. Without handing the column back, the second Down
        // measures from the end of the row the caret is on, which is the far
        // side of the viewport, and lands a row further on every press.
        TemporaryDirectory dir("qtc-viewport-downrows");
        const FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents(QByteArray(300, 'x') + "\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     "the long line did not wrap, so every row is a line");

        // Every row of this line is the same width, so each press should add
        // exactly that many characters.
        const int rowLength = viewport->visibleLine(0).value("text").toString().size();
        QVERIFY(rowLength > 0);
        QCOMPARE(viewport->visibleLine(1).value("text").toString().size(), rowLength);

        viewport->setCursorPosition(0);
        QStringList walked;
        for (int i = 1; i <= 3; ++i) {
            keyMove(fixture.view, QKeySequence::MoveToNextLine);
            walked << QString::number(viewport->cursorPosition());
        }
        QVERIFY2(walked == QStringList({QString::number(rowLength),
                                        QString::number(2 * rowLength),
                                        QString::number(3 * rowLength)}),
                 qPrintable(QString("rows are %1 wide and Down walked to %2")
                                .arg(rowLength)
                                .arg(walked.join(", "))));
    }

    void testShiftDownSelectsARowAtATime()
    {
        // The same column the caret keeps while moving is kept while
        // selecting, and the selection goes through the same rebuilt cursor -
        // so if either the column or the anchor were lost between presses,
        // this would take a row and then some.
        TemporaryDirectory dir("qtc-viewport-shiftdown");
        const FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents(QByteArray(300, 'x') + "\n"));

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     "the long line did not wrap, so every row is a line");

        const int rowLength = viewport->visibleLine(0).value("text").toString().size();
        QVERIFY(rowLength > 0);

        viewport->setCursorPosition(0);
        QStringList taken;
        for (int i = 1; i <= 3; ++i) {
            keyMove(fixture.view, QKeySequence::SelectNextLine);
            taken << QString::number(viewport->selectedCharacterCount());
        }
        QVERIFY2(taken == QStringList({QString::number(rowLength),
                                       QString::number(2 * rowLength),
                                       QString::number(3 * rowLength)}),
                 qPrintable(QString("rows are %1 wide and the selection grew to %2")
                                .arg(rowLength)
                                .arg(taken.join(", "))));

        // And the anchor stayed where it started rather than following.
        QCOMPARE(viewport->selectionStart(), 0);
    }

    void testAWrappedRowCanCarryAMarker()
    {
        // vim's 'showbreak': a marker at the start of every row that continues
        // a line. It takes room whether or not the rows are indented, so the
        // rows must be shaped narrower by it - and the layout that counts rows
        // has to be told, or the two disagree about where a row breaks.
        TemporaryDirectory dir("qtc-viewport-showbreak");
        const FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents(QByteArray(300, 'x') + "\n"));

        const QString wasMarker = displaySettings().showBreak();
        const QScopeGuard restore(
            [wasMarker] { displaySettings().showBreak.setValue(wasMarker); });
        displaySettings().showBreak.setValue(QString());

        ViewportFixture fixture(file, 400, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const int unwrapped = viewport->visibleLineCount();
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->visibleLineCount() > unwrapped,
                     "the long line did not wrap, so no row continues another");

        // No marker asked for, so none anywhere - including the row that
        // starts the line, which never carries one.
        const int plainRow = viewport->visibleLine(1).value("text").toString().size();
        QVERIFY(plainRow > 0);
        QCOMPARE(viewport->visibleLine(1).value("breakMarker").toString(), QString());

        displaySettings().showBreak.setValue("...");
        // The row that starts the line still has none; the ones that continue
        // it do, and they are narrower for it.
        QTRY_COMPARE(viewport->visibleLine(1).value("breakMarker").toString(), QString("..."));
        QCOMPARE(viewport->visibleLine(0).value("breakMarker").toString(), QString());
        QVERIFY2(viewport->visibleLine(1).value("text").toString().size() < plainRow,
                 "the marker was drawn without any room being made for it");
    }

    void testHomeGoesToTheCodeBeforeItGoesToTheMargin()
    {
        TemporaryDirectory dir("qtc-viewport-home");
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents("    indented\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setCursorPosition(9);
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 4);
        // Already there, so now the margin.
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 0);
        // And back to the code from there.
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 4);

        // From inside the indentation the margin is what is nearer, so that is
        // where it goes - not forward to the code. Mirrors handleHomeKey().
        viewport->setCursorPosition(2);
        keyMove(fixture.view, QKeySequence::MoveToStartOfLine);
        QCOMPARE(viewport->cursorPosition(), 0);
    }

    void testMovingTheCaretOffScreenScrollsToIt()
    {
        // A caret the viewport does not follow is a caret that types where the
        // user cannot see, which is worse than not moving at all.
        TemporaryDirectory dir("textviewport-follow");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 5000);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        const int onScreen = viewport->visibleLineCount();

        // Down from the last visible line has to bring the next one into view.
        viewport->setCursorPosition(0);
        for (int i = 0; i < onScreen; ++i)
            QTest::keyClick(&fixture.view, Qt::Key_Down);
        // QTRY, because what is on screen is recomputed in updatePolish() on the
        // next frame rather than when the key arrives.
        QTRY_VERIFY2(viewport->firstVisibleLine() > 0,
                     "the caret walked off the bottom without the viewport following");
        QVERIFY(!viewport->cursorRectangle().isEmpty());

        // And back up again.
        for (int i = 0; i < onScreen; ++i)
            QTest::keyClick(&fixture.view, Qt::Key_Up);
        QTRY_COMPARE(viewport->firstVisibleLine(), 0);
        QVERIFY(!viewport->cursorRectangle().isEmpty());
    }

    void testTabIndentsByTheCodeStyleAndNotByATabCharacter()
    {
        // A text box types a tab character. An editor types what the code style
        // asks for, which is the difference between the two - and the reason
        // Tab has to be taken over rather than left to move the focus.
        TemporaryDirectory dir("textviewport-tab");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("beta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        TextDocument * const doc = fixture.document.textDocument();
        // Written down here rather than taken from the settings, so that the
        // expectation below is a literal and not the same arithmetic twice.
        doc->setTabSettings(TabSettingsData(TabSettingsData::SpacesOnlyTabPolicy, 8, 4,
                                             TabSettingsData::NoContinuationAlign));
        QTextDocument * const text = doc->document();

        viewport->setReadOnly(false);
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_Tab);
        QCOMPARE(text->toPlainText(), QString("    beta\ngamma\n"));
        QVERIFY2(!text->toPlainText().contains('\t'), "Tab typed a tab character");

        // And Shift+Tab takes it back out.
        QTest::keyClick(&fixture.view, Qt::Key_Backtab);
        QCOMPARE(text->toPlainText(), QString("beta\ngamma\n"));
    }

    void testReturnStartsTheNewLineWhereTheIndenterSays()
    {
        // Every TextDocument has a PlainTextIndenter, which carries the
        // previous line's indentation over. A language's own indenter does more
        // than that, but nothing here needs one to show that Return asks.
        TemporaryDirectory dir("textviewport-return");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("    alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        TextDocument * const doc = fixture.document.textDocument();
        doc->setTabSettings(TabSettingsData(TabSettingsData::SpacesOnlyTabPolicy, 8, 4,
                                             TabSettingsData::NoContinuationAlign));
        QTextDocument * const text = doc->document();

        viewport->setReadOnly(false);
        viewport->setCursorPosition(9); // end of "    alpha"
        QTest::keyClick(&fixture.view, Qt::Key_Return);

        QCOMPARE(text->toPlainText(), QString("    alpha\n    \nbeta\n"));
        // And the caret is after that indentation, not before it: a caret at
        // the start of the line would have the user type in column zero.
        QCOMPARE(viewport->cursorPosition(), 14);
    }

    void testASelectionPastTheEndIsStillASelection()
    {
        // Asking for more than there is - selecting "to 200" in a short file,
        // or an edit shortening the document under a selection that was in
        // range when it was made. QTextCursor refuses a position past the end
        // and keeps the one it had, so a cursor built from the two ends came
        // back with nothing selected at all while the viewport still drew the
        // selection.
        TemporaryDirectory dir("textviewport-past-the-end");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("short.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->setSelectionStart(0);
        // The end of what is there, rather than a number past it: asking for
        // a position the document does not have is a warning, and a log full
        // of warnings nobody means is one nobody reads.
        viewport->setSelectionEnd(
            viewport->textDocument()->document()->characterCount() - 1);

        // Everything, rather than nothing.
        QCOMPARE(viewport->selectedText(), QString("alpha\nbeta\n"));
        QVERIFY(viewport->textCursor().hasSelection());
    }

    void testTypingGoesInAtEveryCaret()
    {
        // What a second caret is for. The edits are one undo step, because
        // typing once should not take two undos to take back merely because
        // it happened in two places.
        TemporaryDirectory dir("textviewport-typing-carets");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("two.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QTextDocument * const text = fixture.document.textDocument()->document();
        viewport->setReadOnly(false);

        // "alpha\nbeta\n": the end of each word is 5 and 10.
        QTextCursor first(text);
        first.setPosition(5);
        QTextCursor second(text);
        second.setPosition(10);
        viewport->setMultiTextCursor(Utils::MultiTextCursor({first, second}));
        QCOMPARE(viewport->caretRectangles().size(), 2);

        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText(), QString("alphaX\nbetaX\n"));

        // One step, not two.
        QTest::keyClick(&fixture.view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));

        // Deleting goes to every caret too, or Backspace after typing would
        // not take back what was just typed.
        viewport->setMultiTextCursor(Utils::MultiTextCursor({first, second}));
        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText(), QString("alphaX\nbetaX\n"));
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));

        // And putting the caret somewhere is putting *the* caret somewhere:
        // the extra one is gone, so the next character goes in once.
        viewport->setCursorPosition(0);
        QCOMPARE(viewport->caretRectangles().size(), 1);
        QTest::keyClick(&fixture.view, 'Y');
        QCOMPARE(text->toPlainText(), QString("Yalpha\nbeta\n"));
    }

    void testUndoTakesBackWhatWasTyped()
    {
        // The undo stack is the document's, which is what makes editing through
        // a cursor worth the trouble: it takes back what any other view of the
        // same document did as well.
        TemporaryDirectory dir("textviewport-undo");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        QTextDocument * const text = fixture.document.textDocument()->document();

        viewport->setReadOnly(false);
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText(), QString("Xalpha\n"));

        QTest::keyClick(&fixture.view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString("alpha\n"));
        // Undo puts the caret where the edit was, so that typing carries on
        // from there rather than from wherever it happened to be.
        QCOMPARE(viewport->cursorPosition(), 0);

        QTest::keyClick(&fixture.view, Qt::Key_Z,
                        Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(text->toPlainText(), QString("Xalpha\n"));

        // A read-only viewport has nothing to take back, so it does not.
        viewport->setReadOnly(true);
        QTest::keyClick(&fixture.view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString("Xalpha\n"));
    }

    void testHighlightingReachesWhatIsDrawn()
    {
        // Highlighting arrives late and by a route of its own: it lands as
        // formats on the blocks' own layouts, which is not a content change.
        // The viewport copies those formats into layouts of its own, so it has
        // to be told - otherwise a file is drawn in one colour and nothing
        // anywhere reports a problem.
        // Big, because Creator highlights in timed batches: a small file is
        // coloured before the first frame and would pass whether or not the
        // viewport ever heard about it.
        TemporaryDirectory dir("textviewport-highlighting");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("main.cpp");
        QString source;
        for (int i = 0; i < 5000; ++i)
            source += QString("int function%1() { return %1; }\n").arg(i);
        QVERIFY(file.writeFileContents(source.toUtf8()));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(),
                     QString("int function0() { return 0; }"));
        QVERIFY2(fixture.document.textDocument()->syntaxHighlighter(),
                 "the document has no highlighter, so this would prove nothing");

        // "int" and "return" are keywords, so every visible line has to carry
        // formats the viewport did not put there itself.
        QTRY_VERIFY2(!viewport->visibleLine(2).value("formats").toList().isEmpty(),
                     "the highlighter's colours never reached the viewport");

        // And a line that was nowhere near the first batch. Scrolling polishes
        // by itself, so this waits for the colours to arrive *after* it.
        viewport->setScrollY(viewport->lineHeight() * 4000);
        QTRY_COMPARE(viewport->firstVisibleLine(), 4000);
        QTRY_VERIFY2(!viewport->visibleLine(0).value("formats").toList().isEmpty(),
                     "colours arriving after a scroll never reached the viewport");
    }

    void testAPageThatColoursTheDocumentItselfIsNotSecondGuessed()
    {
        // A page attaching CodeHighlighting to the same document has to win:
        // two highlighters both write the blocks' formats, and that one is told
        // what the text is rather than guessing from the name - which for a
        // .clang-format file, YAML called neither, is the only way to know.
        TemporaryDirectory dir("codedocument-highlight");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main() { return 0; }\n"));

        CodeDocument document;
        document.setFilePath(file);
        QVERIFY(document.isOpened());
        QVERIFY2(document.textDocument()->syntaxHighlighter(),
                 "a document that opened a file was left uncoloured");

        CodeDocument unhighlighted;
        unhighlighted.setHighlight(false);
        unhighlighted.setFilePath(file);
        QVERIFY(unhighlighted.isOpened());
        QVERIFY2(!unhighlighted.textDocument()->syntaxHighlighter(),
                 "highlight: false still put a highlighter on");
    }

    // The group C++ snippets are in, asked for by what they are written in
    // rather than by a constant this plugin would have to borrow.
    static QString cppSnippetGroup()
    {
        for (const SnippetProvider &provider : SnippetProvider::snippetProviders()) {
            if (provider.mimeType() == "text/x-c++src")
                return provider.groupId();
        }
        return {};
    }

    void testABufferDrawsTextThatWasNeverAFile()
    {
        // What the code style preview and the snippet editor hold: text that
        // belongs to an aspect, with a language said out loud because there is
        // no path to guess it from.
        CodeBuffer buffer;
        buffer.setText("int main()\n{\n    return 0;\n}\n");
        buffer.setMimeType("text/x-c++src");
        QVERIFY2(buffer.isHighlighting(), "no highlight definition for C++");
        QVERIFY(buffer.textDocument());

        ViewportFixture fixture(&buffer);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("int main()"));
        QCOMPARE(viewport->visibleLine(2).value("text").toString(), QString("    return 0;"));

        // The highlighter put formats on the blocks, which is the whole reason
        // a buffer is a TextDocument rather than a QString.
        //
        // This is also the assertion that holds TextViewport's connection to
        // SyntaxHighlighter::finished in place. A file happens to be coloured
        // before the viewport's first frame however that connection is wired,
        // so the file test above passes either way; a buffer's colours arrive
        // after it, and only that signal reports them. Weaken this and the
        // connection can be deleted with nothing complaining.
        QTRY_VERIFY2(!viewport->visibleLine(2).value("formats").toList().isEmpty(),
                     "the highlighter coloured nothing");

        // And the formats carry a colour. Highlighting is a foreground colour
        // almost everywhere, so a run of format ranges that all draw in the
        // same colour is text that only looks highlighted from here.
        QSet<QRgb> foregrounds;
        for (int line = 0; line < viewport->visibleLineCount(); ++line) {
            const QVariantList formats = viewport->visibleLine(line).value("formats").toList();
            for (const QVariant &format : formats)
                foregrounds.insert(format.toMap().value("foreground").value<QColor>().rgb());
        }
        QVERIFY2(foregrounds.size() > 1,
                 qPrintable(QString("every format draws in one colour (%1), so nothing is "
                                    "highlighted").arg(foregrounds.size())));
    }

    // A snippet is written in a language, and the group it is in is what says
    // which. Colours come from the mime type, but indenting and completing
    // come from the language's own plugin, which is what the group hands over.
    // Both were lost when the Snippets page stopped being built out of
    // widgets: the decoration only spoke to a TextEditorWidget.
    void testASnippetIsIndentedByItsGroupsLanguage()
    {
        const QString group = cppSnippetGroup();
        QVERIFY2(!group.isEmpty(), "no C++ snippet group - is the CppEditor plugin loaded?");

        CodeBuffer buffer;
        buffer.setMimeType("text/x-c++src");
        buffer.setSnippetGroup(group);
        buffer.setText("void f()\n{");

        ViewportFixture fixture(&buffer);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        // A new line after an opening brace, which is where a language that
        // indents differs from one that does not.
        viewport->setCursorPosition(buffer.textDocument()->document()->characterCount() - 1);
        QTest::keyClick(&fixture.view, Qt::Key_Return);

        const QString written = buffer.text();

        // The line the caret is on rather than the last one in the file. Return
        // is answered by the completer first now, which can put blocks below
        // the caret - a closing brace, where there is one to close - so the
        // last line is not always the one that was just opened.
        const int caret = viewport->cursorPosition();
        const int from = written.lastIndexOf(QLatin1Char('\n'), qMax(0, caret - 1)) + 1;
        const int to = written.indexOf(QLatin1Char('\n'), from);
        const QString caretLine = written.mid(from, to < 0 ? -1 : to - from);
        QVERIFY2(caretLine.startsWith(QLatin1Char(' ')) || caretLine.startsWith(QLatin1Char('\t')),
                 qPrintable("the line after an opening brace was not indented: " + written));
    }

    // The other half a group hands over: the completer, which decides what
    // happens as characters are typed. Unlike the indenter this is the
    // *view's*, so what is checked here is that the view takes the one the
    // source offers - a language's completer differs from the plain one in
    // how it reads context, and CppEditor tests that of its own.
    void testASnippetTakesTheCompleterItsGroupOffers()
    {
        const QString group = cppSnippetGroup();
        QVERIFY2(!group.isEmpty(), "no C++ snippet group - is the CppEditor plugin loaded?");

        CodeBuffer buffer;
        buffer.setMimeType("text/x-c++src");

        ViewportFixture fixture(&buffer);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        TextViewport * const viewport = fixture.viewport;

        // Text that is not a snippet has no language to ask, and the view
        // keeps the plain completer it makes for itself.
        QVERIFY2(!buffer.createAutoCompleter(), "a group-less buffer offered a completer");
        AutoCompleter * const plain = viewport->autoCompleter();
        QVERIFY(plain);

        // Said after the view was bound to the source, which is the order QML
        // sets properties in and the reason the source announces it.
        buffer.setSnippetGroup(group);
        const std::unique_ptr<AutoCompleter> offered(buffer.createAutoCompleter());
        QVERIFY2(offered, "the C++ group offered no completer");
        QVERIFY2(viewport->autoCompleter() != plain,
                 "the view kept its plain completer after the source offered one");

        // And a source that stops being a snippet gets the plain one back,
        // rather than keeping the last language's.
        buffer.setSnippetGroup({});
        QVERIFY2(viewport->autoCompleter() != plain, "the completer was not replaced at all");
        QVERIFY2(!buffer.createAutoCompleter(), "a group-less buffer offered a completer");
    }

    void testABufferCanBeIndentedByALanguagesOwnIndenter()
    {
        // The whole point of the buffer: a code style preview needs a document
        // an indenter can work on, and the text it shows was never a file. The
        // TextEdit path is covered by CodeHighlightingTest; this is the same
        // thing said through a CodeSource.
        ICodeStylePreferences * const codeStyle = codeStyleForLanguage("Cpp");
        QVERIFY2(codeStyle, "no C++ code style - is the CppEditor plugin loaded?");

        CodeBuffer buffer;
        // Deliberately flat: every line at column zero, so any indentation at
        // all is the indenter's doing.
        buffer.setText("int f()\n{\nif (true) {\nreturn 1;\n}\nreturn 0;\n}\n");
        buffer.setMimeType("text/x-c++src");

        CodeIndenting indenting;
        indenting.setSource(&buffer);
        indenting.setLanguageId("Cpp");
        indenting.setCodeStyle(codeStyle);
        QVERIFY2(indenting.isIndenting(), "no indenter for C++ over a CodeSource");

        indenting.reindent();

        const auto indentOf = [](const QString &line) {
            return int(line.size() - QStringView(line).trimmed().size());
        };
        const QStringList lines = buffer.text().split('\n');
        QCOMPARE(lines.size(), 8);
        QCOMPARE(indentOf(lines.at(0)), 0);                     // int f()
        QCOMPARE(indentOf(lines.at(1)), 0);                     // {
        QVERIFY(indentOf(lines.at(2)) > 0);                     // if (true) {
        QVERIFY(indentOf(lines.at(3)) > indentOf(lines.at(2))); // return 1;
        QCOMPARE(indentOf(lines.at(6)), 0);                     // }

        // And it indents by *these* settings, which is what a preview is for:
        // widening the indent widens the preview by itself, with no reindent()
        // call. Merely checking that something was indented passes with any
        // settings at all.
        const int wasIndented = indentOf(lines.at(2));
        ICodeStylePreferences * const current = codeStyle->currentPreferences();
        QVERIFY(current);
        const TabSettingsData original = current->tabSettings();
        TabSettingsData wider = original;
        wider.m_indentSize = original.m_indentSize + 3;
        wider.m_tabSize = wider.m_indentSize;
        current->setTabSettings(wider);
        QTRY_COMPARE(indentOf(buffer.text().split('\n').at(2)), wasIndented + 3);

        current->setTabSettings(original);
        QTRY_COMPARE(indentOf(buffer.text().split('\n').at(2)), wasIndented);
    }

    void testEditingABufferWritesBackToItsText()
    {
        // Two-way, so that an aspect can hold the value and a preview can show
        // it: a buffer whose text did not follow the document would show edits
        // that were never saved anywhere.
        CodeBuffer buffer;
        buffer.setText("alpha\n");

        ViewportFixture fixture(&buffer);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QSignalSpy typed(&buffer, &CodeBuffer::textChanged);
        viewport->setReadOnly(false);
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, 'X');

        QCOMPARE(buffer.text(), QString("Xalpha\n"));
        QCOMPARE(typed.count(), 1);
    }

    void testFillingABufferIsNotReadBackAsAnEdit()
    {
        // A buffer writes its own document when text is set, and the document
        // reports that like any other change. Read back, it would say the user
        // typed what was just handed in - which for a preview bound to an
        // aspect is a write straight back into the aspect on every refill.
        CodeBuffer buffer;
        QSignalSpy changed(&buffer, &CodeBuffer::textChanged);

        buffer.setText("alpha\n");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(buffer.text(), QString("alpha\n"));

        // And setting the same text again is not a change at all.
        buffer.setText("alpha\n");
        QCOMPARE(changed.count(), 1);
    }

    void testTheClipboardCarriesLineBreaksAndNotU2029()
    {
        // A preview or a snippet that could not be copied out of would be worse
        // than the TextArea it replaces, so this is what has to be there before
        // any page gives one up.
        //
        // The clipboard is the machine's, so whatever was on it is put back:
        // running a test should not cost the user their paste buffer.
        QClipboard * const clipboard = QGuiApplication::clipboard();
        const QString borrowed = clipboard->text();
        const QScopeGuard giveItBack([clipboard, borrowed] { clipboard->setText(borrowed); });

        TemporaryDirectory dir("textviewport-clipboard");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // Selecting all and copying works in a view: reading a file means being
        // able to take a copy of it.
        QVERIFY(viewport->isReadOnly());
        clipboard->setText("something else");
        QTest::keyClick(&fixture.view, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(viewport->selectionStart(), 0);
        QTest::keyClick(&fixture.view, Qt::Key_C, Qt::ControlModifier);

        // A real newline. QTextCursor::selectedText() would have given U+2029,
        // which pastes into any other application as a stray character.
        QCOMPARE(clipboard->text(), QString("alpha\nbeta\n"));
        QVERIFY2(!clipboard->text().contains(QChar::ParagraphSeparator),
                 "the clipboard carries a paragraph separator");

        // Cut and paste are edits, so a view does neither.
        QTest::keyClick(&fixture.view, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));

        viewport->setReadOnly(false);
        QTest::keyClick(&fixture.view, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString(""));

        // And what was cut comes back with its lines intact rather than as one.
        QTest::keyClick(&fixture.view, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));
        QCOMPARE(text->blockCount(), 3);
    }

    void testComposingTextIsShownBeforeItIsTyped()
    {
        // A dead key or a CJK input method shows what is being composed before
        // it is committed, and until it is committed it is not in the document.
        // QTextLayout has a place for exactly that, so the viewport keeps no
        // shadow copy of the line.
        TemporaryDirectory dir("textviewport-ime");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();

        viewport->setReadOnly(false);
        viewport->setCursorPosition(5); // end of "alpha"
        const qreal plain = viewport->visibleLine(0).value("width").toReal();

        // Composing. Nothing is in the document, and the line is drawn wider.
        QInputMethodEvent composing;
        composing.setCommitString({});
        QList<QInputMethodEvent::Attribute> underlined;
        QTextCharFormat format;
        format.setFontUnderline(true);
        underlined << QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, 3, format);
        QInputMethodEvent preedit("abc", underlined);
        QCoreApplication::sendEvent(viewport, &preedit);

        QTRY_COMPARE(viewport->visibleLine(0).value("preedit").toString(), QString("abc"));
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));
        QCOMPARE(viewport->visibleLine(0).value("text").toString(), QString("alpha"));
        QVERIFY2(viewport->visibleLine(0).value("width").toReal() > plain,
                 "the composed text was not laid out");

        // Committing puts it in the document and takes it out of the preedit.
        QInputMethodEvent commit;
        commit.setCommitString("abc");
        QCoreApplication::sendEvent(viewport, &commit);

        QCOMPARE(text->toPlainText(), QString("alphaabc\nbeta\n"));
        QTRY_COMPARE(viewport->visibleLine(0).value("preedit").toString(), QString());
        QCOMPARE(viewport->cursorPosition(), 8);
    }

    void testAViewIsNotSomethingAnInputMethodCanTypeInto()
    {
        // ImEnabled is how the platform decides whether to bring up an input
        // method at all, and a view has nothing to type into.
        TemporaryDirectory dir("textviewport-ime-readonly");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // Asked the way the platform asks: an InputMethodQuery event, which
        // QQuickItem turns into inputMethodQuery() calls.
        const auto ask = [viewport](Qt::InputMethodQueries queries, Qt::InputMethodQuery one) {
            QInputMethodQueryEvent query(queries);
            QCoreApplication::sendEvent(viewport, &query);
            return query.value(one);
        };

        QVERIFY(viewport->isReadOnly());
        QCOMPARE(ask(Qt::ImEnabled, Qt::ImEnabled).toBool(), false);

        QInputMethodEvent commit;
        commit.setCommitString("abc");
        QCoreApplication::sendEvent(viewport, &commit);
        QCOMPARE(text->toPlainText(), QString("alpha\nbeta\n"));

        viewport->setReadOnly(false);
        QCOMPARE(ask(Qt::ImEnabled, Qt::ImEnabled).toBool(), true);

        // And what the input method needs in order to place itself and to know
        // what it is editing. Surrounding text is the caret's own line, and the
        // position is counted within it - absolute is a separate question.
        viewport->setCursorPosition(8); // "be|ta" on the second line
        const Qt::InputMethodQueries wanted = Qt::ImSurroundingText | Qt::ImCursorPosition
                                              | Qt::ImAbsolutePosition | Qt::ImCursorRectangle;
        QCOMPARE(ask(wanted, Qt::ImSurroundingText).toString(), QString("beta"));
        QCOMPARE(ask(wanted, Qt::ImCursorPosition).toInt(), 2);
        QCOMPARE(ask(wanted, Qt::ImAbsolutePosition).toInt(), 8);
        QVERIFY(!ask(wanted, Qt::ImCursorRectangle).toRectF().isEmpty());
    }

    void testABufferWithNoStyleFollowsTheGlobalTabSettings()
    {
        // A snippet editor has no language and so no code style, and an indent
        // there is what an indent is everywhere else. A default-constructed
        // TabSettingsData happens to match the shipped defaults, so this only
        // says anything with the global settings moved off them.
        TabSettings &global = globalTabSettings();
        const TabSettingsData original = global.data();
        const QScopeGuard putItBack([&global, original] { global.setData(original); });

        TabSettingsData distinctive = original;
        distinctive.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        distinctive.m_indentSize = original.m_indentSize + 3;
        distinctive.m_tabSize = distinctive.m_indentSize;
        global.setData(distinctive);

        CodeBuffer buffer;
        QVERIFY(buffer.textDocument());
        QCOMPARE(buffer.textDocument()->tabSettings().m_indentSize, distinctive.m_indentSize);

        // And it keeps following them, so a page left open while the settings
        // change does not go on indenting by the old ones. Set through the
        // aspect rather than with setData(), which blocks its own signals on
        // purpose and so tells nobody.
        const int wider = distinctive.m_indentSize + 2;
        global.indentSize.setValue(wider);
        QTRY_COMPARE(buffer.textDocument()->tabSettings().m_indentSize, wider);

        // Until something says what an indent is here, which a code style does.
        buffer.setTabSettings(original);
        QCOMPARE(buffer.textDocument()->tabSettings().m_indentSize, original.m_indentSize);
        global.indentSize.setValue(wider + 1);
        QCOMPARE(buffer.textDocument()->tabSettings().m_indentSize, original.m_indentSize);
    }

    void testASnippetEditorWritesWhatWasTypedBackToItsAspect()
    {
        // The snippet editor is the one place where a lost edit costs the user
        // something they wrote: the code style preview says in so many words
        // that changes to it do not affect the settings, and this one keeps
        // what is typed. Driven as the delegate rather than through the page,
        // because selecting a snippet in the page's table is a different thing
        // to test and it has its own tests.
        AspectContainer page;
        StringAspect snippet(&page);
        snippet.setValue("alpha\nbeta\n");

        QQuickView view;
        installIconProvider(view);
        view.resize(500, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "SnippetEditor { width: 500; height: 200 }"),
                          QUrl("qrc:/test/SnippetEditorTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"aspect", QVariant::fromValue<BaseAspect *>(&snippet)},
             {"mimeType", "text/plain"}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_COMPARE(viewport->visibleLine(0).value("text").toString(), QString("alpha"));

        viewport->forceActiveFocus();
        QVERIFY2(viewport->hasActiveFocus(), "the editor never took focus");
        viewport->setCursorPosition(0);
        QTest::keyClick(&view, 'Z');

        // Still being typed, so the aspect has not been told: an aspect that
        // reloads its value would fight the cursor.
        QCOMPARE(snippet.volatileValue(), QString("alpha\nbeta\n"));

        // Focus leaves, and now it has.
        viewport->setFocus(false);
        QTRY_VERIFY(!viewport->hasActiveFocus());
        QTRY_COMPARE(snippet.volatileValue(), QString("Zalpha\nbeta\n"));
    }

    void testASelectionIsFilledAcrossTheWholeRow()
    {
        // The selection's colours are format ranges, but a background on a
        // format range is painted per glyph run - so it arrives in pieces, with
        // a gap wherever the runs are split, and the leading indentation of a
        // selected line was not filled at all. The fill is one rectangle per
        // row now, and this is what says so without reading pixels.
        TemporaryDirectory dir("textviewport-selfill");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("indented.cpp");
        QVERIFY(file.writeFileContents("void f()\n{\n    int a = 1;\n\n}\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const auto fillOn = [viewport](int row) {
            const QList<QRectF> fills
                = viewport->visibleLine(row).value("selectionFills").value<QList<QRectF>>();
            return fills.isEmpty() ? QRectF() : fills.first();
        };
        QVERIFY2(fillOn(2).isEmpty(), "a row with no selection was filled");

        // Everything, so row 2 - "    int a = 1;" - is selected end to end.
        viewport->setSelectionStart(0);
        // The end of what is there, rather than a number past it: asking for
        // a position the document does not have is a warning, and a log full
        // of warnings nobody means is one nobody reads.
        viewport->setSelectionEnd(
            viewport->textDocument()->document()->characterCount() - 1);
        // The fill is built in updatePolish, so wait for it rather than for the
        // property that changes straight away.
        QTRY_VERIFY(!fillOn(2).isEmpty());

        // From the very left: the four spaces are selected too, and they are
        // what a per-glyph-run background missed.
        QCOMPARE(fillOn(2).left(), 0.0);
        QCOMPARE(fillOn(2).height(), viewport->lineHeight());
        const qreal wholeRow = fillOn(2).width();
        QVERIFY(wholeRow > 0);

        // And a selection that starts inside the line starts where it starts,
        // so the assertion above is about the selection and not about the row.
        // "void f()\n" is 9 and "{\n" is 2, so row 2 starts at 11; +4 puts the
        // start after its indentation.
        viewport->setSelectionStart(15);
        QTRY_VERIFY(fillOn(2).left() > 0);
        QVERIFY(fillOn(2).width() < wholeRow);
    }

    void testASelectionIsFilledOnAWrappedLineAndWhenScrolledSideways()
    {
        // The fill is one rectangle per row, measured with cursorToX on that
        // row's own layout, so the two cases where a row is not simply the
        // whole line are worth stating: a wrapped line, whose rows each carry a
        // slice of it, and a sideways scroll, where the row is drawn shifted.
        TemporaryDirectory dir("textviewport-selwrap");
        QVERIFY(dir.isValid());
        const QString longLine(400, QLatin1Char('x'));
        const FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents((longLine + "\nshort\n").toUtf8()));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const auto fillOn = [viewport](int row) {
            const QList<QRectF> fills
                = viewport->visibleLine(row).value("selectionFills").value<QList<QRectF>>();
            return fills.isEmpty() ? QRectF() : fills.first();
        };

        // Sideways first, while there is still one row per line: the fill is
        // in the row's own coordinates, the same ones the text is drawn in, so
        // scrolling must not move one without the other.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(400);
        QTRY_VERIFY(!fillOn(0).isEmpty());
        const QRectF atRest = fillOn(0);
        QCOMPARE(atRest.left(), 0.0);

        viewport->setScrollX(50);
        QTRY_COMPARE(viewport->scrollX(), 50.0);
        QCOMPARE(fillOn(0), atRest);
        viewport->setScrollX(0);
        QTRY_COMPARE(viewport->scrollX(), 0.0);

        // Wrapped: every row of the selected line is filled, each from its own
        // left edge, and none of them claims the whole line's width.
        viewport->setWrapping(true);
        QTRY_VERIFY2(viewport->visibleLineCount() > 3,
                     qPrintable(QString("only %1 rows after wrapping was turned on")
                                    .arg(viewport->visibleLineCount())));
        QTRY_VERIFY(!fillOn(0).isEmpty());

        int rowsOfFirstLine = 0;
        for (int i = 0; i < viewport->visibleLineCount(); ++i) {
            if (viewport->visibleLine(i).value("lineNumber").toInt() != 1)
                break;
            ++rowsOfFirstLine;
        }
        QVERIFY2(rowsOfFirstLine > 1, "the long line did not wrap, so this proves nothing");
        for (int i = 0; i < rowsOfFirstLine; ++i) {
            QVERIFY2(!fillOn(i).isEmpty(),
                     qPrintable(QString("row %1 of the wrapped line is not filled").arg(i)));
            QCOMPARE(fillOn(i).left(), 0.0);
            QVERIFY(fillOn(i).width() <= viewport->width() + 1);
        }
    }

    void testASelectionIsMergedIntoTheLineFormats()
    {
        // The spike's conclusion, kept true: a selection is format ranges on the
        // line's own layout, not a rectangle drawn behind it. The only thing it
        // cannot express is the newline at the end of a selected line, which is
        // one rect.
        TemporaryDirectory dir("textviewport-selection");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("small.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        const auto selectionOn = [viewport](int line) {
            const QVariantList formats = viewport->visibleLine(line).value("formats").toList();
            for (const QVariant &entry : formats) {
                const QVariantMap range = entry.toMap();
                // The highlighter's own ranges are there too; the selection is
                // the one that paints a background.
                if (range.value("background").value<QColor>().alpha() > 0)
                    return QPair<int, int>(range.value("start").toInt(),
                                           range.value("length").toInt());
            }
            return QPair<int, int>(-1, -1);
        };

        QCOMPARE(selectionOn(0), qMakePair(-1, -1));

        // "alp" of "alpha".
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(3);
        QTRY_COMPARE(selectionOn(0), qMakePair(0, 3));
        QVERIFY2(viewport->visibleLine(0).value("newlineTail").toRectF().isEmpty(),
                 "a selection inside a line covered its newline");

        // Across the first newline and into the second line: the first line's
        // newline is covered, and there is no character there to format.
        viewport->setSelectionEnd(8);
        QTRY_VERIFY(!viewport->visibleLine(0).value("newlineTail").toRectF().isEmpty());
        QCOMPARE(selectionOn(1), qMakePair(0, 2));
    }

    // Text let go over the editor. The QML side is a DropArea handing over the
    // point it happened at; everything that changes the document is here.
    void testDroppedTextArrivesWhereItWasLetGo()
    {
        TemporaryDirectory dir("textviewport-drop");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // Between "be" and "ta" on the second line.
        const QRectF caret = viewport->rectangleAt(8);
        QVERIFY(!caret.isEmpty());
        viewport->dropText("XY", caret.x(), caret.center().y());

        QCOMPARE(text->toPlainText(), QString("alpha\nbeXYta\ngamma\n"));

        // And what arrived is what is selected, so the next keystroke replaces
        // it rather than landing next to it.
        QCOMPARE(viewport->selectedText(), QString("XY"));
    }

    void testAnEmptyOrRefusedDropChangesNothing()
    {
        TemporaryDirectory dir("textviewport-drop-refused");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();
        const QString before = text->toPlainText();

        const QRectF caret = viewport->rectangleAt(2);
        QVERIFY(!caret.isEmpty());

        viewport->dropText({}, caret.x(), caret.center().y());
        QCOMPARE(text->toPlainText(), before);

        // A read-only editor takes nothing, the way it takes no keystroke.
        viewport->setReadOnly(true);
        viewport->dropText("XY", caret.x(), caret.center().y());
        QCOMPARE(text->toPlainText(), before);

        viewport->setReadOnly(false);
        viewport->dropText("XY", caret.x(), caret.center().y());
        QVERIFY2(text->toPlainText() != before, "read-only was not what refused the drop");
    }

    void testDraggingASelectionMovesItRatherThanCopyingIt()
    {
        TemporaryDirectory dir("textviewport-drag");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drag.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // Drag "alpha" (0-5) down to the start of "gamma" (11). The drop point
        // is below what is being taken away, so it has to move up by the five
        // characters that stop being there - dropping at the position as it
        // reads now would land five characters late.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QCOMPARE(viewport->selectedText(), QString("alpha"));

        const QRectF target = viewport->rectangleAt(11);
        QVERIFY(!target.isEmpty());
        viewport->dropText("alpha", target.x(), target.center().y(), true);

        QCOMPARE(text->toPlainText(), QString("\nbeta\nalphagamma\n"));
        QCOMPARE(viewport->selectedText(), QString("alpha"));
    }

    void testDraggingASelectionUpwardsMovesIt()
    {
        TemporaryDirectory dir("textviewport-drag-up");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drag.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // "gamma" (11-16) to the very beginning: nothing is removed from above
        // the drop point, so the position stands as it reads.
        viewport->setSelectionStart(11);
        viewport->setSelectionEnd(16);
        QCOMPARE(viewport->selectedText(), QString("gamma"));

        const QRectF target = viewport->rectangleAt(0);
        QVERIFY(!target.isEmpty());
        viewport->dropText("gamma", target.x(), target.center().y(), true);

        QCOMPARE(text->toPlainText(), QString("gammaalpha\nbeta\n\n"));
    }

    void testDroppingASelectionOnItselfChangesNothing()
    {
        TemporaryDirectory dir("textviewport-drag-self");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drag.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();
        const QString before = text->toPlainText();

        // Picking a selection up and putting it back down where it already is.
        // Removing it first and inserting it after would be a deletion.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        const QRectF inside = viewport->rectangleAt(2);
        QVERIFY(!inside.isEmpty());
        viewport->dropText("alpha", inside.x(), inside.center().y(), true);

        QCOMPARE(text->toPlainText(), before);
    }

    void testDroppedTextIsIndentedWhereItLands()
    {
        // A drop is a paste: the language decides what the indentation of what
        // arrives should be, not the place it was dragged from.
        TemporaryDirectory dir("textviewport-drop-indent");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\n    beta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        viewport->setReadOnly(false);
        TextDocument * const doc = fixture.document.textDocument();
        QTextDocument * const text = doc->document();

        // The start of "gamma", which follows an indented line.
        const int startOfGamma = text->findBlockByNumber(2).position();
        const QRectF target = viewport->rectangleAt(startOfGamma);
        QVERIFY(!target.isEmpty());
        viewport->dropText("delta\n", target.x(), target.center().y());

        const QString indented = text->findBlockByNumber(2).text();
        QCOMPARE(indented.trimmed(), QString("delta"));
        QVERIFY2(indented.startsWith("    "),
                 qPrintable(QString("dropped line was not indented: '%1'").arg(indented)));
    }

    void testTheSelectedTextIsWhatWouldBeDraggedOut()
    {
        // A QTextCursor separates paragraphs with U+2029; text handed to
        // another application has to carry real newlines.
        TemporaryDirectory dir("textviewport-selected-text");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("sel.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        QVERIFY(viewport->selectedText().isEmpty());

        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(8);
        const QString dragged = viewport->selectedText();
        QCOMPARE(dragged, QString("alpha\nbe"));
        QVERIFY2(!dragged.contains(QChar::ParagraphSeparator),
                 "the dragged text carried a paragraph separator");
    }

    // A drop is the one input the editor takes that no key or click produces,
    // so the wiring between the drop area in the form and the document is only
    // ever exercised here.
    void testTextDroppedOnTheFormReachesTheDocument()
    {
        TemporaryDirectory dir("codeviewport-drop");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.root->findChild<QObject *>("editorDropArea"),
                 "the form has no drop area");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // Between "be" and "ta" on the second line, in the coordinates the
        // scene delivers a drop in.
        const QRectF caret = viewport->rectangleAt(8);
        QVERIFY(!caret.isEmpty());
        const QPointF at = viewport->mapToScene(QPointF(caret.x(), caret.center().y()));

        QMimeData dropped;
        dropped.setText("XY");
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &dropped,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &enter);
        QVERIFY2(enter.isAccepted(), "the form refused text it can take");
        QDropEvent drop(at, Qt::CopyAction, &dropped, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &drop);

        QCOMPARE(text->toPlainText(), QString("alpha\nbeXYta\ngamma\n"));
    }

    // Dropping a file on the editor opens it. That only works while the editor
    // itself keeps its hands off URLs - taking them would paste the path in.
    void testAFileDroppedOnTheFormIsNotPastedIntoIt()
    {
        TemporaryDirectory dir("codeviewport-drop-url");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();
        const QString before = text->toPlainText();

        const QRectF caret = viewport->rectangleAt(2);
        QVERIFY(!caret.isEmpty());
        const QPointF at = viewport->mapToScene(QPointF(caret.x(), caret.center().y()));

        // A file manager hands over URLs and sets the text to the path as
        // well, which is what makes ignoring the URLs the only way to tell a
        // dropped file from dropped text.
        QMimeData urls;
        urls.setUrls({QUrl::fromLocalFile(file.toFSPathString())});
        urls.setText(file.toFSPathString());
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &urls,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &enter);
        QVERIFY2(!enter.isAccepted(), "the form took a dropped file for itself");
        QDropEvent drop(at, Qt::CopyAction, &urls, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &drop);

        QCOMPARE(text->toPlainText(), before);
    }

    void testAReadOnlyFormTakesNoDrop()
    {
        TemporaryDirectory dir("codeviewport-drop-readonly");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("drop.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();
        const QString before = text->toPlainText();

        const QRectF caret = viewport->rectangleAt(2);
        QVERIFY(!caret.isEmpty());
        const QPointF at = viewport->mapToScene(QPointF(caret.x(), caret.center().y()));

        QMimeData dropped;
        dropped.setText("XY");
        fixture.root->setProperty("readOnly", true);
        QDragEnterEvent refused(at.toPoint(), Qt::CopyAction, &dropped,
                                Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &refused);
        QVERIFY2(!refused.isAccepted(), "a view offered to take text it cannot hold");
        QDropEvent drop(at, Qt::CopyAction, &dropped, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &drop);
        QCOMPARE(text->toPlainText(), before);

        // And it is the read-only that refused, not the form refusing drops
        // in general.
        fixture.root->setProperty("readOnly", false);
        QDragEnterEvent accepted(at.toPoint(), Qt::CopyAction, &dropped,
                                 Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &accepted);
        QVERIFY(accepted.isAccepted());
        QDropEvent second(at, Qt::CopyAction, &dropped, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.view, &second);
        QVERIFY(text->toPlainText() != before);
    }


    void testAMarksMessageIsNotDrawnWhenAnnotationsAreTurnedOff()
    {
        TemporaryDirectory dir("annotation-off-test");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 200);

        QQuickView view;
        installIconProvider(view);
        view.resize(600, 200);
        QQmlComponent component(view.engine());
        component.setData(QByteArray("import QtCreator.TextEditor\n"
                                     "CodeViewport {\n"
                                     "    width: 600; height: 200\n"
                                     "    showAnnotations: false\n"
                                     "    property string path\n"
                                     "    source: CodeDocument { filePath: path }\n"
                                     "}"),
                          QUrl("qrc:/test/AnnotationOffTest.qml"));
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{"path", file.toUrlishString()}}));
        QVERIFY2(created != nullptr, qPrintable(component.errorString()));

        auto * const item = qobject_cast<QQuickItem *>(created.get());
        QVERIFY(item);
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto * const viewport = item->findChild<TextViewport *>("codeViewport");
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());

        const QString message = "expected ';' after expression";
        TextMark mark(source->textDocument(), 3, TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setIcon(Utils::Icons::WARNING.icon());
        mark.setLineAnnotation(message);

        const auto drawnMessage = [item, &message] {
            for (QQuickItem * const candidate : allItems(item)) {
                if (candidate->property("text").toString() == message && candidate->isVisible())
                    return true;
            }
            return false;
        };

        // The viewport still knows what the mark says - it is the drawing that
        // the setting turns off, not the mark. Waiting for that is also what
        // makes the absence below mean something: the message has arrived, and
        // it is still not on screen.
        QTRY_COMPARE(viewport->visibleLine(2).value("annotation").toString(), message);
        QVERIFY2(!drawnMessage(), "the annotation was drawn with annotations turned off");

        // And turning them on draws it, so the absence above was the setting
        // and not a mark that never made it.
        item->setProperty("showAnnotations", true);
        QTRY_VERIFY2(drawnMessage(), "turning annotations on did not draw the message");
    }

    void testTheCaretIsCentredWhenTheSettingAsksForIt()
    {
        TemporaryDirectory dir("center-on-scroll");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "big.txt", 500);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        const qreal lineHeight = viewport->lineHeight();
        QVERIFY(lineHeight > 0);
        const int rows = viewport->visibleLineCount();
        QVERIFY2(rows > 4, "the window is too short for a middle to be distinct from an edge");

        const bool was = displaySettings().centerCursorOnScroll();
        const QScopeGuard restore(
            [was] { displaySettings().centerCursorOnScroll.setValue(was); });

        QTextDocument * const text = fixture.document.textDocument()->document();
        // Through setTextCursor, which is what moving the caret goes through
        // and what shows it afterwards - the cursorPosition property is only
        // the value and does not scroll.
        const auto moveTo = [viewport, text](int blockNumber) {
            QTextCursor cursor(text);
            cursor.setPosition(text->findBlockByNumber(blockNumber).position());
            viewport->setTextCursor(cursor);
        };
        const auto rowOnScreen = [viewport, lineHeight] {
            return qRound(viewport->cursorRectangle().y() / lineHeight);
        };

        // Scrolling as little as possible puts the caret against the edge it
        // came in from.
        displaySettings().centerCursorOnScroll.setValue(false);
        viewport->setScrollY(0);
        moveTo(0);
        QTRY_COMPARE(rowOnScreen(), 0);
        moveTo(rows + 20);
        QTRY_VERIFY2(rowOnScreen() >= rows - 2,
                     qPrintable(QString("caret on row %1 of %2, expected the bottom edge")
                                    .arg(rowOnScreen()).arg(rows)));

        // Centring puts it in the middle of the window instead, which is the
        // whole difference between the two settings.
        displaySettings().centerCursorOnScroll.setValue(true);
        viewport->setScrollY(0);
        moveTo(0);
        QTRY_COMPARE(rowOnScreen(), 0);
        moveTo(rows + 20);
        const int middle = rows / 2;
        QTRY_VERIFY2(qAbs(rowOnScreen() - middle) <= 1,
                     qPrintable(QString("caret on row %1 of %2, expected about %3")
                                    .arg(rowOnScreen()).arg(rows).arg(middle)));
    }

    void testIndentGuidesFollowTheIndentation()
    {
        TemporaryDirectory dir("indent-guides");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("indented.txt");
        // Four spaces to a level, so with the default indent size these are
        // zero, one and two levels deep - and a blank line inside the deepest
        // block, which has no indentation of its own.
        QVERIFY(file.writeFileContents("alpha\n"
                                       "    beta\n"
                                       "        gamma\n"
                                       "\n"
                                       "        delta\n"));

        const bool was = displaySettings().visualizeIndent();
        const QScopeGuard restore(
            [was] { displaySettings().visualizeIndent.setValue(was); });
        displaySettings().visualizeIndent.setValue(true);

        // The whole form: the guides are drawn by CodeViewport.qml, and a bare
        // TextViewport has no QML around it to draw anything.
        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 4);
        QCOMPARE(fixture.document.textDocument()->tabSettings().m_indentSize, 4);

        const auto guidesOn = [viewport](int row) {
            return viewport->visibleLine(row).value("indentGuides").toInt();
        };
        QTRY_COMPARE(guidesOn(0), 0);
        QCOMPARE(guidesOn(1), 1);
        QCOMPARE(guidesOn(2), 2);
        // The blank line takes the shallower of its neighbours, so the guides
        // run through the gap instead of stopping at it.
        QCOMPARE(guidesOn(3), 2);
        QCOMPARE(guidesOn(4), 2);

        // One level is worth a real number of pixels, or every guide would be
        // drawn on top of the last.
        QVERIFY2(viewport->indentWidth() > 0, "an indent level is worth no width");

        // And they are drawn. A guide is a one-pixel-wide item; which row it
        // is on comes from where it lands in the viewport rather than from its
        // place in the tree.
        const auto guidesDrawnOnRow = [&fixture, viewport](int row) {
            int drawn = 0;
            const qreal wanted = row * viewport->lineHeight();
            for (QQuickItem * const candidate : allItems(fixture.root)) {
                if (candidate->objectName() == "indentGuide" && candidate->isVisible()
                    && qFuzzyCompare(candidate->mapToItem(viewport, QPointF(0, 0)).y() + 1,
                                     wanted + 1)) {
                    ++drawn;
                }
            }
            return drawn;
        };
        QTRY_COMPARE(guidesDrawnOnRow(2), 2);
        QCOMPARE(guidesDrawnOnRow(1), 1);
        QCOMPARE(guidesDrawnOnRow(0), 0);

        // The caret is a one pixel wide item on row 0 as well, so counting by
        // width rather than by what an item is made this fail about one run in
        // ten - whenever the window took focus and the caret became visible.
        // Focus cannot be had reliably here, so this records the collision
        // instead of reproducing it: the two are the same shape in the same
        // place, and only their names tell them apart.
        QQuickItem *caret = nullptr;
        for (QQuickItem * const item : allItems(fixture.root)) {
            if (item->objectName() == "caret")
                caret = item;
        }
        QVERIFY2(caret, "no caret, so nothing here is being told apart from anything");
        QCOMPARE(caret->width(), 1);
        QCOMPARE(caret->mapToItem(viewport, QPointF(0, 0)).y(), qreal(0));
    }

    void testNoIndentGuidesWhenTheSettingIsOff()
    {
        TemporaryDirectory dir("indent-guides-off");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("indented.txt");
        QVERIFY(file.writeFileContents("alpha\n        gamma\n"));

        const bool was = displaySettings().visualizeIndent();
        const QScopeGuard restore(
            [was] { displaySettings().visualizeIndent.setValue(was); });
        displaySettings().visualizeIndent.setValue(false);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        // The line is indented - it is the setting that says not to mark it,
        // and without the indentation there would be nothing to leave undrawn.
        QCOMPARE(fixture.document.textDocument()->document()
                     ->findBlockByNumber(1).text().left(8), QString("        "));
        QTRY_COMPARE(viewport->visibleLine(1).value("indentGuides").toInt(), 0);
    }

    void testAnEditedLineIsMarkedUntilItIsSaved()
    {
        TemporaryDirectory dir("change-marks");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("edited.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        const bool was = displaySettings().markTextChanges();
        const QScopeGuard restore(
            [was] { displaySettings().markTextChanges.setValue(was); });
        displaySettings().markTextChanges.setValue(true);

        // With the gutter showing: the mark is drawn at its right edge, and a
        // gutter of no width has nowhere to put it.
        CodeViewportFixture fixture(file, 400, 200, {{"showLineNumbers", true}});
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        TextDocument * const doc = fixture.document.textDocument();
        QTextDocument * const text = doc->document();

        const auto markOn = [viewport](int row) {
            return viewport->visibleLine(row).value("changed").toInt();
        };
        // Nothing has been touched since the file was read.
        QTRY_COMPARE(markOn(0), int(TextViewport::None));
        QCOMPARE(markOn(1), int(TextViewport::None));

        // Editing the second line marks it and leaves the others alone.
        QTextCursor cursor(text);
        cursor.setPosition(text->findBlockByNumber(1).position());
        cursor.insertText("XY");
        QTRY_COMPARE(markOn(1), int(TextViewport::Changed));
        QCOMPARE(markOn(0), int(TextViewport::None));
        QCOMPARE(markOn(2), int(TextViewport::None));

        // And the mark is drawn: a two-pixel-wide item on that row.
        const auto barsOnRow = [&fixture, viewport](int row) {
            int drawn = 0;
            const qreal wanted = row * viewport->lineHeight();
            for (QQuickItem * const candidate : allItems(fixture.root)) {
                if (candidate->width() == 2 && candidate->isVisible()
                    && qFuzzyCompare(candidate->y() + 1, wanted + 1)) {
                    ++drawn;
                }
            }
            return drawn;
        };
        QTRY_COMPARE(barsOnRow(1), 1);
        QCOMPARE(barsOnRow(0), 0);

        // The colour is what says whether the change is saved, so a bar of the
        // wrong colour is the same bug as no bar at all.
        const auto barColourOnRow = [&fixture, viewport](int row) {
            const qreal wanted = row * viewport->lineHeight();
            for (QQuickItem * const candidate : allItems(fixture.root)) {
                if (candidate->width() == 2 && candidate->isVisible()
                    && qFuzzyCompare(candidate->y() + 1, wanted + 1)) {
                    return candidate->property("color").value<QColor>();
                }
            }
            return QColor();
        };
        QCOMPARE(barColourOnRow(1), viewport->changedLineColor());

        // Saving does not clear the mark, it changes its colour: the line was
        // still edited in this session, and Creator says so in green until the
        // file is closed. The document records that as a negative revision.
        const Utils::Result<> saved = doc->save(file);
        QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error()));
        QTRY_COMPARE(markOn(1), int(TextViewport::Saved));
        QCOMPARE(barsOnRow(1), 1);
        QVERIFY2(viewport->savedLineColor() != viewport->changedLineColor(),
                 "a saved change and an unsaved one are drawn the same");
        QTRY_COMPARE(barColourOnRow(1), viewport->savedLineColor());
        // And a line nobody touched is still not marked at all.
        QCOMPARE(markOn(0), int(TextViewport::None));
    }

    void testNoChangeMarksWhenTheSettingIsOff()
    {
        TemporaryDirectory dir("change-marks-off");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("edited.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        const bool was = displaySettings().markTextChanges();
        const QScopeGuard restore(
            [was] { displaySettings().markTextChanges.setValue(was); });
        displaySettings().markTextChanges.setValue(false);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        QTextDocument * const text = fixture.document.textDocument()->document();

        // How wide the line is before the edit, so that the wait below has
        // something to wait for.
        QTRY_VERIFY(viewport->visibleLine(1).value("width").toReal() > 0);
        const qreal widthBefore = viewport->visibleLine(1).value("width").toReal();

        QTextCursor cursor(text);
        cursor.setPosition(text->findBlockByNumber(1).position());
        cursor.insertText("XY");

        // Wait for the *viewport* to have taken the edit in, not just the
        // document: the line it reports is rebuilt on polish, and asking
        // before that reads the line as it was, which would be unmarked
        // whatever the setting said. The line getting wider is the event that
        // is guaranteed to come after the rebuild.
        QVERIFY(text->findBlockByNumber(1).text().startsWith("XY"));
        QTRY_VERIFY(viewport->visibleLine(1).value("width").toReal() > widthBefore);
        QCOMPARE(viewport->visibleLine(1).value("changed").toInt(), int(TextViewport::None));
    }

    void testTheRightMarginIsWhereTheSettingsPutIt()
    {
        TemporaryDirectory dir("right-margin");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "wide.txt", 20);

        const bool wasShown = marginSettings().showMargin();
        const int wasColumn = marginSettings().marginColumn();
        const bool wasTint = marginSettings().tintMarginArea();
        const QScopeGuard restore([wasShown, wasColumn, wasTint] {
            marginSettings().showMargin.setValue(wasShown);
            marginSettings().marginColumn.setValue(wasColumn);
            marginSettings().tintMarginArea.setValue(wasTint);
        });
        marginSettings().showMargin.setValue(false);

        // Wide enough that a margin at column 20 is on screen, or "where it
        // is" would be a question about clipping instead.
        CodeViewportFixture fixture(file, 800, 200);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        auto * const line = fixture.root->findChild<QQuickItem *>("marginLine");
        QVERIFY2(line, "the form has no right margin at all");
        auto * const area = fixture.root->findChild<QQuickItem *>("marginArea");
        QVERIFY(area);

        // Off: nothing drawn, and the viewport says there is nowhere to draw it.
        QTRY_COMPARE(viewport->marginX(), qreal(-1));
        QVERIFY2(!line->isVisible(), "a margin was drawn without the setting asking for one");
        QVERIFY(!area->isVisible());

        // On, at a column of the test's own choosing.
        marginSettings().marginColumn.setValue(20);
        marginSettings().tintMarginArea.setValue(false);
        marginSettings().showMargin.setValue(true);
        QTRY_VERIFY2(viewport->marginX() > 0, "turning the margin on put it nowhere");
        QTRY_VERIFY2(line->isVisible(), "the margin line is not drawn");
        QVERIFY2(!area->isVisible(), "the area past the margin was tinted without being asked");

        const qreal at20 = viewport->marginX();

        // Twice the column is about twice as far across - "about", because the
        // widget adds a few pixels so a line exactly that long does not touch
        // the margin, and this asserts the relationship rather than repeating
        // the formula.
        marginSettings().marginColumn.setValue(40);
        QTRY_VERIFY(viewport->marginX() > at20);
        const qreal at40 = viewport->marginX();
        QVERIFY2(qAbs(at40 - 2 * at20) < at20 / 4,
                 qPrintable(QString("column 20 at %1, column 40 at %2").arg(at20).arg(at40)));

        // And the tint follows its own setting rather than the margin's.
        marginSettings().tintMarginArea.setValue(true);
        QTRY_VERIFY2(area->isVisible(), "the area past the margin is not tinted");
        QVERIFY2(area->x() >= at40 - 1, "the tint starts before the margin");
        QVERIFY2(area->property("color").value<QColor>()
                     != line->property("color").value<QColor>(),
                 "the tint and the line are the same colour");
    }

    void testTheMouseGetsOutOfTheWayWhileTyping()
    {
        TemporaryDirectory dir("mouse-hiding");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "typed.txt", 20);

        const bool was = globalBehaviorSettings().mouseHiding();
        const QScopeGuard restore(
            [was] { globalBehaviorSettings().mouseHiding.setValue(was); });
        globalBehaviorSettings().mouseHiding.setValue(true);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        viewport->forceActiveFocus();
        QVERIFY(viewport->hasActiveFocus());
        QVERIFY(!viewport->isMouseHidden());

        // The two rules, asked directly rather than only through what this
        // platform happens to do with them. Both branches of each, and both
        // states of the setting - none of which is reachable through the
        // viewport on a Mac.
        const BehaviorSettingsData &on = globalBehaviorSettings().data();
        QCOMPARE(hideMouseWhileTyping(on, false), true);
        QCOMPARE(hideMouseWhileTyping(on, true), false);
        BehaviorSettingsData off = on;
        off.m_mouseHiding = false;
        QCOMPARE(hideMouseWhileTyping(off, false), false);
        QCOMPARE(hideMouseWhileTyping(off, true), false);

        // A modifier says how to read the next key and is not itself typing.
        QCOMPARE(isTypingKey(Qt::Key_A), true);
        QCOMPARE(isTypingKey(Qt::Key_Backspace), true);
        QCOMPARE(isTypingKey(Qt::Key_Shift), false);
        QCOMPARE(isTypingKey(Qt::Key_Control), false);
        QCOMPARE(isTypingKey(Qt::Key_CapsLock), false);

        // The rest is what this platform actually does. On a Mac that is
        // nothing: the pointer is never put away, so neither putting it away
        // nor bringing it back can be exercised from here.
        if (Utils::HostOsInfo::isMacHost()) {
            QTest::keyClick(&fixture.view, Qt::Key_A);
            QVERIFY2(!viewport->isMouseHidden(),
                     "the editor hid the pointer on a platform that does that itself");
            return;
        }

        // A modifier on its own is not typing: it says how to read the next
        // key, and the pointer has to survive Shift being pressed.
        QTest::keyPress(&fixture.view, Qt::Key_Shift);
        QVERIFY2(!viewport->isMouseHidden(), "pressing Shift hid the pointer");
        QTest::keyRelease(&fixture.view, Qt::Key_Shift);

        QTest::keyClick(&fixture.view, Qt::Key_A);
        QTRY_VERIFY2(viewport->isMouseHidden(), "typing did not put the pointer away");

        // And moving the mouse is asking for it back.
        viewport->showMouse();
        QVERIFY(!viewport->isMouseHidden());

        // With the setting off it stays put.
        globalBehaviorSettings().mouseHiding.setValue(false);
        QCOMPARE(hideMouseWhileTyping(globalBehaviorSettings().data(), false), false);
        QTest::keyClick(&fixture.view, Qt::Key_B);
        QVERIFY2(!viewport->isMouseHidden(),
                 "the pointer was hidden with the setting turned off");
    }

    void testTypingABracketClosesItWhereTheLanguageSaysSo()
    {
        TemporaryDirectory dir("auto-brackets");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("typed.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        const bool was = globalCompletionSettings().autoInsertBrackets();
        const QScopeGuard restore(
            [was] { globalCompletionSettings().autoInsertBrackets.setValue(was); });
        globalCompletionSettings().autoInsertBrackets.setValue(true);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        viewport->forceActiveFocus();
        QVERIFY(viewport->hasActiveFocus());
        QTextDocument * const text = fixture.document.textDocument()->document();

        // The base completer knows how to take a pair apart and not how to
        // make one, so typing a bracket types exactly that.
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_BracketLeft);
        QTRY_COMPARE(text->firstBlock().text(), QString("[alpha"));

        // The language's does the other half. What it adds goes *after* the
        // caret, so the next keystroke lands between the two.
        viewport->setAutoCompleter(new ClosingAutoCompleter);
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_BracketLeft);
        QTRY_COMPARE(text->firstBlock().text(), QString("[][alpha"));
        QCOMPARE(viewport->cursorPosition(), 1);

        // And the setting still decides: turning it off types the bracket and
        // nothing else, with the same completer in place.
        globalCompletionSettings().autoInsertBrackets.setValue(false);
        viewport->setAutoCompleter(new ClosingAutoCompleter);
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_BracketLeft);
        QTRY_COMPARE(text->firstBlock().text(), QString("[[][alpha"));
    }

    void testCtrlSpaceOffersTheListAndChoosingFromItReplacesTheWord()
    {
        TemporaryDirectory dir("completion-popup");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alphabetical\nbeta\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 1);
        viewport->forceActiveFocus();
        QVERIFY(viewport->hasActiveFocus());

        auto * const popup = fixture.root->findChild<QObject *>("completionPopup");
        QVERIFY2(popup, "the form has no completion popup");
        QVERIFY2(!popup->property("visible").toBool(), "the popup was open before being asked");

        // A viewport made outside an editor has no provider, so the shortcut
        // has nothing to offer and the popup must stay shut rather than open
        // empty. That is also the assertion the editor test cannot make.
        QTest::keyClick(&fixture.view, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(!viewport->document()->textDocument()->completionAssistProvider());
        QVERIFY2(!popup->property("visible").toBool(),
                 "the popup opened with nothing to put in it");

        // Given a provider - the one every text file gets from the editor -
        // the same keystroke offers the words already in the file.
        static DocumentContentCompletionProvider wordsInTheDocument;
        viewport->document()->textDocument()->setCompletionAssistProvider(&wordsInTheDocument);

        QTextCursor cursor(viewport->document()->textDocument()->document());
        cursor.setPosition(cursor.document()->characterCount() - 1);
        cursor.insertText("alph");
        viewport->setTextCursor(cursor);

        QTest::keyClick(&fixture.view, Qt::Key_Space, Qt::ControlModifier);
        QTRY_VERIFY2(popup->property("visible").toBool(), "the popup never opened");
        QCOMPARE(popup->property("prefix").toString(), QString("alph"));

        // And choosing replaces what was typed rather than adding to it.
        QMetaObject::invokeMethod(popup, "acceptCurrent");
        QTRY_COMPARE(cursor.document()->lastBlock().text(), QString("alphabetical"));
        QTRY_VERIFY2(!popup->property("visible").toBool(),
                     "the popup stayed open after a choice was made");
    }

    void testTypingAnActivationCharacterOffersTheListByItself()
    {
        TemporaryDirectory dir("completion-trigger");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alphabetical\n"));

        const CompletionTrigger was = globalCompletionSettings().completionTrigger();
        const QScopeGuard restore(
            [was] { globalCompletionSettings().completionTrigger.setValue(was); });
        globalCompletionSettings().completionTrigger.setValue(TriggeredCompletion);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        viewport->forceActiveFocus();
        QVERIFY(viewport->hasActiveFocus());

        TextDocument * const doc = viewport->document()->textDocument();
        static DotActivatesCompletion dotTriggers;
        doc->setCompletionAssistProvider(&dotTriggers);

        int asked = 0;
        connect(viewport, &TextViewport::completionRequested, viewport, [&asked] { ++asked; });

        // An ordinary letter is not an invitation.
        viewport->setCursorPosition(0);
        QTest::keyClick(&fixture.view, Qt::Key_A);
        QTRY_COMPARE(doc->document()->firstBlock().text().left(1), QString("a"));
        QCOMPARE(asked, 0);

        // The language's activation character is.
        QTest::keyClick(&fixture.view, Qt::Key_Period);
        QTRY_COMPARE(asked, 1);

        // And with the trigger set to manual, nothing is offered unbidden -
        // the character still goes in, which is what makes this about the
        // setting rather than about the keystroke.
        globalCompletionSettings().completionTrigger.setValue(ManualCompletion);
        const int before = doc->document()->characterCount();
        QTest::keyClick(&fixture.view, Qt::Key_Period);
        QTRY_VERIFY(doc->document()->characterCount() > before);
        QCOMPARE(asked, 1);
    }

    void testAGapMovesTheRowsUnderItAndNothingAbove()
    {
        // An inline diff shows the lines a file no longer has between the ones
        // it does. They are not rows: the rows keep their numbering, and the
        // space only pushes the ones under it down.
        TemporaryDirectory dir("textviewport-gaps");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("gaps.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // "alpha\n" is 0-5, "beta\n" 6-10, "gamma\n" 11-16.
        const qreal firstY = viewport->rectangleAt(0).y();
        const qreal secondY = viewport->rectangleAt(6).y();
        const QRectF third = viewport->rectangleAt(11);
        QVERIFY(!third.isEmpty());
        const qreal contentBefore = viewport->contentHeight();

        const qreal gap = 3 * viewport->lineHeight();
        viewport->setRowSpacers("Test.Spacer", {{2, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01);

        // Above the gap nothing moved.
        QCOMPARE(viewport->rectangleAt(0).y(), firstY);
        QCOMPARE(viewport->rectangleAt(6).y(), secondY);
        // The row the gap sits above, and so everything after it, did.
        QVERIFY(qAbs(viewport->rectangleAt(11).y() - (third.y() + gap)) < 0.01);
        // And the row is no taller for it: a caret there is still text-sized.
        QVERIFY(qAbs(viewport->rectangleAt(11).height() - third.height()) < 0.01);
    }

    // Two things can want room at once - an inline diff showing removed lines
    // and a widget embedded in the text - and neither may take the other's.
    // The list used to be set whole, so the second client to arrive would have
    // silently replaced the first.
    void testTwoClaimsForRoomDoNotReplaceEachOther()
    {
        TemporaryDirectory dir("textviewport-two-spacers");
        QVERIFY(dir.isValid());
        const FilePath file = writeLines(dir, "lines.txt", 40);

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 5);

        const qreal contentBefore = viewport->contentHeight();
        const qreal gap = 2 * viewport->lineHeight();
        QVERIFY(gap > 0);

        viewport->setRowSpacers("Test.First", {{1, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01);

        // The second claim is added to the first rather than put in its place.
        viewport->setRowSpacers("Test.Second", {{3, gap}});
        QTRY_VERIFY2(qAbs(viewport->contentHeight() - (contentBefore + 2 * gap)) < 0.01,
                     "the second claim replaced the first instead of joining it");

        // And giving one back leaves the other standing.
        viewport->setRowSpacers("Test.First", {});
        QTRY_VERIFY2(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01,
                     "giving one claim back took the other with it");

        viewport->setRowSpacers("Test.Second", {});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - contentBefore) < 0.01);
    }

    void testAClickInAGapLandsOnTheRowUnderIt()
    {
        // A gap belongs to no row, so a press in one has to answer with
        // something. The row it sits above is the one it was opened for.
        TemporaryDirectory dir("textviewport-gap-click");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("gaps.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal contentBefore = viewport->contentHeight();
        const qreal gap = 3 * viewport->lineHeight();
        viewport->setRowSpacers("Test.Spacer", {{2, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01);

        // Half way up the gap, which sits directly above "gamma".
        const QRectF third = viewport->rectangleAt(11);
        QCOMPARE(viewport->positionAt(0, third.y() - gap / 2), 11);
        // Just below it is the row itself, and just above is the row before.
        QCOMPARE(viewport->positionAt(0, third.center().y()), 11);
        QCOMPARE(viewport->positionAt(0, third.y() - gap - 1), 6);
    }

    void testGhostRowsOpenAGapAsTallAsTheyAreAndShowTheirText()
    {
        // What an inline diff shows for lines the file no longer has. They
        // size their own gap - two removed lines take two rows - and they are
        // drawn in it.
        TemporaryDirectory dir("textviewport-ghosts");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("ghosts.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal contentBefore = viewport->contentHeight();
        const QRectF thirdBefore = viewport->rectangleAt(11);
        QVERIFY(!thirdBefore.isEmpty());
        QVERIFY(viewport->ghostTextOnScreen().isEmpty());

        viewport->setGhostRows({{2, {"was one", "was two"}}});
        const qreal twoRows = 2 * viewport->lineHeight();
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + twoRows)) < 0.01);

        // The text is laid out, in the order it was given.
        QCOMPARE(viewport->ghostTextOnScreen(), QStringList({"was one", "was two"}));
        // And the row it sits above moved down by exactly the two rows.
        const QRectF third = viewport->rectangleAt(11);
        QVERIFY(qAbs(third.y() - (thirdBefore.y() + twoRows)) < 0.01);

        // Drawn in the gap they opened rather than merely somewhere: they run
        // up from the row they sit above, in order, one row apart.
        const QList<QRectF> ghosts = viewport->ghostRectanglesOnScreen();
        QCOMPARE(ghosts.size(), 2);
        QVERIFY(qAbs(ghosts.at(0).y() - (third.y() - twoRows)) < 0.01);
        QVERIFY(qAbs(ghosts.at(1).y() - (third.y() - viewport->lineHeight())) < 0.01);
    }

    void testAGhostRowIsNotAPlaceInTheDocument()
    {
        // The removed lines are not in the file, so nothing may map a screen
        // position onto them and the positions of the real rows must be
        // exactly what they were.
        TemporaryDirectory dir("textviewport-ghost-positions");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("ghosts.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal contentBefore = viewport->contentHeight();
        viewport->setGhostRows({{2, {"was one", "was two"}}});
        const qreal twoRows = 2 * viewport->lineHeight();
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + twoRows)) < 0.01);

        // Every position still maps to itself, ghost rows or not.
        for (int position = 0; position <= 16; ++position) {
            const QRectF caret = viewport->rectangleAt(position);
            QVERIFY2(!caret.isEmpty(),
                     qPrintable(QString("no caret for position %1").arg(position)));
            QCOMPARE(viewport->positionAt(caret.x(), caret.center().y()), position);
        }

        // And a press among the removed lines lands on the line that replaced
        // them rather than on one of them.
        const QRectF third = viewport->rectangleAt(11);
        QCOMPARE(viewport->positionAt(0, third.y() - twoRows / 2), 11);
    }

    void testTheGhostRowsOfADiffLandWhereTheLinesWereRemovedFrom()
    {
        // The widget decorator and the viewport are handed the same
        // description of a diff. This is the viewport's half of reading it:
        // anchorLine counts from one and rows do not, and a block anchored
        // past the last line belongs after everything.
        TemporaryDirectory dir("textviewport-diff-ghosts");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("diffed.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        const int lines = viewport->lineCount();

        // Removed from above the second line, so above the second row.
        applyInlineDiff(viewport, {{2, {"was here"}, {}}});
        QTRY_COMPARE(viewport->ghostTextOnScreen(), QStringList({"was here"}));
        QCOMPARE(viewport->ghostRows().size(), 1);
        QCOMPARE(viewport->ghostRows().first().row, 1);

        // One past the last line is the end of the file rather than a line.
        applyInlineDiff(viewport, {{lines + 1, {"at the end"}, {}}});
        QTRY_COMPARE(viewport->ghostRows().size(), 1);
        QCOMPARE(viewport->ghostRows().first().row, viewport->rowCount());

        // Further than that describes a document this is not, which happens
        // while a diff of the current contents is still being computed.
        applyInlineDiff(viewport, {{lines + 2, {"nowhere"}, {}}});
        QTRY_VERIFY(viewport->ghostRows().isEmpty());
    }

    void testAChangedLineIsFilledAndItsDifferingCharactersMarked()
    {
        // The other half of a diff: lines the file has that the baseline does
        // not. The whole line takes a background, and the characters that
        // actually differ are marked over the top of it.
        TemporaryDirectory dir("textviewport-changes");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("changed.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        QVERIFY(viewport->changedRowsOnScreen().isEmpty());

        // Line 2 is "beta"; characters 1 to 2 of it are "et".
        InlineDiffDecorator::ChangedRange range;
        range.startLine = 2;
        range.endLine = 2;
        range.charHighlights.insert(2, {{1, 2}});
        applyInlineDiff(viewport, {}, {range});

        QTRY_COMPARE(viewport->changedRowsOnScreen(), QList<int>({2}));
        QCOMPARE(viewport->changedTextOnScreen(), QStringList({"et"}));
    }

    void testAChangedRangeCoversEveryLineInIt()
    {
        // A range is a run of lines and every one of them is part of it, not
        // just the one it starts on.
        TemporaryDirectory dir("textviewport-change-range");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("changed.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\nepsilon\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 4);

        InlineDiffDecorator::ChangedRange range;
        range.startLine = 2;
        range.endLine = 4;
        applyInlineDiff(viewport, {}, {range});

        QTRY_COMPARE(viewport->changedRowsOnScreen(), QList<int>({2, 3, 4}));
        // No character marks were given, so the lines are filled and nothing
        // inside them is picked out.
        QVERIFY(viewport->changedTextOnScreen().isEmpty());
    }

    void testALongRemovalIsElidedRatherThanShownWhole()
    {
        // Deleting a thousand lines should not put a thousand rows between
        // two lines of the file.
        TemporaryDirectory dir("textviewport-elision");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("diffed.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        QStringList removed;
        for (int i = 0; i < 150; ++i)
            removed << QString("gone %1").arg(i);
        applyInlineDiff(viewport, {{2, removed, {}}});
        QTRY_COMPARE(viewport->ghostRows().size(), 1);

        // The first hundred, and one line saying what is not shown.
        const QStringList shown = viewport->ghostRows().first().lines;
        QCOMPARE(shown.size(), 101);
        QCOMPARE(shown.first(), QString("gone 0"));
        QCOMPARE(shown.at(99), QString("gone 99"));
        QVERIFY2(shown.last().contains("50"), qPrintable(shown.last()));
    }

    void testAMessageAskedForOnItsOwnLineGetsOne()
    {
        // BetweenLines puts the message under the line instead of after it,
        // which means there has to be a line under it to put it on: the row
        // opens a gap of its own and everything below moves down.
        TemporaryDirectory dir("annotation-between");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("annotated.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        const auto was = displaySettings().annotationAlignment();
        const QScopeGuard restore(
            [was] { displaySettings().annotationAlignment.setValue(was); });
        displaySettings().annotationAlignment.setValue(AnnotationAlignment::BetweenLines);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const source = viewport->document();
        QVERIFY(source && source->textDocument());

        const qreal contentBefore = viewport->contentHeight();
        const qreal fourthBefore = viewport->visibleLine(3).value("y").toReal();

        const QString message = "expected ';' after expression";
        TextMark mark(source->textDocument(), 3, TextMarkCategory{"Test", "TextEditor.Test.Mark"});
        mark.setIcon(Utils::Icons::WARNING.icon());
        mark.setLineAnnotation(message);

        QTRY_COMPARE(viewport->visibleLine(2).value("annotation").toString(), message);
        // The document is one row taller for it, and the line under the marked
        // one has moved down by exactly that row.
        const qreal row = viewport->lineHeight();
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + row)) < 0.01);
        QVERIFY(qAbs(viewport->visibleLine(3).value("y").toReal() - (fourthBefore + row)) < 0.01);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem * const candidate : allItems(fixture.root)) {
                if (candidate->property("text").toString() == message && candidate->isVisible()) {
                    drawn = candidate;
                    return true;
                }
            }
            return false;
        }());

        // Under the line it belongs to, in the room made for it.
        const QPointF at = drawn->mapToItem(viewport, QPointF(0, 0));
        const qreal markedRowY = viewport->visibleLine(2).value("y").toReal();
        QVERIFY2(qAbs(at.y() - (markedRowY + row - viewport->scrollY())) < 0.01,
                 qPrintable(QString("message at %1, wanted %2")
                                .arg(at.y()).arg(markedRowY + row - viewport->scrollY())));

        // And lined up with the text rather than trailing it, which is the
        // point of giving it a line of its own.
        const qreal lineWidth = viewport->visibleLine(2).value("width").toReal();
        QVERIFY2(lineWidth > 0, "the line has no width, so 'before it' means nothing");
        QVERIFY2(at.x() < lineWidth,
                 qPrintable(QString("message starts at %1, past the text at %2")
                                .arg(at.x()).arg(lineWidth)));
    }

    // Alt+click is how the extra carets are made, and in the widget editor it
    // is a toggle: the gesture that puts one down is the one that picks it up
    // again - except on the last caret, which would leave the view with none.
    // This view only ever added.
    void testAltClickPutsACaretDownAndTakesItBack()
    {
        // The branch that decides this lives in QML, so a real click is what
        // covers it - calling toggleCaretAt() from here would test the viewport
        // and leave the thing that calls it untested.
        TemporaryDirectory dir("textviewport-alt-click");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("carets.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);
        QCOMPARE(viewport->caretRectangles().size(), 1);

        // "alpha\nbeta\n": position 7 is inside "beta" on the second row.
        const QRectF target = viewport->rectangleAt(7);
        QVERIFY(!target.isEmpty());
        const QPoint at = viewport->mapToScene(target.center()).toPoint();

        QTest::mouseClick(&fixture.view, Qt::LeftButton, Qt::AltModifier, at);
        QTRY_COMPARE(viewport->caretRectangles().size(), 2);

        // Clicking the same place again takes it back rather than stacking a
        // third one on it - which is what a real TextEditorWidget does,
        // measured alt-click for alt-click.
        QTest::mouseClick(&fixture.view, Qt::LeftButton, Qt::AltModifier, at);
        QTRY_COMPARE(viewport->caretRectangles().size(), 1);

        // On the only caret there is, it stays *where it is*: a view with no
        // caret has nowhere to type, and one that jumped to the top of the
        // file would still count as one. The widget editor guards the same
        // thing after removing.
        // Not at the top of the file, or "it stayed where it was" and "it
        // jumped to position zero" are the same answer and the assertion
        // below cannot tell them apart.
        viewport->setCursorPosition(3);
        const int lonely = viewport->cursorPosition();
        QCOMPARE(lonely, 3);
        const QRectF only = viewport->rectangleAt(lonely);
        QVERIFY(!only.isEmpty());
        QTest::mouseClick(&fixture.view, Qt::LeftButton, Qt::AltModifier,
                          viewport->mapToScene(only.center()).toPoint());
        QTRY_COMPARE(viewport->caretRectangles().size(), 1);
        QCOMPARE(viewport->cursorPosition(), lonely);

        // And anywhere else it is still another caret.
        QTest::mouseClick(&fixture.view, Qt::LeftButton, Qt::AltModifier, at);
        QTRY_COMPARE(viewport->caretRectangles().size(), 2);

        // And a plain click is still a plain click: one caret, where it landed.
        QTest::mouseClick(&fixture.view, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_COMPARE(viewport->caretRectangles().size(), 1);
    }

    void testACaretGoesToTheEndOfEveryLineASelectionCovers()
    {
        // How a column of carets is made without clicking each one. The line
        // the selection ends on is left out: stopping part way through it was
        // not asking for its end.
        TemporaryDirectory dir("textviewport-line-ends");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("lines.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // Nothing selected is nothing to do, and it must not clear the caret.
        viewport->setCursorPosition(0);
        viewport->addCaretsToLineEnds();
        QCOMPARE(viewport->caretRectangles().size(), 1);

        // Selecting from the start into "gamma": "alpha\n" ends at 5,
        // "beta\n" at 10, and "gamma" is where it stops.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(12);
        viewport->addCaretsToLineEnds();

        QCOMPARE(viewport->caretRectangles().size(), 2);
        QList<int> positions;
        for (const QTextCursor &caret : viewport->multiTextCursor().cursors())
            positions.append(caret.position());
        std::sort(positions.begin(), positions.end());
        QCOMPARE(positions, QList<int>({5, 10}));
    }

    void testACaretGoesToTheNextOccurrenceOfWhatIsSelected()
    {
        // Select a word, ask again, and the next one of it gets a caret too -
        // the way a name is renamed without a refactoring engine.
        TemporaryDirectory dir("textviewport-next-match");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("matches.txt");
        QVERIFY(file.writeFileContents("alpha beta\nalpha gamma\nalpha delta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // With nothing selected there is no word to look for, and the caret
        // is left alone rather than cleared.
        viewport->setCursorPosition(0);
        viewport->addCaretAtNextMatch();
        QCOMPARE(viewport->caretRectangles().size(), 1);

        // The first "alpha".
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        viewport->addCaretAtNextMatch();
        QCOMPARE(viewport->caretRectangles().size(), 2);

        viewport->addCaretAtNextMatch();
        QCOMPARE(viewport->caretRectangles().size(), 3);

        // All three are on "alpha", each on a different line.
        QList<int> starts;
        for (const QTextCursor &caret : viewport->multiTextCursor().cursors()) {
            QCOMPARE(caret.selectedText(), QString("alpha"));
            starts.append(caret.selectionStart());
        }
        std::sort(starts.begin(), starts.end());
        QCOMPARE(starts, QList<int>({0, 11, 23}));

        // Asking again has nowhere left to go, so it stops rather than
        // wrapping onto one that already has a caret.
        viewport->addCaretAtNextMatch();
        QCOMPARE(viewport->caretRectangles().size(), 3);
    }

    void testEscapeGoesBackToOneCaret()
    {
        // Escape collapses them, and only does that when there is more than
        // one - it means other things elsewhere.
        TemporaryDirectory dir("textviewport-escape");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("carets.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        viewport->addCaretAt(7);
        QCOMPARE(viewport->caretRectangles().size(), 2);

        QTest::keyClick(&fixture.view, Qt::Key_Escape);
        QTRY_COMPARE(viewport->caretRectangles().size(), 1);

        // The one left is the one Escape was pressed with, and typing goes in
        // once from here.
        viewport->setReadOnly(false);
        QTextDocument * const text = fixture.document.textDocument()->document();
        QTest::keyClick(&fixture.view, 'X');
        QCOMPARE(text->toPlainText().count('X'), 1);
    }

    void testASecondCaretIsDrawnAndSelectsAlongsideTheFirst()
    {
        // The viewport draws what its list of carets says, rather than one.
        // Nothing puts a second one there yet; this is what will show it when
        // something does.
        TemporaryDirectory dir("textviewport-carets");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("carets.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // Counted rather than looked at: a caret is only visible while the
        // window has the focus, and that cannot be had reliably here.
        const auto caretsDrawn = [&fixture] {
            int drawn = 0;
            for (QQuickItem * const item : allItems(fixture.root)) {
                if (item->objectName() == "caret")
                    ++drawn;
            }
            return drawn;
        };
        const auto fillsOnRow = [viewport](int row) {
            return viewport->visibleLine(row).value("selectionFills")
                .value<QList<QRectF>>().size();
        };

        QTRY_COMPARE(caretsDrawn(), 1);
        QCOMPARE(viewport->caretRectangles().size(), 1);

        QTextDocument * const text = viewport->document()->textDocument()->document();
        // "alpha\n" is 0-5, "beta\n" is 6-10.
        QTextCursor first(text);
        first.setPosition(0);
        first.setPosition(3, QTextCursor::KeepAnchor);
        QTextCursor second(text);
        second.setPosition(6);
        second.setPosition(9, QTextCursor::KeepAnchor);
        viewport->setMultiTextCursor(Utils::MultiTextCursor({first, second}));

        QTRY_COMPARE(caretsDrawn(), 2);
        QCOMPARE(viewport->caretRectangles().size(), 2);
        // One selected run on each of the first two rows.
        QTRY_COMPARE(fillsOnRow(0), 1);
        QCOMPARE(fillsOnRow(1), 1);

        // Two selections on one row are two runs on that row, which is the
        // part a single rectangle could not have said.
        QTextCursor third(text);
        third.setPosition(0);
        third.setPosition(2, QTextCursor::KeepAnchor);
        QTextCursor fourth(text);
        fourth.setPosition(3);
        fourth.setPosition(5, QTextCursor::KeepAnchor);
        viewport->setMultiTextCursor(Utils::MultiTextCursor({third, fourth}));

        QTRY_COMPARE(fillsOnRow(0), 2);
        QCOMPARE(fillsOnRow(1), 0);
    }

    void testWhatQmlDrawsOverTheTextFollowsAGapToo()
    {
        // The indent guides are QML items positioned from the row's own y.
        // Working it out as row * lineHeight instead - the same thing until
        // something claims space between rows - leaves every one of them a
        // gap's worth too high.
        TemporaryDirectory dir("gap-guides");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("gapped.txt");
        QVERIFY(file.writeFileContents("alpha\n    beta\n        gamma\n"));

        const bool was = displaySettings().visualizeIndent();
        const QScopeGuard restore([was] { displaySettings().visualizeIndent.setValue(was); });
        displaySettings().visualizeIndent.setValue(true);

        CodeViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // A guide is a one pixel wide item; where it is drawn is what this is
        // about, so gather them by position rather than by place in the tree.
        const auto guideYs = [&fixture, viewport] {
            QList<qreal> ys;
            for (QQuickItem * const item : allItems(fixture.root)) {
                if (item->objectName() == "indentGuide" && item->isVisible())
                    ys.append(item->mapToItem(viewport, QPointF(0, 0)).y());
            }
            std::sort(ys.begin(), ys.end());
            return ys;
        };

        QTRY_VERIFY(guideYs.operator()().size() >= 2);
        const QList<qreal> before = guideYs();
        const qreal topBefore = before.first();
        const qreal bottomBefore = before.last();
        QVERIFY2(bottomBefore > topBefore, "every guide is on one row");

        // Above the deepest row, so the guides on it move and the ones above
        // it do not.
        const qreal gap = 3 * viewport->lineHeight();
        viewport->setRowSpacers("Test.Spacer", {{2, gap}});

        QTRY_VERIFY2(qAbs(guideYs().last() - (bottomBefore + gap)) < 0.01,
                     qPrintable(QString("deepest guide at %1, wanted %2")
                                    .arg(guideYs().last()).arg(bottomBefore + gap)));
        QCOMPARE(guideYs().size(), before.size());
        QVERIFY(qAbs(guideYs().first() - topBefore) < 0.01);
    }

    void testGhostRowsScrolledOffScreenAreNotLaidOut()
    {
        // A diff of a long file has more removed lines than fit on screen.
        // Laying out the ones nobody can see would make scrolling cost more
        // the bigger the diff is.
        TemporaryDirectory dir("textviewport-ghost-culling");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("long.txt");
        QString contents;
        for (int i = 0; i < 400; ++i)
            contents += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // The far one is near the end of the file rather than merely a long
        // way down it, so that scrolling to the bottom really does show it.
        viewport->setGhostRows({{1, {"near the top"}}, {398, {"far below"}}});
        QTRY_COMPARE(viewport->ghostTextOnScreen(), QStringList({"near the top"}));

        // Scrolled to the far one, only it is laid out.
        viewport->setScrollY(viewport->contentHeight() - viewport->height());
        QTRY_COMPARE(viewport->ghostTextOnScreen(), QStringList({"far below"}));
    }

    void testAScreenPositionAndADocumentPositionAgreeAcrossAGap()
    {
        // The round trip has to survive a gap, or a click lands a row off for
        // every line below the first removed one.
        TemporaryDirectory dir("textviewport-gap-mapping");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("gaps.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));

        TextViewport * const viewport = fixture.viewport;
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        const qreal contentBefore = viewport->contentHeight();
        // Two gaps, so that the running total is exercised and not just one
        // subtraction that would look right either way.
        const qreal gap = 2 * viewport->lineHeight();
        viewport->setRowSpacers("Test.Spacer", {{1, gap}, {3, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + 2 * gap)) < 0.01);

        for (int position = 0; position <= 16; ++position) {
            const QRectF caret = viewport->rectangleAt(position);
            QVERIFY2(!caret.isEmpty(),
                     qPrintable(QString("no caret for position %1").arg(position)));
            QCOMPARE(viewport->positionAt(caret.x(), caret.center().y()), position);
        }
    }
};

QObject *createTextViewportTest()
{
    return new TextViewportTest;
}

} // namespace TextEditor::Internal

#include "textviewport_test.moc"
