// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "textoperations.h"
#include "basehoverhandler.h"
#include "textdocumentlayout.h"
#include "texteditor_global.h"

#include <utils/id.h>
#include <utils/multitextcursor.h>
#include <utils/link.h>

#include <QAbstractItemModel>
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
class QHoverEvent;
class QSequentialAnimationGroup;
class QTimer;
class QWidget;
class QInputMethodEvent;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace Core { struct CodecSelectorResult; }

namespace Utils { class TextEditorLayout; }

namespace Core { class IEditor; }

namespace TextEditor {

class AutoCompleter;
class HoverHandlerRunner;
class IAssistProcessor;
class AssistProposalItemInterface;
class IAssistProposal;

class CodeSource;
class SyntaxHighlighter;
class SymbolRequests;
class TextSuggestion;
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
class TEXTEDITOR_EXPORT TextViewport : public QQuickItem, public HoverTarget
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
    // How wide the widest row on screen is. Wrapping off means a row carries
    // its whole line, so this is what there is to scroll sideways through.
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY metricsChanged)
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
    // How wide the text is allowed to be, as a percentage of the view. Below
    // a hundred the content is centred in what is left - which is what Zen
    // mode narrows, as well as a preference of its own.
    Q_PROPERTY(int contentWidthPercent READ contentWidthPercent NOTIFY metricsChanged)
    // Where this document's marks and its caret are, each as a fraction of
    // the document's height, so that a scroll bar can show them without
    // knowing what a line is. Empty when the display settings say not to.
    Q_PROPERTY(QVariantList scrollBarHighlights READ scrollBarHighlights
                   NOTIFY scrollBarHighlightsChanged)
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
    // The rows on screen, a role per value. A repeater keeps the delegates
    // whose rows kept their place, and a binding reads the one role it wants
    // rather than the whole row.
    Q_PROPERTY(QAbstractItemModel *visibleRows READ visibleRows CONSTANT)
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
    // Every caret's rectangle, the main one first. One entry until something
    // adds a second caret; QML draws what is in here rather than assuming the
    // count.
    // Notified with the rectangle rather than with the position: where a caret
    // is on screen is only known once the rows are laid out, which is after
    // the position last changed.
    Q_PROPERTY(QVariantList caretRectangles READ caretRectangles NOTIFY cursorRectangleChanged)
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorPositionChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorPositionChanged)
    // The column as the reader counts it, with a tab spanning as many columns
    // as it is drawn wide. cursorColumn is the character offset, which is what
    // IEditor asks for and not what a status display should show.
    Q_PROPERTY(int cursorDisplayColumn READ cursorDisplayColumn NOTIFY cursorPositionChanged)
    // Whether the mouse pointer should be off the screen because the user is
    // typing. The form owns the cursor shape; this only says when to blank it.
    Q_PROPERTY(bool mouseHidden READ isMouseHidden NOTIFY mouseHiddenChanged)

    // Where the right margin sits, in the viewport's own coordinates, or -1
    // when there is none to draw. The column it stands for comes from the
    // margin settings and may come from the language's own style.
    Q_PROPERTY(qreal marginX READ marginX NOTIFY metricsChanged)
    // The line, and the tint over everything past it.
    Q_PROPERTY(QColor marginLineColor READ marginLineColor NOTIFY metricsChanged)
    Q_PROPERTY(QColor marginAreaColor READ marginAreaColor NOTIFY metricsChanged)
    // Whether the area past the margin is tinted at all.
    Q_PROPERTY(bool tintMarginArea READ tintMarginArea NOTIFY metricsChanged)

    // What the gutter marks a changed line in. Two properties rather than a
    // colour per line: the form asks which of them a line wants.
    Q_PROPERTY(QColor changedLineColor READ changedLineColor CONSTANT)
    Q_PROPERTY(QColor savedLineColor READ savedLineColor CONSTANT)

    // What one level of indentation is worth on screen, so that the form can
    // put a guide at every level without knowing the font.
    Q_PROPERTY(qreal indentWidth READ indentWidth NOTIFY metricsChanged)
    // What an indent guide is drawn in - the visual whitespace colour, which is
    // what the widget editor draws them in too.
    Q_PROPERTY(QColor indentGuideColor READ indentGuideColor NOTIFY metricsChanged)

    // How much is selected, for a display that says so.
    Q_PROPERTY(int selectedCharacterCount READ selectedCharacterCount NOTIFY selectionChanged)
    // What is selected, with real newlines rather than the U+2029 a QTextCursor
    // hands out - what a drag out of the editor carries.
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    // The file's line ending and encoding, as the toolbar shows them. The
    // document's business rather than this view's, surfaced here because the
    // form has a handle on the viewport and not on the document. Empty when
    // the display settings say not to show them, which is how the widget
    // editor hides its own - so the form has no rule of its own to get wrong.
    Q_PROPERTY(QString fileLineEnding READ fileLineEnding NOTIFY fileFormatChanged)
    Q_PROPERTY(QString fileEncoding READ fileEncoding NOTIFY fileFormatChanged)
    // What the document indents with, as the toolbar shows it - "Spaces: 4".
    // Empty when the display settings say not to show it, so that the rule
    // lives here rather than being restated by whoever draws it.
    Q_PROPERTY(QString tabSettingsLabel READ tabSettingsLabel NOTIFY fileFormatChanged)
    // How wide one level is, so a menu can tick the size in use.
    Q_PROPERTY(int indentSize READ indentSize NOTIFY fileFormatChanged)
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
    // Whether spaces and tabs are drawn. Per view, the way wrapping is: the
    // widget editor's menu entry turns it on for the editor it was used in
    // and not for the settings, and this has to be able to do the same.
    // Follows the setting until the view is told otherwise.
    Q_PROPERTY(bool visualizeWhitespace READ visualizesWhitespace WRITE setVisualizeWhitespace
                   NOTIFY visualizeWhitespaceChanged)

public:
    // How a line differs from what was read from disk, as the gutter marks it.
    // Saved does not mean "the same as the file" - it means the line was
    // edited in this session and the file has been written since, which the
    // document records as a negative block revision. The widget editor's
    // colour role for it is called RevisionReverted, which is why this is
    // worth spelling out.
    enum ChangeMark { None, Changed, Saved };
    Q_ENUM(ChangeMark)

    explicit TextViewport(QQuickItem *parent = nullptr);
    ~TextViewport() override;

    CodeSource *document() const;
    void setDocument(CodeSource *document);

    // HoverTarget. What a hover handler is allowed to ask; the tooltip's
    // parent is the widget hosting this item, which only the host knows.
    TextDocument *textDocument() const override;
    // The document cursor this viewport's caret and selection stand for. Edits
    // go through it so that the document's own undo stack, its layout and the
    // marks on it all see the change - writing the text out and back would
    // lose every one of them.
    QTextCursor textCursor() const override;
    void setContextHelpItem(const Core::HelpItem &item) override;
    QWidget *tooltipParent() override;
    QPoint globalCursorTopLeft() const override;

    void setTooltipHost(QWidget *host);
    Core::HelpItem contextHelpItem() const;

    // The handlers to ask on hover, in the order the editor factory listed
    // them. Not owned - they are the plugins' singletons.
    void setHoverHandlers(const QList<BaseHoverHandler *> &handlers);

    // Follow Symbol. What is under the cursor is the language's business -
    // the view only asks and opens what comes back.
    Q_INVOKABLE void followSymbolUnderCursor(bool inNextSplit = false);
    // Ctrl+click. Answers whether it took the click, so that a file whose
    // language has no finder still gets an ordinary click.
    Q_INVOKABLE bool followSymbolAt(int position, bool inNextSplit = false);

    // Whether a click with these modifiers is a request to follow a link.
    // The widget editor keeps this rule in one place and so does this: the
    // "Enable mouse navigation" setting turns it off, Control asks for it and
    // Shift is a selection gesture rather than a navigation one.
    Q_INVOKABLE bool isMouseNavigation(Qt::KeyboardModifiers modifiers) const;

    // Whether a request to follow a link should land in the other split.
    // \a asked is what the gesture said - Alt on the mouse, or the "in next
    // split" action - and "Always open links in another split" swaps which
    // split that means, the way it does for the widget editor.
    Q_INVOKABLE bool opensInNextSplit(bool asked) const;

    // The link decoration under the pointer: the text is underlined in the
    // scheme's link colour and the cursor becomes a hand, which is how the
    // widget editor says that a Control-click from here will go somewhere.
    void showLink(const Utils::Link &link);
    void clearLink();
    bool openLink(const Utils::Link &link, bool inNextSplit = false);

private:
    void askForTooltip();
    // The same question asked about the caret rather than the mouse, which is
    // what Alt on its own does when "Show help tooltips using the keyboard"
    // is on.
    void askForTooltipAtCaret();
    void askForTooltipAt(int position, const QPointF &at);

public:

    qreal scrollY() const;
    void setScrollY(qreal scrollY);
    qreal scrollX() const;
    void setScrollX(qreal scrollX);

    // Space claimed above a row by something that is not one of the document's
    // rows: the removed lines an inline diff shows between two kept ones, or a
    // spacer holding this view level with another. Rows keep their numbering
    // and their height - a gap belongs to no row, it only moves the ones below
    // it down.
    struct Gap
    {
        int row = 0;
        qreal height = 0;
        bool operator==(const Gap &other) const = default;
    };
    void setRowGaps(const QList<Gap> &gaps);
    QList<Gap> rowGaps() const { return m_rowGaps; }

    // Rows shown between the document's own: the lines an inline diff has
    // that the file no longer does. They are not in the document, so they
    // have no position in it and nothing can be typed into them. Their height
    // is not given but follows from how many there are, which is why they
    // open their gap when the rows are laid out rather than when they are set.
    struct GhostRows
    {
        // What the rows are, which decides how they are drawn: a line the
        // file no longer has is coloured like a diff's removed line, while
        // the rest of an inline suggestion is coloured like the suggestion
        // on the row above it.
        enum class Kind { RemovedLine, Suggestion };

        int row = 0;
        QStringList lines;
        Kind kind = Kind::RemovedLine;
        bool operator==(const GhostRows &other) const = default;
    };
    // Lines a diff has added or changed. The whole line takes a background,
    // and the characters that differ within it are marked over the top - the
    // same two scheme entries the decorated widget uses.
    struct ChangedLine
    {
        int line = 1;                 // 1-based
        QList<QPair<int, int>> chars; // (start, length), within the line
        bool operator==(const ChangedLine &other) const = default;
    };
    void setChangedLines(const QList<ChangedLine> &lines);
    QList<ChangedLine> changedLines() const { return m_changedLines; }
    // The text the character level marks cover, in document order (for tests).
    QStringList changedTextOnScreen() const;
    // The line numbers of the rows carrying a diff's background (for tests).
    QList<int> changedRowsOnScreen() const;

    void setGhostRows(const QList<GhostRows> &ghosts);
    QList<GhostRows> ghostRows() const { return m_ghosts; }
    // What is laid out on screen right now, in document order (for tests).
    QStringList ghostTextOnScreen() const;
    // The row a 1-based document line starts on. Folding and wrapping make
    // the two differ, so a caller working in line numbers has to ask.
    Q_INVOKABLE int rowOfLine(int line);
    // One past the last row, which is where something anchored below the last
    // line of the file belongs.
    Q_INVOKABLE int rowCount();
    // Where those rows are drawn, in the same order (for tests).
    QList<QRectF> ghostRectanglesOnScreen() const;
    // What colour each of them is written in, in the same order (for tests).
    // A row of a suggestion and a row a diff has removed are both ghosts and
    // are drawn quite differently; text alone cannot tell them apart.
    QList<QColor> ghostForegroundsOnScreen() const;

    qreal contentHeight() const;
    qreal contentWidth() const;
    qreal lineHeight() const;
    QColor backgroundColor() const;
    int firstVisibleLine() const;
    QColor currentLineColor() const;
    QColor changedLineColor() const;
    QColor savedLineColor() const;
    qreal indentWidth() const;
    // Asks the file's language what would finish the word the caret is in.
    // The answer arrives on completionsAvailable() and not as a return value:
    // a processor may go away and think about it, and the one every text file
    // gets does exactly that.
    Q_INVOKABLE void requestCompletions();
    // The part of the word already typed, which is what the list is narrowed
    // by and what a chosen completion replaces.
    Q_INVOKABLE QString completionPrefix() const;
    Q_INVOKABLE void applyCompletion(const QString &completion);

    // What the language would offer to fix where the caret is. Same shape as
    // completion: asking is one call, the answer arrives as a signal, and the
    // form decides how to show it. Applying goes back through the proposal
    // rather than inserting text - a fix rewrites the file, and only the item
    // itself knows how.
    // \a provider of nullptr asks the document's own, which is the ordinary
    // case; the clipboard history brings one of its own instead.
    Q_INVOKABLE void requestQuickFixes(IAssistProvider *provider = nullptr);
    Q_INVOKABLE void applyQuickFix(int index);

    // The signature of the call the caret is inside. Unlike the other two
    // this is not a list to choose from: it is shown while the arguments are
    // being typed and taken away when the call is finished, so it follows the
    // caret rather than waiting to be applied.
    Q_INVOKABLE void requestFunctionHint();

    // Replaces the base one with the file's language's, which is what knows to
    // close a bracket or a quote as the user types one. Takes ownership; the
    // completion settings are re-applied to whatever is handed in, so a caller
    // hands over a plain new object.
    void setAutoCompleter(AutoCompleter *completer);
    // Takes the completer the source offers, if it offers one. What makes a
    // snippet in a language complete like that language.
    void adoptSourceCompleter();
    AutoCompleter *autoCompleter() const;

    // Kept alive between the request and the answer: a processor asked to work
    // in a thread is still using itself after start() has returned.
    void deliverCompletions(IAssistProposal *proposal);

    bool isMouseHidden() const;
    // Called by the form when the pointer moves: a mouse that has moved is a
    // mouse the user is looking for again.
    Q_INVOKABLE void showMouse();

    qreal marginX() const;
    QColor marginLineColor() const;
    QColor marginAreaColor() const;
    bool tintMarginArea() const;
    QColor indentGuideColor() const;
    QFont font() const;
    int lineCount() const;
    int visibleLineCount() const;

    int selectionStart() const;
    void setSelectionStart(int position);
    int selectionEnd() const;
    void setSelectionEnd(int position);

    // What the visible line at \a index was laid out with: its "text", its
    // "formats" as a list of {start, length} maps, its "newlineTail" rect, the
    // "preedit" being composed on it and the "width" it came to. Everything,
    // that is, rather than the part the form draws - a selection is merged
    // into the layout's formats rather than drawn as anything of its own, so
    // this is the only way to see that it arrived, and it is what the tests
    // read rows through. Empty for an index that is not on screen.
    Q_INVOKABLE QVariantMap visibleLine(int index) const;
    QAbstractItemModel *visibleRows() const;

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
    // Folding at the caret rather than at a line the pointer landed on.
    Q_INVOKABLE void foldCurrentBlock(bool recursive = false);
    Q_INVOKABLE void unfoldCurrentBlock(bool recursive = false);
    // Closes everything while anything is still open, and opens everything
    // once nothing is.
    Q_INVOKABLE void toggleFoldAll();

    // Home, which goes to the first thing on the line and only to column zero
    // from there. Its own command because the key does more than a plain move
    // to the start of the line.
    Q_INVOKABLE void gotoLineStart(QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    Q_INVOKABLE void selectWordUnderCursor();
    Q_INVOKABLE void clearSelection();

    // To the brackets around the caret, and to the text between them. The
    // last two grow and shrink the selection a pair at a time.
    Q_INVOKABLE void gotoBlockStart(bool select = false);
    Q_INVOKABLE void gotoBlockEnd(bool select = false);
    Q_INVOKABLE bool selectBlockUp();
    Q_INVOKABLE bool selectBlockDown();

    // The commands that only a language can answer. The view asks and someone
    // listening - the language client, today - does the work; nothing here
    // knows what a symbol is. Asked through SymbolRequests so that the
    // listener need not know about Qt Quick.
    Q_INVOKABLE void findUsages();
    Q_INVOKABLE void renameSymbolUnderCursor();
    Q_INVOKABLE void openCallHierarchy();
    // Where the type of the symbol under the caret is defined, which is a
    // different question from where the symbol is and is answered by whoever
    // knows the language.
    Q_INVOKABLE void followTypeUnderCursor(bool inNextSplit = false);
    SymbolRequests *symbolRequests() const;
    // Moving what is shown without moving the caret, which is what the View
    // commands are for.
    Q_INVOKABLE void scrollByRows(int rows);
    Q_INVOKABLE void viewPageUp();
    Q_INVOKABLE void viewPageDown();
    Q_INVOKABLE void viewLineUp();
    Q_INVOKABLE void viewLineDown();

    // Highlights the folds enclosing the row at \a y, the way hovering the
    // widget editor's folding column does - and, like it, whether or not the
    // highlightBlocks setting is on. That setting widens what counts as a
    // hover; it does not turn the highlight on.
    Q_INVOKABLE void highlightScopeAt(qreal y);
    Q_INVOKABLE void clearScopeHighlight();

    int cursorLine() const;
    int cursorColumn() const;
    int cursorDisplayColumn() const;
    int selectedCharacterCount() const;
    QString selectedText() const;

    // Text dropped on the editor, at the point it was let go over. Inserted
    // the way a paste is - the language decides the indentation - and left
    // selected, so that what just arrived is what is highlighted.
    //
    // \a moveFromSelection is a drag that started in this view's own
    // selection: the text is taken from where it was as well as put where it
    // is going, which is what makes it a move rather than a copy.
    Q_INVOKABLE void dropText(const QString &text, qreal x, qreal y,
                              bool moveFromSelection = false);

    // Text dragged out of here and moved into something else: it has gone,
    // so it has to stop being here too.
    Q_INVOKABLE void removeSelectedText();
    int contentWidthPercent() const;
    // How many of the rows on screen had to be shaped by the last layout.
    // Scrolling brings back text that has already been shaped, and the rest
    // are kept, so this is normally far short of what is visible.
    int rowsShapedInLastLayout() const { return m_rowsShaped; }
    QVariantList scrollBarHighlights() const { return m_scrollBarHighlights; }
    QString fileLineEnding() const;
    QString tabSettingsLabel() const;
    int indentSize() const;

    // Changing what this document indents with. Each stops the auto-detection
    // first, the way the widget editor's menu does: having said what to use,
    // the file is no longer being guessed at.
    Q_INVOKABLE void setTabPolicyIsSpaces(bool spaces);
    Q_INVOKABLE void setIndentSize(int size);
    Q_INVOKABLE void detectTabSettings();
    QString fileEncoding() const;

    // Switches the file between Unix and Windows line endings. The document
    // becomes modified: the change is only on disk once it is saved, which is
    // what the widget editor does too.
    Q_INVOKABLE void setFileLineEndingIsWindows(bool windows);

    // Asks which encoding the file should be read or written as, and does it.
    // The asking is a modal dialog Core owns; what to do with the answer is
    // applyEncodingChoice(), which is separate so that it can be tested
    // without one.
    Q_INVOKABLE void selectEncoding();
    // Whether the file is written with a byte order mark, which is the
    // document's to keep and this only turns over.
    Q_INVOKABLE void switchUtf8Bom();
    // Copies the selection with its highlighting, so that pasting into
    // something that understands HTML keeps the colours.
    Q_INVOKABLE void copyWithHtml();

    // The suggestion offered where the caret is, if any. This view cannot
    // draw one yet - that is the last thing missing - but everything else a
    // suggestion needs of a view is here, so taking one works when something
    // puts it there.
    TextSuggestion *currentSuggestion() const;
    // Puts \a suggestion on the line the caret is on and shows it. What
    // something offering one - Copilot today - calls.
    void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion);
    // Takes away the one being shown, if any: it describes something that can
    // no longer happen.
    void clearSuggestion();
    // Gives a suggestion just put on \a block the look and the tab stops of
    // this view, and asks for a layout so that it appears.
    void prepareSuggestion(const QTextBlock &block);
    Q_INVOKABLE void applySuggestion();
    Q_INVOKABLE void applySuggestionWord();
    Q_INVOKABLE void applySuggestionLine();
    void applyEncodingChoice(const Core::CodecSelectorResult &choice);

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

    // \a onScrollBar is the colour to mark these on the scroll bar in, which
    // the widget editor picks per kind and separately from the colour they are
    // drawn in. An invalid one keeps them out of the bar.
    void setHighlights(Utils::Id kind, const QList<Highlight> &highlights,
                       const QColor &onScrollBar = {});
    QList<Highlight> highlights(Utils::Id kind) const;

    void setTextCursor(const QTextCursor &cursor);

    int cursorPosition() const;
    QVariantList caretRectangles() const;
    // The carets as one thing. The main one is the position and selection this
    // has always kept; the rest are extra. Utils::MultiTextCursor is what the
    // widget editor holds too, and it already knows how to apply an edit to
    // several cursors in one undo step and in an order that does not shift the
    // ones not yet reached.
    Utils::MultiTextCursor multiTextCursor() const;
    void transformSelectedText(const TextTransformation &transform);
    // Which markers this file's language comments with, taken from the
    // definition its highlighter was built from. Not cached: the definition
    // arrives after the file does, and a comment taken before it would be the
    // wrong one for the rest of the session.
    // Whether the text may be changed at all. Two separate questions: the
    // view can be told to be read only, and the file itself can be one the
    // filesystem will not take back. Nothing sets either from the other, so a
    // command that asks only the first edits a locked file.
    // What movement is measured against. A wrapped view has a layout of its
    // own that the document's does not know about, and moving down a row
    // rather than over a whole wrapped line depends on using it.
    // Folding indents come from the highlighter, so folding before it has
    // finished would fold whatever range it had worked out so far. Returns
    // true when it had to wait, in which case \a retry runs once it is done
    // and the caller should do nothing now.
    bool waitsForHighlighter(const std::function<void()> &retry);
    // What every fold has to do once the blocks have been changed.
    void foldingChanged();
    // How many rows a page is: a screen of them less one, so that the line
    // the reader was looking at is still there after the page turns.
    int rowsPerPage() const;
    QTextBlock suggestionRowFor(const QTextBlock &block) const;
    void rebuildSuggestionGhosts();
    void updateSuggestion();
    Utils::PlainTextDocumentLayout *movementLayout() const;
    // Asks the hint what it says now, and takes it away when the call the
    // caret was in has ended.
    // The item behind a word the form is showing, or nullptr when the words
    // did not come from a proposal this still has.
    AssistProposalItemInterface *completionItemFor(const QString &text) const;
    void deleteTo(QTextCursor::MoveOperation operation);
    void deleteToCamelCase(bool forward);
    void updateFunctionHint();
    bool canEdit() const;
    Utils::CommentDefinition commentDefinition() const;
    // Grows the carets to whole lines where they have selected nothing, which
    // is what makes the line commands work without selecting first.
    void selectWholeLines();
    void copyLineUpOrDown(bool up);
    void moveLineUpOrDown(bool up);
    void setMultiTextCursor(const Utils::MultiTextCursor &cursors);
    // Runs an edit at every caret, as one undo step. The carets are taken
    // later in the document first: an edit moves everything after it, and a
    // caret that has already been edited at does not have to be moved with it.
    void applyToEveryCaret(const std::function<void(QTextCursor &)> &edit);
    // Another caret, at a document position. The new one becomes the main
    // caret - it is the one just placed - and asking for one where there is
    // already one leaves the count alone.
    Q_INVOKABLE void addCaretAt(int position);
    // A caret at the end of every line a selection covers, which is how a
    // column of them is made without clicking each one.
    Q_INVOKABLE void addCaretsToLineEnds();
    // A caret on the next occurrence of what is selected, wrapping at the end.
    // Searches for the text as it stands: the find bar's case and whole-word
    // settings are the widget editor's, and this view does not share them.
    Q_INVOKABLE void addCaretAtNextMatch();
    // Pulls the line after each caret onto the caret's own line.
    Q_INVOKABLE void joinLines();
    Q_INVOKABLE void uppercaseSelection();
    Q_INVOKABLE void lowercaseSelection();
    Q_INVOKABLE void insertLineAbove();
    Q_INVOKABLE void insertLineBelow();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void sortLines();
    Q_INVOKABLE void unCommentSelection();
    Q_INVOKABLE void duplicateSelectionAndComment();
    Q_INVOKABLE void deleteLine();
    // Copying does not change the text, so it is allowed on a buffer that
    // cannot be edited; cutting is not.
    Q_INVOKABLE void copyLine();
    Q_INVOKABLE void cutLine();
    Q_INVOKABLE void copyLineUp();
    Q_INVOKABLE void copyLineDown();
    Q_INVOKABLE void moveLineUp();
    Q_INVOKABLE void moveLineDown();
    Q_INVOKABLE void rewrapParagraph();

    // What the keys already did, as methods, so that the menu entries and
    // whatever shortcuts the reader has bound can run the same code. A
    // shortcut is handled before the key reaches this item, so an action that
    // did its own version would quietly replace the key handling below.
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // The movement commands, for the menu entries and the shortcuts bound to
    // them. The keys reach handleMoveKeyEvent(), which moves every caret
    // through the same MultiTextCursor::movePosition() this calls - one level
    // below the key event rather than a second implementation of it.
    void moveCursor(QTextCursor::MoveOperation operation,
                    QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    void moveCamelCase(bool forward, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);

    // Deleting as far as a move would have gone. Every one of these is that
    // move with the anchor kept and the selection taken out, which is what
    // makes them the movement commands' twins rather than editing of their
    // own.
    Q_INVOKABLE void deleteEndOfLine();
    Q_INVOKABLE void deleteStartOfLine();
    Q_INVOKABLE void deleteEndOfWord();
    Q_INVOKABLE void deleteStartOfWord();
    Q_INVOKABLE void deleteEndOfWordCamelCase();
    Q_INVOKABLE void deleteStartOfWordCamelCase();

    // Indentation, which the document's indenter decides: this only says
    // which text to ask about.
    Q_INVOKABLE void indent();
    Q_INVOKABLE void unindent();
    Q_INVOKABLE void autoIndent();
    Q_INVOKABLE void autoFormat();

    // Takes the trailing whitespace off the lines the carets cover, which the
    // document does and this only asks for.
    Q_INVOKABLE void cleanWhitespace();
    // Pasting without reformatting what arrives. This view never reformats on
    // paste - it puts the clipboard in as it stands - so it is the same thing
    // as Paste here, and the entry exists so that the menu is not dead.
    Q_INVOKABLE void pasteWithoutFormat();
    // Pasting something other than the last thing copied. With nothing else
    // in the history it is an ordinary paste; with a history it offers it,
    // through the same list quick fixes are offered in.
    Q_INVOKABLE void circularPaste();
    // Asks the form for its context menu at the caret, which is where a
    // keyboard request means rather than wherever the pointer was left.
    Q_INVOKABLE void showContextMenu();
    // A rectangle of text, from where the caret is anchored to \a x, \a y.
    // One caret per line it covers, which is what makes it a selection that
    // can be typed over. The anchor is taken once, when the drag starts, so
    // that dragging back up shrinks the rectangle rather than moving it.
    // \a position of -1 anchors where the caret already is.
    Q_INVOKABLE void anchorBlockSelection(int position);
    Q_INVOKABLE void selectBlockTo(qreal x, qreal y);
    void setCursorPosition(int position);
    QRectF cursorRectangle() const;

    bool isReadOnly() const;
    bool isWrapping() const;
    void setWrapping(bool wrapping);
    bool visualizesWhitespace() const;
    void setVisualizeWhitespace(bool on);

    // The font every editor shows, so these change it for all of them - which
    // is what the widget editor's zoom does too. Separate from zoomBy(), which
    // is the wheel and is turned off by a setting of its own; a command asked
    // for by name is not.
    Q_INVOKABLE void increaseFontZoom();
    Q_INVOKABLE void decreaseFontZoom();
    Q_INVOKABLE void resetFontZoom();
    void setReadOnly(bool readOnly);

signals:
    void documentChanged();
    void scrollYChanged();
    void scrollXChanged();
    void metricsChanged();
    void mouseHiddenChanged();
    // Ctrl+Space, which is the form's cue to ask.
    void completionRequested();
    void contextMenuRequested();
    // What came back, and what to narrow it by. Empty when the language had
    // nothing to say, which the form treats as "no list" rather than "no
    // matches".
    void completionsAvailable(const QStringList &candidates, const QString &prefix);
    // What could be fixed here, in the order the language ranked them. Empty
    // when it offered nothing, which the form shows as no list rather than an
    // empty one.
    void quickFixesAvailable(const QStringList &fixes);
    // The signatures the call could have, and which argument the caret is
    // in. Empty when there is no call to describe any more, which is how the
    // form knows to take the hint away.
    void functionHintAvailable(const QStringList &signatures, int activeArgument);
    void selectionChanged();
    void cursorPositionChanged();
    // One appeared, or the one that was there is gone. What the commands that
    // take a suggestion listen to, so that they are offered only when there
    // is something to take.
    void suggestionChanged();
    void cursorRectangleChanged();
    void readOnlyChanged();
    void fileFormatChanged();
    // A character to pulse where it stands: the bracket that matches the one
    // the caret just arrived beside, once per arrival.
    // What the bar carries, when it is not what it carried before. Every
    // layout would say so otherwise, and a bar that is told to look again
    // rebuilds every mark on it.
    void scrollBarHighlightsChanged();
    void animateCharacter(const QRectF &at, const QString &text, const QColor &foreground,
                          const QColor &background);
    void wrappingChanged();
    void visualizeWhitespaceChanged();

protected:
    void updatePolish() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    // Composing text - a dead key, a CJK input method - is shown before it is
    // committed, and until it is committed it is not in the document. A
    // QTextLayout has a place for exactly that, so the viewport does not have
    // to invent one.
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
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
        // Where the line's message goes. Usually the row's own y, but a
        // message asked for on a line of its own sits in the space opened
        // under the row for it.
        qreal annotationY = 0;
        // Set when a diff says this row's line changed: the whole width of it
        // takes this colour, under everything else drawn on the row.
        QColor diffFill;
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
        // How many indent guides belong on this row - the line's indentation
        // in columns divided by what one level is worth, rounded up, which is
        // the count the widget editor's loop draws. Zero when the setting is
        // off, so a row that draws none and a row that has none look the same
        // to the form.
        int indentGuides = 0;
        // Whether the line was edited in this session, and whether that edit
        // has been written. None when the setting is off, so a line that
        // draws no mark and a line that has nothing to mark look the same to
        // the form.
        ChangeMark changed = None;
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
        // Where that message starts, in item coordinates. The display
        // settings decide: after the text, at the right margin, or against
        // the right edge.
        qreal annotationX = 0;
        // Where a selection runs past the end of the line. Empty otherwise.
        QRectF newlineTail;
        QColor newlineTailColour;
        // The selected parts of this row. One per caret that has a selection
        // reaching into it, so usually none or one. The format ranges
        // carry the selection's colours, but a background on a format range is
        // painted per glyph run, so it comes out in pieces with gaps between
        // them - see updatePaintNode().
        QList<QRectF> selectionFills;
        // Where the spaces and tabs on this row are, when they are being
        // shown. QSGTextNode draws glyph runs and nothing else - the dots and
        // arrows QTextLine::draw() would add are not among them - so they are
        // drawn from here.
        QVariantList whitespace;
        // The wrapped-line marker for this row, where one is set and this row
        // is a continuation. Empty otherwise.
        QString breakMarker;
        qreal breakMarkerX = 0;
        // The nested-scope backgrounds for this row, outermost first: one
        // entry per level, each with the x it starts at, its width and its
        // colour. Empty unless a scope is being highlighted.
        QVariantList scopeBands;
    };
    // What a selection is filled with, read on the GUI thread in
    // updatePolish() and used on the render thread in updatePaintNode().
    QColor m_selectionColour;

    void documentChangedInternal();
    QTextBlock cursorBlock() const;
    // The bracket the caret is beside and the one it belongs to, drawn as
    // highlights. Recomputed whenever the caret moves, the text changes, or
    // the highlighter - which is what records where the brackets are -
    // finishes another pass.
    void updateParenthesesMatch();
    // What the document says is drawn differently - diagnostics, unused
    // symbols, semantic highlighting - turned into this view's highlights.
    void updateDocumentSelections();
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
    void ensureCaretVisibleSideways();
    // Which row a block starts on. A row and a line are the same thing only
    // while wrapping is off; with it on, the layout that lays the rows out is
    // the one that knows, and counting lines lands short by however many rows
    // the lines above took.
    int rowOfBlock(const QTextBlock &block);

    // Where a row sits and how tall it is, as opposed to how tall the text in
    // it is. The two are the same until something claims space of its own
    // between rows, which is what an inline diff's ghost rows and an
    // annotation placed on its own line both need.
    qreal yOfRow(int row) const;
    int rowAtY(qreal y) const;
    qreal rowSpan(int row) const;
    // Total height of the gaps that sit at or above the top of a row.
    qreal gapAbove(int row) const;
    // Recomputes the gaps from the spacers and the ghost rows. True when they
    // changed, so a caller can decide whether a relayout is owed.
    bool rebuildGaps();
    void layOutGhostRows();
    void setScopeBlock(int blockNumber);
    void updateScrollBarHighlights();
    void rebuildVisibleLines();
    void modifyTabSettings(const std::function<void(TabSettingsData &)> &modify);
    void updateLink(const QPointF &pos, Qt::KeyboardModifiers modifiers);
    bool handleSmartBackspace(QTextCursor &cursor);
    void insertTypedText(QTextCursor &cursor, const QString &text);
    void offerCompletionsIfAsked(const QTextCursor &cursor);

    // By kind, each sorted by where it starts so that the lines on screen can
    // be found without walking every match in the file.
    QMap<Utils::Id, QList<Highlight>> m_highlights;
    QMap<Utils::Id, QColor> m_highlightsOnScrollBar;
    QVariantList m_scrollBarHighlights;
    // What the rows were shaped with last time. Anything here changing makes
    // every row's shaping wrong, so none of them can be kept.
    int m_rowsShaped = 0;
    // The rows as QML reads them, rebuilt with the layout.
    class VisibleRowsModel *m_visibleRows = nullptr;
    QFont m_shapedWith;
    qreal m_shapedTabStop = -1;
    qreal m_shapedWrapWidth = -1;
    // The width last handed to the document's layout, which is not the same
    // number as the width rows are shaped at: it carries the margins and the
    // line-separator glyph as well.
    qreal m_documentTextWidth = -1;
    // The layout generation every block was last laid out at. -1 until the
    // first time, which is what makes it happen at all.
    int m_primedGeneration = -1;

    // Backspace between the two halves of a bracket pair removes both. That is
    // all the base AutoCompleter offers; inserting the closing half is a
    // language-specific subclass, handed out per editor factory.
    std::unique_ptr<AutoCompleter> m_autoCompleter;
    // Whether the one in use came from the source, so that a source that
    // stops offering one gets the plain default back rather than keeping the
    // last language's.
    bool m_autoCompleterFromSource = false;
    std::unique_ptr<IAssistProcessor> m_completionProcessor;
    // Kept, not just its words: a completion is applied by asking the item to
    // do it, and an item can do more than put its own text in.
    std::unique_ptr<IAssistProposal> m_completionProposal;
    // The fixes on offer are kept, not just their words: applying one asks
    // the item to do it, and the item belongs to the proposal.
    std::unique_ptr<IAssistProcessor> m_quickFixProcessor;
    std::unique_ptr<IAssistProposal> m_quickFixProposal;
    // Kept while the hint is up: what argument the caret is in has to be
    // asked of the model again every time the caret moves.
    std::unique_ptr<IAssistProcessor> m_functionHintProcessor;
    std::unique_ptr<IAssistProposal> m_functionHintProposal;
    QPointer<Utils::TextEditorLayout> m_editorLayout;
    bool m_wrapping = false;
    // Unset until the view is told: the setting answers until then.
    std::optional<bool> m_visualizeWhitespace;

    QPointer<CodeSource> m_document;
    // The QTextDocument currently connected to, which is not the same one
    // across a reopen.
    QPointer<QTextDocument> m_connectedDocument;
    // The highlighter currently connected to, for the same reason.
    QPointer<SyntaxHighlighter> m_connectedHighlighter;
    qreal m_scrollY = 0;
    qreal m_scrollX = 0;
    // The carets after the first. The main one stays a position and a
    // selection, so everything that asks about "the caret" keeps its answer.
    QList<QTextCursor> m_extraCursors;
    // Where a block selection was started, kept because the carets it makes
    // replace the main one it would otherwise be read back from.
    QTextCursor m_blockSelectionAnchor;
    // Where growing a selection out to its brackets started, so that
    // shrinking it can find its way back. Dropped as soon as the selection
    // goes, because the way back is only meaningful for the selection it
    // was taken for.
    QTextCursor m_selectBlockAnchor;
    SymbolRequests *m_symbolRequests = nullptr;
    // The line a suggestion is being shown on, so that it can be taken away
    // again when the caret leaves it.
    QTextBlock m_suggestionBlock;
    // Set after a line has been moved and cleared by the next key. Moving a
    // line twice is one thing the reader did, so the second move joins the
    // first one's undo step rather than making its own.
    bool m_lineMoveJoinsUndo = false;
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
    // Sorted by row, and the running total of their heights alongside, so that
    // where a row sits is a binary search rather than a walk. Derived from the
    // two things that can open one rather than set directly.
    QList<Gap> m_rowGaps;
    std::vector<qreal> m_gapSums;
    QList<Gap> m_spacers;
    QList<GhostRows> m_ghosts;
    // The rest of a multi-line suggestion, which is the view's own doing
    // rather than something set from outside: kept apart so that whoever sets
    // ghost rows for a diff does not wipe them and is not wiped by them.
    QList<GhostRows> m_suggestionGhosts;
    // The ghost rows that are on screen, laid out in updatePolish() and drawn
    // in updatePaintNode() like any other row.
    std::vector<Line> m_ghostLines;

    QList<ChangedLine> m_changedLines;
    QHash<int, QList<QPair<int, int>>> m_changedByLine;
    QColor m_changedBackground;
    QTextCharFormat m_changedCharFormat;

    qreal m_contentHeight = 0;
    qreal m_contentWidth = 0;
    // Set when the caret moved and cleared once updatePolish() has put it back
    // on screen: where it is sideways is only known once the rows are laid out.
    bool m_caretVisibleXPending = false;
    // The column the caret is trying to keep while it moves up and down. A
    // QTextCursor carries it, and this view builds a fresh one for every key,
    // so it is kept here instead. -1 means it has none yet.
    int m_verticalMovementX = -1;
    // A page key scrolls a screen and then puts the caret back where it was on
    // screen. Where that is can only be turned back into a position once the
    // rows are laid out again, so it waits for updatePolish() like the sideways
    // caret does.
    struct PendingPage
    {
        qreal x = 0;
        qreal y = 0;
        QTextCursor::MoveMode mode = QTextCursor::MoveAnchor;
    };
    std::optional<PendingPage> m_pendingPage;
    QPointer<TextDocument> m_connectedMarkSource;
    QColor m_currentLine = Qt::transparent;
    QColor m_indentGuide = Qt::transparent;
    qreal m_indentWidth = 0;
    bool m_mouseHidden = false;
    // Which block's enclosing folds are highlighted, and what they came out
    // as. -1 when nothing is asking for a highlight. The revision is what the
    // document was at when they were worked out, so that a caret moving along
    // one line does not walk the folds again.
    // What is underlined as a link, and whether the pointer is over the text
    // at all - a Control press only means "show me the link" while it is.
    // A bracket waiting to be pulsed. Worked out while matching, which
    // happens before the rows are laid out, so where it is on screen is not
    // known until they are.
    struct PendingPulse
    {
        int position = -1;
        QColor foreground;
        QColor background;
    };
    std::optional<PendingPulse> m_pendingPulse;
    // A jump within the file being scrolled to rather than snapped to. Kept
    // so that a second jump replaces the first instead of fighting it.
    QPointer<QSequentialAnimationGroup> m_navigationAnimation;
    Utils::Link m_currentLink;
    bool m_hovering = false;
    // Whether the Alt being held was pressed on its own: any other key while
    // it is down means it was a shortcut rather than a request for a tooltip.
    bool m_maybeKeyboardTooltip = false;
    int m_scopeBlock = -1;
    int m_scopeRevision = -1;
    BlockNesting m_scopeNesting;
    qreal m_marginX = -1;
    QColor m_marginLine = Qt::transparent;
    QColor m_marginArea = Qt::transparent;
    bool m_tintMarginArea = false;
    QFont m_font;
    int m_lineCount = 0;
    int m_firstVisibleLine = 0;
    QColor m_background;

    // Hovering. The timer is what turns "the mouse stopped here" into a
    // request; Qt does that for widgets and not for items.
    QList<BaseHoverHandler *> m_hoverHandlers;
    std::unique_ptr<HoverHandlerRunner> m_hoverRunner;
    QTimer *m_hoverTimer = nullptr;
    QPointF m_hoverItemPos;
    QPointer<QWidget> m_tooltipHost;
    Core::HelpItem m_contextHelpItem;
};

} // namespace TextEditor
