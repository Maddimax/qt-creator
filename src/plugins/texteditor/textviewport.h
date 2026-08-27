// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/id.h>

#include <QColor>
#include <QMap>
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

namespace Utils { class TextEditorLayout; }

namespace TextEditor {

class AutoCompleter;

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
// price as ten thousand - while every line is one row tall, which row sits at a
// scroll offset is arithmetic.
//
// Wrapping gives that up, which is why it is off by default: a wrapped line is
// several rows, and nothing can say where a block starts without laying out
// every block above it. Turned on, this pays that cost per relayout, taking the
// row index from a per-view Utils::TextEditorLayout. The widget editor makes
// the same trade for the same reason.
//
// Folding is the exception that costs nothing, because the document already
// pays for it: a folded block's line count is zero, so lineCount() is the
// height in lines and findBlockByLineNumber() says which block a row holds.
// Hence the distinction throughout between a *row* on screen and the *line*
// the document calls it - they are the same number only while nothing is
// folded, and code that assumes so is the bug folding leaves behind.
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
    // The font the text is drawn with, zoom applied. A gutter has to measure
    // its numbers in the same font or its rows do not line up with the text.
    Q_PROPERTY(QFont font READ font NOTIFY metricsChanged)
    // How many lines the document has, which a gutter needs to know how wide
    // to be before it has drawn anything. Every line, folded or not: the
    // gutter has to fit the highest number it can ever show.
    Q_PROPERTY(int lineCount READ lineCount NOTIFY metricsChanged)
    // The first row on screen, counting rows and not document lines - what is
    // folded away is not a row.
    Q_PROPERTY(int firstVisibleLine READ firstVisibleLine NOTIFY metricsChanged)
    // How many rows are laid out. Readable so that a test can say what was
    // drawn without reading the scene graph.
    Q_PROPERTY(int visibleLineCount READ visibleLineCount NOTIFY metricsChanged)
    // Everything on screen, one entry per line, as a property rather than
    // through visibleLine(): an invokable has nothing to notify on, so a
    // delegate that called it would show whatever was true when it was built
    // and never hear that a mark had arrived.
    Q_PROPERTY(QVariantList visibleLines READ visibleLines NOTIFY linesChanged)
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
    // Where the caret is in the terms an editor talks about it: line and
    // column, both counting from one, both of the document rather than of the
    // screen. What a status bar shows and what "copy path and line" copies.
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorPositionChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorPositionChanged)
    // The column as the reader counts it, with a tab spanning as many columns
    // as it is drawn wide. cursorColumn is the character offset, which is what
    // IEditor asks for and not what a status display should show.
    Q_PROPERTY(int cursorDisplayColumn READ cursorDisplayColumn NOTIFY cursorPositionChanged)
    // How much is selected, for a display that says so.
    Q_PROPERTY(int selectedCharacterCount READ selectedCharacterCount NOTIFY selectionChanged)
    // The file's line ending and encoding, as the toolbar shows them. The
    // document's business rather than this view's, surfaced here because the
    // form has a handle on the viewport and not on the document. Empty when
    // the display settings say not to show them, which is how the widget
    // editor hides its own - so the form has no rule of its own to get wrong.
    Q_PROPERTY(QString fileLineEnding READ fileLineEnding NOTIFY fileFormatChanged)
    Q_PROPERTY(QString fileEncoding READ fileEncoding NOTIFY fileFormatChanged)
    // Whether typing does anything. A viewport is a view until told otherwise,
    // so that showing a file cannot accidentally change it.
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    // Whether a line too long for the width is broken across rows.
    //
    // Off by default, and off is the cheap path: with every line exactly one
    // row tall, which row sits at a scroll offset is arithmetic, and that is
    // what makes a million lines cost the same as ten thousand. Wrapping needs
    // to know where every block starts, and nothing can know that without
    // laying out every block above - so turning this on trades the O(visible)
    // promise for O(file). The widget editor makes the same trade for the same
    // reason; see the migration doc.
    Q_PROPERTY(bool wrapping READ isWrapping WRITE setWrapping NOTIFY wrappingChanged)

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
    QVariantList visibleLines() const;

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

    // Folds or unfolds what \a lineNumber starts, counting from one the way
    // visibleLine() reports it. Does nothing for a line that starts no fold,
    // so a gutter may call it for whatever the user clicked.
    Q_INVOKABLE void toggleFold(int lineNumber);

    int cursorLine() const;
    int cursorColumn() const;
    int cursorDisplayColumn() const;
    int selectedCharacterCount() const;
    QString fileLineEnding() const;
    QString fileEncoding() const;

    // Switches the file between Unix and Windows line endings. The document
    // becomes modified: the change is only on disk once it is saved, which is
    // what the widget editor does too.
    Q_INVOKABLE void setFileLineEndingIsWindows(bool windows);

    // Puts the caret on \a line, counting from one, and shows it. Column zero
    // means the line rather than its margin, so the caret lands on the first
    // thing on it. Opens whatever folds were hiding the line: someone who
    // asked to go there asked to see it.
    Q_INVOKABLE void gotoLine(int line, int column = 0, bool centerLine = true);

    // A stretch of the document drawn differently from the text around it: a
    // search result, an error, the other places a symbol is used. The widget
    // editor calls these extra selections and keys them the same way, so that
    // whoever set a set can replace all of it without disturbing anyone
    // else's. QTextEdit::ExtraSelection is a QtWidgets type, which is what
    // this exists instead of.
    struct Highlight
    {
        int start = 0;
        int end = 0;
        QTextCharFormat format;

        // So that setting the same ranges again can do nothing, which is what
        // makes setHighlights() safe to call from anywhere - including from
        // inside a layout pass.
        bool operator==(const Highlight &other) const = default;
    };

    // Selecting by pointer rather than by caret: the word under a double
    // click, the line under a triple one.
    Q_INVOKABLE void selectWordAt(int position);
    Q_INVOKABLE void selectLineAt(int position);

    // Ctrl and the wheel. Zoom is a global setting rather than this view's, so
    // every editor grows together - which is what the widget editor does, and
    // what makes the two agree about how big the text is.
    Q_INVOKABLE void zoomBy(int steps);

    void setHighlights(Utils::Id kind, const QList<Highlight> &highlights);
    QList<Highlight> highlights(Utils::Id kind) const;

    // The document cursor this viewport's caret and selection stand for. Edits
    // go through it so that the document's own undo stack, its layout and the
    // marks on it all see the change - writing the text out and back would
    // lose every one of them.
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor &cursor);

    int cursorPosition() const;
    void setCursorPosition(int position);
    QRectF cursorRectangle() const;

    bool isReadOnly() const;
    bool isWrapping() const;
    void setWrapping(bool wrapping);
    void setReadOnly(bool readOnly);

signals:
    void documentChanged();
    void scrollYChanged();
    void scrollXChanged();
    void metricsChanged();
    void linesChanged();
    void selectionChanged();
    void cursorPositionChanged();
    void cursorRectangleChanged();
    void readOnlyChanged();
    void fileFormatChanged();
    void wrappingChanged();

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
        // What the gutter calls this line. Folding makes it run ahead of the
        // row the line is drawn on.
        int lineNumber = 0;
        // Whether this row is where its line starts. A wrapped line covers
        // several rows and is numbered, marked and annotated only on the
        // first - the continuation rows carry the same lineNumber but must
        // not be labelled with it again.
        bool firstRowOfLine = true;
        // Whether this line starts a fold, and whether that fold is closed.
        bool foldable = false;
        bool folded = false;
        // Which marker to draw, as a URL a QML Image can load. Empty when the
        // line starts no fold.
        QString foldIcon;
        // What stands in for the hidden text, drawn after the line. Empty
        // unless the fold is closed.
        QString foldReplacement;
        // The highest-priority visible mark on this line, as a URL a QML Image
        // can load and the text it explains itself with. Empty when the line
        // carries none.
        QString markIcon;
        // What the mark says about the line, drawn after the text and
        // shown again when the icon is hovered.
        QString annotation;
        // Where a selection runs past the end of the line. Empty otherwise.
        QRectF newlineTail;
        QColor newlineTailColour;
    };

    void documentChangedInternal();
    QTextBlock cursorBlock() const;
    // The bracket the caret is beside and the one it belongs to, drawn as
    // highlights. Recomputed whenever the caret moves, the text changes, or
    // the highlighter - which is what records where the brackets are -
    // finishes another pass.
    void updateParenthesesMatch();
    // The highlighter is replaced on a document that is already being shown -
    // the editor installs one once it knows the file's language - and nothing
    // announces that, so this looks each time round.
    void connectHighlighter(TextDocument *doc);
    void applyCompletionSettings();
    // The per-view index of which row each block starts on, made when wrapping
    // first needs one. A companion to the document's own layout rather than a
    // replacement: the document keeps its TextDocumentLayout, and this answers
    // for *this* view's width.
    Utils::TextEditorLayout *editorLayout();
    void applyGlobalFontSettings();
    void appendHighlights(QList<QTextLayout::FormatRange> &formats,
                          const QTextBlock &block) const;
    // The visible line holding \a position, and the block it came from.
    // index is -1 when the position is not on screen.
    struct Located
    {
        int index = -1;
        int offsetInLine = 0;
    };
    Located locate(int position) const;

    // Keeps the caret on screen after it has been moved by something other than
    // the mouse.
    void ensureCursorVisible();

    // By kind, each sorted by where it starts so that the lines on screen can
    // be found without walking every match in the file.
    QMap<Utils::Id, QList<Highlight>> m_highlights;

    // Backspace between the two halves of a bracket pair removes both. That is
    // all the base AutoCompleter offers; inserting the closing half is a
    // language-specific subclass, handed out per editor factory.
    std::unique_ptr<AutoCompleter> m_autoCompleter;
    QPointer<Utils::TextEditorLayout> m_editorLayout;
    bool m_wrapping = false;

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
