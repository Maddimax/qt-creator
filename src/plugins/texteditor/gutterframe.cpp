// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gutterframe.h"

#include "inlinediffdecorator.h"
#include "textdocumentlayout.h"
#include "texteditorconstants.h"
#include "textmark.h"

#include <utils/algorithm.h>
#include <utils/plaintextedit/texteditorlayout.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>

#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextLayout>

using namespace Utils;

namespace TextEditor {

void GutterFrame::addRect(const QRectF &rect, ColorRole color)
{
    m_primitives.append(Rect{rect, color});
}

void GutterFrame::addLine(const QLine &line, int penWidth, ColorRole color)
{
    m_primitives.append(Line{line, penWidth, color});
}

void GutterFrame::addText(const QString &text, const QRectF &rect, int alignment, FontRole font,
                          ColorRole color, int pixelSize)
{
    m_primitives.append(TextRun{text, rect, alignment, font, color, pixelSize});
}

void GutterFrame::addIcon(const QRect &rect, quint64 cacheKey, const IconPainter &paint)
{
    m_primitives.append(Icon{rect, cacheKey, paint});
}

void GutterFrame::addFoldMarker(const QRect &rect, bool expanded, bool active, bool hovered)
{
    m_primitives.append(FoldMarker{rect, expanded, active, hovered});
}

GutterFrameBuilder::GutterFrameBuilder(const Inputs &inputs)
    : m_in(inputs)
{}

void GutterFrameBuilder::addBackground(const QRectF &rect)
{
    m_frame.addRect(rect, GutterFrame::ColorRole::Background);
}

void GutterFrameBuilder::addBlock(const QTextBlock &block, const QRectF &blockBoundingRect)
{
    addLineNumber(block, blockBoundingRect);
    addTextMarks(block, blockBoundingRect);
    addCodeFolding(block, blockBoundingRect);
    addRevisionMarker(block, blockBoundingRect);
    addDiffChangeSigns(block, blockBoundingRect);
}

int GutterFrameBuilder::mainLayoutOffset(const QTextBlock &block) const
{
    return m_in.editorLayout ? m_in.editorLayout->mainLayoutOffset(block) : 0;
}

void GutterFrameBuilder::addLineNumber(const QTextBlock &block, const QRectF &blockBoundingRect)
{
    if (!m_in.lineNumbersVisible)
        return;

    QTC_ASSERT(m_in.lineNumberString, return);
    const QString number = m_in.lineNumberString(block.blockNumber());
    const bool selected = (
                (m_in.selectionStart < block.position() + block.length()
                 && m_in.selectionEnd > block.position())
                || (m_in.selectionStart == m_in.selectionEnd
                    && m_in.selectionEnd == block.position())
                );
    if (selected && m_in.currentLineNumberHasBackground) {
        m_frame.addRect(QRectF(0, blockBoundingRect.top(),
                               m_in.extraAreaWidth, blockBoundingRect.height()),
                        GutterFrame::ColorRole::CurrentLineNumberBackground);
    }

    QRectF rect;
    rect.setX(m_in.markWidth);
    rect.setY(blockBoundingRect.top() + mainLayoutOffset(block));
    // leave room on the right for the +/- diff sign column, when present
    rect.setWidth(m_in.extraAreaWidth - m_in.markWidth - m_in.signWidth - 4);
    rect.setHeight(blockBoundingRect.height());
    m_frame.addText(number, rect, Qt::AlignRight,
                    selected ? GutterFrame::FontRole::CurrentLineNumber
                             : GutterFrame::FontRole::Default,
                    selected ? GutterFrame::ColorRole::CurrentLineNumber
                             : GutterFrame::ColorRole::LineNumber);
}

void GutterFrameBuilder::addTextMarks(const QTextBlock &block, const QRectF &blockBoundingRect)
{
    auto userData = static_cast<TextBlockUserData *>(block.userData());
    if (!userData || !m_in.marksVisible)
        return;
    TextMarks marks = userData->marks();
    QList<QIcon> icons;
    auto end = marks.crend();
    int marksWithIconCount = 0;
    QIcon overrideIcon;
    for (auto it = marks.crbegin(); it != end; ++it) {
        if ((*it)->isVisible()) {
            const QIcon icon = (*it)->icon();
            if (!icon.isNull()) {
                if ((*it)->isLocationMarker()) {
                    overrideIcon = icon;
                } else {
                    if (icons.size() < 3
                            && !Utils::contains(icons, Utils::equal(&QIcon::cacheKey, icon.cacheKey()))) {
                        icons << icon;
                    }
                    ++marksWithIconCount;
                }
            }
        }
    }

    int size = m_in.lineSpacing - 1;
    int xoffset = 0;
    // marks belong to the block's text, which starts below additional layout
    // items like inline diff ghost rows
    int yoffset = blockBoundingRect.top() + mainLayoutOffset(block);

    const auto addIcon = [this](const QRect &rect, const QIcon &icon) {
        m_frame.addIcon(rect, icon.cacheKey(), [icon](QPainter &painter, const QRect &r) {
            icon.paint(&painter, r, Qt::AlignCenter);
        });
    };
    // the location marker covers the small mark icons, at their original size
    const int overrideSize = size;
    const int overrideYOffset = yoffset;
    const auto addOverrideIcon = [&] {
        if (!overrideIcon.isNull())
            addIcon(QRect(0, overrideYOffset, overrideSize, overrideSize), overrideIcon);
    };

    if (icons.isEmpty()) {
        addOverrideIcon();
        return;
    }

    if (icons.size() == 1) {
        addIcon(QRect(xoffset, yoffset, size, size), icons.first());
        addOverrideIcon();
        return;
    }
    size = size / 2;
    for (const QIcon &icon : std::as_const(icons)) {
        addIcon(QRect(xoffset, yoffset, size, size), icon);
        if (xoffset != 0) {
            yoffset += size;
            xoffset = 0;
        } else {
            xoffset = size;
        }
    }

    const QRect r(size, blockBoundingRect.top() + size, size, size);
    const QString detail = marksWithIconCount > 9 ? QString("+")
                                                  : QString::number(marksWithIconCount);
    m_frame.addText(detail, QRectF(r), Qt::AlignRight, GutterFrame::FontRole::MarkCount,
                    m_in.currentLineNumberHasForeground
                        ? GutterFrame::ColorRole::CurrentLineNumber
                        : GutterFrame::ColorRole::LineNumber,
                    size);
    addOverrideIcon();
}

void GutterFrameBuilder::addCodeFolding(const QTextBlock &block, const QRectF &blockBoundingRect)
{
    if (!m_in.codeFoldingVisible)
        return;

    const QTextBlock &nextBlock = block.next();

    bool drawBox = TextBlockUserData::foldingIndent(block)
                   < TextBlockUserData::foldingIndent(nextBlock);
    if (drawBox) {
        qCDebug(Internal::foldingLog) << "need to paint folding marker";
        qCDebug(Internal::foldingLog) << "folding indent for line" << (block.blockNumber() + 1)
                                      << "is" << TextBlockUserData::foldingIndent(block);
        qCDebug(Internal::foldingLog) << "folding indent for line" << (nextBlock.blockNumber() + 1)
                                      << "is" << TextBlockUserData::foldingIndent(nextBlock);
    }

    const int blockNumber = block.blockNumber();
    bool active = blockNumber == m_in.highlightFoldStart;
    bool hovered = blockNumber >= m_in.highlightFoldStart && blockNumber <= m_in.highlightFoldEnd;

    const int boxWidth = m_in.foldBoxWidth;

    // additional layout items rendered above the block do not belong to the
    // foldable text
    const int offset = mainLayoutOffset(block);

    if (hovered) {
        int itop = qRound(blockBoundingRect.top()) + offset;
        int ibottom = qRound(blockBoundingRect.bottom());
        QRect box = QRect(m_in.extraAreaWidth + 1, itop, boxWidth - 2, ibottom - itop);
        m_frame.addRect(QRectF(box), GutterFrame::ColorRole::FoldingHighlight);
    }

    if (drawBox) {
        bool expanded = nextBlock.isVisible();
        int size = boxWidth / 4;
        QRect box(m_in.extraAreaWidth + size,
                  int(blockBoundingRect.top()) + offset + size,
                  2 * (size) + 1, 2 * (size) + 1);
        m_frame.addFoldMarker(box, expanded, active, hovered);
    }
}

void GutterFrameBuilder::addRevisionMarker(const QTextBlock &block, const QRectF &blockBoundingRect)
{
    if (m_in.revisionsVisible && block.revision() != m_in.lastSaveRevision) {
        // the revision concerns the block's text, not the additional layout
        // items rendered above (mainLayoutOffset) or below it (spacers padding
        // the block to align with a side by side counterpart)
        const int mainOffset = mainLayoutOffset(block);
        const int appended = m_in.editorLayout
                ? m_in.editorLayout->additionalBlockHeight(block, true) - mainOffset
                : 0;
        m_frame.addLine(QLine(m_in.extraAreaWidth - 1,
                              int(blockBoundingRect.top()) + mainOffset,
                              m_in.extraAreaWidth - 1,
                              int(blockBoundingRect.bottom()) - appended - 1),
                        2,
                        block.revision() < 0 ? GutterFrame::ColorRole::RevisionReverted
                                             : GutterFrame::ColorRole::RevisionUnsaved);
    }
}

void GutterFrameBuilder::addDiffChangeSigns(const QTextBlock &block,
                                            const QRectF &blockBoundingRect)
{
    if (m_in.signWidth == 0)
        return;
    TextEditorLayout *layout = m_in.editorLayout;
    if (!layout)
        return;

    // inset by the left padding and left-align, so the wider padding towards
    // the text stays to the right of the glyph
    const qreal x = m_in.extraAreaWidth - m_in.signWidth + StyleHelper::SpacingTokens::PaddingHXs;
    const qreal width = m_in.signWidth - StyleHelper::SpacingTokens::PaddingHXs;
    const auto addSign = [&](QChar sign, qreal top, qreal height) {
        m_frame.addText(QString(sign), QRectF(x, top, width, height),
                        Qt::AlignLeft | Qt::AlignVCenter, GutterFrame::FontRole::Default,
                        GutterFrame::ColorRole::LineNumber);
    };

    // '-' next to each removed line shown as a ghost row. Ghost and spacer
    // items sit above the main line (or below it for the last block), so walk
    // the block's layout items in paint order and mark the ghost rows only.
    if (m_in.diffHasRemovedRows) {
        qreal top = blockBoundingRect.top();
        const QList<Utils::LayoutItem *> items = layout->layoutItems(block);
        for (Utils::LayoutItem *item : items) {
            if (item->category() == inlineDiffGhostCategory()) {
                if (auto *textItem = dynamic_cast<Utils::TextLayoutItem *>(item)) {
                    if (QTextLayout *ghostLayout = textItem->layout()) {
                        for (int i = 0; i < ghostLayout->lineCount(); ++i) {
                            const QTextLine line = ghostLayout->lineAt(i);
                            addSign(u'-', top + line.y(), line.height());
                        }
                    }
                }
            }
            top += item->height();
        }
    }

    // '+' (or '-' on the baseline side) next to the block's own changed line
    const QChar mainSign = m_in.diffChangeSigns.value(block.blockNumber());
    if (!mainSign.isNull()) {
        addSign(mainSign,
                blockBoundingRect.top() + layout->mainLayoutOffset(block),
                m_in.lineSpacing);
    }
}

void paintGutterFrame(QPainter &painter,
                      const GutterFrame &frame,
                      const QPalette &palette,
                      const QTextCharFormat &currentLineNumberFormat,
                      const FoldMarkerPainter &foldMarkerPainter)
{
    const QFont baseFont = painter.font();

    const auto roleColor = [&](GutterFrame::ColorRole role) -> QColor {
        switch (role) {
        case GutterFrame::ColorRole::Background:
            return palette.color(QPalette::Window);
        case GutterFrame::ColorRole::LineNumber:
            return palette.color(QPalette::Dark);
        case GutterFrame::ColorRole::CurrentLineNumber:
            return currentLineNumberFormat.foreground().color();
        case GutterFrame::ColorRole::CurrentLineNumberBackground:
            return currentLineNumberFormat.background().color();
        case GutterFrame::ColorRole::FoldingHighlight:
            return palette.color(QPalette::Highlight);
        case GutterFrame::ColorRole::RevisionUnsaved:
            return QColor(Qt::red);
        case GutterFrame::ColorRole::RevisionReverted:
            return QColor(Qt::darkGreen);
        }
        return {};
    };

    painter.setPen(palette.color(QPalette::Dark));

    for (const GutterFrame::Primitive &primitive : frame.primitives()) {
        if (const auto *rect = std::get_if<GutterFrame::Rect>(&primitive)) {
            if (rect->color == GutterFrame::ColorRole::FoldingHighlight) {
                painter.save();
                painter.setOpacity(0.5);
                painter.fillRect(rect->rect, palette.brush(QPalette::Highlight));
                painter.restore();
            } else {
                painter.fillRect(rect->rect, roleColor(rect->color));
            }
        } else if (const auto *line = std::get_if<GutterFrame::Line>(&primitive)) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(roleColor(line->color), line->penWidth));
            painter.drawLine(line->line);
            painter.restore();
        } else if (const auto *text = std::get_if<GutterFrame::TextRun>(&primitive)) {
            painter.save();
            if (text->font == GutterFrame::FontRole::CurrentLineNumber) {
                QFont f = baseFont;
                f.setBold(currentLineNumberFormat.font().bold());
                f.setItalic(currentLineNumberFormat.font().italic());
                painter.setFont(f);
            } else if (text->font == GutterFrame::FontRole::MarkCount) {
                QFont f = baseFont;
                f.setPixelSize(text->pixelSize);
                painter.setFont(f);
            }
            painter.setPen(roleColor(text->color));
            painter.drawText(text->rect, text->alignment, text->text);
            painter.restore();
        } else if (const auto *icon = std::get_if<GutterFrame::Icon>(&primitive)) {
            if (icon->paint) {
                painter.save();
                icon->paint(painter, icon->rect);
                painter.restore();
            }
        } else if (const auto *marker = std::get_if<GutterFrame::FoldMarker>(&primitive)) {
            if (foldMarkerPainter) {
                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, false);
                foldMarkerPainter(painter, marker->rect, marker->expanded, marker->active,
                                  marker->hovered);
                painter.restore();
            }
        }
    }
}

} // namespace TextEditor
