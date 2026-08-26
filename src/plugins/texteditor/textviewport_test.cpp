// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport_test.h"

#include "codebuffer.h"
#include "codedocument.h"
#include "codeindenting.h"
#include "codestylepool.h"
#include "icodestylepreferences.h"
#include "fontsettings.h"
#include "syntaxhighlighter.h"
#include "tabsettings.h"
#include "textdocument.h"
#include "texteditorconstants.h"
#include "textviewport.h"

#include <utils/temporarydirectory.h>
#include <utils/theme/theme.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickView>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QWheelEvent>
#include <QTest>

using namespace Utils;

namespace TextEditor::Internal {

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

static FilePath writeLines(const TemporaryDirectory &dir, const QString &name, int count)
{
    const FilePath file = dir.filePath(name);
    QString contents;
    for (int i = 0; i < count; ++i)
        contents += QString("line %1\n").arg(i);
    file.writeFileContents(contents.toUtf8());
    return file;
}

// Whatever QML said while a component was alive. A binding loop is a warning
// and nothing else - the component still builds and still draws - so the only
// way a test sees one is by listening.
class QmlComplaints
{
public:
    QmlComplaints() { s_messages = &m_messages; s_previous = qInstallMessageHandler(collect); }
    ~QmlComplaints() { qInstallMessageHandler(s_previous); s_messages = nullptr; }

    QStringList messages() const { return m_messages; }

private:
    static void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (s_messages && type >= QtWarningMsg)
            s_messages->append(message);
        if (s_previous)
            s_previous(type, context, message);
    }

    QStringList m_messages;
    static inline QStringList *s_messages = nullptr;
    static inline QtMessageHandler s_previous = nullptr;
};

class TextViewportTest final : public QObject
{
    Q_OBJECT

private slots:
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
            QmlComplaints listener;

            QQuickView view;
            view.resize(400, 200);
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
        const auto expectedPosition = [viewport] {
            return viewport->scrollY() / viewport->contentHeight();
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
        QTest::keyClick(&fixture.view, Qt::Key_End);
        QCOMPARE(viewport->cursorPosition(), 5);
        QTest::keyClick(&fixture.view, Qt::Key_Down);
        QCOMPARE(viewport->cursorPosition(), 10); // end of "beta"
        QTest::keyClick(&fixture.view, Qt::Key_Home);
        QCOMPARE(viewport->cursorPosition(), 6);

        // Moving without Shift leaves nothing selected.
        QCOMPARE(viewport->selectionStart(), -1);

        // Shift takes the caret's old place as the anchor and grows from it.
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
};

QObject *createTextViewportTest()
{
    return new TextViewportTest;
}

} // namespace TextEditor::Internal

#include "textviewport_test.moc"
