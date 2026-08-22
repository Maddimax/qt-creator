// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <texteditor/gutterframe.h>
#include <texteditor/textdocumentlayout.h>

#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace TextEditor;

const int lineHeight = 14;

static GutterFrameBuilder::Inputs baseInputs()
{
    GutterFrameBuilder::Inputs in;
    in.lineNumbersVisible = true;
    // a cursor position always marks some block as current; keep it off every
    // block so tests opt in explicitly
    in.selectionStart = -1;
    in.selectionEnd = -1;
    in.lineSpacing = lineHeight;
    in.markWidth = 0;
    in.signWidth = 0;
    in.extraAreaWidth = 40;
    in.foldBoxWidth = 15;
    in.lineNumberString = [](int blockNumber) { return QString::number(blockNumber + 1); };
    return in;
}

// mimics the extraAreaPaintEvent walk: visible blocks, stacked top to bottom
static GutterFrame buildFrame(const QTextDocument &doc, const GutterFrameBuilder::Inputs &inputs)
{
    GutterFrameBuilder builder(inputs);
    qreal top = 0;
    for (QTextBlock block = doc.firstBlock(); block.isValid(); block = block.next()) {
        if (!block.isVisible())
            continue;
        const QRectF boundingRect(0, top, inputs.extraAreaWidth + inputs.foldBoxWidth,
                                  lineHeight);
        builder.addBlock(block, boundingRect);
        top += boundingRect.height();
    }
    return builder.takeFrame();
}

template<typename T>
static QList<T> primitivesOf(const GutterFrame &frame)
{
    QList<T> result;
    for (const GutterFrame::Primitive &primitive : frame.primitives()) {
        if (const auto *p = std::get_if<T>(&primitive))
            result.append(*p);
    }
    return result;
}

class tst_gutterframe : public QObject
{
    Q_OBJECT

private slots:
    void lineNumbers();
    void currentLineNumber();
    void foldingMarker();
    void foldedRegion();
    void revisionMarker();
};

void tst_gutterframe::lineNumbers()
{
    QTextDocument doc;
    doc.setPlainText("one\ntwo\nthree\nfour\nfive");

    const GutterFrame frame = buildFrame(doc, baseInputs());

    const QList<GutterFrame::TextRun> runs = primitivesOf<GutterFrame::TextRun>(frame);
    QCOMPARE(runs.size(), 5);
    for (int i = 0; i < runs.size(); ++i) {
        const GutterFrame::TextRun &run = runs.at(i);
        QCOMPARE(run.text, QString::number(i + 1));
        QCOMPARE(run.font, GutterFrame::FontRole::Default);
        QCOMPARE(run.color, GutterFrame::ColorRole::LineNumber);
        QCOMPARE(run.alignment, int(Qt::AlignRight));
        QCOMPARE(run.rect.top(), i * qreal(lineHeight));
        QCOMPARE(run.rect.height(), qreal(lineHeight));
        if (i > 0)
            QVERIFY(run.rect.top() >= runs.at(i - 1).rect.bottom());
    }
}

void tst_gutterframe::currentLineNumber()
{
    QTextDocument doc;
    doc.setPlainText("one\ntwo\nthree");

    GutterFrameBuilder::Inputs in = baseInputs();
    const QTextBlock second = doc.findBlockByNumber(1);
    in.selectionStart = second.position();
    in.selectionEnd = second.position();
    in.currentLineNumberHasBackground = true;

    const GutterFrame frame = buildFrame(doc, in);

    const QList<GutterFrame::TextRun> runs = primitivesOf<GutterFrame::TextRun>(frame);
    QCOMPARE(runs.size(), 3);
    QCOMPARE(runs.at(0).font, GutterFrame::FontRole::Default);
    QCOMPARE(runs.at(0).color, GutterFrame::ColorRole::LineNumber);
    QCOMPARE(runs.at(1).font, GutterFrame::FontRole::CurrentLineNumber);
    QCOMPARE(runs.at(1).color, GutterFrame::ColorRole::CurrentLineNumber);
    QCOMPARE(runs.at(2).font, GutterFrame::FontRole::Default);

    const QList<GutterFrame::Rect> rects = primitivesOf<GutterFrame::Rect>(frame);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects.first().color, GutterFrame::ColorRole::CurrentLineNumberBackground);
    QCOMPARE(rects.first().rect, QRectF(0, lineHeight, in.extraAreaWidth, lineHeight));
}

void tst_gutterframe::foldingMarker()
{
    QTextDocument doc;
    doc.setPlainText("if {\n    body\n}");
    // a fold region opens where the following block is indented deeper
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(0), 0);
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(1), 1);
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(2), 0);

    GutterFrameBuilder::Inputs in = baseInputs();
    in.codeFoldingVisible = true;

    const GutterFrame frame = buildFrame(doc, in);

    const QList<GutterFrame::FoldMarker> markers = primitivesOf<GutterFrame::FoldMarker>(frame);
    QCOMPARE(markers.size(), 1);
    const GutterFrame::FoldMarker marker = markers.first();
    QVERIFY(marker.expanded);
    QVERIFY(!marker.active);
    QVERIFY(!marker.hovered);
    const int size = in.foldBoxWidth / 4;
    QCOMPARE(marker.rect, QRect(in.extraAreaWidth + size, size, 2 * size + 1, 2 * size + 1));
}

void tst_gutterframe::foldedRegion()
{
    QTextDocument doc;
    doc.setPlainText("if {\n    body\n}");
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(0), 0);
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(1), 1);
    TextBlockUserData::setFoldingIndent(doc.findBlockByNumber(2), 0);
    QTextBlock body = doc.findBlockByNumber(1);
    body.setVisible(false);

    GutterFrameBuilder::Inputs in = baseInputs();
    in.codeFoldingVisible = true;

    const GutterFrame frame = buildFrame(doc, in);

    const QList<GutterFrame::FoldMarker> markers = primitivesOf<GutterFrame::FoldMarker>(frame);
    QCOMPARE(markers.size(), 1);
    QVERIFY(!markers.first().expanded);

    // the folded body produces no line number
    const QList<GutterFrame::TextRun> runs = primitivesOf<GutterFrame::TextRun>(frame);
    QCOMPARE(runs.size(), 2);
    QCOMPARE(runs.at(0).text, QString("1"));
    QCOMPARE(runs.at(1).text, QString("3"));
}

void tst_gutterframe::revisionMarker()
{
    QTextDocument doc;
    doc.setPlainText("one\ntwo");

    GutterFrameBuilder::Inputs in = baseInputs();
    in.revisionsVisible = true;
    in.lastSaveRevision = doc.firstBlock().revision();

    QTextCursor cursor(doc.findBlockByNumber(1));
    cursor.insertText("x");

    const GutterFrame frame = buildFrame(doc, in);

    const QList<GutterFrame::Line> lines = primitivesOf<GutterFrame::Line>(frame);
    QCOMPARE(lines.size(), 1);
    const GutterFrame::Line line = lines.first();
    QCOMPARE(line.color, GutterFrame::ColorRole::RevisionUnsaved);
    QCOMPARE(line.penWidth, 2);
    QCOMPARE(line.line.x1(), in.extraAreaWidth - 1);
    QCOMPARE(line.line.x2(), in.extraAreaWidth - 1);
    QCOMPARE(line.line.y1(), lineHeight);
    QCOMPARE(line.line.y2(), 2 * lineHeight - 1);
}

QTEST_MAIN(tst_gutterframe)

#include "tst_gutterframe.moc"
