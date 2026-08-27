// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QColor>
#include <QHash>
#include <QLine>
#include <QList>
#include <QRectF>
#include <QString>

#include <functional>
#include <variant>

QT_BEGIN_NAMESPACE
class QPainter;
class QPalette;
class QTextBlock;
class QTextCharFormat;
QT_END_NAMESPACE

namespace Utils { class TextEditorLayout; }

namespace TextEditor {

// A backend-neutral description of one gutter (extra area) repaint, in
// viewport-local coordinates and paint order. It holds no live document
// state; blocks are resolved to geometry while building. Two primitives stay
// opaque on purpose: icons carry a painter callback because mark icon
// painting (TextMark::paintIcon) is virtual API overridden out of tree, and
// fold markers carry only their state so each backend draws its own
// affordance.
class TEXTEDITOR_EXPORT GutterFrame
{
public:
    enum class ColorRole {
        Background,                  // QPalette::Window
        LineNumber,                  // QPalette::Dark
        CurrentLineNumber,           // C_CURRENT_LINE_NUMBER foreground
        CurrentLineNumberBackground, // C_CURRENT_LINE_NUMBER background
        FoldingHighlight,            // QPalette::Highlight at half opacity
        RevisionUnsaved,
        RevisionReverted,
    };

    enum class FontRole {
        Default,           // the gutter font
        CurrentLineNumber, // gutter font with C_CURRENT_LINE_NUMBER bold/italic
        MarkCount,         // gutter font at TextRun::pixelSize
    };

    using IconPainter = std::function<void(QPainter &, const QRect &)>;

    struct Rect
    {
        QRectF rect;
        ColorRole color;
    };

    struct Line
    {
        QLine line;
        int penWidth;
        ColorRole color;
    };

    struct TextRun
    {
        QString text;
        QRectF rect;
        int alignment; // Qt::Alignment flags
        FontRole font;
        ColorRole color;
        int pixelSize; // FontRole::MarkCount only
    };

    struct Icon
    {
        QRect rect;
        quint64 cacheKey; // stable identity for texture caching, 0 if none
        IconPainter paint;
    };

    struct FoldMarker
    {
        QRect rect;
        bool expanded;
        bool active;
        bool hovered;
    };

    using Primitive = std::variant<Rect, Line, TextRun, Icon, FoldMarker>;

    void addRect(const QRectF &rect, ColorRole color);
    void addLine(const QLine &line, int penWidth, ColorRole color);
    void addText(const QString &text, const QRectF &rect, int alignment, FontRole font,
                 ColorRole color, int pixelSize = -1);
    void addIcon(const QRect &rect, quint64 cacheKey, const IconPainter &paint);
    void addFoldMarker(const QRect &rect, bool expanded, bool active, bool hovered);

    const QList<Primitive> &primitives() const { return m_primitives; }

private:
    QList<Primitive> m_primitives;
};

// Builds a GutterFrame from one walk over the visible blocks. All widget
// state is resolved into Inputs up front; per block only the block itself and
// the already-laid-out geometry are consulted.
class TEXTEDITOR_EXPORT GutterFrameBuilder
{
public:
    struct Inputs
    {
        bool lineNumbersVisible = false;
        bool marksVisible = false;
        bool codeFoldingVisible = false;
        bool revisionsVisible = false;
        int selectionStart = 0;
        int selectionEnd = 0;
        int lineSpacing = 0;
        int markWidth = 0;
        int signWidth = 0;
        int extraAreaWidth = 0;
        int foldBoxWidth = 0;
        bool currentLineNumberHasBackground = false;
        bool currentLineNumberHasForeground = false;
        int lastSaveRevision = 0;
        int highlightFoldStart = -1;
        int highlightFoldEnd = -1;
        bool diffHasRemovedRows = false;
        QHash<int, QChar> diffChangeSigns;
        Utils::TextEditorLayout *editorLayout = nullptr;
        std::function<QString(int blockNumber)> lineNumberString;
    };

    explicit GutterFrameBuilder(const Inputs &inputs);

    void addBackground(const QRectF &rect);
    void addBlock(const QTextBlock &block, const QRectF &blockBoundingRect);

    GutterFrame takeFrame() { return std::move(m_frame); }

private:
    void addLineNumber(const QTextBlock &block, const QRectF &blockBoundingRect);
    void addTextMarks(const QTextBlock &block, const QRectF &blockBoundingRect);
    void addCodeFolding(const QTextBlock &block, const QRectF &blockBoundingRect);
    void addRevisionMarker(const QTextBlock &block, const QRectF &blockBoundingRect);
    void addDiffChangeSigns(const QTextBlock &block, const QRectF &blockBoundingRect);

    int mainLayoutOffset(const QTextBlock &block) const;

    const Inputs m_in;
    GutterFrame m_frame;
};

// QPainter backend. The painter's font must be the gutter font; the fold
// marker callback draws one folding indicator (widget style dependent).
using FoldMarkerPainter
    = std::function<void(QPainter &, const QRect &, bool expanded, bool active, bool hovered)>;

// What the gutter marks a changed line in: red until the change is saved, and
// green where an edit was undone back to what is on disk. Named here rather
// than picked at each drawing site, so the two editors cannot drift apart.
// Constants rather than theme colours, which is what they have always been.
TEXTEDITOR_EXPORT QColor revisionUnsavedColor();
TEXTEDITOR_EXPORT QColor revisionRevertedColor();

TEXTEDITOR_EXPORT void paintGutterFrame(QPainter &painter,
                                        const GutterFrame &frame,
                                        const QPalette &palette,
                                        const QTextCharFormat &currentLineNumberFormat,
                                        const FoldMarkerPainter &foldMarkerPainter);

} // namespace TextEditor
