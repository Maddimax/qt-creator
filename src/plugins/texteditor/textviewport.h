// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QColor>
#include <QTextLayout>
#include <QPointer>
#include <QQmlEngine>
#include <QFont>
#include <QQuickItem>
#include <QRectF>
#include <QVariantMap>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QInputMethodEvent;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace TextEditor {

class CodeSource;
class SyntaxHighlighter;
class TextDocument;

// A Qt Quick view of a TextEditor::TextDocument drawn with the scene graph: one
// QSGTextNode per visible line, re-emitted every frame. This is the heavy path,
// the one a real editor needs - as opposed to CodeDocument on a TextEdit, which
// is a QTextDocument shown by a widget-shaped control and does not scale.
//
// Three rules from the spike (see "The QSGTextNode spike" in the migration
// design doc), each measured rather than reasoned about:
//
//  - Lay out on the GUI thread in updatePolish() and never in
//    updatePaintNode(). Creator's document layout is QObject-based, and laying
//    out on the render thread produced exactly the cross-thread QObject and
//    QTimer failures the plan predicted.
//  - Re-emit every visible line every frame. A per-line node cache saves 0.55 ms
//    of render thread at 1M lines and is not needed to hold vsync, so it is not
//    here to go stale.
//  - Merge selection foreground *and* background into QTextLayout::formats().
//    Emitting selection backgrounds as scene-graph rectangles punches
//    unhighlighted holes at tabs and at BiDi boundaries, and the geometry that
//    would fix it is private API. The one thing formats cannot cover is the
//    newline tail, which is a single rect.
//
// Cost is O(visible), not O(file), which is what makes a million lines the same
// price as ten thousand. That holds only while every line is the same height,
// so this does not wrap: a wrapped view needs a height cache before it can say
// which line is at a given scroll offset without laying out the ones above.
class TEXTEDITOR_EXPORT TextViewport : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // What is being shown. A CodeSource rather than a TextDocument because the
    // source owns the document and replaces it - a file that reopens is a
    // different QTextDocument - and rather than a CodeDocument because a
    // preview's text was never a file. See CodeDocument and CodeBuffer.
    Q_PROPERTY(TextEditor::CodeSource *document READ document WRITE setDocument
                   NOTIFY documentChanged)
    // How far into the document the viewport is, in pixels.
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollYChanged)
    Q_PROPERTY(qreal scrollX READ scrollX WRITE setScrollX NOTIFY scrollXChanged)
    // The whole document's height, so that a ScrollBar has something to size
    // itself against without knowing what a line is.
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY metricsChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight NOTIFY metricsChanged)
    // The theme's editor background. The viewport does not paint it - a QML
    // Rectangle behind it does - but the colour lives in FontSettings, which
    // QML cannot reach on its own.
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY metricsChanged)
    // The colour behind the line the caret is on. Transparent when the scheme
    // sets none, because a scheme that asks for no highlight must not get an
    // opaque one - see backgroundColor for the same trap.
    Q_PROPERTY(QColor currentLineColor READ currentLineColor NOTIFY metricsChanged)
    // What is on screen. Readable so that a test can say what was drawn without
    // reading the scene graph.
    // How many lines the document has, which a gutter needs to know how wide
    // to be before it has drawn anything.
    // The font the text is drawn with, zoom applied. A gutter has to measure
    // its numbers in the same font or its rows do not line up with the text.
    Q_PROPERTY(QFont font READ font NOTIFY metricsChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY metricsChanged)
    Q_PROPERTY(int firstVisibleLine READ firstVisibleLine NOTIFY metricsChanged)
    Q_PROPERTY(int visibleLineCount READ visibleLineCount NOTIFY metricsChanged)
    // The selection, as positions in the document. Both -1 for none.
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart
                   NOTIFY selectionChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionChanged)
    // Where the caret is, and where that lands on screen. The rect is a
    // property rather than only a call because QML has to *bind* to it: it
    // moves when the document is scrolled or relaid out, not just when the
    // position changes, and a binding on rectangleAt() would not notice.
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition
                   NOTIFY cursorPositionChanged)
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorRectangleChanged)
    // Whether typing does anything. A viewport is a view until told otherwise,
    // so that showing a file cannot accidentally change it.
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly NOTIFY readOnlyChanged)

public:
    explicit TextViewport(QQuickItem *parent = nullptr);
    ~TextViewport() override;

    CodeSource *document() const;
    void setDocument(CodeSource *document);

    qreal scrollY() const;
    void setScrollY(qreal scrollY);
    qreal scrollX() const;
    void setScrollX(qreal scrollX);

    qreal contentHeight() const;
    qreal lineHeight() const;
    QColor backgroundColor() const;
    int firstVisibleLine() const;
    QColor currentLineColor() const;
    QFont font() const;
    int lineCount() const;
    int visibleLineCount() const;

    int selectionStart() const;
    void setSelectionStart(int position);
    int selectionEnd() const;
    void setSelectionEnd(int position);

    // What the visible line at \a index was laid out with: its "text", its
    // "formats" as a list of {start, length} maps, its "newlineTail" rect, the
    // "preedit" being composed on it and the "width" it came to.
    // A selection is merged into the layout's formats rather than drawn as
    // anything of its own - that is the whole conclusion of the spike - so this
    // is the only way to see that it arrived. Empty for an index that is not on
    // screen.
    Q_INVOKABLE QVariantMap visibleLine(int index) const;

    // The two halves of the mapping between the document and the screen, which
    // is what a caret and a mouse need and what nothing above the scene graph
    // can work out for itself.
    //
    // rectangleAt() is where the caret goes for a document position, in item
    // coordinates; empty for a position that is not on screen. QML draws the
    // caret, the same way it draws the background - a blinking Rectangle is not
    // the scene graph's problem.
    //
    // positionAt() is the document position under an item coordinate, for a
    // click. Held inside what is laid out, so a drag that leaves the viewport
    // selects to the edge of it rather than jumping to the end of the file;
    // -1 only when nothing is laid out at all.
    Q_INVOKABLE QRectF rectangleAt(int position) const;
    Q_INVOKABLE int positionAt(qreal x, qreal y) const;

    int cursorPosition() const;
    void setCursorPosition(int position);
    QRectF cursorRectangle() const;

    bool isReadOnly() const;
    void setReadOnly(bool readOnly);

signals:
    void documentChanged();
    void scrollYChanged();
    void scrollXChanged();
    void metricsChanged();
    void selectionChanged();
    void cursorPositionChanged();
    void cursorRectangleChanged();
    void readOnlyChanged();

protected:
    void updatePolish() override;
    void keyPressEvent(QKeyEvent *event) override;
    // Composing text - a dead key, a CJK input method - is shown before it is
    // committed, and until it is committed it is not in the document. A
    // QTextLayout has a place for exactly that, so the viewport does not have
    // to invent one.
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // One visible line, ready to hand to the render thread. It owns its layout:
    // the document's own layouts belong to the GUI thread and to a QObject-based
    // document layout, and nothing here may reach back into either.
    struct Line
    {
        Line();
        ~Line();
        Line(Line &&other) noexcept;
        Line &operator=(Line &&other) noexcept;

        std::unique_ptr<QTextLayout> layout;
        QPointF at;
        // Where this line's text starts in the document, so that a screen
        // coordinate can be turned back into a document position.
        int blockPosition = 0;
        int blockLength = 0;
        // The highest-priority visible mark on this line, as a URL a QML Image
        // can load and the text it explains itself with. Empty when the line
        // carries none.
        QString markIcon;
        QString markToolTip;
        // Where a selection runs past the end of the line. Empty otherwise.
        QRectF newlineTail;
        QColor newlineTailColour;
    };

    void documentChangedInternal();
    // The visible line holding \a position, and the block it came from.
    // index is -1 when the position is not on screen.
    struct Located
    {
        int index = -1;
        int offsetInLine = 0;
    };
    Located locate(int position) const;

    // The document cursor this viewport's caret and selection stand for. Edits
    // go through it so that the document's own undo stack, its layout and the
    // marks on it all see the change - writing the text out and back would
    // lose every one of them.
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor &cursor);
    // Keeps the caret on screen after it has been moved by something other than
    // the mouse.
    void ensureCursorVisible();

    QPointer<CodeSource> m_document;
    // The QTextDocument currently connected to, which is not the same one
    // across a reopen.
    QPointer<QTextDocument> m_connectedDocument;
    // The highlighter currently connected to, for the same reason.
    QPointer<SyntaxHighlighter> m_connectedHighlighter;
    qreal m_scrollY = 0;
    qreal m_scrollX = 0;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    int m_cursorPosition = 0;
    bool m_readOnly = true;
    // What is being composed but not yet typed, and how the input method wants
    // it drawn. Empty when nothing is being composed.
    QString m_preeditText;
    QList<QTextLayout::FormatRange> m_preeditFormats;

    // Everything below is produced in updatePolish() on the GUI thread and read
    // in updatePaintNode() on the render thread, with the GUI thread blocked.
    std::vector<Line> m_lines;
    qreal m_lineHeight = 0;
    qreal m_contentHeight = 0;
    QPointer<TextDocument> m_connectedMarkSource;
    QColor m_currentLine = Qt::transparent;
    QFont m_font;
    int m_lineCount = 0;
    int m_firstVisibleLine = 0;
    QColor m_background;
};

} // namespace TextEditor
