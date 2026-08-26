// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport_test.h"

#include "codedocument.h"
#include "fontsettings.h"
#include "textdocument.h"
#include "texteditorconstants.h"
#include "textviewport.h"

#include <utils/temporarydirectory.h>
#include <utils/theme/theme.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

using namespace Utils;

namespace TextEditor::Internal {

// A viewport in a window, because polish only happens for an item in a scene:
// without one nothing is ever laid out and every assertion below reads zero.
class ViewportFixture
{
public:
    explicit ViewportFixture(const FilePath &file, int width = 400, int height = 200)
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
            document.setFilePath(file);
            viewport->setDocument(&document);
        }
        view.show();
    }

    bool isReady() const { return viewport != nullptr; }
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
