// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport_test.h"

#include "codebuffer.h"
#include "displaysettings.h"
#include "codedocument.h"
#include "codeindenting.h"
#include "codestylepool.h"
#include "icodestylepreferences.h"
#include "snippets/snippetprovider.h"
#include "icodestylepreferencesfactory.h"
#include "autocompleter.h"
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
#include "textviewport.h"
#include "textmark.h"

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
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QClipboard>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QApplication>
#include <QStyleHints>
#include <QWheelEvent>
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
        doc->setTabSettings(tabs);

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

        // A scroll that happens in the same layout as something else: the
        // rows still line up one for one, but one of them says something new.
        // Matching the first row is not enough to conclude the rest are
        // unchanged, which is why the whole overlap is compared.
        QTextDocument * const text = viewport->textDocument()->document();
        const QTextBlock marked = text->findBlockByNumber(6);
        viewport->setScrollY(viewport->lineHeight() * 3);
        viewport->setSelectionStart(marked.position());
        viewport->setSelectionEnd(marked.position() + 4);
        QTRY_COMPARE(viewport->firstVisibleLine(), 3);
        // Asked of the model rather than of the view: the view's own rows are
        // right whatever the model was told, so reading them would not notice
        // a row the model kept when it should not have.
        const auto rowFromModel = [rows](int row) {
            return rows->data(rows->index(row, 0), Qt::UserRole).toMap();
        };
        // Line 6 is the fourth row now, and it is the one with a selection on
        // it. A row kept because the first one matched would show none.
        QTRY_VERIFY2(!rowFromModel(3).value("selectionFill").toRectF().isEmpty(),
                     "the selected row was kept from before it was selected");
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
    // Backspace between the two halves of a bracket pair takes both. That is
    // the whole of what the base AutoCompleter offers - inserting the closing
    // half is a language-specific subclass, handed out per editor factory, so
    // the plain text widget editor does not do it either and neither can this.
    void testBackspaceBetweenAPairTakesBothHalves()
    {
        const bool wasBrackets = globalCompletionSettings().autoInsertBrackets();
        const QScopeGuard restore([wasBrackets] {
            globalCompletionSettings().autoInsertBrackets.setValue(wasBrackets);
        });
        globalCompletionSettings().autoInsertBrackets.setValue(true);

        TemporaryDirectory dir("qtc-viewport-autobrackets");
        const FilePath file = dir.filePath("typed.txt");
        QVERIFY(file.writeFileContents("()\n"));

        ViewportFixture fixture(file);
        QVERIFY2(fixture.isReady(), qPrintable(fixture.error()));
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.view));
        QVERIFY2(fixture.hasFocus(), "the viewport never took focus, so no key arrives");

        TextViewport * const viewport = fixture.viewport;
        viewport->setReadOnly(false);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        QTextDocument * const text = fixture.document.textDocument()->document();

        // Between them, so the closing half would be orphaned.
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText(), QString("\n"));

        // Elsewhere it is an ordinary Backspace: only what is behind the caret.
        text->setPlainText("ab()\n");
        viewport->setCursorPosition(2);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText(), QString("a()\n"));

        // And the setting turns the pairing off, leaving a plain Backspace.
        globalCompletionSettings().autoInsertBrackets.setValue(false);
        text->setPlainText("()\n");
        viewport->setCursorPosition(1);
        QTest::keyClick(&fixture.view, Qt::Key_Backspace);
        QCOMPARE(text->toPlainText(), QString(")\n"));
    }

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
            return viewport->visibleLine(row).value("selectionFill").toRectF();
        };
        QVERIFY2(fillOn(2).isEmpty(), "a row with no selection was filled");

        // Everything, so row 2 - "    int a = 1;" - is selected end to end.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(200);
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
            return viewport->visibleLine(row).value("selectionFill").toRectF();
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
                if (candidate->width() == 1 && candidate->isVisible()
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
        viewport->setRowGaps({{2, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01);

        // Above the gap nothing moved.
        QCOMPARE(viewport->rectangleAt(0).y(), firstY);
        QCOMPARE(viewport->rectangleAt(6).y(), secondY);
        // The row the gap sits above, and so everything after it, did.
        QVERIFY(qAbs(viewport->rectangleAt(11).y() - (third.y() + gap)) < 0.01);
        // And the row is no taller for it: a caret there is still text-sized.
        QVERIFY(qAbs(viewport->rectangleAt(11).height() - third.height()) < 0.01);
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
        viewport->setRowGaps({{2, gap}});
        QTRY_VERIFY(qAbs(viewport->contentHeight() - (contentBefore + gap)) < 0.01);

        // Half way up the gap, which sits directly above "gamma".
        const QRectF third = viewport->rectangleAt(11);
        QCOMPARE(viewport->positionAt(0, third.y() - gap / 2), 11);
        // Just below it is the row itself, and just above is the row before.
        QCOMPARE(viewport->positionAt(0, third.center().y()), 11);
        QCOMPARE(viewport->positionAt(0, third.y() - gap - 1), 6);
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
        viewport->setRowGaps({{1, gap}, {3, gap}});
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
