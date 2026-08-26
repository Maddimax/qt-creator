// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport.h"

#include "codedocument.h"
#include "fontsettings.h"
#include "tabsettings.h"
#include "textdocument.h"
#include "texteditorconstants.h"

#include <utils/theme/theme.h>

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace TextEditor {

TextViewport::Line::Line() = default;
TextViewport::Line::~Line() = default;
TextViewport::Line::Line(Line &&other) noexcept = default;
TextViewport::Line &TextViewport::Line::operator=(Line &&other) noexcept = default;

TextViewport::TextViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setClip(true);
}

TextViewport::~TextViewport() = default;

CodeDocument *TextViewport::document() const
{
    return m_document;
}

void TextViewport::setDocument(CodeDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        m_document->disconnect(this);

    m_document = document;

    if (m_document) {
        // What is shown has to follow what the file says, and a file that is
        // still opening says nothing yet.
        connect(m_document, &CodeDocument::openedChanged,
                this, &TextViewport::documentChangedInternal);
        connect(m_document, &CodeDocument::modifiedChanged,
                this, &TextViewport::documentChangedInternal);
    }

    documentChangedInternal();
    emit documentChanged();
}

qreal TextViewport::scrollY() const
{
    return m_scrollY;
}

void TextViewport::setScrollY(qreal scrollY)
{
    // Held inside the document: scrolling past the end shows nothing, and a
    // negative offset shows nothing twice.
    const qreal clamped = qBound(0.0, scrollY, qMax(0.0, m_contentHeight - height()));
    if (qFuzzyCompare(m_scrollY + 1, clamped + 1))
        return;
    m_scrollY = clamped;
    polish();
    emit scrollYChanged();
}

qreal TextViewport::scrollX() const
{
    return m_scrollX;
}

void TextViewport::setScrollX(qreal scrollX)
{
    const qreal clamped = qMax(0.0, scrollX);
    if (qFuzzyCompare(m_scrollX + 1, clamped + 1))
        return;
    m_scrollX = clamped;
    polish();
    emit scrollXChanged();
}

qreal TextViewport::contentHeight() const
{
    return m_contentHeight;
}

qreal TextViewport::lineHeight() const
{
    return m_lineHeight;
}

QColor TextViewport::backgroundColor() const
{
    return m_background;
}

int TextViewport::firstVisibleLine() const
{
    return m_firstVisibleLine;
}

int TextViewport::visibleLineCount() const
{
    return int(m_lines.size());
}

int TextViewport::selectionStart() const
{
    return m_selectionStart;
}

void TextViewport::setSelectionStart(int position)
{
    if (m_selectionStart == position)
        return;
    m_selectionStart = position;
    polish();
    emit selectionChanged();
}

int TextViewport::selectionEnd() const
{
    return m_selectionEnd;
}

void TextViewport::setSelectionEnd(int position)
{
    if (m_selectionEnd == position)
        return;
    m_selectionEnd = position;
    polish();
    emit selectionChanged();
}

QVariantMap TextViewport::visibleLine(int index) const
{
    if (index < 0 || index >= int(m_lines.size()))
        return {};

    const Line &line = m_lines.at(index);
    QVariantList formats;
    const QList<QTextLayout::FormatRange> ranges = line.layout->formats();
    for (const QTextLayout::FormatRange &range : ranges) {
        formats.append(QVariantMap{{"start", range.start},
                                   {"length", range.length},
                                   {"background", range.format.background().color()}});
    }
    return QVariantMap{{"text", line.layout->text()},
                       {"formats", formats},
                       {"newlineTail", line.newlineTail}};
}

TextViewport::Located TextViewport::locate(int position) const
{
    for (int i = 0; i < int(m_lines.size()); ++i) {
        const Line &line = m_lines.at(i);
        // blockLength counts the newline, and the caret is allowed to sit on
        // it: that is the position at the end of the line.
        if (position >= line.blockPosition && position < line.blockPosition + line.blockLength)
            return {i, position - line.blockPosition};
    }
    return {};
}

QRectF TextViewport::cursorRectangle(int position) const
{
    const Located found = locate(position);
    if (found.index < 0)
        return {};

    const Line &line = m_lines.at(found.index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return {};

    int offset = found.offsetInLine;
    const qreal x = textLine.cursorToX(&offset);
    return QRectF(line.at.x() + x, line.at.y(), 1, m_lineHeight);
}

int TextViewport::positionAt(qreal x, qreal y) const
{
    if (m_lines.empty() || m_lineHeight <= 0)
        return -1;

    // Clamped to what is laid out rather than to the document: a drag that
    // leaves the viewport should select to the edge of it, not jump to the end
    // of the file.
    const int index = qBound(0, int((y + m_scrollY) / m_lineHeight) - m_firstVisibleLine,
                             int(m_lines.size()) - 1);
    const Line &line = m_lines.at(index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return line.blockPosition;

    return line.blockPosition + textLine.xToCursor(x - line.at.x());
}

void TextViewport::documentChangedInternal()
{
    polish();
    update();
}

void TextViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

void TextViewport::updatePolish()
{
    m_lines.clear();

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text || height() <= 0) {
        m_lineHeight = 0;
        m_contentHeight = 0;
        m_firstVisibleLine = 0;
        emit metricsChanged();
        update();
        return;
    }

    const FontSettingsData &fonts = doc->fontSettings();
    QFont font = fonts.font();
    // font() is the unzoomed font and lineSpacing() is already zoomed, so a
    // font taken straight from font() draws glyphs of one size on lines of
    // another. Zoom it the way lineSpacing() does.
    font.setPointSize(std::max(fonts.fontSize() * fonts.fontZoom() / 100, 1));
    const QFontMetricsF metrics(font);
    // The same height for every line is what makes the line at a scroll offset
    // arithmetic rather than a walk, which is what holds at a million lines.
    // lineSpacing() is what the widget editor lays out with, in pixels, so the
    // two backends put a line in the same place.
    const qreal previousLineHeight = m_lineHeight;
    m_lineHeight = qMax(1.0, fonts.lineSpacing());
    m_contentHeight = m_lineHeight * text->blockCount();
    // The widget editor fills with the *brush*, so a scheme that sets no
    // background paints nothing and the palette shows through. A colour has no
    // way to say "nothing", and QBrush().color() is black, so ask the theme
    // instead of painting the fallback black.
    const QBrush backgroundBrush = fonts.toTextCharFormat(C_TEXT).background();
    m_background = backgroundBrush.style() == Qt::NoBrush
                       ? Utils::creatorColor(Utils::Theme::BackgroundColorNormal)
                       : backgroundBrush.color();

    const QTextCharFormat selectionFormat = fonts.toTextCharFormat(C_SELECTION);
    const int selectionFrom = qMin(m_selectionStart, m_selectionEnd);
    const int selectionTo = qMax(m_selectionStart, m_selectionEnd);
    const bool hasSelection = m_selectionStart >= 0 && m_selectionEnd >= 0
                              && selectionFrom != selectionTo;

    QTextOption option;
    option.setTabStopDistance(doc->tabSettings().m_tabSize * metrics.horizontalAdvance(' '));
    // No wrapping: see the class comment. A wrapped line breaks the arithmetic
    // above, not just the layout.
    option.setWrapMode(QTextOption::NoWrap);

    m_firstVisibleLine = qMax(0, int(m_scrollY / m_lineHeight));
    // The last line that starts before the bottom edge - not one more. A hair
    // off the edge so that a viewport an exact number of lines tall does not
    // lay out one that begins where it ends.
    const int lastOnScreen = int((m_scrollY + height() - 0.001) / m_lineHeight);
    const int last = qMin(text->blockCount() - 1, lastOnScreen);

    QTextBlock block = text->findBlockByNumber(m_firstVisibleLine);
    for (int number = m_firstVisibleLine; number <= last && block.isValid();
         ++number, block = block.next()) {
        Line line;
        line.layout = std::make_unique<QTextLayout>(block.text(), font);
        line.layout->setTextOption(option);
        line.layout->setCacheEnabled(true);

        // What the highlighter said about this block, plus the selection over
        // the top of it. Read here, on the GUI thread: the block's own layout
        // belongs to the document and must not be reached from the render one.
        QList<QTextLayout::FormatRange> formats = block.layout()->formats();
        if (hasSelection) {
            const int blockStart = block.position();
            const int from = qMax(0, selectionFrom - blockStart);
            const int to = qMin(block.length() - 1, selectionTo - blockStart);
            if (to > from) {
                QTextLayout::FormatRange range;
                range.start = from;
                range.length = to - from;
                range.format = selectionFormat;
                formats.append(range);
            }
        }
        line.layout->setFormats(formats);

        line.layout->beginLayout();
        QTextLine textLine = line.layout->createLine();
        if (textLine.isValid()) {
            textLine.setPosition(QPointF(0, 0));
            textLine.setLineWidth(std::numeric_limits<qreal>::max());
        }
        line.layout->endLayout();

        line.at = QPointF(-m_scrollX, number * m_lineHeight - m_scrollY);
        line.blockPosition = block.position();
        line.blockLength = block.length();

        // The one thing a format range cannot cover: a selection that runs past
        // the end of the line covers the newline too, and there is no character
        // there to format. One rect, a quarter of a line wide, which is what
        // QTextLayout::draw() does for the same case.
        const int blockEnd = block.position() + block.length() - 1;
        if (hasSelection && selectionFrom <= blockEnd && selectionTo > blockEnd
            && textLine.isValid()) {
            const qreal right = textLine.naturalTextRect().right();
            line.newlineTail = QRectF(right, 0, m_lineHeight / 4, m_lineHeight);
            line.newlineTailColour = selectionFormat.background().color();
        }

        m_lines.push_back(std::move(line));
    }

    if (!qFuzzyCompare(previousLineHeight + 1, m_lineHeight + 1))
        setScrollY(m_scrollY); // re-clamp: the document is a different height now
    emit metricsChanged();
    update();
}

QSGNode *TextViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Nothing here reads the document. Everything it draws was decided in
    // updatePolish() on the GUI thread.
    delete oldNode;
    if (m_lines.empty())
        return nullptr;

    QQuickWindow * const win = window();
    if (!win)
        return nullptr;

    auto *root = new QSGNode;
    for (const Line &line : m_lines) {
        if (!line.newlineTail.isEmpty()) {
            QSGRectangleNode * const tail = win->createRectangleNode();
            tail->setRect(line.newlineTail.translated(line.at));
            tail->setColor(line.newlineTailColour);
            root->appendChildNode(tail);
        }
        QSGTextNode * const node = win->createTextNode();
        node->setRenderType(QSGTextNode::NativeRendering);
        node->addTextLayout(line.at, line.layout.get());
        root->appendChildNode(node);
    }
    return root;
}

} // namespace TextEditor
