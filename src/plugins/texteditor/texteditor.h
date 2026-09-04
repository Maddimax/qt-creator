// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include "basehoverhandler.h"

#include "codeassist/assistenums.h"
#include "completionsettings.h"
#include "indenter.h"
#include "refactoroverlay.h"
#include "snippets/snippetparser.h"
#include "textdocument.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/helpitem.h>

#include <utils/elidinglabel.h>
#include <utils/link.h>
#include <utils/multitextcursor.h>
#include <utils/plaintextedit/plaintextedit.h>
#include <utils/textutils.h>
#include <utils/theme/theme.h>
#include <utils/uncommentselection.h>

#include <QPlainTextEdit>
#include <QSharedPointer>
#include <QToolButton>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QToolBar;
class QPrinter;
class QMenu;
class QPainter;
class QPoint;
class QRect;
class QTextBlock;
QT_END_NAMESPACE

namespace Core {
class HighlightScrollBarController;
class MinimapController;
}

namespace TextEditor {
class AssistInterface;
class BaseHoverHandler;
class CompletionAssistProvider;
class IAssistProvider;
class ICodeStylePreferences;
class RefactorOverlay;
class SyntaxHighlighter;
class TextDocument;
class TextMark;
class TextSuggestion;
using RefactorMarkers = QList<RefactorMarker>;
using TextMarks = QList<TextMark *>;

namespace Internal {
class BaseTextEditorPrivate;
class LineColumnButtonPrivate;
class TextEditorFactoryPrivate;
class TextEditorWidgetPrivate;
class TextEditorOverlay;
}

class AutoCompleter;
class BaseTextEditor;
class TextEditorFactory;
class TextEditorWidget;
class PlainTextEditorFactory;

class BehaviorSettingsData;
class DisplaySettingsData;
class ExtraEncodingSettingsData;
class FontSettingsData;
class MarginSettingsData;
class StorageSettingsData;
class TypingSettingsData;

namespace OptionalActions {
enum Mask {
    None = 0,
    Format = 1,
    UnCommentSelection = 2,
    UnCollapseAll = 4,
    FollowSymbolUnderCursor = 8,
    FollowTypeUnderCursor = 16,
    JumpToFileUnderCursor = 32,
    RenameSymbol = 64,
    FindUsage = 128,
    CallHierarchy = 256,
    TypeHierarchy = 512,
};
} // namespace OptionalActions

class TEXTEDITOR_EXPORT EmbeddedWidgetInterface : public QObject
{
    Q_OBJECT
public:
    ~EmbeddedWidgetInterface() override;
    void resize();
    void close();

signals:
    void resized();
    void closed();
    void shouldClose();
};

class TEXTEDITOR_EXPORT BaseTextEditor : public Core::IEditor
{
    Q_OBJECT

public:
    BaseTextEditor();
    ~BaseTextEditor() override;

    virtual void finalizeInitialization() {}

    static BaseTextEditor *currentTextEditor();
    static QList<BaseTextEditor *> openedTextEditors();
    static QList<BaseTextEditor *> textEditorsForDocument(TextDocument *textDocument);
    static QList<BaseTextEditor *> textEditorsForFilePath(const Utils::FilePath &path);

    TextEditorWidget *editorWidget() const;
    TextDocument *textDocument() const;

    // Some convenience text access
    void setTextCursor(const QTextCursor &cursor);
    QTextCursor textCursor() const;
    QChar characterAt(int pos) const;
    QString textAt(int from, int to) const;

    void addContext(Utils::Id id);

    // IEditor
    Core::IDocument *document() const override;

    BaseTextEditor *duplicate() override;

    QByteArray saveState() const override;
    void restoreState(const QByteArray &state) override;
    QWidget *toolBar() override;

    int currentLine() const override;
    int currentColumn() const override;
    void gotoLine(int line, int column = 0, bool centerLine = true) override;

    /*! Returns the amount of visible columns (in characters) in the editor */
    int columnCount() const;

    /*! Returns the amount of visible lines (in characters) in the editor */
    int rowCount() const;

    /*! Returns the position of the current main text cursor */
    int position() const;

    /*! Converts the \a pos in characters from beginning of document to \a line and \a column */
    void convertPosition(int pos, int *line, int *column) const;

    QString selectedText() const override;

    /*! Removes \a length characters to the right of the cursor. */
    void remove(int length);
    /*! Removes the current selected text and inserts the given string to the right of the cursor. */
    void insert(const QString &string);
    /*! Replaces \a length characters to the right of the cursor with the given string. */
    void replace(int length, const QString &string);
    /*! Sets current cursor position to \a pos. */
    void setCursorPosition(int pos);
    /*! Selects text between current cursor position and \a toPos. */
    void select(int toPos);

private:
    friend class TextEditorFactory;
    friend class Internal::TextEditorFactoryPrivate;

    void saveCurrentStateForNavigationHistory();
    void addSavedStateToNavigationHistory();
    void addCurrentStateToNavigationHistory();

    Internal::BaseTextEditorPrivate *d;
};

class TEXTEDITOR_EXPORT TextEditorWidget : public Utils::PlainTextEdit, public HoverTarget
{
    Q_OBJECT
public:
    explicit TextEditorWidget(QWidget *parent = nullptr);
    ~TextEditorWidget() override;

    void setTextDocument(const QSharedPointer<TextDocument> &doc);

    // Shows in-editor merge conflict resolution controls when the document has
    // conflict markers. On by default; turn off for editors that provide their
    // own conflict handling (e.g. the inline diff editor).
    void setMergeConflictResolutionEnabled(bool enabled);
    TextDocument *textDocument() const override;
    QSharedPointer<TextDocument> textDocumentPtr() const;

    // HoverTarget. textCursor() is PlainTextEdit's; it is named here so that
    // the two bases do not both offer it.
    QTextCursor textCursor() const override;
    QWidget *tooltipParent() override { return this; }
    QPoint globalCursorTopLeft() const override;

    virtual void aboutToOpen(const Utils::FilePath &filePath, const Utils::FilePath &realFilePath);
    virtual void openFinishedSuccessfully();
    // IEditor
    QByteArray saveState() const;
    virtual void restoreState(const QByteArray &state);
    void gotoLine(int line, int column = 0, bool centerLine = true, bool animate = false);
    int position() const;
    QTextCursor textCursorAt(int position) const;
    Utils::Text::Position lineColumn() const;
    void convertPosition(int pos, int *line, int *column) const;
    using PlainTextEdit::cursorRect;
    QRect cursorRect(int pos) const;
    void setCursorPosition(int pos);
    QWidget *toolBarWidget() const;
    QToolBar *toolBar() const;

    void print(QPrinter *);

    void appendStandardContextMenuActions(QMenu *menu);

    uint optionalActions();
    void setOptionalActions(uint optionalActions);
    void addOptionalActions(uint optionalActions);

    void setAutoCompleter(AutoCompleter *autoCompleter);
    AutoCompleter *autoCompleter() const;

    // Works only in conjunction with a syntax highlighter that puts
    // parentheses into text block user data
    void setParenthesesMatchingEnabled(bool b);
    bool isParenthesesMatchingEnabled() const;

    void setHighlightCurrentLine(bool b);
    bool highlightCurrentLine() const;

    void setLineNumbersVisible(bool b);
    bool lineNumbersVisible() const;

    void setAlwaysOpenLinksInNextSplit(bool b);
    bool alwaysOpenLinksInNextSplit() const;

    void setMarksVisible(bool b);
    bool marksVisible() const;

    void setRequestMarkEnabled(bool b);
    bool requestMarkEnabled() const;

    void setLineSeparatorsAllowed(bool b);
    bool lineSeparatorsAllowed() const;

    bool codeFoldingVisible() const;

    void setCodeFoldingSupported(bool b);
    bool codeFoldingSupported() const;

    void setMouseNavigationEnabled(bool b);
    bool mouseNavigationEnabled() const;

    void setMouseHidingEnabled(bool b);
    bool mouseHidingEnabled() const;

    void setScrollWheelZoomingEnabled(bool b);
    bool scrollWheelZoomingEnabled() const;

    void setCamelCaseNavigationEnabled(bool b);
    bool camelCaseNavigationEnabled() const;

    void setRevisionsVisible(bool b);
    bool revisionsVisible() const;

    void setMinimapVisible(bool visible);
    bool minimapVisible() const;

    void setVisibleWrapColumn(int column);
    int visibleWrapColumn() const;

    int columnCount() const;
    int rowCount() const;

    // replaces the text from the current cursor position to the base position with the snippet
    // and starts the snippet replacement mode
    void insertCodeSnippet(int basePosition,
                           const QString &snippet,
                           const SnippetParser &parse);

    Utils::MultiTextCursor multiTextCursor() const;
    void setMultiTextCursor(const Utils::MultiTextCursor &cursor);

    QRegion translatedLineRegion(int lineStart, int lineEnd) const;

    QPoint toolTipPosition(const QTextCursor &c) const;
    void showTextMarksToolTip(const QPoint &pos,
                              const TextMarks &marks,
                              const TextMark *mainTextMark = nullptr) const;

    void invokeAssist(AssistKind assistKind, IAssistProvider *provider = nullptr);
    void setCompletionTriggerOverride(CompletionTrigger trigger);

    virtual std::unique_ptr<AssistInterface> createAssistInterface(AssistKind assistKind,
                                                                   AssistReason assistReason) const;

    static QString msgTextTooLarge(quint64 size);

    QWidget *extraArea() const;
    virtual int extraAreaWidth(int *markWidthPtr = nullptr) const;
    virtual void extraAreaPaintEvent(QPaintEvent *);
    virtual void extraAreaLeaveEvent(QEvent *);
    virtual void extraAreaContextMenuEvent(QContextMenuEvent *);
    virtual void extraAreaMouseEvent(QMouseEvent *);
    virtual void extraAreaToolTipEvent(QHelpEvent *e);
    void updateFoldingHighlight(const QPoint &pos);
    void updateFoldingHighlight(const QTextCursor &cursor);

    void setLanguageSettingsId(Utils::Id settingsId);
    Utils::Id languageSettingsId() const;

    const DisplaySettingsData &displaySettings() const;
    const MarginSettingsData &marginSettings() const;
    const BehaviorSettingsData &behaviorSettings() const;

    void ensureCursorVisible();
    void ensureBlockIsUnfolded(QTextBlock block);

    static Utils::Id FakeVimSelection;
    static Utils::Id SnippetPlaceholderSelection;
    static Utils::Id CurrentLineSelection;
    static Utils::Id ParenthesesMatchingSelection;
    static Utils::Id AutoCompleteSelection;
    static Utils::Id CodeWarningsSelection;
    static Utils::Id CodeSemanticsSelection;
    static Utils::Id CursorSelection;
    static Utils::Id UndefinedSymbolSelection;
    static Utils::Id UnusedSymbolSelection;
    static Utils::Id OtherSelection;
    static Utils::Id ObjCSelection;
    static Utils::Id DebuggerExceptionSelection;

    void setExtraSelections(Utils::Id kind, const QList<QTextEdit::ExtraSelection> &selections);
    QList<QTextEdit::ExtraSelection> extraSelections(Utils::Id kind) const;

    RefactorMarkers refactorMarkers() const;
    void setRefactorMarkers(const RefactorMarkers &markers);
    void setRefactorMarkers(const RefactorMarkers &markers, const Utils::Id &type);
    void clearRefactorMarkers(const Utils::Id &type);

    enum Side { Left, Right };
    QAction *insertExtraToolBarWidget(Side side, QWidget *widget);
    void insertExtraToolBarAction(Side side, QAction *action);
    void setToolbarOutline(QWidget* widget);
    const QWidget *toolbarOutlineWidget();

    // keep the auto completion even if the focus is lost
    void keepAutoCompletionHighlight(bool keepHighlight);
    void setAutoCompleteSkipPosition(const QTextCursor &cursor);

    virtual void copy();
    virtual void paste();
    virtual void cut();
    virtual void selectAll();

    virtual void autoIndent();
    virtual void rewrapParagraph();
    virtual void unCommentSelection();

    virtual void autoFormat();

    virtual void encourageApply();

    virtual void setDisplaySettings(const TextEditor::DisplaySettingsData &);
    // Publishes the +/- diff signs for the extra area: blockSigns maps a
    // 0-based block number of a changed (added/removed) line to its sign;
    // hasRemovedRows tells the widget that removed lines are shown as ghost
    // rows, which get a '-' derived from the layout. Used by InlineDiffDecorator.
    void setDiffChangeSigns(const QHash<int, QChar> &blockSigns, bool hasRemovedRows);

    // A marker on the scroll bar covering the 1-based document lines
    // [firstLine, lastLine], in a theme color of its own.
    class ScrollBarHighlight
    {
    public:
        int firstLine = 1;
        int lastLine = 1;
        Utils::Theme::Color color = Utils::Theme::TextColorNormal;
    };
    // Publishes markers the widget draws on its scroll bar next to its own
    // ones (search results, marks), e.g. for the changes of an inline diff or
    // the merge conflicts of a file. A call replaces the category's previous
    // markers, an empty list removes them. The lines are mapped to the
    // current layout on every scroll bar update, so folding and inserted rows
    // carry the markers along.
    void setScrollBarHighlights(Utils::Id category, const QList<ScrollBarHighlight> &highlights);
    virtual void setMarginSettings(const TextEditor::MarginSettingsData &);
    void setBehaviorSettings(const TextEditor::BehaviorSettingsData &);
    void setTypingSettings(const TextEditor::TypingSettingsData &);
    void setStorageSettings(const TextEditor::StorageSettingsData &);
    void setExtraEncodingSettings(const TextEditor::ExtraEncodingSettingsData &);
    void updateCompletionSettings();

    void circularPaste();
    void pasteWithoutFormat();
    void switchUtf8bom();

    void increaseFontZoom();
    void decreaseFontZoom();
    void zoomF(float delta);
    void zoomReset();

    void cutLine();
    void copyLine();
    void copyWithHtml();
    void duplicateSelection();
    void duplicateSelectionAndComment();
    void deleteLine();
    void deleteEndOfLine();
    void deleteEndOfWord();
    void deleteEndOfWordCamelCase();
    void deleteStartOfLine();
    void deleteStartOfWord();
    void deleteStartOfWordCamelCase();
    void toggleFoldAll();
    void unfoldAll(bool unfold);
    void fold(const QTextBlock &block, bool recursive = false);
    void foldCurrentBlock();
    void unfold(const QTextBlock &block, bool recursive = false);
    void unfoldCurrentBlock();
    void selectEncoding();
    void updateTextCodecLabel();
    void selectLineEnding(Utils::TextFileFormat::LineTerminationMode lineEnding);
    void updateTextLineEndingLabel();
    void addSelectionNextFindMatch();
    void addCursorsToLineEnds();

    void gotoBlockStart();
    void gotoBlockEnd();
    void gotoBlockStartWithSelection();
    void gotoBlockEndWithSelection();

    void gotoDocumentStart();
    void gotoDocumentEnd();
    void gotoLineStart();
    void gotoLineStartWithSelection();
    void gotoLineEnd();
    void gotoLineEndWithSelection();
    void gotoNextLine();
    void gotoNextLineWithSelection();
    void gotoPreviousLine();
    void gotoPreviousLineWithSelection();
    void gotoPreviousCharacter();
    void gotoPreviousCharacterWithSelection();
    void gotoNextCharacter();
    void gotoNextCharacterWithSelection();
    void gotoPreviousWord();
    void gotoPreviousWordWithSelection();
    void gotoNextWord();
    void gotoNextWordWithSelection();
    void gotoPreviousWordCamelCase();
    void gotoPreviousWordCamelCaseWithSelection();
    void gotoNextWordCamelCase();
    void gotoNextWordCamelCaseWithSelection();

    virtual bool selectBlockUp();
    virtual bool selectBlockDown();
    void selectWordUnderCursor();
    void clearSelection();

    void showContextMenu();

    void moveLineUp();
    void moveLineDown();

    void viewPageUp();
    void viewPageDown();
    void viewLineUp();
    void viewLineDown();

    void copyLineUp();
    void copyLineDown();

    void joinLines();

    void insertLineAbove();
    void insertLineBelow();

    void uppercaseSelection();
    void lowercaseSelection();

    void sortLines();

    void cleanWhitespace();

    void indent();
    void unindent();

    virtual void undo();
    virtual void redo();

    virtual bool isUndoAvailable() const;
    virtual bool isRedoAvailable() const;

    void openLinkUnderCursor();
    void openLinkUnderCursorInNextSplit();
    void openTypeUnderCursor();
    void openTypeUnderCursorInNextSplit();

    virtual void findUsages();
    virtual void renameSymbolUnderCursor();
    virtual void openCallHierarchy();

    /// Abort code assistant if it is running.
    void abortAssist();

    /// Overwrite the current highlighter with a new generic highlighter based on the mimetype of
    /// the current document
    void configureGenericHighlighter();
    /// Overwrite the current highlighter with a new generic highlighter based on the given mimetype
    void configureGenericHighlighter(const Utils::MimeType &mimeType);

    /// Overwrite the current highlighter with a new generic highlighter based on the given definition
    Utils::Result<> configureGenericHighlighter(const QString &definitionName);

    Q_INVOKABLE void inSnippetMode(bool *active); // Used by FakeVim.
    // Used by FakeVim: true while the editor performs an inline rename that
    // consumes key events itself (none in the base editor).
    Q_INVOKABLE virtual void inInlineRename(bool *active);

    /*! Returns the document line number for the visible \a row.
     *
     * The first visible row is 0, the last visible row is rowCount() - 1.
     *
     * Any invalid row will return -1 as line number.
     */
    int blockNumberForVisibleRow(int row) const;

    /*! Returns the first visible line of the document. */
    int firstVisibleBlockNumber() const;
    /*! Returns the last visible line of the document. */
    int lastVisibleBlockNumber() const;
    /*! Returns the line visible closest to the vertical center of the editor. */
    int centerVisibleBlockNumber() const;

    Core::HighlightScrollBarController *highlightScrollBarController() const;
    Core::MinimapController *minimapController() const;

    void addHoverHandler(BaseHoverHandler *handler);
    void removeHoverHandler(BaseHoverHandler *handler);

    void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion);
    void clearSuggestion();
    TextSuggestion *currentSuggestion() const;
    bool suggestionVisible() const override;
    bool suggestionsBlocked() const;

    using SuggestionBlocker = std::shared_ptr<void>;
    // Returns an object that blocks suggestions until it is destroyed.
    SuggestionBlocker blockSuggestions();

    std::unique_ptr<EmbeddedWidgetInterface> insertWidget(QWidget *widget, int pos);

    QTextCursor autoCompleteHighlightPosition() const;

#ifdef WITH_TESTS
    void processTooltipRequest(const QTextCursor &c);
    QString textToPrint(bool selectionOnly) const;
#endif

signals:
    void assistFinished(); // Used in tests.

    void textDocumentChanged();

    void requestBlockUpdate(const QTextBlock &);

    void requestLinkAt(const QTextCursor &cursor, const Utils::LinkHandler &callback,
                       bool resolveTarget, bool inNextSplit);
    void requestTypeAt(const QTextCursor &cursor, const Utils::LinkHandler &callback,
                       bool resolveTarget, bool inNextSplit);
    void requestUsages(const QTextCursor &cursor);
    void requestRename(const QTextCursor &cursor);
    void requestCallHierarchy(const QTextCursor &cursor);
    void toolbarOutlineChanged(QWidget *newOutline);
    void tabSettingsChanged();

    // used by the IEditor
    void saveCurrentStateForNavigationHistory();
    void addSavedStateToNavigationHistory();
    void addCurrentStateToNavigationHistory();

    void resized();
    void embeddedWidgetsShouldClose();

protected:
    QTextBlock blockForVisibleRow(int row) const;
    QTextBlock blockForVerticalOffset(int offset) const;
    bool event(QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void changeEvent(QEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void showEvent(QShowEvent *) override;
    bool viewportEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
    virtual void paintBlock(QPainter *painter,
                            const QTextBlock &block,
                            const QPointF &offset,
                            const QList<QTextLayout::FormatRange> &selections,
                            const QRect &clipRect) const;
    void timerEvent(QTimerEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void dragEnterEvent(QDragEnterEvent *e) override;

    QMimeData *createMimeDataFromSelection() const override;
    QMimeData *createMimeDataFromSelection(bool withHtml) const;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

    virtual QString plainTextFromSelection(const QTextCursor &cursor) const;
    virtual QString plainTextFromSelection(const Utils::MultiTextCursor &cursor) const;

    virtual QString lineNumber(int blockNumber) const;
    virtual int lineNumberDigits() const;
    virtual bool selectionVisible(int blockNumber) const;
    virtual bool replacementVisible(int blockNumber) const;
    virtual QColor replacementPenColor(int blockNumber) const;

    virtual void triggerPendingUpdates();
    virtual void applyFontSettings();

    void showDefaultContextMenu(QContextMenuEvent *e, Utils::Id menuContextId);
    virtual void finalizeInitialization() {}
    virtual void finalizeInitializationAfterDuplication(TextEditorWidget *) {}
    static QTextCursor flippedCursor(const QTextCursor &cursor);

    void setVisualIndentOffset(int offset);

    void updateUndoRedoActions();

public:
    QString selectedText() const;

    void setupGenericHighlighter();
    void setupFallBackEditor(Utils::Id id);

    void remove(int length);
    void replace(int length, const QString &string);
    void replace(int pos, int length, const QString &string);
    QChar characterAt(int pos) const;
    QString textAt(int from, int to) const;

    void contextHelpItem(const Core::IContext::HelpCallback &callback);
    void setContextHelpItem(const Core::HelpItem &item) override;

    Q_INVOKABLE bool inFindScope(const QTextCursor &cursor) const;

    static TextEditorWidget *currentTextEditorWidget();
    static TextEditorWidget *fromEditor(const Core::IEditor *editor);
    static QList<TextEditorWidget *> textEditorWidgetsForDocument(TextDocument *document);

    /*!
       Returns whether the link was opened successfully.
     */
    bool openLink(const Utils::Link &link, bool inNextSplit = false);

protected:
    /*!
       Reimplement this function to enable code navigation.

       \a resolveTarget is set to true when the target of the link is relevant
       (it isn't until the link is used).
     */
    virtual void findLinkAt(const QTextCursor &,
                            const Utils::LinkHandler &processLinkCallback,
                            bool resolveTarget = true,
                            bool inNextSplit = false);

    virtual void findTypeAt(const QTextCursor &,
                            const Utils::LinkHandler &processLinkCallback,
                            bool resolveTarget = true,
                            bool inNextSplit = false);

    /*!
      Reimplement this function to change the default replacement text.
      */
    virtual QString foldReplacementText(const QTextBlock &block) const;
    virtual void drawCollapsedBlockPopup(QPainter &painter,
                                         const QTextBlock &block,
                                         QPointF offset,
                                         const QRect &clip);
    int visibleFoldedBlockNumber() const;
    void doSetTextCursor(const QTextCursor &cursor) override;
    void doSetTextCursor(const QTextCursor &cursor, bool keepMultiSelection);

signals:
    void tooltipOverrideRequested(TextEditor::TextEditorWidget *widget,
        const QPoint &globalPos, int position, bool *handled);
    void tooltipRequested(const QPoint &globalPos, int position);
    void activateEditor(Core::EditorManager::OpenEditorFlags flags = {});

protected:
    virtual void slotCursorPositionChanged(); // Used in VcsBase

private:
    std::unique_ptr<Internal::TextEditorWidgetPrivate> d;
    friend class TextEditorFactory;
    friend class Internal::TextEditorFactoryPrivate;
    friend class Internal::TextEditorWidgetPrivate;
    friend class Internal::TextEditorOverlay;
    friend class RefactorOverlay;

    bool singleShotAfterHighlightingDone(std::function<void()> &&f);
    void updateVisualWrapColumn();
};

class TEXTEDITOR_EXPORT TextEditorLinkLabel : public Utils::ElidingLabel
{
public:
    TextEditorLinkLabel(QWidget *parent = nullptr);

    void setLink(Utils::Link link);
    Utils::Link link() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPoint m_dragStartPosition;
    Utils::Link m_link;
};

class TEXTEDITOR_EXPORT TextEditorFactory : public Core::IEditorFactory
{

public:
    TextEditorFactory();
    ~TextEditorFactory() override;

    using EditorCreator = std::function<BaseTextEditor *()>;
    using DocumentCreator = std::function<TextDocument *()>;
    // editor widget must be castable (qobject_cast or Aggregate::query) to TextEditorWidget
    using EditorWidgetCreator = std::function<QWidget *()>;
    using SyntaxHighLighterCreator = std::function<SyntaxHighlighter *()>;
    using IndenterCreator = std::function<Indenter *(QTextDocument *)>;
    using AutoCompleterCreator = std::function<AutoCompleter *()>;
    // Declared beside TextDocument, which can also carry one.
    using LinkFinder = TextEditor::LinkFinder;

    void setDocumentCreator(const DocumentCreator &creator);
    void setEditorWidgetCreator(const EditorWidgetCreator &creator);
    void setEditorCreator(const EditorCreator &creator);
    void setIndenterCreator(const IndenterCreator &creator);
    void setSyntaxHighlighterCreator(const SyntaxHighLighterCreator &creator);
    void setUseGenericHighlighter(bool enabled);
    void setAutoCompleterCreator(const AutoCompleterCreator &creator);
    void setOptionalActionMask(int optionalActions);

    void addHoverHandler(BaseHoverHandler *handler);
    void setCompletionAssistProvider(CompletionAssistProvider *provider);

    // What this factory would give a document of its language. An editor that
    // is not built by a factory - the Qt Quick one - finds the factory that
    // claims the file's mime type and asks it, rather than doing without a
    // language's indenter and completions entirely.
    IndenterCreator indenterCreator() const;
    AutoCompleterCreator autoCompleterCreator() const;
    CompletionAssistProvider *completionAssistProvider() const;
    QList<BaseHoverHandler *> hoverHandlers() const;
    LinkFinder linkFinder() const;

    // What Ctrl+click and Follow Symbol do in this language. Registered here
    // rather than overridden on an editor widget, so that a view which is not
    // one - the Qt Quick editor - can follow symbols too.
    void setLinkFinder(const LinkFinder &finder);

    // The link finder for \a document's language, or an empty one where the
    // language has none.
    static LinkFinder linkFinderFor(TextDocument *document);

    // The one that would build an editor for \a filePath, or nullptr where no
    // factory claims it. Walks the mime type's parents, so a C++ file finds
    // the C++ factory and a plain text file finds the plain one.
    static TextEditorFactory *preferredFactoryFor(const Utils::FilePath &filePath);

    // A context every editor this factory builds is in, whichever view it
    // shows the document in. The language's own commands are registered
    // against it, so an editor that does not carry it is one those commands
    // do not reach - which is why it belongs to the factory rather than to
    // the editor creator, where only one of the two views would see it.
    void addEditorContext(Utils::Id id);

    // Which view an editor built here shows its document in: the Qt Quick
    // editor rather than a TextEditorWidget. The document, the indenter, the
    // highlighter and the completions are this factory's either way - only
    // the view changes - so a language turns this on once the Quick editor
    // has what it overrides on the widget.
    void setUsesQuickEditor(bool on);
    bool usesQuickEditor() const;

    void setCommentDefinition(Utils::CommentDefinition definition);
    void setDuplicatedSupported(bool on);
    void setMarksVisible(bool on);
    void setParenthesesMatchingEnabled(bool on);
    void setCodeFoldingSupported(bool on);

private:
    friend class BaseTextEditor;
    friend class PlainTextEditorFactory;
    Internal::TextEditorFactoryPrivate *d;
};

class TEXTEDITOR_EXPORT LineColumnButton : public QToolButton
{
public:
    LineColumnButton(TextEditorWidget *parent);
    ~LineColumnButton() override;

private:
    void update();
    bool event(QEvent *event) override;
    QSize sizeHint() const override;

private:
    std::unique_ptr<Internal::LineColumnButtonPrivate> m_d;
};

// Where the caret is in \a editor, as a cursor over its document - selection
// and all. For code that wants a cursor and has no business knowing which view
// the reader is looking at: both the widget editor and the Qt Quick one have a
// caret, and only one of them is a QPlainTextEdit to ask for it. Null where
// \a editor shows no text.
TEXTEDITOR_EXPORT QTextCursor textCursorOf(Core::IEditor *editor);
// The other direction, for code that has a widget and owes somebody an editor.
TEXTEDITOR_EXPORT Core::IEditor *editorForWidget(TextEditorWidget *widget);

// What \a editor's view offers at places in the file, replacing whatever was
// offered for \a type. Through the widget where the view is one, so that it
// keeps painting its own overlay; through the document otherwise, which is
// where TextViewport reads them.
TEXTEDITOR_EXPORT void setRefactorMarkersIn(Core::IEditor *editor, Utils::Id type,
                                            const RefactorMarkers &markers);

// The outline an editor shows in the toolbar: a tree of what is in the file,
// which row the caret is inside, and a way to go to one. A language fills this
// in and parents it to the editor; a view finds it there and draws it.
//
// The editor rather than the document, because the current row follows a caret
// and two views of one file have two.
class TEXTEDITOR_EXPORT ToolBarOutline : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QModelIndex currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentIndexChanged)

public:
    using QObject::QObject;

    virtual QAbstractItemModel *model() const = 0;
    virtual QModelIndex currentIndex() const = 0;
    virtual QString currentText() const = 0;
    // Go to what \a index names, which is what picking a row means.
    Q_INVOKABLE virtual void activate(const QModelIndex &index) = 0;

signals:
    void currentIndexChanged();
};

// A choice the language offers in the toolbar: which of several ways this file
// is being parsed, say. Kept by the *document*, unlike the outline above -
// which of several project parts a file belongs to is a fact about the file,
// and only the row the caret is in follows a view.
class TEXTEDITOR_EXPORT ToolBarChoice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(QString toolTip READ toolTip NOTIFY changed)
    // False where there is nothing to choose between, which is the usual case
    // and is why the widget editor hides its combo.
    Q_PROPERTY(bool available READ isAvailable NOTIFY changed)

public:
    using QObject::QObject;

    virtual QAbstractItemModel *model() const = 0;
    virtual int currentIndex() const = 0;
    virtual QString toolTip() const = 0;
    virtual bool isAvailable() const = 0;
    Q_INVOKABLE virtual void choose(int index) = 0;
    // Forget the choice and go back to what the code model would pick.
    Q_INVOKABLE virtual void clearChoice() = 0;

signals:
    void changed();
};

// What a language does with an edit in one view before the view does anything
// with it - typing inside an in-place rename, where the same edit is made at
// every use of the name at once.
//
// Parented to the editor, the way ToolBarOutline is: what it wraps is a caret
// and the ranges around it, and two views of one file have two of each. A view
// asks the one it finds there.
class TEXTEDITOR_EXPORT EditHandler : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Answer true to have taken the edit. \a processNormally is what the view
    // would have done with the key, for a handler that wraps the ordinary edit
    // - in an undo block, say - rather than replacing it.
    virtual bool handleKeyPress(QKeyEvent *event,
                                const std::function<void()> &processNormally) = 0;
    // The clipboard and Select All, which a handler spanning several places at
    // once has to take as well: a paste replaces the name everywhere, not only
    // where the caret happens to be.
    virtual bool handlePaste() { return false; }
    virtual bool handleCut() { return false; }
    virtual bool handleSelectAll() { return false; }
    // Renaming the symbol under the caret, where the handler is the one that
    // can do it here and now - every use of a local name is on the screen, so
    // there is nothing to search for.
    virtual bool handleRename() { return false; }

    // Whether \a event should reach the view at all, rather than triggering
    // whatever shortcut is bound to it. Asked before Qt's shortcut system
    // runs, which is the only moment at which a key can still be claimed: by
    // the time handleKeyPress() is called the shortcut has already had it.
    // This is what a modal editing mode needs in order to see keys like
    // Ctrl+W that Creator binds elsewhere.
    virtual bool wantsKeyBeforeShortcuts(QKeyEvent *event) { Q_UNUSED(event) return false; }
};

// Paste into / cut from \a editor's view the way the view itself would, with
// nothing the language does with the clipboard in between. What an EditHandler
// calls when it is the one deciding that a paste happens at all - going back
// through the language would come straight back here.
TEXTEDITOR_EXPORT void pasteIn(Core::IEditor *editor);
TEXTEDITOR_EXPORT void cutIn(Core::IEditor *editor);

// Add or remove a hover handler on whichever view \a editor has. A language's
// own handlers are registered on its editor factory and reach every view from
// there; these are the ones that arrive later - a language server's tooltips,
// which come and go with a project - and only the widget editor could be told
// about them. Not owned.
TEXTEDITOR_EXPORT void addHoverHandlerIn(Core::IEditor *editor, BaseHoverHandler *handler);
TEXTEDITOR_EXPORT void removeHoverHandlerIn(Core::IEditor *editor, BaseHoverHandler *handler);

// Whether \a editor's view refuses edits. Not the file's own read-only state,
// which the document answers - this is a caller saying that what it put in a
// scratch editor is there to be read. Both views have the notion; only the
// widget one had a way to be told.
TEXTEDITOR_EXPORT void setReadOnlyIn(Core::IEditor *editor, bool readOnly);

// Follow the symbol the caret is on, in whichever view \a editor has. The
// language's link finder is what answers; this only decides who asks.
TEXTEDITOR_EXPORT void followSymbolUnderCursorIn(Core::IEditor *editor,
                                                 bool inNextSplit = false);

// The suggestion \a editor is showing, if any, and a way to hold them off.
// While the token returned lives, nothing is offered in that editor - what a
// modal editing mode holds outside insert mode. Letting go of it lifts the
// block; nothing suppressed meanwhile is offered again afterwards.
TEXTEDITOR_EXPORT TextSuggestion *currentSuggestionIn(Core::IEditor *editor);
TEXTEDITOR_EXPORT std::shared_ptr<void> blockSuggestionsIn(Core::IEditor *editor);
TEXTEDITOR_EXPORT void clearSuggestionIn(Core::IEditor *editor);

// How much of the file \a editor is showing: whole lines down, and columns of
// its own font across. Zero where \a editor is not a text editor at all.
TEXTEDITOR_EXPORT int visibleRowCountOf(Core::IEditor *editor);
TEXTEDITOR_EXPORT int visibleColumnCountOf(Core::IEditor *editor);

// An editor showing \a document, where one is open - the current editor when
// that is one of them, so that a caller meaning "where the user can see this"
// gets the view being looked at rather than an arbitrary split.
TEXTEDITOR_EXPORT Core::IEditor *editorForDocument(TextDocument *document);

// Rename the symbol the caret is on, in whichever view \a editor has. Which
// rename that is - every use of a local name at once in the view, or a search
// across the project - is the language's to decide.
TEXTEDITOR_EXPORT void renameSymbolUnderCursorIn(Core::IEditor *editor);

// What a key press should be sent to for \a editor: the widget, or the Quick
// item - not the QQuickWidget wrapping that item, which forwards nothing.
TEXTEDITOR_EXPORT QObject *keyTargetOf(Core::IEditor *editor);

// Put the caret in \a editor's view where \a cursor is, and ask its language
// for \a kind. Both dispatch on the view the same way the pair above does.
TEXTEDITOR_EXPORT void setTextCursorOf(Core::IEditor *editor, const QTextCursor &cursor);
// \a provider is the one to ask, where the caller has a particular one in
// mind - the choice of override behind a virtual call is a proposal from a
// provider of its own rather than the document's.
TEXTEDITOR_EXPORT void invokeAssistIn(Core::IEditor *editor, AssistKind kind,
                                      IAssistProvider *provider = nullptr);

// Go to \a link the way \a editor's view would: a jump within the same file,
// or the editor manager for anything else. Answers whether it went anywhere.
TEXTEDITOR_EXPORT bool openLinkInEditor(Core::IEditor *editor, const Utils::Link &link,
                                        bool inNextSplit = false);

// Ranges drawn differently in \a editor's view, replacing whatever was drawn
// for \a kind. This view rather than the document, because these follow a
// caret - where else the symbol under it is used - and two views of one file
// have two carets. TextDocument::setExtraSelections() is the other half of the
// pair, for the ones that are facts about the file.
TEXTEDITOR_EXPORT void setViewSelections(Core::IEditor *editor, Utils::Id kind,
                                         const QList<TextDocument::ExtraSelection> &selections);
TEXTEDITOR_EXPORT QList<TextDocument::ExtraSelection> viewSelections(Core::IEditor *editor,
                                                                     Utils::Id kind);

} // namespace TextEditor

QT_BEGIN_NAMESPACE

size_t qHash(const QColor &color);

QT_END_NAMESPACE
