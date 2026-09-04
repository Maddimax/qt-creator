// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quicktexteditor.h"

#include "colorpreviewhoverhandler.h"

#include "codesource.h"
#include "textdocument.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"
#include "textindenter.h"
#include "codestylepool.h"
#include "autocompleter.h"
#include "codeassist/assistproposalitem.h"
#include "codeassist/assisttarget.h"
#include "codeassist/documentcontentcompletion.h"
#include "codeassist/genericproposal.h"
#include "codeassist/genericproposalmodel.h"
#include "codeassist/iassistprocessor.h"
#include "codeassist/iassistprovider.h"
#include "completionsettings.h"
#include "behaviorsettings.h"
#include "displaysettings.h"
#include "extraencodingsettings.h"
#include "highlighter.h"
#include "icodestylepreferences.h"
#include "storagesettings.h"
#include "texteditor.h"
#include "typingsettings.h"
#include "highlighterhelper.h"
#include "textdocumentlayout.h"
#include "refactoringchanges.h"
#include "suggestionhost.h"
#include "symbolrequests.h"
#include "textsuggestion.h"
#include "typehierarchy.h"
#include "linenumberfilter.h"
#include "textviewport.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/locator/locatormanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>

#include <QMainWindow>
#include <coreplugin/dialogs/codecselector.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>

#include <qtcquick/actionmodel.h>
#include <qtcquick/qtcquickwidget.h>

#include "fontsettings.h"

#include <coreplugin/find/basetextfind.h>
#include <coreplugin/find/ifindsupport.h>

#include <utils/aggregate.h>
#include <utils/changeset.h>
#include <utils/textutils.h>
#include <utils/algorithm.h>
#include <utils/theme/theme.h>
#include <utils/mimeutils.h>
#include <utils/macroexpander.h>
#include <utils/temporarydirectory.h>

#include <QDataStream>
#include <QMenu>
#include <QQuickItem>
#include <QClipboard>
#include <QQuickWidget>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTextEdit>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <memory>

namespace TextEditor::Internal {

const char QUICK_TEXT_EDITOR_ID[] = "TextEditor.QuickTextEditor";

// A view of a document somebody else owns. CodeDocument opens a file and
// CodeBuffer holds text of its own; an editor's document is opened by the
// editor manager before the editor is ever shown, so this only points at it.
class AdoptedSource final : public CodeSource
{
public:
    explicit AdoptedSource(TextDocument *document)
        : m_document(document)
    {}

    TextDocument *textDocument() const final { return m_document; }

private:
    TextDocument * const m_document;
};

const char QUICK_FIND_HIGHLIGHTS[] = "TextEditor.QuickTextEditor.FindResults";

// C_SEARCH_RESULT is one of the scheme's *overlay* categories, so its
// QTextCharFormat is deliberately near-empty - FontSettingsData::
// toTextCharFormat() drops the foreground for those, because the widget editor
// paints them as an overlay rather than as a format. This draws a format range,
// so it takes the same colour the overlay does, darkened the same way.
//
// And it falls back rather than leaving the format empty: a scheme that names
// no search colour would otherwise highlight nothing at all, silently. The
// same trap as an unset current-line brush, met a third time.
static QTextCharFormat searchResultFormat(const FontSettingsData &fonts)
{
    const QBrush brush = fonts.toTextCharFormat(C_SEARCH_RESULT).background();
    const bool named = brush.style() != Qt::NoBrush && brush.color().isValid();

    QTextCharFormat format;
    format.setBackground(named ? brush.color().darker(120)
                               : Utils::creatorColor(
                                     Utils::Theme::TextEditor_SearchResult_ScrollBarColor));
    return format;
}

// Ctrl+F, over a viewport rather than over a widget. BaseTextFindBase asks for
// five things - a cursor, a document, whether it is read-only, and a widget to
// anchor the find bar to - none of which needs the editor to *be* a widget.
class QuickTextFind final : public Core::BaseTextFindBase
{
public:
    QuickTextFind(TextViewport *viewport, QWidget *host)
        : m_viewport(viewport)
        , m_host(host)
    {
        // BaseTextFindBase draws nothing itself: it asks, and whoever can draw
        // answers. This is that answer.
        setResultHighlightingEnabled(true);
        connect(this, &Core::BaseTextFindBase::highlightAllRequested,
                this, &QuickTextFind::highlightMatches);
    }

private:
    QTextCursor textCursor() const final
    {
        return m_viewport ? m_viewport->textCursor() : QTextCursor();
    }

    void setTextCursor(const QTextCursor &cursor) final
    {
        if (m_viewport)
            m_viewport->setTextCursor(cursor);
    }

    QTextDocument *document() const final
    {
        TextDocument * const doc = m_viewport && m_viewport->document()
                                       ? m_viewport->document()->textDocument()
                                       : nullptr;
        return doc ? doc->document() : nullptr;
    }

    bool isReadOnly() const final { return !m_viewport || m_viewport->isReadOnly(); }
    QWidget *widget() const final { return m_host; }

    void highlightMatches(const QString &text, Utils::FindFlags flags)
    {
        QTextDocument * const doc = document();
        if (!m_viewport || !doc)
            return;

        QList<TextViewport::Highlight> found;
        if (!text.isEmpty()) {
            const QTextCharFormat format
                = searchResultFormat(m_viewport->document()->textDocument()->fontSettings());
            const QRegularExpression expression
                = Core::BaseTextFindBase::regularExpression(text, flags);
            QTextDocument::FindFlags options
                = flags & Utils::FindCaseSensitively ? QTextDocument::FindCaseSensitively
                                                     : QTextDocument::FindFlags();
            if (flags & Utils::FindWholeWords)
                options |= QTextDocument::FindWholeWords;

            QTextCursor at(doc);
            while (!(at = doc->find(expression, at, options)).isNull()) {
                if (inScope(at))
                    found.append({at.selectionStart(), at.selectionEnd(), format});
                if (at.selectionEnd() == at.selectionStart())
                    at.movePosition(QTextCursor::NextCharacter); // an empty match never advances
            }
        }
        // On the scroll bar too, in the colour the theme keeps for that -
        // which is not the one they are drawn in, so that a match off screen
        // still says where it is.
        m_viewport->setHighlights(
            QUICK_FIND_HIGHLIGHTS, found,
            Utils::creatorColor(Utils::Theme::TextEditor_SearchResult_ScrollBarColor));
    }

    const QPointer<TextViewport> m_viewport;
    QWidget * const m_host;
};

class QuickTextEditor final : public Core::IEditor
{
    Q_OBJECT

public:
    QuickTextEditor()
        : QuickTextEditor(TextDocumentPtr(new TextDocument(QUICK_TEXT_EDITOR_ID)))
    {}

    // A split view is two editors on one document, so duplicating shares it
    // rather than opening the file again.
    explicit QuickTextEditor(TextDocumentPtr document,
                             uint optionalActions = OptionalActions::None)
        : m_document(std::move(document))
        , m_source(std::make_unique<AdoptedSource>(m_document.get()))
        , m_optionalActions(optionalActions)
    {
        // duplicate() answers, so say so: the editor manager asks this rather
        // than trying, and an editor it thinks cannot be duplicated is moved
        // between split views instead of copied - which loses the tab the
        // first view was keeping for it.
        setDuplicateSupported(true);

        // What a right click offers. Taken from the same place the widget
        // editor takes it, and asked again each time the menu opens, because
        // the ActionManager's containers gain entries as plugins register
        // them.
        m_contextActions.setProvider([] {
            Core::ActionContainer * const container
                = Core::ActionManager::actionContainer(Constants::M_STANDARDCONTEXTMENU);
            return container && container->menu() ? container->menu()->actions()
                                                  : QList<QAction *>();
        });

        auto widget = new QtcQuick::QuickWidget;
        // Set before the source: the form's root property is required, and a
        // required property has to be there when the component is created.
        widget->quickWidget()->setInitialProperties(
            {{"source", QVariant::fromValue(m_source.get())},
             {"contextActions", QVariant::fromValue(&m_contextActions)},
             {"wrapLines", displaySettings().textWrapping()},
             {"showLineNumbers", displaySettings().displayLineNumbers()},
             {"showFoldMarkers", displaySettings().displayFoldingMarkers()},
             {"highlightCurrentLine", displaySettings().highlightCurrentLine()},
             {"showAnnotations", displaySettings().displayAnnotations()}});
        widget->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/MainEditor.qml"));
        // Before anything that configures the view: viewport() looks through
        // widget(), so everything below this line would silently do nothing.
        setWidget(widget);

        // A tooltip is a widget and has to be placed in screen coordinates.
        // The item is in a QQuickWidget's offscreen window, whose own position
        // is meaningless, so the hosting widget is what it maps through.
        // Qt offers the focus widget a ShortcutOverride before it fires a
        // shortcut, and that offer is the only moment at which a key can still
        // be claimed for the editor. The QQuickWidget is the focus widget; the
        // item inside it never sees the offer, so the wrapper passes it on.
        widget->quickWidget()->installEventFilter(this);

        if (TextViewport * const view = viewport()) {
            view->setTooltipHost(widget->quickWidget());
            // So that the view can reach what a language parents to the editor
            // rather than to the document - see TextEditor::KeyHandler.
            view->setEditor(this);
            // Whoever follows the caret - the outline, the type hierarchy -
            // listens to the editor rather than to a widget.
            connect(view, &TextViewport::cursorPositionChanged,
                    this, &Core::IEditor::cursorPositionChanged);
            connect(view, &TextViewport::cursorPositionChanged,
                    this, &QuickTextEditor::recordAJumpTheReaderHasLeft);
        }

        // Preferences are pushed into a document, not read from one, so an
        // editor that pushes nothing saves and indents differently from every
        // other editor in Creator without saying so. The widget editor
        // connects to exactly these; the ones it keeps for itself - display,
        // margin, completion - are widget state rather than the document's.
        const QList<Utils::AspectContainer *> containers{&globalStorageSettings(),
                                                         &globalTypingSettings(),
                                                         &globalExtraEncodingSettings()};
        for (Utils::AspectContainer * const settings : containers) {
            connect(settings, &Utils::AspectContainer::changed, this, [this] {
                applyGlobalSettings();
            });
        }
        applyGlobalSettings();
        applyWhitespaceVisualization();

        // Changing any of these in Preferences has to reach an editor that is
        // already open, not only the next one to be built.
        const auto pushDisplaySettings = [widget] {
            QQuickItem * const form = widget->quickWidget()->rootObject();
            if (!form)
                return;
            form->setProperty("wrapLines", displaySettings().textWrapping());
            form->setProperty("showLineNumbers", displaySettings().displayLineNumbers());
            form->setProperty("showFoldMarkers", displaySettings().displayFoldingMarkers());
            form->setProperty("highlightCurrentLine", displaySettings().highlightCurrentLine());
            form->setProperty("showAnnotations", displaySettings().displayAnnotations());
        };
        connect(&displaySettings(), &Utils::AspectContainer::changed, this,
                [this, pushDisplaySettings] {
                    pushDisplaySettings();
                    applyWhitespaceVisualization();
                });

        // Which language to colour the file as comes from its mime type,
        // and the editor manager opens the document *after* building the
        // editor - so this waits for the path rather than reading it now.
        // TextEditorFactory does the same thing for the widget editor, which
        // is why a document built outside one is never highlighted at all.
        // The highlighter first: it is what puts the mime type on the document,
        // and the language's services are looked up by mime type.
        connect(m_document.get(), &Core::IDocument::filePathChanged, this, [this] {
            configureHighlighter();
            configureLanguageServices();
        });

        // Where the reader last changed something, which is what Go to Last
        // Edit goes to. TextEditorWidget sets this from its own cursor handler,
        // off a flag its code calls too heavy; an edit on the document says the
        // same thing without one.
        //
        // Not covered by a test: what reads it back is
        // EditorManagerPrivate::gotoLastEditLocation(), which is private to
        // Core and reachable only through a command, and a command does not
        // route to an editor that no window has given focus to.
        connect(m_document->document(), &QTextDocument::contentsChanged, this, [this] {
            if (Core::EditorManager::currentEditor() == this)
                Core::EditorManager::setLastEditLocation(this);
        });
        configureHighlighter();
        configureLanguageServices();

        // Two contexts: the shared one, which is what a command meant for
        // "any editor of this kind" uses, and one of this editor's own. A
        // per-editor action has to be registered against the second, or the
        // next editor of the same kind collides with it - the widget editor
        // generates an id per instance for exactly this reason.
        setContext(Core::Context(QUICK_TEXT_EDITOR_ID, m_editorContext));
        // The Wrap Lines menu item. The widget editor registers the same
        // command in its own context and toggles that editor rather than the
        // preference, so this does the same: without it the menu entry is
        // simply dead whenever a Quick editor is the current one.
        // Per editor rather than per setting, which is what the widget
        // editor's entry does: showing whitespace in the file being read
        // should not turn it on everywhere.
        m_whitespaceAction = Core::ActionBuilder(this, Constants::VISUALIZE_WHITESPACE)
                                 .setContext(Core::Context(m_editorContext))
                                 .setCheckable(true)
                                 .addOnToggled(this, [this](bool checked) {
                                     if (TextViewport * const view = viewport())
                                         view->setVisualizeWhitespace(checked);
                                 })
                                 .contextAction();
        m_whitespaceAction->setChecked(displaySettings().visualizeWhitespace());

        m_wrapAction = Core::ActionBuilder(this, Constants::TEXT_WRAPPING)
                           .setContext(Core::Context(m_editorContext))
                           .setCheckable(true)
                           .addOnToggled(this, [this](bool checked) {
                               if (TextViewport * const view = viewport())
                                   view->setWrapping(checked);
                           })
                           .contextAction();
        m_wrapAction->setChecked(displaySettings().textWrapping());

        // Follow Symbol. The action is global and its shortcut is the user's;
        // what it does here is ask the file's language where the symbol is,
        // which is what the widget editor's findLinkAt() override used to be.
        const auto followSymbol = [this](Utils::Id id, bool inNextSplit, uint needs) {
            gate(Core::ActionBuilder(this, id)
                     .setContext(Core::Context(m_editorContext))
                     .addOnTriggered(this, [this, inNextSplit] {
                         if (TextViewport * const view = viewport())
                             view->followSymbolUnderCursor(view->opensInNextSplit(inNextSplit));
                     })
                     .contextAction(),
                 needs);
        };
        // The same action the widget editor answers; which editor is current
        // decides who does. Without this the menu entry is dead whenever a
        // Quick editor is the current one.
        Core::ActionBuilder(this, Constants::ADD_CURSORS_TO_LINE_ENDS)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->addCaretsToLineEnds();
            });

        // Taking the suggestion that is showing. Disabled until one is,
        // because their shortcuts are Tab, Shift+Tab and the next-word key,
        // which have to go on meaning what they usually mean the rest of the
        // time - an enabled shortcut is taken before the key ever reaches the
        // view.
        const auto suggestionCommand = [this](Utils::Id id, const QString &text,
                                              const QString &tip,
                                              void (TextViewport::*take)()) {
            m_suggestionActions << Core::ActionBuilder(this, id)
                                       .setContext(Core::Context(m_editorContext))
                                       .setText(text)
                                       .setToolTip(tip)
                                       .addOnTriggered(this, [this, take] {
                                           if (TextViewport * const view = viewport())
                                               (view->*take)();
                                       })
                                       .setScriptable(true)
                                       .setEnabled(false)
                                       .contextAction();
        };
        suggestionCommand(Constants::SUGGESTION_APPLY, Tr::tr("Apply"),
                          Tr::tr("Apply the current suggestion."),
                          &TextViewport::applySuggestion);
        suggestionCommand(Constants::SUGGESTION_APPLY_WORD, Tr::tr("Apply one Word"),
                          Tr::tr("Apply one word of the current suggestion."),
                          &TextViewport::applySuggestionWord);
        suggestionCommand(Constants::SUGGESTION_APPLY_LINE, Tr::tr("Apply Line"),
                          Tr::tr("Apply one line of the current suggestion."),
                          &TextViewport::applySuggestionLine);
        if (TextViewport * const view = viewport()) {
            connect(view, &TextViewport::suggestionChanged, this, [this] {
                TextViewport * const view = viewport();
                const bool takeable = view && view->currentSuggestion();
                for (const QPointer<QAction> &action : std::as_const(m_suggestionActions)) {
                    if (action)
                        action->setEnabled(takeable);
                }
            });
        }

        // The line commands. Each is the widget editor's menu entry answered
        // in this editor's context, so that it stops being dead when a Quick
        // editor is the current one.
        const auto command = [this](Utils::Id id, void (TextViewport::*run)(),
                                    uint needs = OptionalActions::None) {
            gate(Core::ActionBuilder(this, id)
                     .setContext(Core::Context(m_editorContext))
                     .addOnTriggered(this, [this, run] {
                         if (TextViewport * const view = viewport())
                             (view->*run)();
                     })
                     .contextAction(),
                 needs);
        };
        command(Constants::UPPERCASE_SELECTION, &TextViewport::uppercaseSelection);
        command(Constants::LOWERCASE_SELECTION, &TextViewport::lowercaseSelection);
        command(Constants::INSERT_LINE_ABOVE, &TextViewport::insertLineAbove);
        command(Constants::INSERT_LINE_BELOW, &TextViewport::insertLineBelow);
        command(Constants::DUPLICATE_SELECTION, &TextViewport::duplicateSelection);
        command(Constants::SORT_LINES, &TextViewport::sortLines);
        command(Constants::UN_COMMENT_SELECTION, &TextViewport::unCommentSelection,
                OptionalActions::UnCommentSelection);
        command(Constants::DUPLICATE_SELECTION_AND_COMMENT,
                &TextViewport::duplicateSelectionAndComment);
        command(Constants::DELETE_LINE, &TextViewport::deleteLine);
        command(Constants::COPY_LINE, &TextViewport::copyLine);
        command(Constants::CUT_LINE, &TextViewport::cutLine);
        command(Constants::COPY_LINE_UP, &TextViewport::copyLineUp);
        command(Constants::COPY_LINE_DOWN, &TextViewport::copyLineDown);
        command(Constants::MOVE_LINE_UP, &TextViewport::moveLineUp);
        command(Constants::MOVE_LINE_DOWN, &TextViewport::moveLineDown);
        command(Constants::REWRAP_PARAGRAPH, &TextViewport::rewrapParagraph);

        // Deleting as far as a move would go. The movement commands above and
        // these are the same operations; what differs is whether the anchor
        // is kept and what is under it taken out.
        command(Constants::DELETE_END_OF_LINE, &TextViewport::deleteEndOfLine);
        command(Constants::DELETE_START_OF_LINE, &TextViewport::deleteStartOfLine);
        command(Constants::DELETE_END_OF_WORD, &TextViewport::deleteEndOfWord);
        command(Constants::DELETE_START_OF_WORD, &TextViewport::deleteStartOfWord);
        command(Constants::DELETE_END_OF_WORD_CAMEL_CASE,
                &TextViewport::deleteEndOfWordCamelCase);
        command(Constants::DELETE_START_OF_WORD_CAMEL_CASE,
                &TextViewport::deleteStartOfWordCamelCase);

        command(Constants::INDENT, &TextViewport::indent);
        command(Constants::UNINDENT, &TextViewport::unindent);
        command(Constants::AUTO_INDENT_SELECTION, &TextViewport::autoIndent,
                OptionalActions::Format);
        command(Constants::AUTO_FORMAT_SELECTION, &TextViewport::autoFormat,
                OptionalActions::Format);

        command(Core::Constants::ZOOM_IN, &TextViewport::increaseFontZoom);
        command(Core::Constants::ZOOM_OUT, &TextViewport::decreaseFontZoom);
        command(Core::Constants::ZOOM_RESET, &TextViewport::resetFontZoom);

        command(Constants::CLEAN_WHITESPACE, &TextViewport::cleanWhitespace);
        command(Constants::NO_FORMAT_PASTE, &TextViewport::pasteWithoutFormat);
        command(Constants::SHOWCONTEXTMENU, &TextViewport::showContextMenu);

        // Folding. The recursive pair take the same method with its argument
        // set, which is why they are built here rather than through command().
        const auto folding = [this](Utils::Id id, bool unfold, bool recursive) {
            Core::ActionBuilder(this, id)
                .setContext(Core::Context(m_editorContext))
                .addOnTriggered(this, [this, unfold, recursive] {
                    TextViewport * const view = viewport();
                    if (!view)
                        return;
                    if (unfold)
                        view->unfoldCurrentBlock(recursive);
                    else
                        view->foldCurrentBlock(recursive);
                });
        };
        folding(Constants::FOLD, false, false);
        folding(Constants::UNFOLD, true, false);
        folding(Constants::FOLD_RECURSIVELY, false, true);
        folding(Constants::UNFOLD_RECURSIVELY, true, true);
        command(Constants::UNFOLD_ALL, &TextViewport::toggleFoldAll,
                OptionalActions::UnCollapseAll);

        command(Constants::SELECT_WORD_UNDER_CURSOR, &TextViewport::selectWordUnderCursor);
        command(Constants::CLEAR_SELECTION, &TextViewport::clearSelection);

        // The bracket commands. Growing and shrinking return whether they
        // could, which the menu entry does not care about.
        const auto block = [this](Utils::Id id, void (TextViewport::*run)(bool), bool select) {
            Core::ActionBuilder(this, id)
                .setContext(Core::Context(m_editorContext))
                .addOnTriggered(this, [this, run, select] {
                    if (TextViewport * const view = viewport())
                        (view->*run)(select);
                });
        };
        block(Constants::GOTO_BLOCK_START, &TextViewport::gotoBlockStart, false);
        block(Constants::GOTO_BLOCK_START_WITH_SELECTION, &TextViewport::gotoBlockStart, true);
        block(Constants::GOTO_BLOCK_END, &TextViewport::gotoBlockEnd, false);
        block(Constants::GOTO_BLOCK_END_WITH_SELECTION, &TextViewport::gotoBlockEnd, true);
        Core::ActionBuilder(this, Constants::SELECT_BLOCK_UP)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] { growSelectionIn(this); });
        Core::ActionBuilder(this, Constants::SELECT_BLOCK_DOWN)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] { shrinkSelectionIn(this); });
        command(Constants::VIEW_PAGE_UP, &TextViewport::viewPageUp);
        command(Constants::VIEW_PAGE_DOWN, &TextViewport::viewPageDown);
        command(Constants::VIEW_LINE_UP, &TextViewport::viewLineUp);
        command(Constants::VIEW_LINE_DOWN, &TextViewport::viewLineDown);

        // Home is not a plain move to the start of the line, so it does not
        // go through the movement table above.
        Core::ActionBuilder(this, Constants::GOTO_LINE_START)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->gotoLineStart(QTextCursor::MoveAnchor);
            });
        Core::ActionBuilder(this, Constants::GOTO_LINE_START_WITH_SELECTION)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->gotoLineStart(QTextCursor::KeepAnchor);
            });

        // The editing commands every editor answers. These keys already
        // reached the viewport on their own, and a shortcut is handled before
        // the key gets there - so these must run the very code the key
        // handler runs, or registering them would quietly replace it.
        command(Core::Constants::SELECTALL, &TextViewport::selectAll);
        command(Core::Constants::COPY, &TextViewport::copy);
        command(Core::Constants::CUT, &TextViewport::cut);
        command(Core::Constants::PASTE, &TextViewport::paste);
        command(Core::Constants::UNDO, &TextViewport::undo);
        command(Core::Constants::REDO, &TextViewport::redo);

        // Moving about. Same reasoning as the editing commands above and the
        // same requirement: these go through the movePosition() the key
        // handler ends up in, so a shortcut and its key cannot drift apart.
        const auto movement = [this](Utils::Id id, QTextCursor::MoveOperation operation,
                                     QTextCursor::MoveMode mode) {
            Core::ActionBuilder(this, id)
                .setContext(Core::Context(m_editorContext))
                .addOnTriggered(this, [this, operation, mode] {
                    if (TextViewport * const view = viewport())
                        view->moveCursor(operation, mode);
                });
        };
        const auto camelCase = [this](Utils::Id id, bool forward, QTextCursor::MoveMode mode) {
            Core::ActionBuilder(this, id)
                .setContext(Core::Context(m_editorContext))
                .addOnTriggered(this, [this, forward, mode] {
                    if (TextViewport * const view = viewport())
                        view->moveCamelCase(forward, mode);
                });
        };
        const QTextCursor::MoveMode keep = QTextCursor::KeepAnchor;
        const QTextCursor::MoveMode move = QTextCursor::MoveAnchor;
        movement(Constants::GOTO_DOCUMENT_END, QTextCursor::End, move);
        movement(Constants::GOTO_DOCUMENT_START, QTextCursor::Start, move);
        movement(Constants::GOTO_LINE_END, QTextCursor::EndOfLine, move);
        movement(Constants::GOTO_LINE_END_WITH_SELECTION, QTextCursor::EndOfLine, keep);
        movement(Constants::GOTO_NEXT_CHARACTER, QTextCursor::NextCharacter, move);
        movement(Constants::GOTO_NEXT_CHARACTER_WITH_SELECTION, QTextCursor::NextCharacter, keep);
        movement(Constants::GOTO_NEXT_LINE, QTextCursor::Down, move);
        movement(Constants::GOTO_NEXT_LINE_WITH_SELECTION, QTextCursor::Down, keep);
        movement(Constants::GOTO_NEXT_WORD, QTextCursor::NextWord, move);
        movement(Constants::GOTO_NEXT_WORD_WITH_SELECTION, QTextCursor::NextWord, keep);
        movement(Constants::GOTO_PREVIOUS_CHARACTER, QTextCursor::PreviousCharacter, move);
        movement(Constants::GOTO_PREVIOUS_CHARACTER_WITH_SELECTION,
                 QTextCursor::PreviousCharacter, keep);
        movement(Constants::GOTO_PREVIOUS_LINE, QTextCursor::Up, move);
        movement(Constants::GOTO_PREVIOUS_LINE_WITH_SELECTION, QTextCursor::Up, keep);
        movement(Constants::GOTO_PREVIOUS_WORD, QTextCursor::PreviousWord, move);
        movement(Constants::GOTO_PREVIOUS_WORD_WITH_SELECTION, QTextCursor::PreviousWord, keep);
        camelCase(Constants::GOTO_NEXT_WORD_CAMEL_CASE, true, move);
        camelCase(Constants::GOTO_NEXT_WORD_CAMEL_CASE_WITH_SELECTION, true, keep);
        camelCase(Constants::GOTO_PREVIOUS_WORD_CAMEL_CASE, false, move);
        camelCase(Constants::GOTO_PREVIOUS_WORD_CAMEL_CASE_WITH_SELECTION, false, keep);

        Core::ActionBuilder(this, Constants::JOIN_LINES)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->joinLines();
            });

        Core::ActionBuilder(this, Constants::ADD_SELECT_NEXT_FIND_MATCH)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->addCaretAtNextMatch();
            });

        // Ctrl+Space already reaches the viewport as a key and the form
        // already draws what comes back; what was missing was the menu entry
        // and any shortcut the reader has bound instead. Same method the key
        // handler calls, so the two cannot come to mean different things.
        command(Constants::COMPLETE_THIS, &TextViewport::requestCompletions);
        // Its own lambda rather than command(): asking for quick fixes takes
        // an optional provider now, so the member pointer no longer fits the
        // no-argument shape.
        Core::ActionBuilder(this, Constants::QUICKFIX_THIS)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [this] {
                if (TextViewport * const view = viewport())
                    view->requestQuickFixes();
            });
        command(Constants::CIRCULAR_PASTE, &TextViewport::circularPaste);
        command(Constants::SWITCH_UTF8BOM, &TextViewport::switchUtf8Bom);
        command(Constants::COPY_WITH_HTML, &TextViewport::copyWithHtml);
        command(Constants::SELECT_ENCODING, &TextViewport::selectEncoding);

        // Asks the locator rather than this editor - the line to go to is
        // typed there. Registered per editor, so it was dead here only for
        // want of registering.
        Core::ActionBuilder(this, Core::Constants::GOTO)
            .setContext(Core::Context(m_editorContext))
            .addOnTriggered(this, [] {
                Core::LocatorManager::showFilter(lineNumberFilter());
            });
        command(Constants::FUNCTION_HINT, &TextViewport::requestFunctionHint);

        command(Constants::FIND_USAGES, &TextViewport::findUsages,
                OptionalActions::FindUsage);
        command(Constants::RENAME_SYMBOL, &TextViewport::renameSymbolUnderCursor,
                OptionalActions::RenameSymbol);
        command(Constants::OPEN_CALL_HIERARCHY, &TextViewport::openCallHierarchy,
                OptionalActions::CallHierarchy);

        followSymbol(Constants::FOLLOW_SYMBOL_UNDER_CURSOR, false,
                     OptionalActions::FollowSymbolUnderCursor);
        followSymbol(Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT, true,
                     OptionalActions::FollowSymbolUnderCursor);
        // The same operation the widget editor gives two entries: following a
        // symbol and jumping to the file under the cursor are one question
        // asked by two shortcuts.
        followSymbol(Constants::JUMP_TO_FILE_UNDER_CURSOR, false,
                     OptionalActions::JumpToFileUnderCursor);
        followSymbol(Constants::JUMP_TO_FILE_UNDER_CURSOR_IN_NEXT_SPLIT, true,
                     OptionalActions::JumpToFileUnderCursor);

        // Where the type of the symbol is, which is a different question.
        const auto followType = [this](Utils::Id id, bool inNextSplit) {
            gate(Core::ActionBuilder(this, id)
                     .setContext(Core::Context(m_editorContext))
                     .addOnTriggered(this, [this, inNextSplit] {
                         if (TextViewport * const view = viewport())
                             view->followTypeUnderCursor(view->opensInNextSplit(inNextSplit));
                     })
                     .contextAction(),
                 OptionalActions::FollowTypeUnderCursor);
        };
        followType(Constants::FOLLOW_SYMBOL_TO_TYPE, false);
        followType(Constants::FOLLOW_SYMBOL_TO_TYPE_IN_NEXT_SPLIT, true);

        // Not a question for the view at all - it opens a pane and tells it
        // to look at whatever is current - but it is registered per editor,
        // so it is dead here until this editor registers it too.
        gate(Core::ActionBuilder(this, Constants::OPEN_TYPE_HIERARCHY)
                 .setContext(Core::Context(m_editorContext))
                 .addOnTriggered(this, [] {
                     updateTypeHierarchy(Core::NavigationWidget::activateSubWidget(
                         Constants::TYPE_HIERARCHY_FACTORY_ID, Core::Side::Left));
                 })
                 .contextAction(),
             OptionalActions::TypeHierarchy);

        // Ctrl+F reaches an editor by asking its widget for an IFindSupport,
        // so this has to hang off the widget rather than off the editor.
        if (TextViewport * const view = viewport()) {
            Utils::Aggregation::aggregate({widget, new QuickTextFind(view, widget)});
            // Two of the gated commands edit, so being allowed to is part of
            // whether they are offered - the widget editor asks the same
            // question in updateActions().
            connect(view, &TextViewport::readOnlyChanged, this, [this] {
                updateOptionalActions();
            });
        }
        connect(m_document.get(), &Core::IDocument::changed, this, [this] {
            updateOptionalActions();
        });
        updateOptionalActions();
    }

    // See the ShortcutOverride note in the constructor: accepting the offer is
    // what stops the shortcut and lets the key arrive as an ordinary key press.
    bool eventFilter(QObject *watched, QEvent *event) final
    {
        if (event->type() == QEvent::ShortcutOverride) {
            if (TextViewport * const view = viewport()) {
                if (view->wantsKeyBeforeShortcuts(static_cast<QKeyEvent *>(event))) {
                    event->accept();
                    return true;
                }
            }
        }
        return Core::IEditor::eventFilter(watched, event);
    }

    Core::IDocument *document() const final { return m_document.get(); }

    // A command only some languages can answer, and the bit of the factory's
    // mask that says whether this one does. Registered either way, so that the
    // menu entry keeps its place and its shortcut; disabled where the language
    // has nothing to answer with, which is what the widget editor does.
    void gate(QAction *action, uint needs)
    {
        if (needs != OptionalActions::None)
            m_gatedActions.append({action, needs});
    }

    void updateOptionalActions()
    {
        TextViewport * const view = viewport();
        const bool writable = view && view->canEdit();
        for (const auto &[action, needs] : std::as_const(m_gatedActions)) {
            if (!action)
                continue;
            const bool edits = needs & (OptionalActions::Format
                                        | OptionalActions::UnCommentSelection);
            action->setEnabled((m_optionalActions & needs) && (!edits || writable));
        }
    }

    // The editor's own part of the toolbar row. Built on demand and once: the
    // editor manager asks whenever this editor becomes current, and a new one
    // each time would drop whatever the previous one was showing.
    QWidget *toolBar() final
    {
        if (m_toolBar)
            return m_toolBar;
        TextViewport * const view = viewport();
        if (!view)
            return nullptr;

        // What the language wants there, beside what the toolbar shows of its
        // own. Asked again each time, because a document gains them as it is
        // configured - the same reason the context menu is asked again.
        m_toolBarActions.setProvider([this] {
            TextDocument * const doc = m_document.get();
            return doc ? doc->toolBarActions() : QList<QAction *>();
        });
        // And asked again when the list itself changes, not only when the
        // toolbar is built: a language client attaches to a document after it
        // is open, so its button arrives later than this row does.
        if (TextDocument * const doc = m_document.get()) {
            connect(doc, &TextDocument::toolBarActionsChanged,
                    &m_toolBarActions, &QtcQuick::ActionModel::refresh);
        }

        // The outline the language keeps for this editor, where it keeps one.
        // Parented to the editor by whoever made it, which is why it is found
        // rather than handed over - the editor is built before the language
        // has anything to say about the file it will hold. For the same reason
        // it can arrive after this row does, which childEvent() below answers.
        ToolBarOutline * const outline = findChild<ToolBarOutline *>();

        auto bar = new QtcQuick::QuickWidget;
        bar->quickWidget()->setInitialProperties(
            {{"viewport", QVariant::fromValue(view)},
             {"languageActions", QVariant::fromValue(&m_toolBarActions)},
             {"outline", QVariant::fromValue(outline)},
             {"choice", QVariant::fromValue(m_document->toolBarChoice())}});
        bar->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/EditorToolBar.qml"));
        m_toolBar = bar;
        return m_toolBar;
    }

    Core::IEditor *duplicate() final { return new QuickTextEditor(m_document); }

    QString selectedText() const final
    {
        TextViewport * const view = viewport();
        return view ? view->selectedText() : QString();
    }

    int currentLine() const final
    {
        TextViewport * const view = viewport();
        return view ? view->cursorLine() : 0;
    }

    int currentColumn() const final
    {
        TextViewport * const view = viewport();
        return view ? view->cursorColumn() : 0;
    }

    // What every jump into a file goes through: a search result, a compiler
    // message, go-to-definition, the locator.
    void gotoLine(int line, int column, bool centerLine) final
    {
        if (TextViewport * const view = viewport()) {
            view->gotoLine(line, column, centerLine);
            rememberThisAsSomewhereJumpedTo();
        }
    }

    // Where a jump landed, kept until the reader moves off it. Go Back is
    // meant to return to the place a search result or a definition took them
    // to, and the manager only learns of it when they leave - recording it on
    // arrival would put the entry in front of the caret that is still on it.
    void rememberThisAsSomewhereJumpedTo()
    {
        m_stateOfAJumpNotYetLeft = saveState();
        m_jumpedHereAndStillOnIt = true;
    }

    void recordAJumpTheReaderHasLeft()
    {
        if (!m_jumpedHereAndStillOnIt)
            return;
        m_jumpedHereAndStillOnIt = false;
        // The manager records "the current position", so a view the reader is
        // not in would push somewhere they never were. Same guard as the one
        // on a jump inside one file.
        if (Core::EditorManager::currentEditor() == this) {
            Core::EditorManager::addCurrentPositionToNavigationHistory(
                m_stateOfAJumpNotYetLeft);
        }
    }

    // Where the reader was, so that closing and reopening - or stepping back
    // through the navigation history - returns them to it. A private format:
    // only this editor is ever handed it back.
    QByteArray saveState() const final
    {
        TextViewport * const view = viewport();
        if (!view)
            return {};

        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << kStateVersion << view->cursorPosition() << view->scrollY();
        return state;
    }

    void restoreState(const QByteArray &state) final
    {
        if (state.isEmpty()) {
            // Opened rather than reopened: there are no folds to put back, so
            // this is where the licence header gets folded if the user asked
            // for that. Which markers start a comment comes from the file's
            // language, which is not known until the highlighter has run.
            //
            // Captures the document rather than the editor, and the connection
            // inside is on the document too - so an editor closed before the
            // highlighter finishes takes the pending fold with it.
            TextDocument * const doc = m_document.get();
            const auto fold = [doc] {
                if (displaySettings().autoFoldFirstComment())
                    doc->foldLicenseHeader();
            };
            if (!doc->singleShotAfterHighlightingDone(fold))
                fold();
            return;
        }

        TextViewport * const view = viewport();
        if (!view)
            return;

        QDataStream stream(state);
        int version = 0;
        stream >> version;
        if (version != kStateVersion)
            return;

        int position = 0;
        qreal scrollY = 0;
        stream >> position >> scrollY;
        view->setCursorPosition(position);
        // After the cursor: setting it scrolls to it, and where the reader
        // left the view is the more specific answer.
        view->setScrollY(scrollY);
    }

private:
    static constexpr int kStateVersion = 1;

    void applyGlobalSettings()
    {
        m_document->setStorageSettings(globalStorageSettings().data());
        m_document->setTypingSettings(globalTypingSettings().data());
        m_document->setExtraEncodingSettings(globalExtraEncodingSettings().data());
        // The code style is what tab settings come from, and what an indenter
        // reads. The file's language has one where it registered a style
        // factory; only a file whose language did not falls back to the
        // editor's generic global. Chosen here rather than where the indenter
        // is installed, because this runs again on every settings change and
        // would otherwise put the generic one back.
        m_document->setCodeStyle(languageCodeStyle());
    }

    // Whether spaces and tabs are drawn is a property of the *document's*
    // layout rather than of anything the viewport owns, so it is set where the
    // widget editor sets it. The colour comes for free: SyntaxHighlighter::
    // formatSpaces() puts a C_VISUAL_WHITESPACE format on every whitespace run
    // whenever it runs, and does not consult this flag - which is also why
    // there is no rehighlight here. The widget editor does one; it cannot
    // change any format, and on a large file it is not cheap.
    void applyWhitespaceVisualization()
    {
        QTextDocument * const text = m_document->document();
        const QTextOption current = text->defaultTextOption();
        QTextOption::Flags flags = current.flags();
        flags.setFlag(QTextOption::AddSpaceForLineAndParagraphSeparators);
        flags.setFlag(QTextOption::ShowTabsAndSpaces, displaySettings().visualizeWhitespace());
        if (flags == current.flags())
            return;

        QTextOption option = current;
        option.setFlags(flags);
        text->setDefaultTextOption(option);
    }

    // What the file's language would give an editor of its own. The services a
    // TextEditorFactory holds are reachable from any editor: the factory that
    // claims a mime type is found the same way the editor manager finds one,
    // and it is asked rather than each editor going without.
    //
    // Without this a C++ file is indented by the plain indenter, which copies
    // the previous line - so a brace opens no block and a paste lands at the
    // wrong depth.
    // The language's own style, or the editor's generic one. Not the
    // document's mime type when it has none yet: an empty path maps to no
    // language, which is the generic answer anyway.
    ICodeStylePreferences *languageCodeStyle() const
    {
        const Utils::Id language = TextEditor::languageId(m_document->mimeType());
        if (language.isValid()) {
            if (ICodeStylePreferencesFactory * const style = codeStyleFactory(language)) {
                if (ICodeStylePreferences * const preferences = style->globalCodeStyle())
                    return preferences;
            }
        }
        return &globalCodeStyle();
    }

    void configureLanguageServices()
    {
        if (m_document->filePath().isEmpty())
            return;

        TextEditorFactory * const factory
            = TextEditorFactory::preferredFactoryFor(m_document->filePath());
        // The Quick editor claims text/plain itself and is not a
        // TextEditorFactory, so this finds the language's one or the plain
        // text editor's - never this editor.
        if (!factory)
            return;

        // The language's *style* first, which is where indenting really lives:
        // codeStyleFactory() is keyed by a language id that a mime type maps
        // to, and it is what CppEditorDocument asks - so this reaches C++,
        // which keeps nothing on its editor factory. The editor factory's own
        // creator is the answer for languages that registered there instead,
        // JSON among them.
        const Utils::Id language = TextEditor::languageId(m_document->mimeType());
        ICodeStylePreferencesFactory * const style
            = language.isValid() ? codeStyleFactory(language) : nullptr;
        Indenter * const fromStyle = style ? style->createIndenter(m_document->document())
                                           : nullptr;
        if (fromStyle)
            m_document->setIndenter(fromStyle);
        else if (const TextEditorFactory::IndenterCreator creator = factory->indenterCreator())
            m_document->setIndenter(creator(m_document->document()));
        // An indenter that formats through an external tool needs to know
        // which file it is working on; ClangFormat is the one that does.
        if (Indenter * const indenter = m_document->indenter())
            indenter->setFileName(m_document->filePath());

        // And the style the indenter reads, now that the language is known.
        m_document->setCodeStyle(languageCodeStyle());
        // Set whether or not there is a popup to show it yet: the document is
        // where an assist processor looks, and a document that answers nothing
        // cannot be told apart from a language with no completions.
        // The factory's own, or the words already in the file. That fallback is
        // the widget editor's too - it is applied in TextEditorFactory's editor
        // creator rather than kept on the factory, which is why asking the
        // factory for it comes back empty for plain text.
        static DocumentContentCompletionProvider wordsInTheDocument;
        CompletionAssistProvider * const provider = factory->completionAssistProvider();
        m_document->setCompletionAssistProvider(provider ? provider : &wordsInTheDocument);

        // The base AutoCompleter only knows how to take a bracket pair apart
        // again; closing one as it is typed is what a language's subclass adds.
        if (const TextEditorFactory::AutoCompleterCreator creator = factory->autoCompleterCreator()) {
            if (TextViewport * const view = viewport())
                view->setAutoCompleter(creator());
        }

        // Tooltips. The handlers are the language's, plus the ones every
        // editor gets; asking the factory is what makes a C++ file show C++
        // tooltips here.
        if (TextViewport * const view = viewport()) {
            QList<BaseHoverHandler *> handlers = factory->hoverHandlers();
            handlers.append(&colorPreviewHoverHandler());
            view->setHoverHandlers(handlers);
        }
    }

    void configureHighlighter()
    {
        // Nothing to go on yet. The constructor calls this so that a duplicate
        // - whose document already has a path - is highlighted too, and asking
        // the mime database about an empty path prints a warning and answers
        // nothing.
        if (m_document->filePath().isEmpty())
            return;

        m_document->setMimeType(
            Utils::mimeTypeForFile(m_document->filePath(),
                                   Utils::MimeMatchMode::MatchDefaultAndRemote)
                .name());

        // A document that came with a highlighter of its own keeps it.
        // CppEditorDocument builds a CppHighlighter in its constructor, and
        // the block states that leaves behind are what says a line is inside
        // a comment - which the generic definition does not know, so replacing
        // it silently changes what completion and indenting see.

        if (SyntaxHighlighter * const own = m_document->syntaxHighlighter()) {
            if (!qobject_cast<Highlighter *>(own))
                return;
        }

        const HighlighterHelper::Definitions definitions
            = HighlighterHelper::definitionsForDocument(m_document.get());
        const HighlighterHelper::Definition definition
            = definitions.isEmpty() ? HighlighterHelper::Definition() : definitions.first();

        m_document->resetSyntaxHighlighter([definition] {
            auto highlighter = new Highlighter;
            highlighter->setDefinition(definition);
            return highlighter;
        });
    }

public:
    // The form's viewport. Found rather than held: the QML owns it, and it
    // does not exist until the component has been created. Public because
    // viewportForEditor() hands it to plugins that have to answer both this
    // A language makes its outline a child of this editor, and it does so
    // after the file is open - so a toolbar row that only looked when it was
    // built would never show one. Deferred and coalesced: at ChildAdded the
    // child is still being constructed, and several children may arrive
    // together.
    void childEvent(QChildEvent *event) final
    {
        Core::IEditor::childEvent(event);
        if (!m_toolBar || m_outlineUpdateScheduled)
            return;
        if (!event->added() && !event->removed())
            return;
        m_outlineUpdateScheduled = true;
        QMetaObject::invokeMethod(this, [this] {
            m_outlineUpdateScheduled = false;
            showOutlineInToolBar();
        }, Qt::QueuedConnection);
    }

    // Told rather than asked, because the row is already drawn by now. The
    // same lookup the row used when it was built, so the two cannot disagree.
    void showOutlineInToolBar()
    {
        auto * const bar = qobject_cast<QtcQuick::QuickWidget *>(m_toolBar.data());
        if (!bar)
            return;
        QQuickItem * const root = bar->quickWidget()->rootObject();
        if (!root)
            return;
        root->setProperty("outline",
                          QVariant::fromValue(findChild<ToolBarOutline *>()));
    }

    // view and the widget one.
    TextViewport *viewport() const
    {
        auto * const quick = static_cast<QtcQuick::QuickWidget *>(widget());
        QQuickItem * const root = quick->quickWidget()->rootObject();
        return root ? root->findChild<TextViewport *>() : nullptr;
    }

    // Shared because a duplicated editor would show the same document; the
    // editor manager is what decides that, not this. The same handle the
    // widget editor uses, so that whoever wants the document need not know
    // which kind of view is showing it.
    TextDocumentPtr m_document;
    // See rememberThisAsSomewhereJumpedTo(). BaseTextEditor keeps the same
    // pair for the widget editor.
    QByteArray m_stateOfAJumpNotYetLeft;
    bool m_jumpedHereAndStillOnIt = false;

    QtcQuick::ActionModel m_contextActions;
    QtcQuick::ActionModel m_toolBarActions;
    bool m_outlineUpdateScheduled = false;
    // Owned by the toolbar the editor manager puts it in, so a QPointer.
    QPointer<QWidget> m_toolBar;
    QPointer<QAction> m_wrapAction;
    // Enabled only while there is a suggestion to take.
    QList<QPointer<QAction>> m_suggestionActions;
    struct GatedAction { QPointer<QAction> action; uint needs; };
    QList<GatedAction> m_gatedActions;
    const uint m_optionalActions = OptionalActions::None;
    QPointer<QAction> m_whitespaceAction;
    // This editor alone, so that a per-editor action does not collide with
    // the same action on the next one.
    const Utils::Id m_editorContext = Utils::Id::generate();
    std::unique_ptr<AdoptedSource> m_source;
};

class QuickTextEditorFactory final : public Core::IEditorFactory
{
public:
    QuickTextEditorFactory()
    {
        setId(QUICK_TEXT_EDITOR_ID);
        setDisplayName(Tr::tr("Code Editor (Qt Quick)"));
        // The same mime type the plain text editor takes, and registered
        // before it: the default for a mime type is the first factory that
        // claims it, so this is what a text file opens in. The lookup walks a
        // mime type's parents, so source files come here too.
        addMimeType(QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT));
        addMimeType(QLatin1String("text/css")); // freedesktop calls css text/x-csrc
        // The same three the widget plain text editor grants: a text file can
        // be formatted, commented and unfolded, and has no symbols to follow
        // or rename. See PlainTextEditorFactory.
        setEditorCreator([] {
            return new QuickTextEditor(TextDocumentPtr(new TextDocument(QUICK_TEXT_EDITOR_ID)),
                                       OptionalActions::Format
                                           | OptionalActions::UnCommentSelection
                                           | OptionalActions::UnCollapseAll);
        });
    }
};

Core::IEditor *editorForViewport(TextViewport *view)
{
    if (!view || !view->textDocument())
        return nullptr;
    const QList<Core::IEditor *> editors
        = Core::DocumentModel::editorsForDocument(view->textDocument());
    for (Core::IEditor * const editor : editors) {
        if (viewportForEditor(editor) == view)
            return editor;
    }
    return nullptr;
}

Core::IEditor *createQuickTextEditor(const TextDocumentPtr &document,
                                     const Core::Context &context,
                                     uint optionalActions)
{
    auto * const editor = new QuickTextEditor(document, optionalActions);
    // Added rather than set: the editor gave itself the two contexts every
    // Quick editor needs - the shared one and its own - in the constructor,
    // and replacing them would take the per-editor actions with them.
    Core::Context contexts = editor->context();
    contexts.add(context);
    editor->setContext(contexts);
    return editor;
}

void setupQuickTextEditor()
{
    static QuickTextEditorFactory theQuickTextEditorFactory;
    Q_UNUSED(theQuickTextEditorFactory)
}

#ifdef WITH_TESTS

// Registering the same command twice in one context is a warning and nothing
// else: the second registration is dropped and the editor quietly has no
// action. Asserting the absence of that line is the only way a test sees it.
class ActionCollisions
{
public:
    ActionCollisions() { s_hits = &m_hits; s_previous = qInstallMessageHandler(collect); }
    ~ActionCollisions() { qInstallMessageHandler(s_previous); s_hits = nullptr; }

    QStringList hits() const { return m_hits; }

private:
    static void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (s_hits && message.contains("already registered"))
            s_hits->append(message);
        if (s_previous)
            s_previous(type, context, message);
    }

    QStringList m_hits;
    static inline QStringList *s_hits = nullptr;
    static inline QtMessageHandler s_previous = nullptr;
};

// A Repeater's delegates are visual children of the item and QObject children
// of somewhere else, so findChild() never sees one. See
// textviewport_test.cpp, which walks the tree the same way.
// Every item drawn under \a root with that name, top to bottom. itemNamed()
// answers the first one, which is no use for a column of them.
static QList<QQuickItem *> itemsNamed(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> found;
    if (!root)
        return found;
    if (root->objectName() == name)
        found.append(root);
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem * const child : children)
        found += itemsNamed(child, name);
    std::sort(found.begin(), found.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->y() < b->y();
    });
    return found;
}

static QQuickItem *itemNamed(QQuickItem *root, const QString &name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem * const found = itemNamed(child, name))
            return found;
    }
    return nullptr;
}

class QuickTextEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // The one thing this must not do while it is unfinished: become what a
    // text file opens in. The default for a mime type is the first factory
    // that claims it, and the plain text editor is still offered beside it.
    void testTheEditorSaysWhatIsSelected()
    {
        // Whoever wants to act on a selection rather than on the file asks
        // the editor. Answering with nothing is how pasting a selection came
        // to paste the whole document instead.
        Utils::TemporaryDirectory dir("quick-editor-selection");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("selected.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        QVERIFY2(editor->selectedText().isEmpty(), "nothing is selected yet");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 2);

        // "alpha\nbeta\ngamma\n": positions 6 to 10 are "beta".
        view->setSelectionStart(6);
        view->setSelectionEnd(10);
        QCOMPARE(editor->selectedText(), QString("beta"));
    }

    // The point of registering these commands rather than only implementing
    // them: the menu entry and whatever shortcut the reader has bound to it
    // are the action, and an action registered in the widget editor's context
    // does nothing while a Quick editor is the current one. Every test so far
    // has called the viewport's method directly, which proves the command
    // works and not that anything can reach it.
    void testTheLineCommandsAreReachableAsCommands()
    {
        Utils::TemporaryDirectory dir("quick-editor-commands");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("lines.txt");
        QVERIFY(file.writeFileContents("first\n      second\nthird\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 2);
        view->setReadOnly(false);

        Core::Command * const command = Core::ActionManager::command(Constants::JOIN_LINES);
        QVERIFY2(command, "Join Lines is not a registered command at all");

        // Asked of the editor's own context rather than of the front action.
        // Whether the front action is enabled depends on what has focus, and
        // in a test nothing does - it is disabled for a widget editor here
        // too, so it cannot tell a registration apart from a missing one.
        // Every id the editor carries, not the first: an editor's context is
        // the editor type and an id of its own, and the commands are
        // registered against the second.
        const Core::Context context = editor->context();
        QVERIFY2(!context.isEmpty(), "the editor has no context of its own");
        QAction *action = nullptr;
        for (const Utils::Id &id : context) {
            if ((action = command->actionForContext(id)))
                break;
        }
        QVERIFY2(action, "Join Lines is not registered in the Quick editor's context");

        view->setCursorPosition(0);
        action->trigger();

        QTextDocument * const text = view->textDocument()->document();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("first second"));

        // The editing commands too. These are the ones whose keys the
        // viewport already handled, so the risk was the opposite one: an
        // action registered for Ctrl+C is handled before the key reaches the
        // item, and would replace the handling rather than add to it.
        const auto inContext = [&context](const Utils::Id &id) -> QAction * {
            Core::Command * const cmd = Core::ActionManager::command(id);
            if (!cmd)
                return nullptr;
            for (const Utils::Id &each : context) {
                if (QAction * const a = cmd->actionForContext(each))
                    return a;
            }
            return nullptr;
        };

        QAction * const selectAll = inContext(Core::Constants::SELECTALL);
        QVERIFY2(selectAll, "Select All is not registered in the Quick editor's context");
        selectAll->trigger();
        QCOMPARE(view->selectionStart(), 0);
        QCOMPARE(view->selectionEnd(), text->characterCount() - 1);

        QGuiApplication::clipboard()->clear();
        QAction * const copy = inContext(Core::Constants::COPY);
        QVERIFY2(copy, "Copy is not registered in the Quick editor's context");
        copy->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), text->toPlainText());

        // And moving about. The selection Select All just made has to go
        // first: setting the position does not clear it, and a move with
        // MoveAnchor over a selection collapses it to its end rather than
        // moving a word - which looks exactly like a word move that worked.
        view->setSelectionStart(-1);
        view->setSelectionEnd(-1);
        view->setCursorPosition(0);
        QVERIFY(!view->textCursor().hasSelection());

        QAction * const nextWord = inContext(Constants::GOTO_NEXT_WORD);
        QVERIFY2(nextWord, "Go to Next Word is not registered in the editor's context");
        nextWord->trigger();
        // "first second": the start of the second word, not one character on
        // and not the end of the document.
        const int afterWord = view->cursorPosition();
        QCOMPARE(afterWord, 6);

        // With selection, which is the same operation and the other mode.
        QAction * const selectWord = inContext(Constants::GOTO_NEXT_WORD_WITH_SELECTION);
        QVERIFY2(selectWord, "Select Next Word is not registered in the editor's context");
        selectWord->trigger();
        QVERIFY2(view->textCursor().hasSelection(), "the selecting move selected nothing");
        QCOMPARE(view->textCursor().anchor(), afterWord);
    }

    // The symbol commands ask rather than answer: the view emits through the
    // relay and whoever knows the language does the work. This checks the
    // asking, which is this plugin's half - what the language client makes of
    // it needs a server and is not tested here.
    void testTheSymbolCommandsAskThroughTheRelay()
    {
        Utils::TemporaryDirectory dir("quick-editor-symbols");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int value = 1;\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        // Reached the way another plugin reaches it, which is the point of
        // the relay: no Qt Quick anywhere in the question.
        TextEditor::SymbolRequests * const requests
            = TextEditor::symbolRequestsForEditor(editor);
        QVERIFY2(requests, "the editor offers no relay to ask through");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 0);

        QSignalSpy usages(requests, &TextEditor::SymbolRequests::requestUsages);
        QSignalSpy rename(requests, &TextEditor::SymbolRequests::requestRename);
        QSignalSpy hierarchy(requests, &TextEditor::SymbolRequests::requestCallHierarchy);

        view->setCursorPosition(6);
        view->findUsages();
        view->renameSymbolUnderCursor();
        view->openCallHierarchy();

        QCOMPARE(usages.size(), 1);
        QCOMPARE(rename.size(), 1);
        QCOMPARE(hierarchy.size(), 1);
        // With the caret where it was put, so that the answer is about the
        // symbol the reader is standing on.
        QCOMPARE(qvariant_cast<QTextCursor>(usages.at(0).at(0)).position(), 6);

        // And reachable as commands, not only as methods.
        const Core::Context context = editor->context();
        const auto inContext = [&context](const Utils::Id &id) -> QAction * {
            Core::Command * const cmd = Core::ActionManager::command(id);
            if (!cmd)
                return nullptr;
            for (const Utils::Id &each : context) {
                if (QAction * const a = cmd->actionForContext(each))
                    return a;
            }
            return nullptr;
        };
        QAction * const findUsages = inContext(Constants::FIND_USAGES);
        QVERIFY2(findUsages, "Find Usages is not registered in the editor's context");
        // Registered, and disabled: this is a plain text file, whose factory
        // asks for no symbol command at all. Enabling it is what makes it
        // reach the relay - see testTheOptionalCommandsFollowTheFactorysMask.
        QVERIFY2(!findUsages->isEnabled(),
                 "a plain text file is offered Find Usages");
        findUsages->setEnabled(true);
        findUsages->trigger();
        QCOMPARE(usages.size(), 2);

        // Where the type is, which unlike the three above answers back - so
        // the request has to carry somewhere to answer to.
        QSignalSpy typeAsked(requests, &TextEditor::SymbolRequests::requestTypeAt);
        QAction * const toType = inContext(Constants::FOLLOW_SYMBOL_TO_TYPE);
        QVERIFY2(toType, "Follow Symbol to Type is not registered in the editor's context");
        toType->setEnabled(true);
        toType->trigger();
        QCOMPARE(typeAsked.size(), 1);
        QVERIFY2(qvariant_cast<Utils::LinkHandler>(typeAsked.at(0).at(1)) != nullptr,
                 "the request carried nowhere to answer to");

        // Jump to File is the same question as Follow Symbol, given a second
        // entry and a second shortcut; both have to reach this editor.
        QVERIFY2(inContext(Constants::JUMP_TO_FILE_UNDER_CURSOR),
                 "Jump to File is not registered in the editor's context");
        QVERIFY2(inContext(Constants::OPEN_TYPE_HIERARCHY),
                 "Open Type Hierarchy is not registered in the editor's context");
        // The three that ask something other than the view: the locator, the
        // codec dialog, and the document's own byte order mark.
        QVERIFY2(inContext(Core::Constants::GOTO),
                 "Go to Line is not registered in the editor's context");
        QVERIFY2(inContext(Constants::SELECT_ENCODING),
                 "Select Encoding is not registered in the editor's context");
        QVERIFY2(inContext(Constants::SWITCH_UTF8BOM),
                 "Switch UTF-8 BOM is not registered in the editor's context");
    }

    // Scrolling shows the scrollbar. A ScrollBar attached to a Flickable is
    // made active while that Flickable moves, and an inactive one is drawn at
    // zero opacity - these are attached to nothing, so scrolling with a wheel
    // or a trackpad moved the text and left the bar invisible.
    void testScrollingShowsTheScrollBar()
    {
        Utils::TemporaryDirectory dir("quick-editor-scrollbar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("long.txt");
        QString text;
        for (int line = 0; line < 500; ++line)
            text += QString("line %1\n").arg(line);
        QVERIFY(file.writeFileContents(text.toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 1);

        auto * const bar = quick->rootObject()->findChild<QQuickItem *>("verticalScrollBar");
        QVERIFY2(bar, "the editor has no vertical scrollbar");
        auto * const handle = bar->property("contentItem").value<QQuickItem *>();
        QVERIFY(handle);
        // There is more text than fits, or there would be nothing to show.
        QTRY_VERIFY(bar->property("size").toReal() < 1.0);

        // The pointer resting on the bar makes it active by itself, and then
        // this proves nothing either way.
        if (bar->property("hovered").toBool())
            QSKIP("the pointer is over the scrollbar, which shows it regardless");

        QCOMPARE(handle->opacity(), 0.0);

        // What the wheel handler does when a trackpad is used.
        view->setScrollY(view->contentHeight() / 4);

        // The bar says it is showing itself, which is what this change is
        // about: nothing set active before, and the style draws an inactive
        // bar at zero opacity. Asked first on purpose - it is a binding, and
        // nothing here draws frames, so reading it is what makes the style's
        // state follow. In the running editor the frames do that.
        QTRY_VERIFY2(bar->property("active").toBool(),
                     "scrolling did not wake the scrollbar");
        QTRY_VERIFY2(handle->opacity() > 0.0, "the scrollbar stayed invisible while scrolling");
    }

    // Something offering an inline suggestion - Copilot is the only one -
    // reaches this editor through a handle rather than through a widget, and
    // the suggestion it puts there is drawn. Driven in the order the client
    // drives it: look at the view, decide, then offer.
    void testASuggestionCanBeOfferedToThisEditor()
    {
        Utils::TemporaryDirectory dir("quick-editor-suggestion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        // Reached the way the client reaches it: an editor in, a handle out,
        // and no Qt Quick in between.
        TextEditor::SuggestionHost * const host = TextEditor::suggestionHostForEditor(editor);
        QVERIFY2(host, "this editor cannot be offered a suggestion");
        QCOMPARE(host, TextEditor::suggestionHostForEditor(editor));

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 1);

        // What the client asks before it asks the server anything.
        QCOMPARE(host->textDocument(), view->textDocument());
        QCOMPARE(host->document(), view->textDocument()->document());
        QVERIFY(!host->isReadOnly());
        QVERIFY(!host->multiTextCursor().hasMultipleCursors());
        QVERIFY2(!host->suggestionVisible(), "a suggestion is showing before one was offered");

        // The caret moving is how the client learns its answer is stale, so
        // the handle has to say so - the widget's own signal is not reachable
        // from here.
        QSignalSpy moved(host, &TextEditor::SuggestionHost::cursorPositionChanged);
        view->setCursorPosition(3);
        QTRY_VERIFY(moved.size() > 0);

        // And the answer, offered exactly as the client offers it.
        const Utils::Text::Range range{{1, 0}, {1, 3}};
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{range, {1, 3}, "return value;"}},
            host->document());
        host->insertSuggestion(std::move(suggestion));

        QTRY_COMPARE(view->visibleLine(0).value("text").toString(), QString("return value;"));
        QVERIFY2(host->suggestionVisible(), "the suggestion it just put there is not showing");
        // A picture of what taking it would do, not the doing of it.
        QCOMPARE(host->document()->findBlockByNumber(0).text(), QString("ret"));
    }

    // The three ways of taking a suggestion are menu entries, not only
    // methods - and they are offered only while there is one to take. Their
    // shortcuts are Tab, Shift+Tab and the next-word key, so an entry left
    // enabled would take those keys away from what they usually do.
    void testTakingASuggestionIsOfferedOnlyWhileThereIsOne()
    {
        Utils::TemporaryDirectory dir("quick-editor-suggestion-cmds");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 1);

        const Core::Context context = editor->context();
        const auto inContext = [&context](const Utils::Id &id) -> QAction * {
            Core::Command * const cmd = Core::ActionManager::command(id);
            if (!cmd)
                return nullptr;
            for (const Utils::Id &each : context) {
                if (QAction * const a = cmd->actionForContext(each))
                    return a;
            }
            return nullptr;
        };
        QAction * const apply = inContext(Constants::SUGGESTION_APPLY);
        QAction * const applyWord = inContext(Constants::SUGGESTION_APPLY_WORD);
        QAction * const applyLine = inContext(Constants::SUGGESTION_APPLY_LINE);
        QVERIFY2(apply && applyWord && applyLine,
                 "taking a suggestion is not registered in the editor's context");
        QVERIFY2(!apply->isEnabled() && !applyWord->isEnabled() && !applyLine->isEnabled(),
                 "the entries are live with no suggestion to take");

        view->setReadOnly(false);
        view->setCursorPosition(3);
        QTextDocument * const text = view->textDocument()->document();
        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "return value;"}}, text, 0);
        suggestion->setCurrentPosition(3);
        view->insertSuggestion(std::move(suggestion));

        QVERIFY2(apply->isEnabled(), "there is a suggestion but no way to take it");
        QVERIFY(applyWord->isEnabled() && applyLine->isEnabled());

        // And triggering the entry takes it, so the entry is wired to
        // something and not merely lit up.
        apply->trigger();
        QCOMPARE(text->findBlockByNumber(0).text(), QString("return value;"));

        // They go dead again when the suggestion does. Taking one does not
        // end it - the line then reads what it offered, so it still describes
        // the text and the widget editor keeps it too - but leaving the line
        // does.
        view->setCursorPosition(text->findBlockByNumber(1).position());
        QVERIFY2(!apply->isEnabled() && !applyWord->isEnabled() && !applyLine->isEnabled(),
                 "the entries stayed live with no suggestion to take");
    }

    // And the widget editor still is, which is what the handle replaced: it
    // is the editor Copilot has always offered to, and the port must not have
    // moved suggestions from one editor to the other.
    void testTheWidgetEditorIsStillOfferedTo()
    {
        Utils::TemporaryDirectory dir("widget-editor-suggestion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("ret\nsecond\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const base = qobject_cast<BaseTextEditor *>(editor);
        QVERIFY2(base, "the plain text editor is not a widget text editor");

        TextEditor::SuggestionHost * const host = TextEditor::suggestionHostForEditor(editor);
        QVERIFY2(host, "the widget editor cannot be offered a suggestion");
        QCOMPARE(host->textDocument(), base->textDocument());
        QVERIFY(!host->suggestionVisible());

        const Utils::Text::Range range{{1, 0}, {1, 3}};
        base->editorWidget()->setCursorPosition(3);
        host->insertSuggestion(std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{range, {1, 3}, "return value;"}}, host->document()));
        QVERIFY2(host->suggestionVisible(), "the suggestion it just put there is not showing");
    }

    // Ctrl+Space has always reached this editor as a key, and the form has
    // always drawn what came back; the menu entry reached nothing. It answers
    // by the same method the key handler calls.
    void testAskingForCompletionsIsReachableAsACommand()
    {
        Utils::TemporaryDirectory dir("quick-editor-complete");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents("int value = 1;\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 0);

        const Core::Context context = editor->context();
        QAction *action = nullptr;
        if (Core::Command * const cmd = Core::ActionManager::command(Constants::COMPLETE_THIS)) {
            for (const Utils::Id &id : context) {
                if ((action = cmd->actionForContext(id)))
                    break;
            }
        }
        QVERIFY2(action, "Complete This is not registered in the Quick editor's context");

        // The answer arrives as a signal whether or not a language had
        // anything to say - an empty list is still an answer, and is what
        // this gets with no server running.
        QSignalSpy answered(view, &TextViewport::completionsAvailable);
        view->setCursorPosition(3);
        action->trigger();
        QTRY_VERIFY2(!answered.isEmpty(), "asking produced no answer at all");
    }

    void testAFileWithNoEditorOfItsOwnOpensHereToo()
    {
        // Lua has no editor of its own, so a script is a text file like any
        // other and lands here. Whoever assumed that every text file opened
        // in a widget meets this view instead - the Lua plugin put a Run
        // button on the tool bar without checking, which was a crash.
        const Utils::MimeType lua = Utils::mimeTypeForName("text/x-lua");
        if (!lua.isValid())
            QSKIP("no lua mime type registered on this system");
        QVERIFY2(lua.inherits(QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT)),
                 "a lua script is not a text file here, so this proves nothing");

        const Core::EditorFactories factories = Core::IEditorFactory::defaultEditorFactories(lua);
        QVERIFY(!factories.isEmpty());
        QCOMPARE(factories.first()->id().toString(), QLatin1String(QUICK_TEXT_EDITOR_ID));
    }

    void testItIsWhatATextFileOpensIn()
    {
        const Utils::MimeType text = Utils::mimeTypeForName(
            QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT));
        QVERIFY(text.isValid());

        const Core::EditorFactories factories = Core::IEditorFactory::defaultEditorFactories(text);
        QStringList ids;
        for (Core::IEditorFactory * const factory : factories)
            ids << factory->id().toString();

        QVERIFY2(ids.contains(QLatin1String(QUICK_TEXT_EDITOR_ID)),
                 qPrintable("not offered for a text file at all: " + ids.join(", ")));
        QCOMPARE(ids.first(), QLatin1String(QUICK_TEXT_EDITOR_ID));
        QVERIFY2(ids.size() > 1, "nothing else is offered, so Open With has no choice to make");
    }

    // Opening a file goes through the editor manager, which creates the editor
    // and then opens its document - so the form has to show a document it did
    // not open. This is what says the two are the same one.
    void testTheFormShowsTheDocumentTheEditorManagerOpened()
    {
        Utils::TemporaryDirectory dir("quick-editor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hello.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] {
            Core::EditorManager::closeEditors({editor});
        });

        QCOMPARE(editor->document()->filePath(), file);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("alpha\nbeta\ngamma\n"));

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY2(quick, "the editor built no Qt Quick form");
        // A form whose component failed to load is Ready's opposite and has no
        // root object, which is a blank editor rather than an absent one.
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        QVERIFY(quick->rootObject());

        // And it is showing *that* document rather than one of its own.
        QObject * const viewport = quick->rootObject()->findChild<QObject *>("codeViewport");
        QVERIFY(viewport);
        auto * const shown = viewport->property("document").value<CodeSource *>();
        QVERIFY2(shown, "the form is showing no document");
        QCOMPARE(shown->textDocument(), document);
    }

    // Every jump into a file - a search result, a compiler message,
    // go-to-definition, the locator - is EditorManager asking the editor to
    // go to a line. An editor that does not answer opens at the top instead,
    // which looks like the feature is broken rather than the editor.
    void testGoingToALineIsWhereTheEditorSaysItIs()
    {
        Utils::TemporaryDirectory dir("quick-editor-goto");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("jump.txt");
        QString contents;
        // Indented, because column zero is supposed to mean "the code on this
        // line" - and on an unindented line that is the same as the margin,
        // so an unindented file cannot tell the two apart.
        for (int i = 0; i < 200; ++i)
            contents += QString("    line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        QCOMPARE(editor->currentLine(), 1);
        QCOMPARE(editor->currentColumn(), 1);

        editor->gotoLine(120, 3);
        QCOMPARE(editor->currentLine(), 120);
        QCOMPARE(editor->currentColumn(), 4);
        // And the line is on screen, not merely under the caret. One condition,
        // not two: "the first visible line is at most 119" is true of an
        // editor that never scrolled at all.
        const auto onScreen = [viewport] {
            return viewport->firstVisibleLine() <= 119
                   && viewport->firstVisibleLine() + viewport->visibleLineCount() > 119;
        };
        QTRY_VERIFY2(onScreen(),
                     qPrintable(QString("line 120 is not on screen: rows %1 to %2, height %3")
                                    .arg(viewport->firstVisibleLine())
                                    .arg(viewport->firstVisibleLine() + viewport->visibleLineCount())
                                    .arg(viewport->height())));

        // Column zero means the line, so it lands on the code rather than in
        // the indentation before it.
        editor->gotoLine(50, 0);
        QCOMPARE(editor->currentLine(), 50);
        QCOMPARE(editor->currentColumn(), 5);

        // A line inside a fold is still a line someone can be sent to, so
        // going there has to open the fold rather than scroll to where the
        // line would have been.
        QTextDocument * const text = document->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        for (int i = 61; i <= 80; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(60), /*unfold=*/false);
        layout->requestUpdate();
        QTRY_VERIFY(!text->findBlockByNumber(70).isVisible());

        editor->gotoLine(71, 0);
        QVERIFY2(text->findBlockByNumber(70).isVisible(),
                 "going to a folded line left it folded, so the caret is nowhere");
        QCOMPARE(editor->currentLine(), 71);

    }

    // Closing and reopening a file, or stepping back through the navigation
    // history, puts the reader back where they were - which is the editor
    // manager handing the editor its own state again.
    void testTheEditorRemembersWhereTheReaderWas()
    {
        Utils::TemporaryDirectory dir("quick-editor-state");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("remembered.txt");
        QString contents;
        for (int i = 0; i < 200; ++i)
            contents += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        editor->gotoLine(150, 0);
        QCOMPARE(editor->currentLine(), 150);
        const qreal wasScrolledTo = viewport->scrollY();
        QVERIFY(wasScrolledTo > 0);

        const QByteArray state = editor->saveState();
        QVERIFY2(!state.isEmpty(), "the editor saved nothing to come back to");

        editor->gotoLine(1, 0);
        QCOMPARE(editor->currentLine(), 1);

        editor->restoreState(state);
        QCOMPARE(editor->currentLine(), 150);
        // The view too, not only the caret: coming back to a line that is
        // technically on screen at the very bottom is not coming back.
        QCOMPARE(viewport->scrollY(), wasScrolledTo);
    }

    // Ctrl+F. The find bar reaches an editor by asking its widget for an
    // IFindSupport, so an editor that aggregates none simply does nothing when
    // the user presses it - no error, no bar, no clue.
    void testFindMovesTheCaretAndMarksEveryMatch()
    {
        Utils::TemporaryDirectory dir("quick-editor-find");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("searchable.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma beta\ndelta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        // Without the second argument, closing after the replace below asks
        // about the modified document - a modal dialog in a test run.
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const find = Utils::Aggregation::query<Core::IFindSupport>(editor->widget());
        QVERIFY2(find, "the editor offers no find support, so Ctrl+F does nothing in it");
        QVERIFY(find->supportsReplace());

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // Stepping selects the match rather than merely scrolling to it.
        viewport->setCursorPosition(0);
        QCOMPARE(find->findStep("beta", {}), Core::IFindSupport::Found);
        QCOMPARE(viewport->selectionStart(), 6);
        QCOMPARE(viewport->selectionEnd(), 10);

        // And again takes the next one, on the line below.
        QCOMPARE(find->findStep("beta", {}), Core::IFindSupport::Found);
        QCOMPARE(viewport->selectionStart(), 17);

        // Highlighting all of them reaches the viewport, and through it what
        // is drawn - the point being that the reader can see where the other
        // matches are without stepping to each one.
        find->highlightAll("beta", {});
        QTRY_COMPARE(viewport->highlights(QUICK_FIND_HIGHLIGHTS).size(), 2);

        // Both matches, and each one carrying something that will actually
        // show. C_SEARCH_RESULT is an overlay category whose QTextCharFormat
        // is near-empty by design, and an empty format range draws nothing
        // while looking exactly like a highlight that worked.
        const QList<TextViewport::Highlight> marks = viewport->highlights(QUICK_FIND_HIGHLIGHTS);
        QCOMPARE(marks.size(), 2);
        QCOMPARE(marks.at(0).start, 6);
        QCOMPARE(marks.at(0).end, 10);
        for (const TextViewport::Highlight &mark : marks) {
            QVERIFY2(mark.format.background().style() != Qt::NoBrush
                         && mark.format.background().color().isValid(),
                     "a match is highlighted in nothing, so nobody can see it");
        }

        // That these reach the scene graph is the viewport's own test. Core
        // clears a find's highlights as soon as its widget stops being the
        // current find target, so a test that spun the event loop here would
        // be measuring focus rather than drawing.

        // They are on the scroll bar as well, which is how a match below the
        // fold says where it is. Once per line: both matches on one line
        // would be one place to scroll to.
        const QColor onBarColour
            = Utils::creatorColor(Utils::Theme::TextEditor_SearchResult_ScrollBarColor);
        const auto onBar = [viewport, onBarColour] {
            int count = 0;
            const QVariantList bar = viewport->scrollBarHighlights();
            for (const QVariant &entry : bar) {
                if (entry.toMap().value("color").value<QColor>() == onBarColour)
                    ++count;
            }
            return count;
        };
        QTRY_COMPARE(onBar(), 2);

        // Seven matches over three lines, and three places to scroll to: the
        // bar says which lines carry one, not how many each carries.
        find->highlightAll("a", {});
        QTRY_COMPARE(onBar(), 3);

        find->clearHighlights();
        QVERIFY(viewport->highlights(QUICK_FIND_HIGHLIGHTS).isEmpty());
        QTRY_COMPARE(onBar(), 0);

        // Replacing writes through the document, so undo can take it back.
        find->replaceStep("beta", "delta", {});
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(document->plainText().contains("gamma delta"),
                 qPrintable("replace did nothing: " + document->plainText()));
    }

    // Right-clicking has to offer what every other editor offers: the actions
    // the ActionManager assembled, not a list written out here. An editor with
    // an empty menu looks like Creator has lost its actions.
    void testTheRightClickMenuIsWhatTheActionManagerAssembled()
    {
        Utils::TemporaryDirectory dir("quick-editor-menu");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("clickable.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());

        // What the widget editor would have put in its menu, for comparison.
        Core::ActionContainer * const container
            = Core::ActionManager::actionContainer(Constants::M_STANDARDCONTEXTMENU);
        QVERIFY2(container && container->menu(), "there is no standard context menu to show");
        const QList<QAction *> expected = container->menu()->actions();
        QVERIFY2(!expected.isEmpty(), "the standard context menu is empty, so this proves nothing");

        auto * const model
            = quick->rootObject()->property("contextActions").value<QtcQuick::ActionModel *>();
        QVERIFY2(model, "the form was given no actions, so a right click offers nothing");
        QCOMPARE(model->rowCount(), expected.size());

        // Not just the right number of them: the right ones, in order.
        for (int row = 0; row < expected.size(); ++row) {
            const QModelIndex at = model->index(row, 0);
            if (expected.at(row)->isSeparator()) {
                QVERIFY(at.data(QtcQuick::ActionModel::SeparatorRole).toBool());
                continue;
            }
            QCOMPARE(at.data(QtcQuick::ActionModel::TextRole).toString(),
                     expected.at(row)->text());
            QCOMPARE(at.data(QtcQuick::ActionModel::EnabledRole).toBool(),
                     expected.at(row)->isEnabled());
        }

        // And triggering an entry triggers the action behind it, which is the
        // whole point of listing them rather than reimplementing them.
        int firstReal = -1;
        for (int row = 0; row < expected.size(); ++row) {
            if (!expected.at(row)->isSeparator()) {
                firstReal = row;
                break;
            }
        }
        QVERIFY(firstReal >= 0);
        QSignalSpy triggered(expected.at(firstReal), &QAction::triggered);
        expected.at(firstReal)->setEnabled(true);
        model->trigger(firstReal);
        QCOMPARE(triggered.count(), 1);

        // A disabled action stays put: a Quick MenuItem can be told to look
        // disabled and still be told to fire.
        expected.at(firstReal)->setEnabled(false);
        model->trigger(firstReal);
        QCOMPARE(triggered.count(), 1);
    }

    // The whole point of an editor: type in it, and the file on disk changes.
    // Everything else here is worth nothing if this does not hold.
    void testTypingMarksTheFileDirtyAndSavingWritesIt()
    {
        Utils::TemporaryDirectory dir("quick-editor-save");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("edited.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(!document->isModified(), "a freshly opened file is already dirty");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // Typed through the form, not written into the document behind it:
        // what is being tested is that the editor's own editing path reaches
        // the file.
        viewport->forceActiveFocus();
        QVERIFY2(viewport->hasActiveFocus(), "the viewport never took focus, so no key arrives");
        viewport->setCursorPosition(0);
        QTest::keyClick(quick->quickWindow(), 'X');

        QCOMPARE(document->plainText(), QString("Xalpha\n"));
        QVERIFY2(document->isModified(), "typing did not make the document dirty");

        // Not QVERIFY2: its message argument is evaluated whether or not the
        // condition held, and Result::error() asserts on a value.
        const Utils::Result<> saved = document->save(file);
        if (!saved)
            QFAIL(qPrintable(saved.error()));
        QVERIFY(!document->isModified());

        const Utils::Result<QByteArray> onDisk = file.fileContents();
        QVERIFY(onDisk.has_value());
        QCOMPARE(QString::fromUtf8(*onDisk), QString("Xalpha\n"));
    }

    // Opening a source file has to colour it. The generic highlighter is
    // chosen from the file's mime type, which is something the *factory* does
    // for the widget editor - a document created without it shows every
    // language as grey text.
    void testReloadedDefinitionsReachAnEditorThatIsNotAWidget()
    {
        // Downloading or reloading the generic definitions rebuilds the
        // highlighter on the open documents. It used to walk the editors and
        // ask each widget to reconfigure itself, so a file open in a view that
        // is not a widget kept whatever it had until it was closed and
        // reopened.
        Utils::TemporaryDirectory dir("quick-editor-reload");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("sample.json");
        QVERIFY(file.writeFileContents("{ \"key\": 42 }\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const HighlighterHelper::Definitions own
            = HighlighterHelper::definitionsForDocument(document);
        if (own.isEmpty())
            QSKIP("no syntax definitions are installed, so nothing could colour anything");

        const auto definitionOnDocument = [document] {
            auto * const highlighter = qobject_cast<Highlighter *>(document->syntaxHighlighter());
            return highlighter ? highlighter->definition().name() : QString();
        };
        QCOMPARE(definitionOnDocument(), own.first().name());

        // Put a definition on it that is not this file's, so that leaving it
        // alone and putting the right one back are different outcomes.
        const HighlighterHelper::Definition other
            = HighlighterHelper::definitionForName("Bash");
        if (!other.isValid() || other.name() == own.first().name())
            QSKIP("no second definition to tell apart from this file's own");
        HighlighterHelper::setDefinitionOn(document, other);
        QCOMPARE(definitionOnDocument(), other.name());

        // A reload puts the file's own definition back, here as much as in a
        // widget.
        HighlighterHelper::reload();
        QTRY_COMPARE(definitionOnDocument(), own.first().name());
    }

    void testOpeningASourceFileHighlightsIt()
    {
        Utils::TemporaryDirectory dir("quick-editor-highlight");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("sample.json");
        QVERIFY(file.writeFileContents("{ \"key\": 42, \"other\": true }\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        if (HighlighterHelper::definitionsForDocument(document).isEmpty())
            QSKIP("no syntax definitions are installed, so nothing could colour anything");

        QVERIFY2(document->syntaxHighlighter(),
                 "the editor installed no highlighter, so every language is grey text");

        // And the document knows what it is. Highlighting does not need this -
        // a definition is found from the file name first - but the code style,
        // the indenter and anything else asking the document what language it
        // holds do.
        QVERIFY(!document->mimeType().isEmpty());
        QCOMPARE(document->mimeType(),
                 Utils::mimeTypeForFile(file, Utils::MimeMatchMode::MatchDefaultAndRemote).name());

        // Saving under a different name changes the language. TextDocument
        // reads its mime type when it *opens* and never again, so this is the
        // editor's job - the same reason the widget one reconfigures on
        // filePathChanged.
        const Utils::FilePath renamed = dir.filePath("sample.py");
        const QString expected
            = Utils::mimeTypeForFile(renamed, Utils::MimeMatchMode::MatchDefaultAndRemote).name();
        QVERIFY2(expected != document->mimeType(),
                 "both names have the same mime type, so renaming proves nothing");

        document->setFilePath(renamed);
        QCOMPARE(document->mimeType(), expected);
        QVERIFY(document->syntaxHighlighter());

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // And the colours reach what is drawn, which is the half a highlighter
        // being present does not prove.
        const auto colours = [viewport] {
            QSet<QRgb> found;
            const QVariantList ranges = viewport->visibleLine(0).value("formats").toList();
            for (const QVariant &range : ranges)
                found.insert(range.toMap().value("foreground").value<QColor>().rgba());
            return found;
        };
        QTRY_VERIFY2(colours().size() > 1,
                     qPrintable(QString("the line is drawn in %1 colour(s)").arg(colours().size())));
    }

    // Whitespace inside a comment or a string carries the visual-whitespace
    // format, which SyntaxHighlighter builds by copying the comment's own
    // background onto it - a background the comment does not have. A run that
    // says it has one and then paints nothing is drawn as a solid black cell
    // by a QSGTextNode, so the flattening has to drop it.
    void testWhitespaceInACommentIsNotDrawnAsABlackCell()
    {
        Utils::TemporaryDirectory dir("whitespace-cells");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("commented.cpp");
        QVERIFY(file.writeFileContents("// a b c\nint x = 1;\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const auto runs = [viewport] {
            return viewport->visibleLine(0).value("formats").toList();
        };
        // The highlighter arrives separately; before it does there is nothing
        // to check and the comment is not coloured yet.
        QTRY_VERIFY(!runs().isEmpty());

        for (const QVariant &run : runs()) {
            const QVariantMap format = run.toMap();
            QVERIFY2(!format.value("hasBackground").toBool(),
                     qPrintable(QString("run at %1 carries a background, which nothing on a "
                                        "plain comment line should")
                                    .arg(format.value("start").toInt())));
        }
    }

    // The same highlighter is what works out where the folds are, so a real
    // file opened in this editor has to be foldable - the folding tests fold
    // by hand and would not notice this arriving unwired.
    void testARealFileCanBeFoldedWithoutBeingToldWhere()
    {
        Utils::TemporaryDirectory dir("quick-editor-realfold");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("nested.json");
        QVERIFY(file.writeFileContents("{\n  \"a\": {\n    \"b\": 1\n  }\n}\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        if (HighlighterHelper::definitionsForDocument(document).isEmpty())
            QSKIP("no syntax definitions are installed, so nothing works out any folds");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        // Nobody set a folding indent here: the highlighter did, from the
        // braces it recognised. Which line ends up owning the fold is its
        // business - a line holding nothing but "{" is put *inside* the fold
        // rather than made its owner - so this asks for any of them.
        const auto foldableRow = [viewport] {
            for (int row = 0; row < viewport->visibleLineCount(); ++row) {
                if (viewport->visibleLine(row).value("foldable").toBool())
                    return row;
            }
            return -1;
        };
        QTRY_VERIFY2(foldableRow() >= 0,
                     "no line offers a fold, so the highlighter never worked any out");

        const int row = foldableRow();
        const int line = viewport->visibleLine(row).value("lineNumber").toInt();
        viewport->toggleFold(line);
        QTRY_VERIFY(viewport->visibleLine(row).value("folded").toBool());
        QVERIFY2(!viewport->visibleLine(row).value("foldReplacement").toString().isEmpty(),
                 "the fold closed but says nothing about what it hid");
    }

    // Preferences apply to this editor too. The global settings containers are
    // *pushed* into a document by whoever owns it - a TextDocument reads none
    // of them itself - so an editor that does not push them saves differently
    // from every other editor in Creator, silently.
    void testTheDocumentFollowsThePreferencesForSavingAndTyping()
    {
        const StorageSettingsData wasStorage = globalStorageSettings().data();
        const TypingSettingsData wasTyping = globalTypingSettings().data();
        const QScopeGuard restore([wasStorage, wasTyping] {
            globalStorageSettings().setData(wasStorage);
            globalTypingSettings().setData(wasTyping);
        });

        Utils::TemporaryDirectory dir("quick-editor-settings");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("saved.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        // Deliberately not the shipped defaults: a test that asks for the
        // default cannot tell "follows Preferences" from "ignores them".
        StorageSettingsData storage = wasStorage;
        storage.m_cleanWhitespace = true;
        storage.m_inEntireDocument = true;
        storage.m_addFinalNewLine = true;
        QVERIFY2(storage.m_addFinalNewLine != wasStorage.m_addFinalNewLine
                     || storage.m_inEntireDocument != wasStorage.m_inEntireDocument,
                 "the settings asked for are the ones already in force");
        globalStorageSettings().setData(storage);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->storageSettings().m_addFinalNewLine, true);
        QCOMPARE(document->storageSettings().m_inEntireDocument, true);
        // Deliberately not the default here either.
        TypingSettingsData typing = wasTyping;
        typing.m_tabKeyBehavior = wasTyping.m_tabKeyBehavior == TypingSettingsData::TabAlwaysIndents
                                      ? TypingSettingsData::TabNeverIndents
                                      : TypingSettingsData::TabAlwaysIndents;
        globalTypingSettings().setData(typing);
        QTRY_COMPARE(document->typingSettings().m_tabKeyBehavior, typing.m_tabKeyBehavior);

        // A line with trailing spaces and no newline at the end: saving has to
        // clean the one and add the other.
        document->document()->setPlainText("alpha   \nbeta");
        const Utils::Result<> saved = document->save(file);
        if (!saved)
            QFAIL(qPrintable(saved.error()));

        const Utils::Result<QByteArray> onDisk = file.fileContents();
        QVERIFY(onDisk.has_value());
        QCOMPARE(QString::fromUtf8(*onDisk), QString("alpha\nbeta\n"));

        // And a change made while the editor is open reaches it.
        StorageSettingsData relaxed = storage;
        relaxed.m_cleanWhitespace = false;
        globalStorageSettings().setData(relaxed);
        QTRY_COMPARE(document->storageSettings().m_cleanWhitespace, false);

        // How wide an indent is comes from the code style, which the document
        // has to be given: the viewport reads its tab settings for both the
        // tab stops it draws and what the Tab key inserts.
        const TabSettingsData wasTabs = globalCodeStyle().tabSettings();
        const QScopeGuard restoreTabs([wasTabs] { globalCodeStyle().setTabSettings(wasTabs); });

        // Derived from what the *document* holds, not from the global value:
        // an earlier test in this process may have left the global settings
        // anywhere, and asking for a number the document already had would
        // pass without anything having been propagated.
        const int wanted = document->tabSettings().m_indentSize + 3;
        TabSettingsData wider = wasTabs;
        wider.m_indentSize = wanted;
        globalCodeStyle().setTabSettings(wider);
        QTRY_COMPARE(document->tabSettings().m_indentSize, wanted);
    }

    // Putting the caret beside a bracket has to show which one it belongs to.
    // Where the brackets are is the highlighter's answer, so this needs a real
    // file in a real editor rather than a viewport told where they are.
    void testTheBracketBesideTheCaretIsPairedWithItsMatch()
    {
        Utils::TemporaryDirectory dir("quick-editor-parens");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("paired.json");
        //                              0        9
        QVERIFY(file.writeFileContents("{ \"a\": [1, 2] }\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        if (HighlighterHelper::definitionsForDocument(document).isEmpty())
            QSKIP("no syntax definitions are installed, so nothing records any brackets");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        const Utils::Id kind("TextEditor.TextViewport.ParenthesesMatch");

        // Away from any bracket, nothing is paired.
        viewport->setCursorPosition(4);
        QTRY_VERIFY(viewport->highlights(kind).isEmpty());

        // Immediately *before* the "[" at 7, which closes at 12. The caret has
        // to be before an opening bracket or after a closing one - beside it
        // on the inside is not beside it.
        const QString text = document->plainText();
        QCOMPARE(text.at(7), QChar('['));
        QCOMPARE(text.at(12), QChar(']'));
        viewport->setCursorPosition(7);

        QTRY_COMPARE(viewport->highlights(kind).size(), 2);
        QList<TextViewport::Highlight> pair = viewport->highlights(kind);
        QCOMPARE(pair.at(0).start, 7);
        QCOMPARE(pair.at(0).end, 8);
        QCOMPARE(pair.at(1).start, 12);
        QCOMPARE(pair.at(1).end, 13);
        // One character each, not everything between them.
        for (const TextViewport::Highlight &one : pair)
            QCOMPARE(one.end - one.start, 1);
        // And drawn in something, or nobody can see the pairing.
        QVERIFY2(pair.at(0).format.background().style() != Qt::NoBrush
                     && pair.at(0).format.background().color().isValid(),
                 "the pair is highlighted in nothing");

        // The other way round: after the closing bracket, looking backwards.
        viewport->setCursorPosition(13);
        QTRY_COMPARE(viewport->highlights(kind).size(), 2);
        pair = viewport->highlights(kind);
        QCOMPARE(pair.at(0).start, 7);
        QCOMPARE(pair.at(1).start, 12);

        // Moving away takes it back down again.
        viewport->setCursorPosition(4);
        QTRY_VERIFY(viewport->highlights(kind).isEmpty());

        // And the display setting turns it off, the way it does for the
        // widget. Turned off *while a pair is showing*, so that what is waited
        // for is the pair going away - an event - rather than its continued
        // absence, which is not one and would pass before anything happened.
        viewport->setCursorPosition(7);
        QTRY_COMPARE(viewport->highlights(kind).size(), 2);

        const bool wasOn = displaySettings().highlightMatchingParentheses();
        const QScopeGuard restore(
            [wasOn] { displaySettings().highlightMatchingParentheses.setValue(wasOn); });
        QVERIFY2(wasOn, "matching was already off, so turning it off proves nothing");
        displaySettings().highlightMatchingParentheses.setValue(false);

        QTRY_VERIFY2(viewport->highlights(kind).isEmpty(),
                     "brackets stayed paired although the setting says not to");
    }

    // Every other editor shows where the caret is in the toolbar row. Core
    // draws the row either way, so an editor that supplies nothing looks like
    // one whose indicator has stopped working.
    void testTheToolBarSaysWhereTheCaretIs()
    {
        Utils::TemporaryDirectory dir("quick-editor-toolbar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("where.txt");
        // A tab on the second line: a column count that ignores how wide a tab
        // is drawn would say 2 where the reader sees the ninth column.
        QVERIFY(file.writeFileContents("alpha\n\tbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        // Asked twice, the same one: the editor manager asks on every
        // activation, and a fresh one each time would lose what it was showing.
        QCOMPARE(editor->toolBar(), bar);

        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        QVERIFY(quick->rootObject());
        QObject * const label = quick->rootObject()->findChild<QObject *>("lineColumnLabel");
        QVERIFY2(label, "the toolbar shows no line and column");

        auto * const viewport = editor->widget()->findChild<QQuickWidget *>()
                                    ->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);

        viewport->setCursorPosition(2);
        QTRY_COMPARE(label->property("text").toString(), QString("Line: 1, Col: 3"));

        // After the tab on line 2, at one indent's width plus one.
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        const int tabWidth = document->tabSettings().m_tabSize;
        QVERIFY(tabWidth > 1);
        viewport->setCursorPosition(7); // "alpha\n" is 6, then the tab
        QTRY_COMPARE(label->property("text").toString(),
                     QString("Line: 2, Col: %1").arg(tabWidth + 1));

        // And it says how much is selected, when something is.
        viewport->setSelectionStart(6);
        viewport->setSelectionEnd(10);
        QTRY_VERIFY2(label->property("text").toString().contains("(Sel: 4)"),
                     qPrintable("no selection count: " + label->property("text").toString()));

        // Beside that, what the file is: its line endings and its encoding.
        QObject * const ending
            = quick->rootObject()->findChild<QObject *>("lineEndingLabel");
        QObject * const encoding
            = quick->rootObject()->findChild<QObject *>("encodingLabel");
        QVERIFY(ending && encoding);

        // The encoding is off by default and the line ending on - the same two
        // defaults the widget editor has - so both are set here rather than
        // assumed, and each is then checked to move when its own setting does.
        const bool wasEnding = displaySettings().displayFileLineEnding();
        const bool wasEncoding = displaySettings().displayFileEncoding();
        const QScopeGuard restore([wasEnding, wasEncoding] {
            displaySettings().displayFileLineEnding.setValue(wasEnding);
            displaySettings().displayFileEncoding.setValue(wasEncoding);
        });

        displaySettings().displayFileLineEnding.setValue(true);
        displaySettings().displayFileEncoding.setValue(true);
        QVERIFY(!document->encoding().displayName().isEmpty());
        QTRY_COMPARE(ending->property("text").toString(), QString("LF"));
        QTRY_COMPARE(encoding->property("text").toString(), document->encoding().displayName());

        // And each answers to its own setting rather than to the other's.
        displaySettings().displayFileEncoding.setValue(false);
        QTRY_COMPARE(encoding->property("text").toString(), QString());
        QCOMPARE(ending->property("text").toString(), QString("LF"));

        displaySettings().displayFileLineEnding.setValue(false);
        QTRY_COMPARE(ending->property("text").toString(), QString());
    }

    // The line ending is not only shown but changed: clicking it offers Unix
    // or Windows, and choosing marks the document modified rather than writing
    // to disk, which is what the widget editor does.
    void testTheToolBarChangesTheFilesLineEndings()
    {
        Utils::TemporaryDirectory dir("quick-editor-lineendings");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("endings.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->lineTerminationMode(), Utils::TextFileFormat::LFLineTerminator);
        QVERIFY(!document->isModified());

        const bool wasEnding = displaySettings().displayFileLineEnding();
        const QScopeGuard restore(
            [wasEnding] { displaySettings().displayFileLineEnding.setValue(wasEnding); });
        displaySettings().displayFileLineEnding.setValue(true);

        QWidget * const bar = editor->toolBar();
        QVERIFY(bar);
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QObject * const label = quick->rootObject()->findChild<QObject *>("lineEndingLabel");
        QVERIFY(label);
        QTRY_COMPARE(label->property("text").toString(), QString("LF"));

        // The menu is a popup, so its entries are not in the item tree under
        // the toolbar - it has to be asked directly.
        QObject * const menu = quick->rootObject()->findChild<QObject *>("lineEndingMenu");
        QVERIFY2(menu, "the line ending offers nothing to choose from");
        QObject * const windows = menu->findChild<QObject *>("windowsLineEndings");
        QObject * const unix = menu->findChild<QObject *>("unixLineEndings");
        QVERIFY(windows && unix);

        QMetaObject::invokeMethod(windows, "triggered");
        QCOMPARE(document->lineTerminationMode(), Utils::TextFileFormat::CRLFLineTerminator);
        QTRY_COMPARE(label->property("text").toString(), QString("CRLF"));
        QVERIFY2(document->isModified(),
                 "the line ending changed without the file needing to be saved");

        // What is on disk has not moved: it is a change to save, not a write.
        const Utils::Result<QByteArray> onDisk = file.fileContents();
        QVERIFY(onDisk.has_value());
        QCOMPARE(QString::fromUtf8(*onDisk), QString("alpha\nbeta\n"));

        // Saving is what applies it.
        const Utils::Result<> saved = document->save(file);
        if (!saved)
            QFAIL(qPrintable(saved.error()));
        const Utils::Result<QByteArray> after = file.fileContents();
        QVERIFY(after.has_value());
        QCOMPARE(QString::fromUtf8(*after), QString("alpha\r\nbeta\r\n"));

        // And back again - from an *already modified* document, which is the
        // case that needs the change announced on its own. setModified(true)
        // on something already modified emits nothing, so a label relying on
        // that signal would go stale exactly here.
        document->document()->setPlainText("alpha\nbeta\ngamma\n");
        QVERIFY2(document->isModified(), "the document is clean, so this proves nothing");
        QMetaObject::invokeMethod(unix, "triggered");
        QCOMPARE(document->lineTerminationMode(), Utils::TextFileFormat::LFLineTerminator);
        QTRY_COMPARE(label->property("text").toString(), QString("LF"));
    }

    // Beside the line ending, what the document indents with - and a menu to
    // change it for this document. "Display tab settings" is on by default,
    // so this is a button every user of the widget editor has.
    void testTheToolbarSaysWhatTheDocumentIndentsWith()
    {
        Utils::TemporaryDirectory dir("quick-editor-tabsettings");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("indent.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const bool was = displaySettings().displayTabSettings();
        const QScopeGuard restore(
            [was] { displaySettings().displayTabSettings.setValue(was); });
        displaySettings().displayTabSettings.setValue(true);

        // Said outright rather than taken from the global code style, which
        // another test in this process may have moved.
        TabSettingsData tabs = document->tabSettings();
        tabs.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        tabs.m_indentSize = 4;
        document->setTabSettings(tabs);

        QWidget * const bar = editor->toolBar();
        QVERIFY(bar);
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QObject * const label = quick->rootObject()->findChild<QObject *>("tabSettingsLabel");
        QVERIFY(label);
        QTRY_COMPARE(label->property("text").toString(), QString("Spaces: 4"));

        // The menu is a popup, so it has to be asked directly rather than
        // found under the toolbar.
        QObject * const menu = quick->rootObject()->findChild<QObject *>("tabSettingsMenu");
        QVERIFY2(menu, "the tab settings offer nothing to choose from");

        QObject * const useTabs = menu->findChild<QObject *>("indentWithTabs");
        QVERIFY(useTabs);
        QMetaObject::invokeMethod(useTabs, "triggered");
        QCOMPARE(document->tabSettings().m_tabPolicy, TabSettingsData::TabsOnlyTabPolicy);
        QTRY_COMPARE(label->property("text").toString(), QString("Tabs: 4"));

        // A submenu is a popup of its own, so it is not under the menu that
        // opens it - it has to be asked in the same way.
        // A submenu is a popup of its own, so it is not under the menu that
        // opens it and has to be asked in the same way.
        QObject * const sizes = quick->rootObject()->findChild<QObject *>("indentSizeMenu");
        QVERIFY(sizes);
        QObject * const twoWide = sizes->findChild<QObject *>("indentSize2");
        QVERIFY2(twoWide, "the indent size menu offers no sizes");
        QMetaObject::invokeMethod(twoWide, "triggered");
        QCOMPARE(document->tabSettings().m_indentSize, 2);
        QTRY_COMPARE(label->property("text").toString(), QString("Tabs: 2"));

        // Choosing any of it stops the file being guessed at, which is what
        // the widget editor's menu does before every change.
        QVERIFY(!document->tabSettings().m_autoDetect);

        // And the display setting takes the whole thing away.
        displaySettings().displayTabSettings.setValue(false);
        QTRY_COMPARE(label->property("text").toString(), QString());
    }

    // Wrapping is a display setting, so the editor has to open with whatever
    // it says and follow it while open - the Wrap Lines action toggles exactly
    // this while a file is in front of the reader.
    void testTheEditorWrapsWhenThePreferenceSaysTo()
    {
        const bool wasWrapping = displaySettings().textWrapping();
        const QScopeGuard restore(
            [wasWrapping] { displaySettings().textWrapping.setValue(wasWrapping); });
        displaySettings().textWrapping.setValue(true);

        Utils::TemporaryDirectory dir("quick-editor-wrap");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("long.txt");
        const QString longLine = QString("word ").repeated(80).trimmed();
        QVERIFY(file.writeFileContents((longLine + "\n").toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);

        // Opened with the setting on, so it is already wrapping.
        QVERIFY2(viewport->isWrapping(),
                 "the editor opened without wrapping although the preference is on");

        // And turning it off reaches the editor that is already open.
        displaySettings().textWrapping.setValue(false);
        QTRY_VERIFY2(!viewport->isWrapping(),
                     "turning wrapping off left the open editor wrapping");
        displaySettings().textWrapping.setValue(true);
        QTRY_VERIFY(viewport->isWrapping());
    }

    // Wrap Lines is a menu item with a shortcut, registered per editor in that
    // editor's context. Without one of its own, the entry is dead whenever a
    // Quick editor is the current one - it would look like the feature is
    // missing rather than the editor being unfinished.
    void testTheWrapLinesActionTogglesThisEditor()
    {
        const bool wasWrapping = displaySettings().textWrapping();
        const QScopeGuard restore(
            [wasWrapping] { displaySettings().textWrapping.setValue(wasWrapping); });
        displaySettings().textWrapping.setValue(false);

        // Two editors of this kind exist below, and each registers the same
        // command. Registered in a context they share, the second is dropped
        // with a warning and no failure - so the warning is what is asserted.
        ActionCollisions collisions;

        Utils::TemporaryDirectory dir("quick-editor-wrapaction");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("long.txt");
        QVERIFY(file.writeFileContents((QString("word ").repeated(80) + "\n").toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QVERIFY(!viewport->isWrapping());

        // The real command, not a stray QAction: this is the entry the menu
        // and the shortcut go through.
        Core::Command * const command = Core::ActionManager::command(Constants::TEXT_WRAPPING);
        QVERIFY2(command, "there is no Wrap Lines command to register against");

        // The editor registers several context actions; this is about the one
        // Wrap Lines goes through, which is the one the command holds for
        // this editor's context.
        QAction *wrap = nullptr;
        for (const Utils::Id context : editor->context()) {
            if (QAction * const forContext = command->actionForContext(context)) {
                wrap = forContext;
                break;
            }
        }
        QVERIFY2(wrap, "the editor registered no Wrap Lines action of its own");
        QVERIFY2(wrap->isCheckable(), "Wrap Lines is a toggle, not a one-shot");
        QCOMPARE(wrap->isChecked(), false);

        wrap->setChecked(true);
        QTRY_VERIFY2(viewport->isWrapping(), "the action was checked and nothing wrapped");

        wrap->setChecked(false);
        QTRY_VERIFY(!viewport->isWrapping());

        // It reflects the preference the editor opened with, so the menu shows
        // a tick when the file in front of the reader is wrapped.
        displaySettings().textWrapping.setValue(true);
        const Utils::FilePath other = dir.filePath("second.txt");
        QVERIFY(other.writeFileContents("short\n"));
        Core::IEditor * const second
            = Core::EditorManager::openEditor(other, QUICK_TEXT_EDITOR_ID);
        QVERIFY(second);
        const QScopeGuard closeSecond(
            [second] { Core::EditorManager::closeEditors({second}, false); });
        QAction *secondWrap = nullptr;
        for (const Utils::Id context : second->context()) {
            if (QAction * const forContext = command->actionForContext(context)) {
                secondWrap = forContext;
                break;
            }
        }
        QVERIFY(secondWrap);
        QVERIFY2(secondWrap->isChecked(),
                 "the editor opened wrapped but the menu entry is unticked");

        // join() rather than first(): QVERIFY2 builds its message whether or
        // not the condition held, and first() on an empty list asserts.
        QVERIFY2(collisions.hits().isEmpty(),
                 qPrintable("two editors of this kind collided over the same command: "
                            + collisions.hits().join("; ")));
    }

    // The other half of the same mechanism: a widget editor's diagnostics are
    // put on the document too, so a view that is not a TextEditorWidget can
    // draw them. Every producer of these - the language clients, the code
    // model - goes through the one method this exercises.
    void testAWidgetEditorsDiagnosticsReachTheDocument()
    {
        Utils::TemporaryDirectory dir("widget-editor-diagnostics");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("warned.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const base = qobject_cast<BaseTextEditor *>(editor);
        QVERIFY2(base, "the plain text editor is not a BaseTextEditor any more");
        TextEditorWidget * const widget = base->editorWidget();
        TextDocument * const document = base->textDocument();
        QVERIFY(widget && document);

        QTextCursor over(document->document());
        over.setPosition(0);
        over.setPosition(5, QTextCursor::KeepAnchor);
        QTextCharFormat warning;
        warning.setBackground(QColor(Qt::red));

        QVERIFY(document->extraSelections(TextEditorWidget::CodeWarningsSelection).isEmpty());
        widget->setExtraSelections(TextEditorWidget::CodeWarningsSelection, {{over, warning}});

        const QList<TextDocument::ExtraSelection> shared
            = document->extraSelections(TextEditorWidget::CodeWarningsSelection);
        QCOMPARE(shared.size(), 1);
        QCOMPARE(shared.first().cursor.selectionStart(), 0);
        QCOMPARE(shared.first().cursor.selectionEnd(), 5);
        QCOMPARE(shared.first().format.background().color(), QColor(Qt::red));

        // And what belongs to one view stays there: which bracket *this* view
        // is matching is not a fact about the file.
        widget->setExtraSelections(TextEditorWidget::ParenthesesMatchingSelection,
                                   {{over, warning}});
        QCOMPARE(widget->extraSelections(TextEditorWidget::ParenthesesMatchingSelection).size(), 1);
        QVERIFY2(document->extraSelections(TextEditorWidget::ParenthesesMatchingSelection).isEmpty(),
                 "a view's own bracket match was published to the document");
    }

    // A language server attaches to a document rather than to a language, so
    // it cannot register a finder on a factory - and for a C++ file it has to
    // be preferred over the one CppEditor registers, or Follow Symbol answers
    // from the built-in model while clangd is the thing that knows.
    void testADocumentsOwnLinkFinderBeatsItsLanguages()
    {
        Utils::TemporaryDirectory dir("document-link-finder");
        QVERIFY(dir.isValid());

        TextDocument document;
        document.setFilePath(dir.filePath("thing.cpp"));
        // Real text and a real cursor, because if the preference went the
        // other way the *language's* finder would run instead - and CppEditor's
        // dereferences the cursor's document, so a default-constructed one
        // turns a clean failure into a crash.
        document.setPlainText("int alpha() { return 0; }\n");
        QTextCursor cursor(document.document());
        cursor.setPosition(4);

        // The control on the fixture: if no language answered for a C++ file
        // there would be nothing to be preferred over.
        QVERIFY2(TextEditorFactory::linkFinderFor(&document),
                 "no language answers Follow Symbol for a C++ file, so preferring "
                 "the document's own finder would prove nothing");

        bool askedTheDocuments = false;
        document.setLinkFinder([&askedTheDocuments](TextDocument *, const QTextCursor &,
                                                    const Utils::LinkHandler &, bool, bool) {
            askedTheDocuments = true;
        });

        const LinkFinder finder = TextEditorFactory::linkFinderFor(&document);
        QVERIFY(finder);
        finder(&document, cursor, [](const Utils::Link &) {}, false, false);
        QVERIFY2(askedTheDocuments,
                 "the language's finder was used for a document carrying its own");

        // And cleared again, which is what a server going away has to leave
        // behind: the language answers for its own files once more. Not
        // invoked - what is being checked is that there is one, and running
        // CppEditor's for real needs a project this test has no business
        // setting up.
        document.setLinkFinder({});
        QVERIFY2(TextEditorFactory::linkFinderFor(&document),
                 "clearing the document's finder left the language without one");
    }

    void testSplittingAStringLiteral_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Enter inside a string literal ends it and opens another on the next
    // line. CppEditorWidget did this in its own keyPressEvent(), so a C++ file
    // in this editor got a plain newline in the middle of a string.
    //
    // Against the widget row this is the control on the fixture: it says the
    // file, the caret and the expected text are ones C++ really produces.
    void testSplittingAStringLiteral()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-string-split");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("split.cpp");
        QVERIFY(file.writeFileContents("const char *s = \"alpha beta\";\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // The key goes in through the view that has focus, not into the
        // document behind it: what is being tested is that the reader pressing
        // Enter reaches the language.
        QWidget *keyTarget = nullptr;
        if (quick) {
            auto * const quickWidget = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(quickWidget && quickWidget->rootObject());
            auto * const viewport = quickWidget->rootObject()->findChild<TextViewport *>();
            QVERIFY(viewport);
            QTRY_VERIFY(viewport->visibleLineCount() > 0);
            viewport->forceActiveFocus();
            QVERIFY2(viewport->hasActiveFocus(), "the viewport never took focus");
            keyTarget = quickWidget;
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            widget->setFocus();
            keyTarget = widget;
        }

        // Just after "alpha", inside the literal.
        editor->gotoLine(1, 22);
        const QTextCursor caret = textCursorOf(editor);
        QCOMPARE(document->plainText().mid(caret.position() - 5, 5), QString("alpha"));

        QTest::keyClick(keyTarget, Qt::Key_Return);

        QCOMPARE(document->plainText(),
                 QString("const char *s = \"alpha\"\n                \" beta\";\n"));
    }

    // The two branches of the moved code that a careless extraction would
    // drop: Shift escapes the line ending instead of closing the literal, and
    // the setting turns the whole thing off. Both in the Quick view, which is
    // the one that had neither until now.
    void testTheOtherWaysEnterTreatsAString_data()
    {
        QTest::addColumn<bool>("shift");
        QTest::addColumn<bool>("splitting");
        QTest::addColumn<QString>("expected");
        QTest::newRow("shift escapes the line ending")
            << true << true << QString("const char *s = \"alpha\\\n beta\";\n");
        QTest::newRow("setting off, no splitting")
            << false << false << QString("const char *s = \"alpha\n        beta\";\n");
    }

    void testTheOtherWaysEnterTreatsAString()
    {
        QFETCH(bool, shift);
        QFETCH(bool, splitting);
        QFETCH(QString, expected);

        auto &setting = globalCompletionSettings().autoSplitStrings;
        const bool wasSplitting = setting();
        const QScopeGuard restoreSetting(
            [&setting, wasSplitting] { setting.setValue(wasSplitting); });
        setting.setValue(splitting);

        Utils::TemporaryDirectory dir("quick-editor-string-split-modes");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("split.cpp");
        QVERIFY(file.writeFileContents("const char *s = \"alpha beta\";\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        auto * const quickWidget = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quickWidget && quickWidget->rootObject());
        auto * const viewport = quickWidget->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);
        viewport->forceActiveFocus();
        QVERIFY(viewport->hasActiveFocus());

        editor->gotoLine(1, 22);
        QTest::keyClick(quickWidget, Qt::Key_Return,
                        shift ? Qt::ShiftModifier : Qt::NoModifier);

        QCOMPARE(document->plainText(), expected);
    }

    // Why CppEditorWidget still calls the split itself, rather than leaving it
    // to the document like every other view: with several carets the document
    // is not asked at all - keyPressEvent() gates it on a single one - and the
    // widget's own call site is not gated. Pinned so that removing the
    // override is a decision rather than an accident.
    void testSeveralCaretsStillSplitInTheWidget()
    {
        Utils::TemporaryDirectory dir("widget-string-split-multi");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("split.cpp");
        QVERIFY(file.writeFileContents("const char *a = \"one two\";\n"
                                       "const char *b = \"three four\";\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(false);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        widget->setFocus();

        // Just after "one" and just after "three". The last one is the main
        // caret, which is the one the widget's own call site edits at.
        QTextCursor first(document->document());
        first.setPosition(20);
        QTextCursor second(document->document());
        second.setPosition(22 + QString("const char *a = \"one two\";\n").size());
        widget->setMultiTextCursor(Utils::MultiTextCursor({first, second}));
        QVERIFY(widget->multiTextCursor().hasMultipleCursors());

        QTest::keyClick(widget, Qt::Key_Return);

        // Only the main caret: the other line is untouched, and the key was
        // swallowed rather than reaching it. Not a defence of that - it is
        // what the widget has always done, and what the document path would
        // change if the override went away.
        QCOMPARE(document->plainText(),
                 QString("const char *a = \"one two\";\n"
                         "const char *b = \"three\"\n                \" four\";\n"));
    }

    // What the language offers at the caret, in the right-click menu. The fixes
    // arrive on their own signal after the menu is already up, so what this
    // checks is that the form is listening and that picking one reaches the
    // view.
    void testTheMenuOffersWhatTheLanguageWouldFix()
    {
        class OneFixItem final : public AssistProposalItem
        {
        public:
            void apply(AssistTarget &target, int basePosition) const override
            {
                target.replace(basePosition, target.position() - basePosition, "fixed");
            }
        };
        class OneFixProcessor final : public IAssistProcessor
        {
        public:
            IAssistProposal *perform() override
            {
                auto * const item = new OneFixItem;
                item->setText("Replace with fixed");
                QSharedPointer<GenericProposalModel> model(new GenericProposalModel);
                model->loadContent({item});
                return new GenericProposal(0, model);
            }
        };
        class OneFixProvider final : public IAssistProvider
        {
        public:
            IAssistProcessor *createProcessor(const AssistInterface *) const override
            {
                return new OneFixProcessor;
            }
        };

        Utils::TemporaryDirectory dir("quick-editor-menu-fixes");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("code.txt");
        QVERIFY(file.writeFileContents("broken\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 0);

        QObject * const menu = quick->rootObject()->findChild<QObject *>("editorContextMenu");
        QVERIFY2(menu, "the form drew no context menu at all");
        QVERIFY2(menu->property("extraItems").toStringList().isEmpty(),
                 "the menu offered the language's fixes before anything asked for them");

        // Not there at all yet, rather than there and greyed out: only
        // CppEditorWidget builds a Refactor menu in the widget editor, and a
        // language with nothing to offer here should leave the menu as it
        // found it.
        QVERIFY2(!menu->findChild<QObject *>("languageSubmenu"),
                 "the menu had a submenu for a language that offered nothing");
        const int entriesBefore = menu->property("count").toInt();

        OneFixProvider provider;
        document->setQuickFixAssistProvider(&provider);
        view->setCursorPosition(6);
        view->requestContextFixes();

        QTRY_COMPARE(menu->property("extraItems").toStringList(),
                     QStringList({"Replace with fixed"}));

        // In the menu, and not merely made: findChild() finds the submenu as
        // an object whether or not addMenu() ever put it in front of anybody,
        // so the menu's own entry count is what says it is there.
        QTRY_COMPARE(menu->property("count").toInt(), entriesBefore + 1);

        // And the entries themselves, not just the property they came from: a
        // submenu that drew nothing would leave that looking right.
        QObject * const submenu = menu->findChild<QObject *>("languageSubmenu");
        QVERIFY2(submenu, "the menu drew no submenu for the language's fixes");
        QTRY_COMPARE(submenu->property("count").toInt(), 1);
        QVERIFY2(!submenu->property("visible").toBool(),
                 "the submenu opened itself instead of waiting to be picked");

        // And picking one is what the view is told to apply, which is the other
        // half of the wiring: the entries are the language's, the doing is not.
        QMetaObject::invokeMethod(menu, "extraTriggered", Q_ARG(int, 0));
        QTRY_COMPARE(document->document()->findBlockByNumber(0).text(), QString("fixed"));
    }

    void testGoBackReturnsFromAJumpInsideOneFile_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Following a symbol to somewhere in the same file is a jump and not an
    // open, so the editor manager never sees it and never records it. The
    // widget editor tells it; a view that is not one has to as well, or Go
    // Back walks past the place the reader came from.
    //
    // Against the widget row this is the control on the fixture: it says the
    // jump and the walk back are ones Creator really does.
    void testGoBackReturnsFromAJumpInsideOneFile()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-goback");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("jump.cpp");
        QVERIFY(file.writeFileContents("int target();\n\nint caller()\n{\n"
                                       "    return target();\n}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        QCOMPARE(Core::EditorManager::currentEditor(), editor);

        // On the call, which is where following the symbol starts from.
        editor->gotoLine(5, 11);
        QCOMPARE(textCursorOf(editor).blockNumber(), 4);

        // The jump the language would have asked for, to the declaration on
        // line 1 of this same file.
        const Utils::Link here(file, 1, 4);
        if (quick) {
            TextViewport * const view = viewportForEditor(editor);
            QVERIFY(view);
            QVERIFY(view->openLink(here, false));
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            QVERIFY(widget->openLink(here, false));
        }
        QTRY_COMPARE(textCursorOf(editor).blockNumber(), 0);

        Core::EditorManager::goBackInNavigationHistory();
        QTRY_COMPARE_WITH_TIMEOUT(textCursorOf(editor).blockNumber(), 4, 5000);
    }

    // And only for the editor the reader is actually in. The manager records
    // "the current position", so a jump driven in a view the reader is not
    // looking at - a split, or a language client working in the background -
    // would push the *other* editor's position and Go Back would stop there.
    // BaseTextEditor guards its own call the same way.
    void testAJumpInABackgroundViewRecordsNothing()
    {
        Utils::TemporaryDirectory dir("quick-editor-goback-background");
        QVERIFY(dir.isValid());
        const Utils::FilePath first = dir.filePath("first.cpp");
        QVERIFY(first.writeFileContents("int target();\n\nint caller()\n{\n"
                                        "    return target();\n}\n"));
        const Utils::FilePath second = dir.filePath("second.cpp");
        QVERIFY(second.writeFileContents("int elsewhere() { return 0; }\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(first);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const background = Core::EditorManager::openEditor(first);
        QVERIFY(background);
        const QScopeGuard closeFirst(
            [background] { Core::EditorManager::closeEditors({background}, false); });
        background->gotoLine(5, 11);

        // Opening this one is what puts the first into the background, and is
        // also the history entry Go Back is expected to land on.
        Core::IEditor * const front = Core::EditorManager::openEditor(second);
        QVERIFY(front);
        const QScopeGuard closeSecond(
            [front] { Core::EditorManager::closeEditors({front}, false); });
        QCOMPARE(Core::EditorManager::currentEditor(), front);

        TextViewport * const view = viewportForEditor(background);
        QVERIFY(view);
        QVERIFY(view->openLink(Utils::Link(first, 1, 4), false));

        // One step back reaches the editor the reader left, because the jump
        // above added nothing. Were it recorded, that step would be spent on
        // the front editor's own position instead.
        Core::EditorManager::goBackInNavigationHistory();
        QTRY_COMPARE_WITH_TIMEOUT(Core::EditorManager::currentEditor(), background, 5000);
    }

    // A jump is remembered once the reader moves off it, so that Go Back
    // returns to what a search result or a definition took them to. The widget
    // editor does this through TextEditorWidget::slotCursorPositionChanged();
    // a view that is not one has to do it itself.
    //
    // Against the widget row this is the control on the fixture: it says the
    // jump, the move and the step back are ones Creator really does.
    void testGoBackReturnsToAJumpTheReaderLeft_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testGoBackReturnsToAJumpTheReaderLeft()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-navprobe");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("lines.cpp");
        QString text;
        for (int i = 0; i < 40; ++i)
            text += QString("int line%1();\n").arg(i);
        QVERIFY(file.writeFileContents(text.toUtf8()));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        QWidget *keyTarget = nullptr;
        if (quick) {
            auto * const qw = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(qw && qw->rootObject());
            auto * const view = qw->rootObject()->findChild<TextViewport *>();
            QVERIFY(view);
            QTRY_VERIFY(view->visibleLineCount() > 0);
            view->forceActiveFocus();
            keyTarget = qw;
        } else {
            TextEditorWidget * const w = TextEditorWidget::fromEditor(editor);
            QVERIFY(w);
            w->setFocus();
            keyTarget = w;
        }

        editor->gotoLine(1, 0);
        editor->gotoLine(20, 0);
        const int afterJump = textCursorOf(editor).blockNumber();

        for (int i = 0; i < 5; ++i)
            QTest::keyClick(keyTarget, Qt::Key_Down);
        const int afterMove = textCursorOf(editor).blockNumber();

        Core::EditorManager::goBackInNavigationHistory();
        const int afterBack = textCursorOf(editor).blockNumber();

        QCOMPARE(afterJump, 19);
        QCOMPARE(afterMove, 24);
        QCOMPARE(afterBack, 19);

        // And typing at the destination instead of moving off it, which the
        // widget editor treats differently: an edit is not the reader leaving.
        editor->gotoLine(30, 0);
        QTest::keyClick(keyTarget, 'x');
        const int afterTyping = textCursorOf(editor).blockNumber();
        Core::EditorManager::goBackInNavigationHistory();
        QCOMPARE(afterTyping, 29);
        // Neither view moves: an edit is not the reader leaving, so there is
        // no entry to step back to. Measured on both rather than reasoned
        // from the widget's flags, which its own code calls too heavy.
        QCOMPARE(textCursorOf(editor).blockNumber(), 29);
    }

    void testGoToLastEditReturnsToTheEdit_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Go to Last Edit returns to where the reader last changed something.
    // TextEditorWidget tells the manager from its own cursor handler; a view
    // that is not one has to say so itself, or the command has nothing to go
    // to and lands nowhere.
    //
    // Against the widget row this is the control on the fixture: it says the
    // edit, the wander and the command are ones Creator really does.
    void testGoToLastEditReturnsToTheEdit()
    {
        QFETCH(bool, quick);

        // The command's context is Context(C_EDITORMANAGER, C_DESIGN_MODE),
        // which follows a mode this process never enters - so its action stays
        // disabled and triggering it does nothing. Adding the context by hand
        // is what lets a test drive a command at all; showing the main window
        // does not, which was measured before settling on this.
        const Core::Context editorContext(Core::Constants::C_EDITORMANAGER);
        Core::ICore::addAdditionalContext(editorContext);
        const QScopeGuard dropContext(
            [editorContext] { Core::ICore::removeAdditionalContext(editorContext); });

        Utils::TemporaryDirectory dir("quick-editor-lastedit");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("edit.cpp");
        QString text;
        for (int i = 0; i < 40; ++i)
            text += QString("int line%1();\n").arg(i);
        QVERIFY(file.writeFileContents(text.toUtf8()));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        QWidget *keyTarget = nullptr;
        if (quick) {
            auto * const qw = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(qw && qw->rootObject());
            auto * const view = qw->rootObject()->findChild<TextViewport *>();
            QVERIFY(view);
            QTRY_VERIFY(view->visibleLineCount() > 0);
            view->forceActiveFocus();
            keyTarget = qw;
        } else {
            TextEditorWidget * const w = TextEditorWidget::fromEditor(editor);
            QVERIFY(w);
            w->setFocus();
            keyTarget = w;
        }

        // Change something on line 5, then wander off to line 30.
        editor->gotoLine(5, 0);
        QTest::keyClick(keyTarget, 'x');
        QCOMPARE(textCursorOf(editor).blockNumber(), 4);
        editor->gotoLine(30, 0);
        QCOMPARE(textCursorOf(editor).blockNumber(), 29);

        Core::Command * const cmd = Core::ActionManager::command(Core::Constants::GOTOLASTEDIT);
        QVERIFY(cmd);
        QVERIFY2(cmd->action()->isEnabled(),
                 "the command is disabled, so triggering it would prove nothing");
        cmd->action()->trigger();

        QTRY_COMPARE(textCursorOf(editor).blockNumber(), 4);
    }

    // And only the editor the reader is in says so. An edit in a view they are
    // not looking at - a refactoring rewriting a file open in another split -
    // would otherwise move Go to Last Edit to somewhere they never typed.
    // TextEditorWidget guards its own call the same way.
    void testAnEditInABackgroundViewIsNotTheLastEdit()
    {
        const Core::Context editorContext(Core::Constants::C_EDITORMANAGER);
        Core::ICore::addAdditionalContext(editorContext);
        const QScopeGuard dropContext(
            [editorContext] { Core::ICore::removeAdditionalContext(editorContext); });

        Utils::TemporaryDirectory dir("quick-editor-lastedit-background");
        QVERIFY(dir.isValid());
        const Utils::FilePath first = dir.filePath("first.cpp");
        QString text;
        for (int i = 0; i < 40; ++i)
            text += QString("int line%1();\n").arg(i);
        QVERIFY(first.writeFileContents(text.toUtf8()));
        const Utils::FilePath second = dir.filePath("second.cpp");
        QVERIFY(second.writeFileContents("int elsewhere();\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(first);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const background = Core::EditorManager::openEditor(first);
        QVERIFY(background);
        const QScopeGuard closeFirst(
            [background] { Core::EditorManager::closeEditors({background}, false); });

        auto * const qw = background->widget()->findChild<QQuickWidget *>();
        QVERIFY(qw && qw->rootObject());
        auto * const view = qw->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 0);
        view->forceActiveFocus();

        // The edit the reader made, while they were here.
        background->gotoLine(5, 0);
        QTest::keyClick(qw, 'x');
        QCOMPARE(textCursorOf(background).blockNumber(), 4);

        Core::IEditor * const front = Core::EditorManager::openEditor(second);
        QVERIFY(front);
        const QScopeGuard closeSecond(
            [front] { Core::EditorManager::closeEditors({front}, false); });
        QCOMPARE(Core::EditorManager::currentEditor(), front);

        // And an edit in the first one while it is behind: not theirs to go
        // back to, so it must not move where Go to Last Edit points.
        background->gotoLine(30, 0);
        QTextCursor edit = textCursorOf(background);
        edit.insertText("y");

        Core::Command * const cmd = Core::ActionManager::command(Core::Constants::GOTOLASTEDIT);
        QVERIFY(cmd);
        QVERIFY2(cmd->action()->isEnabled(), "the command is disabled, so this proves nothing");
        cmd->action()->trigger();

        QTRY_COMPARE(Core::EditorManager::currentEditor(), background);
        QCOMPARE(textCursorOf(background).blockNumber(), 4);
    }

    // Which languages open in the Qt Quick editor, written down rather than
    // grepped for. Three times in this migration a gap has been "closed" in
    // code that nothing reaches, because the language's files still open in
    // the widget editor: dragging a mark, the QML debugger's exception
    // highlight, and QmlJSEditor's own warnings. Telling meant finding every
    // setUsesQuickEditor() call, and the single production one is easy to miss
    // among forty test scope guards.
    //
    // So: ask what a file of each language actually opens in. A language
    // moving is a deliberate act, and moving it should change this list in the
    // same commit.
    void testWhichLanguagesOpenInTheQuickEditor()
    {
        Utils::TemporaryDirectory dir("quick-editor-census");
        QVERIFY(dir.isValid());

        struct Expectation { const char *name; bool quick; };
        const QList<Expectation> expected{
            {"main.cpp", true},
            {"header.h", true},
            {"notes.txt", true},
            {"Thing.qml", false},
            {"project.pro", false},
        };

        QStringList wrong;
        int checked = 0;
        for (const Expectation &e : expected) {
            const Utils::FilePath file = dir.filePath(QString::fromLatin1(e.name));
            QVERIFY(file.writeFileContents(""));
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            if (!editor) {
                wrong << QString("%1: nothing opened it").arg(e.name);
                continue;
            }
            const bool isQuick = !TextEditorWidget::fromEditor(editor);
            Core::EditorManager::closeEditors({editor}, false);
            ++checked;
            if (isQuick != e.quick) {
                wrong << QString("%1 opened in the %2 editor")
                             .arg(QString::fromLatin1(e.name),
                                  isQuick ? QString("Qt Quick") : QString("widget"));
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join("; ")));
        QCOMPARE(checked, expected.size());
    }

    // A census, the way the settings pages have one: every language that has
    // been moved off its findLinkAt() override has to register a finder in
    // its place, or Follow Symbol quietly stops working in that language -
    // in the widget editor as well as here, and with no test to say so.
    void testEveryConvertedLanguageRegistersALinkFinder()
    {
        Utils::TemporaryDirectory dir("link-finders");
        QVERIFY(dir.isValid());

        // File names rather than mime types: preferredFactoryFor() walks the
        // mime type's parents, and that walk is half of what is being checked.
        const QStringList names{"CMakeLists.txt", "project.pro", "Thing.qml",
                                "project.qbs", "module.nim", "main.cpp", "header.h"};

        QStringList missing;
        int checked = 0;
        for (const QString &name : names) {
            const Utils::FilePath file = dir.filePath(name);
            QVERIFY(file.writeFileContents(""));
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            if (!factory) {
                missing << name + " (no editor factory claims it)";
                continue;
            }
            ++checked;
            if (!factory->linkFinder())
                missing << name;
        }
        QVERIFY2(missing.isEmpty(), qPrintable("no link finder for: " + missing.join(", ")));
        QCOMPARE(checked, names.size());
    }

    // Following a symbol used to be a virtual on a TextEditorWidget subclass,
    // one per language, which a single editor for every language cannot
    // implement. The language registers a link finder instead, and the Quick
    // editor asks the one that claims the file.
    void testFollowingASymbolAsksTheLanguage()
    {
        Utils::TemporaryDirectory dir("quick-editor-follow");
        QVERIFY(dir.isValid());
        const Utils::FilePath included = dir.filePath("Included.cmake");
        QVERIFY(included.writeFileContents("set(SOMETHING 1)\n"));
        const Utils::FilePath lists = dir.filePath("CMakeLists.txt");
        QVERIFY(lists.writeFileContents("include(Included.cmake)\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(lists, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [] { Core::EditorManager::closeAllEditors(false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);

        // The CMake plugin is what knows that include() names a file. It is
        // reached by mime type, so a .txt file has no finder and a
        // CMakeLists.txt does.
        QVERIFY2(TextEditorFactory::linkFinderFor(document),
                 "no link finder for a CMakeLists.txt");

        // On "Included.cmake", inside include(...).
        viewport->setCursorPosition(12);
        viewport->followSymbolUnderCursor();

        // Opening the file is the editor manager's doing and is asynchronous
        // in neither direction, but the finder may answer through a callback.
        QTRY_COMPARE(Core::EditorManager::currentDocument()->filePath(), included);

        // Ctrl+click is the other way in, and it follows what is under the
        // pointer rather than what is under the caret. Back to the first
        // editor, with the caret somewhere else entirely.
        Core::EditorManager::activateEditor(editor);
        QTRY_COMPARE(Core::EditorManager::currentDocument()->filePath(), lists);
        viewport->setCursorPosition(0);
        QVERIFY(viewport->followSymbolAt(12));
        QTRY_COMPARE(Core::EditorManager::currentDocument()->filePath(), included);

        // And through the form, which is what actually turns a Ctrl+click into
        // the call above: a plain click has to keep putting the caret down.
        Core::EditorManager::activateEditor(editor);
        QTRY_COMPARE(Core::EditorManager::currentDocument()->filePath(), lists);
        viewport->setCursorPosition(0);
        QVERIFY(viewport->lineHeight() > 0);
        const QRectF onInclude = viewport->rectangleAt(12);
        QVERIFY(!onInclude.isNull());
        const QPoint clickAt = viewport
                                   ->mapToScene(QPointF(onInclude.center().x(),
                                                        viewport->lineHeight() / 2))
                                   .toPoint();
        QTest::mouseClick(quick, Qt::LeftButton, Qt::ControlModifier, clickAt);
        QTRY_COMPARE(Core::EditorManager::currentDocument()->filePath(), included);

        // A file whose language has no finder must not swallow the click, or
        // Ctrl+click stops putting the caret anywhere in a plain text file.
        const Utils::FilePath plain = dir.filePath("notes.txt");
        QVERIFY(plain.writeFileContents("nothing to follow\n"));
        Core::IEditor * const plainEditor
            = Core::EditorManager::openEditor(plain, QUICK_TEXT_EDITOR_ID);
        QVERIFY(plainEditor);
        auto * const plainQuick = plainEditor->widget()->findChild<QQuickWidget *>();
        QVERIFY(plainQuick && plainQuick->rootObject());
        auto * const plainViewport = plainQuick->rootObject()->findChild<TextViewport *>();
        QVERIFY(plainViewport);
        QVERIFY2(!plainViewport->followSymbolAt(3),
                 "a plain text file claimed to know where a symbol is defined");
    }

    // Hovering. A tooltip in Creator is a hover handler's answer, and the
    // handlers used to be reachable only from a TextEditorWidget. The Quick
    // view answers the same questions, so the same handlers run over it.
    void testRestingTheMouseAsksTheHoverHandlers()
    {
        Utils::TemporaryDirectory dir("quick-editor-hover");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hovered.txt");
        // One line and no trailing newline, so the file is a single block:
        // where the view has scrolled to cannot then change which block the
        // pointer is over, and this test is not about scrolling.
        QVERIFY(file.writeFileContents("alpha beta gamma"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // A tooltip is a widget wherever it is asked for, so the view has to
        // name one to place it against - and it is the hosting QQuickWidget,
        // not the offscreen window the item actually lives in.
        QCOMPARE(viewport->tooltipParent(), quick);
        // And it is listening for the mouse at all: without this the scene
        // delivers no hover events and none of the below ever happens.
        QVERIFY(viewport->acceptHoverEvents());
        QCOMPARE(viewport->textDocument(), qobject_cast<TextDocument *>(editor->document()));

        // Standing in for a language's handler: what matters here is that it
        // is asked at all, and about the right place. It notes what the view
        // says is under the pointer at the moment it is asked - the view goes
        // on laying itself out, and a reading taken later is a reading of a
        // different layout.
        class RecordingHoverHandler final : public BaseHoverHandler
        {
        public:
            TextViewport *view = nullptr;
            QPointF pointer;

            int askedAt = -1;
            int underPointer = -1;
            int shown = 0;
            TextDocument *seenDocument = nullptr;
            QPoint at;

            void identifyMatch(HoverTarget *target, int pos, ReportPriority report) override
            {
                askedAt = pos;
                underPointer = view->positionAt(pointer.x(), pointer.y());
                seenDocument = target->textDocument();
                setToolTip("the answer");
                report(Priority_Tooltip);
            }
            void operateTooltip(HoverTarget *, const QPoint &point) override
            {
                ++shown;
                at = point;
            }
        } handler;
        handler.view = viewport;
        viewport->setHoverHandlers({&handler});

        // The pointer goes somewhere inside "beta". The y is a fraction of the
        // view's own line height so that it is the first line whatever the
        // font turns out to be, and the x is where the view is drawing the
        // word.
        //
        // Put back and asked again if the answer is about somewhere else: a
        // real mouse moving across the editor restarts the view's timer with
        // a point of its own, which is the view doing its job and this test
        // losing its question.
        QVERIFY(viewport->lineHeight() > 0);
        QPointF over;
        for (int attempt = 0; attempt < 5 && handler.askedAt != 6; ++attempt) {
            const QRectF beta = viewport->rectangleAt(8);
            QVERIFY2(!beta.isNull(), "the view could not place the eighth character");
            over = QPointF(beta.center().x(), viewport->lineHeight() / 2);
            handler.pointer = over;

            const int before = handler.shown;
            QHoverEvent move(QEvent::HoverMove, over, over, QPointF(-1, -1));
            QCoreApplication::sendEvent(viewport, &move);

            // Asked once the mouse has rested, which is what the view times;
            // nothing else in this test produces that event.
            QTRY_VERIFY(handler.shown > before);
        }

        // About the start of the word under the mouse, the way a hover handler
        // is always asked - not about the character the pointer is on, and not
        // about the top of the file.
        QCOMPARE(handler.askedAt, 6);
        QCOMPARE(handler.underPointer, 8);
        QCOMPARE(handler.seenDocument, qobject_cast<TextDocument *>(editor->document()));

        // Placed on the screen, over the editor it is about. A tooltip is a
        // window of its own, so an anchor in the item's own coordinates would
        // put it in a corner of the display.
        const QRect onScreen(quick->mapToGlobal(QPoint(0, 0)), quick->size());
        QVERIFY2(onScreen.contains(handler.at),
                 qPrintable(QString("tooltip at %1,%2 with the editor at %3,%4 %5x%6")
                                .arg(handler.at.x()).arg(handler.at.y())
                                .arg(onScreen.x()).arg(onScreen.y())
                                .arg(onScreen.width()).arg(onScreen.height())));

        // And moving on takes the question back: what it was about is no
        // longer under the mouse.
        const int shownBeforeLeaving = handler.shown;
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), over, over);
        QCoreApplication::sendEvent(viewport, &leave);
        QCOMPARE(handler.shown, shownBeforeLeaving);
    }

    // The same question without a mouse: Alt on its own asks about the caret.
    // "Show help tooltips using the keyboard" is off by default and was read
    // nowhere, so it did nothing at all.
    void testAltOnItsOwnAsksAboutTheCaret()
    {
        const bool was = globalBehaviorSettings().keyboardTooltips();
        const QScopeGuard restore(
            [was] { globalBehaviorSettings().keyboardTooltips.setValue(was); });
        globalBehaviorSettings().keyboardTooltips.setValue(true);

        Utils::TemporaryDirectory dir("quick-editor-keytooltip");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("asked.txt");
        QVERIFY(file.writeFileContents("alpha beta gamma"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        class AskedHandler final : public BaseHoverHandler
        {
        public:
            int askedAt = -1;
            int shown = 0;

            void identifyMatch(HoverTarget *, int pos, ReportPriority report) override
            {
                askedAt = pos;
                setToolTip("the answer");
                report(Priority_Tooltip);
            }
            void operateTooltip(HoverTarget *, const QPoint &) override { ++shown; }
        } handler;
        viewport->setHoverHandlers({&handler});

        // The caret goes inside "beta", and a handler is asked about the word
        // rather than the character, so it is asked about 6.
        viewport->setCursorPosition(8);
        QTRY_VERIFY(viewport->cursorRectangle().height() > 0);

        // Sent to the view rather than typed at the window: which item has
        // the keyboard is a question about focus, and this test is about what
        // the view does with the keys it gets.
        const auto altDown = [viewport] {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
            QCoreApplication::sendEvent(viewport, &press);
        };
        const auto altUp = [viewport] {
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &release);
        };

        // Alt down and up again with nothing in between.
        altDown();
        altUp();
        QTRY_COMPARE(handler.shown, 1);
        QCOMPARE(handler.askedAt, 6);

        // Alt as part of a shortcut is not a request for a tooltip.
        handler.shown = 0;
        altDown();
        QKeyEvent shortcut(QEvent::KeyPress, Qt::Key_F5, Qt::AltModifier);
        QCoreApplication::sendEvent(viewport, &shortcut);
        altUp();
        QVERIFY2(handler.shown == 0, "a shortcut asked for a tooltip");

        // And with the setting off, Alt on its own asks nothing. Checked
        // after a request that does go through, so that the absence is read
        // once the view has had its chance rather than before it.
        globalBehaviorSettings().keyboardTooltips.setValue(false);
        viewport->setCursorPosition(2);
        QTRY_VERIFY(viewport->cursorRectangle().height() > 0);
        altDown();
        altUp();
        globalBehaviorSettings().keyboardTooltips.setValue(true);
        altDown();
        altUp();
        QTRY_COMPARE(handler.shown, 1);
        QCOMPARE(handler.askedAt, 0);
    }

    // A hover handler asks for the message under the mouse. It gets it from
    // the document, which is what lets a view that is not a TextEditorWidget
    // show the same diagnostics.
    void testTheMessageUnderTheMouseIsAskedOfTheDocument()
    {
        Utils::TemporaryDirectory dir("hovered");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hovered.txt");
        QVERIFY(file.writeFileContents("alpha beta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const base = qobject_cast<BaseTextEditor *>(editor);
        QVERIFY(base);
        TextEditorWidget * const widget = base->editorWidget();
        TextDocument * const document = base->textDocument();
        QVERIFY(widget && document);

        QCOMPARE(document->extraSelectionTooltip(2), QString());

        QTextCursor over(document->document());
        over.setPosition(0);
        over.setPosition(5, QTextCursor::KeepAnchor);
        QTextCharFormat warning;
        warning.setToolTip("alpha is not a word");
        widget->setExtraSelections(TextEditorWidget::CodeWarningsSelection, {{over, warning}});

        QCOMPARE(document->extraSelectionTooltip(2), QString("alpha is not a word"));
        // Outside the range is not "the nearest message".
        QCOMPARE(document->extraSelectionTooltip(8), QString());

        // And the handler reaches it through the interface, without knowing
        // what kind of view it is talking to.
        HoverTarget * const target = widget;
        QCOMPARE(target->extraSelectionTooltip(2), QString("alpha is not a word"));
    }

    // Choosing an encoding does one of two quite different things, and which
    // one is the whole point of the dialog asking. Tested without the dialog:
    // Core owns the asking, this owns what is done with the answer.
    void testChoosingAnEncodingEitherRereadsOrRewrites()
    {
        Utils::TemporaryDirectory dir("quick-editor-encoding");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("bytes.txt");

        // Latin-1 bytes: 0xE4 is a-umlaut there and not valid UTF-8, so which
        // encoding the file is read as is visible in the text.
        const QByteArray latin1 = QByteArray("caf\xE4\n");
        QVERIFY(file.writeFileContents(latin1));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);

        // The label is what opens the chooser, so it has to accept a click.
        // Only the handler can be asserted here: what it opens is a modal
        // dialog, and a test that opened one would stop. It lives in the
        // toolbar, which is its own form and is built on demand.
        QWidget * const bar = editor->toolBar();
        QVERIFY(bar);
        auto * const barForm = bar->findChild<QQuickWidget *>();
        QVERIFY(barForm && barForm->rootObject());
        QObject * const label = barForm->rootObject()->findChild<QObject *>("encodingLabel");
        QVERIFY(label);
        const QList<QObject *> attached = label->findChildren<QObject *>();
        QVERIFY2(Utils::anyOf(attached, [](QObject *child) {
                     return QString::fromLatin1(child->metaObject()->className())
                         .contains("TapHandler");
                 }),
                 "the encoding label takes no clicks, so it opens nothing");

        const Utils::TextEncoding latin1Encoding("ISO-8859-1");
        QVERIFY2(latin1Encoding.isValid(), "no Latin-1 codec, so this proves nothing");
        QVERIFY(document->encoding() != latin1Encoding);

        // Cancel does nothing at all - not even mark the document.
        const QString before = document->plainText();
        const bool wasModified = document->isModified();
        viewport->applyEncodingChoice({Core::CodecSelectorResult::Cancel, latin1Encoding});
        QCOMPARE(document->plainText(), before);
        QCOMPARE(document->isModified(), wasModified);

        // Reload reads the same bytes as something else: the text changes and
        // the file does not.
        viewport->applyEncodingChoice({Core::CodecSelectorResult::Reload, latin1Encoding});
        QCOMPARE(document->encoding(), latin1Encoding);
        QCOMPARE(document->plainText(), QString::fromLatin1("caf\xE4\n"));
        const Utils::Result<QByteArray> onDisk = file.fileContents();
        QVERIFY(onDisk.has_value());
        QCOMPARE(*onDisk, latin1);

        // Save is the other direction: the text stays and the bytes change.
        const Utils::TextEncoding utf8("UTF-8");
        QVERIFY(utf8.isValid());
        const QString shown = document->plainText();
        viewport->applyEncodingChoice({Core::CodecSelectorResult::Save, utf8});
        QCOMPARE(document->encoding(), utf8);
        QCOMPARE(document->plainText(), shown);
        const Utils::Result<QByteArray> rewritten = file.fileContents();
        QVERIFY(rewritten.has_value());
        QVERIFY2(*rewritten != latin1,
                 "saving as UTF-8 left the Latin-1 bytes on disk");
        QCOMPARE(QString::fromUtf8(*rewritten), shown);
    }

    // A split view is two editors on one document. Duplicating has to share
    // the document rather than open the file twice, or an edit in one half
    // does not appear in the other.
    void testASplitShowsTheSameDocumentTwice()
    {
        Utils::TemporaryDirectory dir("quick-editor-split");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("shared.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        // Said as well as done: the editor manager asks this rather than
        // trying, and one it believes cannot be duplicated is moved between
        // split views instead of copied, which loses the tab the first view
        // was keeping for it.
        QVERIFY2(editor->duplicateSupported(),
                 "the editor can be split but does not say so");

        std::unique_ptr<Core::IEditor> other(editor->duplicate());
        QVERIFY2(other, "the editor cannot be split");
        QCOMPARE(other->document(), editor->document());
    }



    void testTheSelectionCanBeRemovedForADragThatMovedIt()
    {
        Utils::TemporaryDirectory dir("quick-editor-drag-out");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("drag.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        // Without asking: the test dirties the document, and a modal "save
        // changes?" has nobody to answer it here.
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QVERIFY2(quick->rootObject()->findChild<QObject *>("editorDragProxy"),
                 "the form has nothing to drag a selection out with");
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 1);

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // What the drag carries is the selection, and what a move leaves
        // behind is nothing.
        viewport->setSelectionStart(0);
        viewport->setSelectionEnd(5);
        QCOMPARE(viewport->selectedText(), QString("alpha"));
        viewport->removeSelectedText();
        QCOMPARE(document->plainText(), QString("\nbeta\n"));
        QVERIFY(viewport->selectedText().isEmpty());
    }

    // Preferences > Text Editor > Display describes what an editor draws. The
    // widget editor consults every one of these; an editor that ignores them
    // draws something the user did not ask for, and the current-line highlight
    // is the one that shows: it is off by default, so an editor that always
    // draws it is the odd one out on a stock Creator.
    void testTheEditorDrawsWhatTheDisplaySettingsAskFor()
    {
        Utils::TemporaryDirectory dir("quick-editor-display");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("display.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        const bool wasCurrentLine = displaySettings().highlightCurrentLine();
        const bool wasNumbers = displaySettings().displayLineNumbers();
        const bool wasFolding = displaySettings().displayFoldingMarkers();
        const bool wasAnnotations = displaySettings().displayAnnotations();
        const QScopeGuard restore([wasCurrentLine, wasNumbers, wasFolding, wasAnnotations] {
            displaySettings().highlightCurrentLine.setValue(wasCurrentLine);
            displaySettings().displayLineNumbers.setValue(wasNumbers);
            displaySettings().displayFoldingMarkers.setValue(wasFolding);
            displaySettings().displayAnnotations.setValue(wasAnnotations);
        });

        // Set before the editor is built, so that what is asserted first is
        // the value the form was *created* with. Pushing a change into an open
        // editor is a second path and is exercised below; a test that only
        // changed things afterwards would leave the first one unstated.
        displaySettings().highlightCurrentLine.setValue(false);
        displaySettings().displayLineNumbers.setValue(false);
        displaySettings().displayFoldingMarkers.setValue(false);
        displaySettings().displayAnnotations.setValue(false);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QQuickItem * const form = quick->rootObject();
        auto * const viewport = form->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        auto * const highlight = form->findChild<QQuickItem *>("currentLineHighlight");
        QVERIFY2(highlight, "the form has no current-line highlight at all");
        auto * const gutter = form->findChild<QQuickItem *>("codeGutter");
        QVERIFY(gutter);

        // Everything the form was *built* with, asserted before anything is
        // changed: a single later change pushes all of these in again, which
        // would cover for an initial value that was never read.
        QVERIFY2(!highlight->isVisible(),
                 "the current line was marked without the setting asking for it");
        QCOMPARE(form->property("showLineNumbers").toBool(), false);
        QCOMPARE(form->property("showFoldMarkers").toBool(), false);
        // What this one turns off is checked where the annotation can be seen,
        // in TextViewportTest; here it is that the editor hands the setting to
        // the form at all.
        QCOMPARE(form->property("showAnnotations").toBool(), false);
        QVERIFY2(gutter->width() < 1,
                 "the gutter took room for numbers the settings turned off");

        // And each of them reaches an editor that is already open, rather than
        // only the next one to be built.
        displaySettings().highlightCurrentLine.setValue(true);
        QTRY_VERIFY2(highlight->isVisible(), "turning the setting on did not reach the editor");
        displaySettings().displayLineNumbers.setValue(true);
        QTRY_COMPARE(form->property("showLineNumbers").toBool(), true);
        QTRY_VERIFY2(gutter->width() > 0, "turning the numbers on did not reach the editor");
        displaySettings().displayFoldingMarkers.setValue(true);
        QTRY_COMPARE(form->property("showFoldMarkers").toBool(), true);
        displaySettings().displayAnnotations.setValue(true);
        QTRY_COMPARE(form->property("showAnnotations").toBool(), true);
    }

    void testWhitespaceIsShownWhenTheSettingAsksForIt()
    {
        // Whether spaces and tabs are drawn is a flag on the document's own
        // text option, so this is about the document rather than about
        // anything the form owns - and an editor that never sets it shows no
        // whitespace however the setting is left.
        Utils::TemporaryDirectory dir("quick-editor-whitespace");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("spaces.txt");
        QVERIFY(file.writeFileContents("alpha  beta\n\tgamma\n"));

        const bool was = displaySettings().visualizeWhitespace();
        const QScopeGuard restore(
            [was] { displaySettings().visualizeWhitespace.setValue(was); });
        displaySettings().visualizeWhitespace.setValue(false);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        const auto shown = [document] {
            return document->document()->defaultTextOption().flags()
                   .testFlag(QTextOption::ShowTabsAndSpaces);
        };
        QVERIFY2(!shown(), "whitespace was drawn without the setting asking for it");

        displaySettings().visualizeWhitespace.setValue(true);
        QTRY_VERIFY2(shown(), "turning the setting on did not reach the document");

        // What colours the drawn spaces is the highlighter, which puts a
        // C_VISUAL_WHITESPACE format on every whitespace run whenever it runs.
        // Without one they would be drawn in the text colour.
        QVERIFY2(document->syntaxHighlighter(),
                 "no highlighter, so nothing would colour the whitespace");

        displaySettings().visualizeWhitespace.setValue(false);
        QTRY_VERIFY2(!shown(), "turning the setting off did not reach the document");
    }


    // Every file in this repository opens with a licence header, so an editor
    // that does not fold it starts every file several lines further down than
    // the widget editor does.
    void testTheLicenceHeaderIsFoldedOnOpen()
    {
        Utils::TemporaryDirectory dir("quick-editor-licence");
        QVERIFY(dir.isValid());
        // A C++ file, so that the highlighter has comment markers to offer.
        const Utils::FilePath file = dir.filePath("licensed.cpp");
        QVERIFY(file.writeFileContents("/* Copyright (C) 2026 The Qt Company Ltd.\n"
                                       "   SPDX-License-Identifier: whatever\n"
                                       "*/\n"
                                       "\n"
                                       "int main() { return 0; }\n"));

        const bool was = displaySettings().autoFoldFirstComment();
        const QScopeGuard restore(
            [was] { displaySettings().autoFoldFirstComment.setValue(was); });
        displaySettings().autoFoldFirstComment.setValue(true);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        const QTextBlock first = document->document()->firstBlock();
        // The highlighter is what makes a comment foldable, and it has not run
        // when the editor comes back - which is exactly why the fold waits for
        // it too.
        QTRY_VERIFY2(TextBlockUserData::canFold(first),
                     "the header is not foldable, so folding it would say nothing");

        QTRY_VERIFY2(TextBlockUserData::isFolded(first),
                     "the licence header was left open");
        // Folded means the lines under it are not shown; the first line of the
        // comment still is.
        QVERIFY(first.isVisible());
        QVERIFY2(!first.next().isVisible(), "the header is marked folded but still drawn");
    }

    void testTheLicenceHeaderIsLeftAloneWhenTheSettingIsOff()
    {
        Utils::TemporaryDirectory dir("quick-editor-licence-off");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("licensed.cpp");
        QVERIFY(file.writeFileContents("/* Copyright (C) 2026 The Qt Company Ltd.\n"
                                       "   SPDX-License-Identifier: whatever\n"
                                       "*/\n"
                                       "\n"
                                       "int main() { return 0; }\n"));

        const bool was = displaySettings().autoFoldFirstComment();
        const QScopeGuard restore(
            [was] { displaySettings().autoFoldFirstComment.setValue(was); });
        displaySettings().autoFoldFirstComment.setValue(false);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        const QTextBlock first = document->document()->firstBlock();

        // Wait for the highlighter, which is what the fold waits for: if the
        // header were going to be folded it would have happened by then, so
        // the check below is about the setting and not about being early.
        QTRY_VERIFY2(TextBlockUserData::canFold(first),
                     "the highlighter never made the header foldable");
        QVERIFY2(!TextBlockUserData::isFolded(first),
                 "the header was folded with the setting turned off");
    }

    // The services a language's editor factory holds are reachable from any
    // editor, so an editor that is not built by one need not go without them.
    // JSON rather than C++ on purpose: the JSON factory keeps its indenter in
    // the factory, where this can find it, and CppEditor keeps its in a
    // TextDocument subclass its document creator builds - which this cannot
    // reach, because the document is made before the path is known.
    void testTheEditorTakesTheLanguagesIndenter()
    {
        Utils::TemporaryDirectory dir("quick-editor-indenter");
        QVERIFY(dir.isValid());
        const Utils::FilePath json = dir.filePath("object.json");
        QVERIFY(json.writeFileContents("{\n}\n"));
        const Utils::FilePath txt = dir.filePath("plain.txt");
        QVERIFY(txt.writeFileContents("alpha\n"));

        // The factory lookup is the whole mechanism, so it is worth asserting
        // on its own: a JSON file has to find a different factory from a plain
        // text one, or everything below is about one indenter twice.
        TextEditorFactory * const forJson = TextEditorFactory::preferredFactoryFor(json);
        TextEditorFactory * const forTxt = TextEditorFactory::preferredFactoryFor(txt);
        QVERIFY2(forJson, "no editor factory claims a JSON file");
        QVERIFY2(forTxt, "no editor factory claims a text file");
        QVERIFY2(forJson != forTxt, "a JSON file and a text file found the same factory");
        QVERIFY2(forJson->indenterCreator(),
                 "the JSON factory holds no indenter, so this tests nothing");

        // Before the editor is built, so that what is asserted below is the
        // completer the editor installed with the settings as they stand -
        // not one the test put there itself.
        const bool wasBrackets = globalCompletionSettings().autoInsertBrackets();
        const QScopeGuard restoreBrackets([wasBrackets] {
            globalCompletionSettings().autoInsertBrackets.setValue(wasBrackets);
        });
        globalCompletionSettings().autoInsertBrackets.setValue(true);

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(json, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY(document->indenter());

        // And the language's own AutoCompleter, in place of the base one: the
        // base knows how to take a bracket pair apart and not how to make one.
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QVERIFY(view->autoCompleter());

        // Asked what it would do rather than what it is: the base completer
        // answers nothing here, so a closing brace is the language's. Nothing
        // is installed here - the setting was turned on before the editor was
        // opened, so this is the completer the *editor* put in place.
        QTextCursor probe(document->document());
        probe.setPosition(document->document()->characterCount() - 1);
        QCOMPARE(view->autoCompleter()->autoComplete(probe, "{", false), QString("}"));

        // What the indenter is for: a brace opens a block, and the line after
        // it is a level deeper. The plain indenter copies the previous line
        // and would leave it at column zero.
        QTextDocument * const text = document->document();
        QTextCursor cursor(text);
        cursor.setPosition(text->findBlockByNumber(0).position() + 1);
        cursor.insertText("\n");
        document->autoIndent(cursor);
        const QString opened = cursor.block().text();
        QVERIFY2(opened.startsWith(" ") || opened.startsWith("\t"),
                 qPrintable(QString("the line after '{' was not indented: '%1'").arg(opened)));
    }

    // A language does not open in the Quick editor by finding it, but by its
    // own factory building it: the document, the indenter, the highlighter
    // and the completions stay the factory's, and only the view changes. That
    // is what makes a language editor - CppEditorDocument and all - showable
    // here at all, since the document is built before the path is known and
    // cannot be looked up afterwards.
    void testAFactoryCanBuildTheQuickEditorInstead()
    {
        const Utils::Id factoryId("QuickEditorSeamTest");
        // What a language adds beside its factory's own id - CppEditor adds
        // the C++ language id, and that is what says "this is a C++ editor"
        // to everything that asks.
        static const auto languageContext = [] { return Utils::Id("QuickEditorSeamLanguage"); };

        class SeamFactory final : public TextEditorFactory
        {
        public:
            explicit SeamFactory(Utils::Id id)
            {
                setId(id);
                setDisplayName("Quick Editor Seam Test");
                // No mime type: this claims no file, so it changes nothing
                // about what the rest of the run opens.
                setDocumentCreator([id] { return new TextDocument(id); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setIndenterCreator([](QTextDocument *doc) { return new TextIndenter(doc); });
                addEditorContext(languageContext());
            }
        };

        SeamFactory factory(factoryId);

        // The widget editor while nothing says otherwise, so that what is
        // asserted below is the switch rather than the factory.
        const std::unique_ptr<Core::IEditor> widgetEditor(factory.createEditor());
        QVERIFY2(widgetEditor.get(), "the factory built nothing");
        QVERIFY2(qobject_cast<BaseTextEditor *>(widgetEditor.get()),
                 "a factory nobody switched over stopped building the widget editor");
        QVERIFY2(widgetEditor->context().contains(languageContext()),
                 "the factory's language context does not reach the widget editor");
        QVERIFY2(!factory.usesQuickEditor(), "the switch is on before anyone set it");

        factory.setUsesQuickEditor(true);
        const std::unique_ptr<Core::IEditor> quickEditor(factory.createEditor());
        QVERIFY2(quickEditor.get(), "the switched factory built nothing");
        QVERIFY2(!qobject_cast<BaseTextEditor *>(quickEditor.get()),
                 "the factory built the widget editor anyway");

        // The document is the one the factory made and configured, not one
        // the view made for itself - which is the whole point.
        auto * const document = qobject_cast<TextDocument *>(quickEditor->document());
        QVERIFY2(document, "the Quick editor has no text document");
        QCOMPARE(document->id(), factoryId);
        QVERIFY2(document->indenter(), "the factory's indenter never reached the document");
        QVERIFY2(document->completionAssistProvider(),
                 "the factory's completions never reached the document");

        // And the view draws that same document rather than one of its own.
        TextViewport * const view = viewportForEditor(quickEditor.get());
        QVERIFY2(view, "the editor the factory built has no Quick viewport");
        QVERIFY(view->document());
        QCOMPARE(view->document()->textDocument(), document);

        // A language registers its commands against its factory's id, so they
        // reach this view only if that context came with it - and the Quick
        // editor's own two have to survive, or its per-editor actions die.
        QVERIFY2(quickEditor->context().contains(factoryId),
                 "the factory's context did not reach the Quick editor");
        QVERIFY2(quickEditor->context().contains(QUICK_TEXT_EDITOR_ID),
                 "the Quick editor lost the context its own commands use");
        QVERIFY2(quickEditor->context().contains(languageContext()),
                 "the factory's language context does not reach the Quick editor");
    }

    // A refactoring edits the document that is open, not the file on disk.
    // Which view shows it is the part that changed, and RefactoringFile looked
    // for a TextEditorWidget - so a file open in the Quick editor was
    // refactored as a stale copy read back from disk, throwing away whatever
    // had not been saved and then writing the result over it.
    void testARefactoringWorksOnTheOpenDocument()
    {
        Utils::TemporaryDirectory dir("quick-editor-refactoring");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        // The premise, asserted rather than assumed: a text file opens in the
        // Quick editor, so there is no widget for a refactoring to find.
        QVERIFY2(viewportForEditor(editor), "a text file no longer opens in the Quick editor");
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QTextCursor typing(document->document());
        typing.movePosition(QTextCursor::End);
        typing.insertText("beta\n");
        QVERIFY2(document->isModified(), "nothing was typed, so there is nothing to lose");

        const RefactoringFilePtr refactoring = PlainRefactoringFileFactory().file(file);
        QVERIFY2(refactoring->isValid(), "the refactoring found no file");
        QVERIFY(refactoring->document());
        QCOMPARE(refactoring->document()->toPlainText(), QString("alpha\nbeta\n"));

        // And the change lands in the open document, which is what the user
        // sees and can undo - not in the file behind their back.
        Utils::ChangeSet change;
        QVERIFY(change.insert(0, "gamma\n"));
        QVERIFY(refactoring->apply(change));
        QCOMPARE(document->document()->toPlainText(), QString("gamma\nalpha\nbeta\n"));
        QCOMPARE(file.fileContents().value_or(QByteArray()), QByteArray("alpha\n"));
    }

    // The other half of the same decision: a file nobody had edited is written
    // out again after a refactoring, which is what "Auto-save files after
    // refactoring" promises. That too asked whether there was a widget, so a
    // file open in the Quick editor was left dirty instead.
    void testARefactoringSavesAFileNobodyHadEdited()
    {
        Utils::TemporaryDirectory dir("quick-editor-refactoring-save");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        // Read rather than set: the setting lives in coreplugin's Internal
        // namespace, and it is on by default.
        if (!Core::EditorManager::autoSaveAfterRefactoring())
            QSKIP("auto-saving after a refactoring is turned off in these settings");

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(!document->isModified(), "the file was edited, so it would not be saved anyway");

        Utils::ChangeSet change;
        QVERIFY(change.insert(0, "gamma\n"));
        QVERIFY(PlainRefactoringFileFactory().file(file)->apply(change));
        QCOMPARE(document->document()->toPlainText(), QString("gamma\nalpha\n"));
        QCOMPARE(file.fileContents().value_or(QByteArray()), QByteArray("gamma\nalpha\n"));
    }

    // What a language does with an edit in this view before the view does it
    // itself: typing inside an in-place rename goes to every use of the name
    // at once, and so do the clipboard and Select All while it lasts.
    void testTheViewAsksTheLanguageBeforeItEdits()
    {
        enum What { Take, Wrap, Decline };

        class TestHandler final : public EditHandler
        {
        public:
            using EditHandler::EditHandler;

            bool handleKeyPress(QKeyEvent *event, const std::function<void()> &processNormally)
                override
            {
                Q_UNUSED(event)
                ++m_keys;
                if (what == Decline)
                    return false;
                if (what == Wrap)
                    processNormally();
                return true;
            }
            bool handlePaste() override { ++m_pastes; return what != Decline; }
            bool handleCut() override { ++m_cuts; return what != Decline; }
            bool handleSelectAll() override { ++m_selectAlls; return what != Decline; }
            bool handleRename() override { ++m_renames; return what != Decline; }

            What what = Take;
            int m_keys = 0;
            int m_pastes = 0;
            int m_cuts = 0;
            int m_selectAlls = 0;
            int m_renames = 0;
        };

        Utils::TemporaryDirectory dir("quick-editor-edit-handler");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QTextDocument * const text = document->document();

        // Parented to the editor, which is where the view looks: a language
        // has nothing to say about a file until it has been read, so the
        // handler is put there after the editor was built.
        auto * const handler = new TestHandler(editor);

        QTextCursor caret(text);
        caret.movePosition(QTextCursor::EndOfLine);
        view->setTextCursor(caret);

        // A handler that takes the key is the only one that acts on it.
        QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier, "X");
        QCoreApplication::sendEvent(view, &typed);
        QCOMPARE(handler->m_keys, 1);
        QCOMPARE(text->toPlainText(), QString("alpha\n"));

        // One that wraps it lets the view do the edit, in the middle of
        // whatever else it is doing - which is what an in-place rename needs.
        handler->what = Wrap;
        QCoreApplication::sendEvent(view, &typed);
        QCOMPARE(handler->m_keys, 2);
        QCOMPARE(text->toPlainText(), QString("alphaX\n"));

        // And one that declines leaves the view to it entirely.
        handler->what = Decline;
        QCoreApplication::sendEvent(view, &typed);
        QCOMPARE(handler->m_keys, 3);
        QCOMPARE(text->toPlainText(), QString("alphaXX\n"));

        // The clipboard is asked the same way. A paste during a rename
        // replaces the name in every place it is used, not only here.
        QGuiApplication::clipboard()->setText("pasted");
        handler->what = Take;
        view->paste();
        QCOMPARE(handler->m_pastes, 1);
        QCOMPARE(text->toPlainText(), QString("alphaXX\n"));
        handler->what = Decline;
        view->paste();
        QCOMPARE(handler->m_pastes, 2);
        QCOMPARE(text->toPlainText(), QString("alphaXXpasted\n"));

        // Cut, which is the same edit backwards.
        caret = view->textCursor();
        caret.movePosition(QTextCursor::StartOfLine);
        caret.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        view->setTextCursor(caret);
        handler->what = Take;
        view->cut();
        QCOMPARE(handler->m_cuts, 1);
        QCOMPARE(text->toPlainText(), QString("alphaXXpasted\n"));
        handler->what = Decline;
        view->cut();
        QCOMPARE(handler->m_cuts, 2);
        QCOMPARE(text->toPlainText(), QString("\n"));

        // Select All, which during a rename means the name rather than the
        // file.
        handler->what = Take;
        view->selectAll();
        QCOMPARE(handler->m_selectAlls, 1);
        QVERIFY2(!view->textCursor().hasSelection(),
                 "the view selected the file although the language took the command");
        handler->what = Decline;
        view->selectAll();
        QCOMPARE(handler->m_selectAlls, 2);
        QVERIFY(view->textCursor().hasSelection());

        // And renaming, where a handler that can do it here and now saves the
        // search that would otherwise be asked for.
        QSignalSpy searched(view->symbolRequests(), SIGNAL(requestRename(QTextCursor)));
        handler->what = Take;
        view->renameSymbolUnderCursor();
        QCOMPARE(handler->m_renames, 1);
        QCOMPARE(searched.count(), 0);
        handler->what = Decline;
        view->renameSymbolUnderCursor();
        QCOMPARE(handler->m_renames, 2);
        QCOMPARE(searched.count(), 1);
    }

    // Something the file offers at a place in it - a quick fix waiting, a
    // toolbar to open. These were markers the widget editor drew, held in its
    // overlay, and nothing else could see or act on them.
    void testTheViewActsOnARefactorMarker()
    {
        Utils::TemporaryDirectory dir("quick-editor-refactor-marker");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // At the end of the first line, which is where a producer puts one.
        QTextCursor at(document->document());
        at.movePosition(QTextCursor::EndOfLine);
        const int markerPosition = at.position();

        Core::IEditor *acted = nullptr;
        RefactorMarker marker;
        marker.cursor = at;
        marker.tooltip = "Show available quick fixes";
        marker.type = Utils::Id("QuickEditorMarkerTest");
        marker.callback = [&acted](Core::IEditor *in) { acted = in; };
        document->setRefactorMarkers(marker.type, {marker});

        // The form is what draws them, so it has to be able to ask.
        const QVariantList listed = view->refactorMarkers();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().toMap().value("position").toInt(), markerPosition);
        QCOMPARE(listed.first().toMap().value("toolTip").toString(),
                 QString("Show available quick fixes"));

        // And acting on it hands the producer the editor the reader is in,
        // rather than a widget it would have to be to have one.
        QVERIFY2(view->applyRefactorMarkerAt(markerPosition), "the marker was not acted on");
        QCOMPARE(acted, editor);

        // Somewhere there is no marker does nothing, so what is asserted above
        // is the marker rather than any click.
        acted = nullptr;
        QVERIFY2(!view->applyRefactorMarkerAt(document->document()->characterCount() - 1),
                 "a place with no marker was acted on anyway");
        QCOMPARE(acted, nullptr);

        // And emptying the kind takes it away again.
        document->setRefactorMarkers(marker.type, {});
        QVERIFY(view->refactorMarkers().isEmpty());
    }

    // And the form draws one. The viewport says where each marker is on
    // screen, so the item has to be there, be visible, and be where the
    // viewport put it - not merely exist.
    void testTheFormDrawsARefactorMarker()
    {
        Utils::TemporaryDirectory dir("quick-editor-marker-drawn");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("many.txt");
        QString text;
        for (int line = 0; line < 200; ++line)
            text += QString("line %1\n").arg(line);
        QVERIFY(file.writeFileContents(text.toUtf8()));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Nothing offered yet, so nothing drawn.
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QVERIFY2(!itemNamed(quick->rootObject(), "refactorMarker"),
                 "a marker was drawn before anything offered one");

        QTextCursor at(document->document());
        at.movePosition(QTextCursor::EndOfLine);
        const int markerPosition = at.position();
        RefactorMarker marker;
        marker.cursor = at;
        marker.tooltip = "Show available quick fixes";
        marker.type = Utils::Id("QuickEditorDrawnMarkerTest");
        marker.callback = [](Core::IEditor *) {};
        document->setRefactorMarkers(marker.type, {marker});

        // Looked up each time rather than held: the list is rebuilt whenever
        // anything moves, so the Repeater destroys its delegates and makes new
        // ones, and a pointer kept across that is dangling.
        const auto markerItem = [quick] {
            return itemNamed(quick->rootObject(), "refactorMarker");
        };
        QTRY_VERIFY2(markerItem(), "the form drew nothing for a marker the document offers");
        QVERIFY2(markerItem()->isVisible(), "the marker was drawn where it cannot be seen");

        // Where the viewport says it is, which is the whole reason the form
        // asks rather than working it out from a line number.
        const QRectF where = view->rectangleAt(markerPosition);
        QVERIFY2(!where.isEmpty(), "the viewport places the marker nowhere");
        QCOMPARE(markerItem()->y(), where.y());

        // And it follows the text: scrolling a screenful down takes the first
        // line off screen, and the marker with it.
        view->setScrollY(view->lineHeight() * 100);
        QTRY_VERIFY2(markerItem() && !markerItem()->isVisible(),
                     "the marker stayed on screen while its line scrolled away");

        view->setScrollY(0);
        QTRY_VERIFY2(markerItem() && markerItem()->isVisible(),
                     "the marker did not come back with its line");

        // Emptying the kind takes the item away again.
        document->setRefactorMarkers(marker.type, {});
        QTRY_VERIFY2(!itemNamed(quick->rootObject(), "refactorMarker"),
                     "the form kept a marker nobody offers any more");
    }

    // What a refactoring means by "the cursor": where the reader is looking,
    // not the start of the file. RefactoringFile asked the widget and fell
    // back to a fresh cursor otherwise, so every refactoring built on
    // isCursorOn() matched nothing in a view that is not one.
    void testARefactoringSeesWhereTheReaderIs()
    {
        Utils::TemporaryDirectory dir("quick-editor-refactoring-cursor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Into the third line, which is nowhere near where a fresh cursor is.
        editor->gotoLine(3, 3);
        const int caret = TextEditor::textCursorOf(editor).position();
        QVERIFY2(caret > 0, "the caret never moved");

        const RefactoringFilePtr refactoring = PlainRefactoringFileFactory().file(file);
        QVERIFY2(refactoring->isValid(), "the refactoring found no file");
        QCOMPARE(refactoring->cursor().position(), caret);
    }

    // And the other direction: everything that produces one today does it
    // through TextEditorWidget, so the widget has to tell the document or the
    // other view never learns of any marker that exists in practice.
    void testTheWidgetTellsTheDocumentAboutItsMarkers()
    {
        Utils::TemporaryDirectory dir("widget-editor-refactor-marker");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        QVERIFY2(widget, "the plain text editor is no longer a widget one");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        const Utils::Id kind("WidgetMarkerTest");
        QVERIFY(document->refactorMarkers(kind).isEmpty());

        QTextCursor at(document->document());
        at.movePosition(QTextCursor::EndOfLine);
        RefactorMarker marker;
        marker.cursor = at;
        marker.type = kind;
        marker.callback = [](Core::IEditor *) {};
        widget->setRefactorMarkers({marker}, kind);

        QCOMPARE(document->refactorMarkers(kind).size(), 1);
        QCOMPARE(document->refactorMarkerAt(at.position()).type, kind);

        // Clearing has to reach it too, or a marker outlives what put it there.
        widget->clearRefactorMarkers(kind);
        QVERIFY2(document->refactorMarkers(kind).isEmpty(),
                 "the document kept a marker the widget had cleared");
    }

    // What a language wants in the toolbar row. The document says so and the
    // form draws it; CppEditorDocument's is the button that asks how the file
    // should be preprocessed, and this is the same seam with a document made
    // for the purpose, because TextEditor has no language of its own.
    void testTheFormDrawsTheLanguagesToolBarActions()
    {
        class ToolBarDocument final : public TextDocument
        {
        public:
            ToolBarDocument()
                : TextDocument("QuickEditorToolBarTest")
                , m_action(new QAction("#", this))
            {
                m_action->setToolTip("Additional Preprocessor Directives...");
            }
            QList<QAction *> ownToolBarActions() const override { return {m_action}; }
            QAction * const m_action;
        };

        class ToolBarFactory final : public TextEditorFactory
        {
        public:
            ToolBarFactory()
            {
                setId("QuickEditorToolBarTest");
                setDisplayName("Quick Editor Tool Bar Test");
                setDocumentCreator([] { return new ToolBarDocument; });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        ToolBarFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = static_cast<ToolBarDocument *>(editor->document());
        QVERIFY(document);

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the toolbar drew nothing for the language's action");
        QCOMPARE(drawn->property("text").toString(), QString("#"));
        QVERIFY2(drawn->isVisible(), "the button was drawn where it cannot be seen");

        // And pressing it is what the action does, which is the whole point of
        // describing it rather than handing over a widget.
        QSignalSpy pressed(document->m_action, &QAction::triggered);
        QMetaObject::invokeMethod(drawn, "clicked");
        QTRY_VERIFY2(!pressed.isEmpty(), "the button did nothing");
    }

    // A document gains a language client's button after it is open, so an
    // action added once the toolbar row already exists has to appear in it.
    // TextDocument::toolBarActionsChanged() is what says so; it was declared
    // and never emitted until something needed to add one.
    void testTheFormFollowsAnActionAddedAfterTheToolBarWasBuilt()
    {
        class PlainFactory final : public TextEditorFactory
        {
        public:
            PlainFactory()
            {
                setId("QuickEditorLateToolBarTest");
                setDisplayName("Quick Editor Late Tool Bar Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorLateToolBarTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        PlainFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());

        // The document offers nothing of its own, so there is nothing to find
        // yet - and without this the test would pass on a toolbar that drew
        // the button from the start.
        QVERIFY2(!itemNamed(quick->rootObject(), "languageToolBarButton"),
                 "the toolbar already drew a language button with no action to draw");

        auto * const late = new QAction("later", document);
        document->addToolBarAction(late);
        QVERIFY2(document->toolBarActions().contains(late),
                 "the document does not list an action that was added to it");

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "an action added after the toolbar was built never appeared in it");
        QCOMPARE(drawn->property("text").toString(), QString("later"));

        // And taking it away again empties the row, which is the path the
        // client uses when the last server for a file goes away.
        document->removeToolBarAction(late);
        QTRY_VERIFY2(!itemNamed(quick->rootObject(), "languageToolBarButton"),
                     "the button stayed after its action was removed");
    }

    // The outline the toolbar shows: which function the caret is in, and the
    // tree behind it. The language keeps one and parents it to the editor;
    // this is a stand-in for it, because TextEditor has no language of its own.
    void testTheFormDrawsTheLanguagesOutline()
    {
        class TestOutline final : public ToolBarOutline
        {
        public:
            explicit TestOutline(QObject *parent)
                : ToolBarOutline(parent)
            {
                m_model.appendRow(new QStandardItem("alpha()"));
                m_model.appendRow(new QStandardItem("beta()"));
            }
            QAbstractItemModel *model() const override
            {
                return const_cast<QStandardItemModel *>(&m_model);
            }
            QModelIndex currentIndex() const override { return m_current; }
            QString currentText() const override
            {
                return m_current.isValid() ? m_current.data().toString() : QString();
            }
            void activate(const QModelIndex &index) override
            {
                m_activated = index;
                m_current = index;
                emit currentIndexChanged();
            }
            void showRow(int row)
            {
                m_current = m_model.index(row, 0);
                emit currentIndexChanged();
            }

            QStandardItemModel m_model;
            QModelIndex m_current;
            QModelIndex m_activated;
        };

        class OutlineFactory final : public TextEditorFactory
        {
        public:
            OutlineFactory()
            {
                setId("QuickEditorOutlineTest");
                setDisplayName("Quick Editor Outline Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorOutlineTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        OutlineFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");

        // Parented to the editor before the toolbar is asked for, which is
        // what the plugin does when it makes one for a file.
        auto * const outline = new TestOutline(editor.get());
        outline->showRow(0);

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "outlineButton")),
                     "the toolbar drew nothing for the language's outline");
        QCOMPARE(drawn->property("text").toString(), QString("alpha()"));
        QVERIFY2(drawn->isVisible(), "the outline was drawn where it cannot be seen");

        // It says where the caret is, so it has to follow it.
        outline->showRow(1);
        QTRY_COMPARE(drawn->property("text").toString(), QString("beta()"));
    }

    // A language client makes its outline after the file is open, so one
    // parented to the editor once the row already exists has to appear in it.
    // The row used to look for an outline only when it was built.
    void testTheFormFollowsAnOutlineAddedAfterTheToolBarWasBuilt()
    {
        class LateOutline final : public ToolBarOutline
        {
        public:
            explicit LateOutline(QObject *parent)
                : ToolBarOutline(parent)
            {
                m_model.appendRow(new QStandardItem("alpha()"));
            }
            QAbstractItemModel *model() const override
            {
                return const_cast<QStandardItemModel *>(&m_model);
            }
            QModelIndex currentIndex() const override { return m_current; }
            QString currentText() const override
            {
                return m_current.isValid() ? m_current.data().toString() : QString();
            }
            void activate(const QModelIndex &) override {}
            void showRow(int row)
            {
                m_current = m_model.index(row, 0);
                emit currentIndexChanged();
            }

        private:
            QStandardItemModel m_model;
            QModelIndex m_current;
        };

        class OutlineFactory final : public TextEditorFactory
        {
        public:
            OutlineFactory()
            {
                setId("QuickEditorLateOutlineTest");
                setDisplayName("Quick Editor Late Outline Test");
                setDocumentCreator([] {
                    return new TextDocument("QuickEditorLateOutlineTest");
                });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        OutlineFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");

        // The row first, with no outline to show.
        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        // The button is in the tree either way - it is its visibility that
        // says whether there is an outline - so this waits for the form to
        // load rather than for the button to exist.
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = itemNamed(quick->rootObject(), "outlineButton")));
        QVERIFY2(!button->isVisible(),
                 "the row showed an outline button with no outline to draw");

        // And now the language has one.
        auto * const outline = new LateOutline(editor.get());
        outline->showRow(0);

        QTRY_VERIFY2(button->isVisible(),
                     "an outline parented to the editor after the row was built "
                     "never appeared in it");
        QCOMPARE(button->property("text").toString(), QString("alpha()"));

        // And it goes when the outline does, which is what a client stopping
        // to serve the file looks like.
        delete outline;
        QTRY_VERIFY2(!button->isVisible(),
                     "the button stayed after the outline was deleted");
    }

    // What an external tool, a wizard or a custom command is told about the
    // file being looked at: %{CurrentDocument:Row} and its six siblings. Each
    // asked BaseTextEditor::currentTextEditor(), which a Quick editor is not,
    // so every one of them answered 0 or nothing at all - for plain text since
    // this editor became its default, and for C++ since the switch.
    void testTheCurrentDocumentVariablesAnswerForThisEditor()
    {
        Utils::TemporaryDirectory dir("quick-editor-document-variables");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma delta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QCOMPARE(Core::EditorManager::currentEditor(), editor);
        QTRY_VERIFY(view->visibleLineCount() > 0);

        // On "delta", with "gamma" selected before it.
        QTextDocument * const text = view->textDocument()->document();
        QTextCursor caret(text);
        caret.setPosition(text->findBlockByNumber(1).position());
        caret.setPosition(caret.position() + QString("gamma").size(),
                          QTextCursor::KeepAnchor);
        view->setTextCursor(caret);

        Utils::MacroExpander * const expander = Utils::globalMacroExpander();
        QCOMPARE(expander->expand(QString("%{CurrentDocument:Selection}")), QString("gamma"));
        // Both 1-based, which is what BaseTextEditor answers too - the
        // variable's own description says the column starts at 0 and has been
        // wrong about that in either view for as long as it has said it.
        QCOMPARE(expander->expand(QString("%{CurrentDocument:Row}")), QString("2"));
        QCOMPARE(expander->expand(QString("%{CurrentDocument:Column}")),
                 QString::number(QString("gamma").size() + 1));
        QCOMPARE(expander->expand(QString("%{CurrentDocument:WordUnderCursor}")),
                 QString("gamma"));

        // The font size is the document's setting, and has to be a real one
        // rather than the zero this used to answer.
        const int fontSize = view->textDocument()->fontSettings().fontSize();
        QVERIFY(fontSize > 0);
        QCOMPARE(expander->expand(QString("%{CurrentDocument:FontSize}")),
                 QString::number(fontSize));

        // How much is on screen. Only that it is a real count: how many lines
        // fit depends on the size the test happens to give the view.
        QCOMPARE(expander->expand(QString("%{CurrentDocument:RowCount}")),
                 QString::number(view->visibleLineCount()));
        QVERIFY2(expander->expand(QString("%{CurrentDocument:RowCount}")) != "0",
                 "the number of visible lines is still reported as none");
    }

    // Qt offers the focus widget a ShortcutOverride before firing a shortcut,
    // and accepting it is the only way a key bound to a command can still
    // reach the editor. The widget editor answers that offer; this one did
    // not, so ordinary typing was a shortcut's for the taking, Escape could
    // not be reserved for dismissing a suggestion, and a modal editing mode
    // had no way to ask for a key at all.
    void testTheViewIsOfferedAKeyBeforeTheShortcutSystem()
    {
        Utils::TemporaryDirectory dir("quick-editor-shortcut-override");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        auto * const host = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(host);

        // The offer as Qt makes it, to the widget that has the focus.
        const auto offer = [host](int key, Qt::KeyboardModifiers mods) {
            QKeyEvent event(QEvent::ShortcutOverride, key, mods);
            event.ignore();
            QCoreApplication::sendEvent(host, &event);
            return event.isAccepted();
        };

        // Ordinary typing is the editor's, whatever a plugin has bound to it.
        QVERIFY2(offer(Qt::Key_A, Qt::NoModifier), "a plain letter was left to the shortcut system");
        QVERIFY2(offer(Qt::Key_A, Qt::ShiftModifier), "a shifted letter was left to the shortcuts");
        // And a real shortcut is not.
        QVERIFY2(!offer(Qt::Key_S, Qt::ControlModifier), "the editor claimed Ctrl+S");

        // Escape only while there is something to dismiss with it.
        QVERIFY2(!offer(Qt::Key_Escape, Qt::NoModifier),
                 "the editor claimed Escape with nothing to dismiss");
        Utils::MultiTextCursor several = view->multiTextCursor();
        QTextCursor second(view->textDocument()->document());
        second.setPosition(2);
        several.addCursor(second);
        view->setMultiTextCursor(several);
        QVERIFY(view->multiTextCursor().hasMultipleCursors());
        QVERIFY2(offer(Qt::Key_Escape, Qt::NoModifier),
                 "Escape was left to the shortcut system while a second caret was up");

        // And a language can claim whatever it likes - which is what a modal
        // editing mode needs, and the reason this seam exists.
        class GreedyHandler final : public EditHandler
        {
        public:
            using EditHandler::EditHandler;
            bool handleKeyPress(QKeyEvent *, const std::function<void()> &) override
            {
                return false;
            }
            bool wantsKeyBeforeShortcuts(QKeyEvent *event) override
            {
                return event->key() == Qt::Key_W && event->modifiers() == Qt::ControlModifier;
            }
        };
        QVERIFY2(!offer(Qt::Key_W, Qt::ControlModifier),
                 "Ctrl+W was claimed before any language asked for it");
        new GreedyHandler(editor);
        QVERIFY2(offer(Qt::Key_W, Qt::ControlModifier),
                 "the language asked for Ctrl+W and did not get it");
    }

    // Typing over what is there instead of pushing it along. The Insert key
    // has always done this in the widget editor; the Quick editor had no such
    // mode at all, so a C++ file lost it at the switch and a text file long
    // before that. It is also one of the four things FakeVim needs from a view.
    void testInsertTogglesTypingOverTheTextThatIsThere()
    {
        Utils::TemporaryDirectory dir("quick-editor-overwrite");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("abcdef\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QTRY_VERIFY(view->visibleLineCount() > 0);
        QTextDocument * const text = view->textDocument()->document();

        const auto type = [view](int key, const QString &written,
                                 Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent event(QEvent::KeyPress, key, mods, written);
            QCoreApplication::sendEvent(view, &event);
        };
        const auto caretWidth = [view] {
            const QVariantList rects = view->caretRectangles();
            return rects.isEmpty() ? 0.0 : rects.first().toRectF().width();
        };

        view->setCursorPosition(0);
        QVERIFY2(!view->overwriteMode(), "the editor started out typing over");
        const qreal insertingCaret = caretWidth();
        QVERIFY(insertingCaret > 0);

        // Inserting: what is there is pushed along.
        type(Qt::Key_X, "X");
        QCOMPARE(text->toPlainText(), QString("Xabcdef\n"));

        // Shift+Insert is paste and must not be mistaken for the toggle.
        type(Qt::Key_Insert, {}, Qt::ShiftModifier);
        QVERIFY2(!view->overwriteMode(), "Shift+Insert was taken for the overwrite toggle");

        type(Qt::Key_Insert, {});
        QVERIFY2(view->overwriteMode(), "Insert did not turn typing-over on");
        QVERIFY2(caretWidth() > insertingCaret,
                 "the caret is still drawn between two characters, not over one");

        // Typing over: the character under the caret goes.
        type(Qt::Key_Y, "Y");
        QCOMPARE(text->toPlainText(), QString("XYbcdef\n"));

        // At the end of a line there is nothing to type over, so the newline
        // survives and the text grows.
        QTextCursor atEnd(text);
        atEnd.movePosition(QTextCursor::EndOfBlock);
        view->setTextCursor(atEnd);
        type(Qt::Key_Z, "Z");
        QCOMPARE(text->toPlainText(), QString("XYbcdefZ\n"));

        // And back again.
        type(Qt::Key_Insert, {});
        QVERIFY2(!view->overwriteMode(), "Insert did not turn typing-over off again");
        view->setCursorPosition(0);
        type(Qt::Key_W, "W");
        QCOMPARE(text->toPlainText(), QString("WXYbcdefZ\n"));
    }

    // The other three things a view has to answer for FakeVim to drive it:
    // a cursor at a point, how wide a tab is, and whether scrolling centres.
    void testTheViewAnswersWhatAnEditingModeAsksOfIt()
    {
        Utils::TemporaryDirectory dir("quick-editor-editing-mode-surface");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QString content;
        for (int i = 0; i < 200; ++i)
            content += QString("line %1 of the file\n").arg(i);
        QVERIFY(file.writeFileContents(content.toUtf8()));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QTRY_VERIFY(view->visibleLineCount() > 2);

        // A cursor at a point, which is what "gm" - go to the middle of the
        // screen line - is worked out from.
        view->setCursorPosition(0);
        const QVariantList carets = view->caretRectangles();
        QVERIFY(!carets.isEmpty());
        const QRectF caret = carets.first().toRectF();
        const QTextCursor atCaret = view->cursorForPosition(
            QPoint(int(caret.x() + caret.width() / 2), int(caret.y() + caret.height() / 2)));
        QVERIFY2(!atCaret.isNull(), "the view could not name a position under a point");
        QCOMPARE(atCaret.position(), 0);
        // And somewhere along the first line, which must not be the start.
        const QTextCursor further = view->cursorForPosition(
            QPoint(int(caret.x() + caret.width() * 6), int(caret.y() + caret.height() / 2)));
        QVERIFY2(further.position() > 0, "every point answered the same position");

        // How wide a tab is. Asked in pixels, because that is what the widget
        // editors take; kept as the columns this view lays out by.
        const qreal spaceWidth = QFontMetricsF(view->textDocument()->fontSettings().font())
                                     .horizontalAdvance(QLatin1Char(' '));
        QVERIFY(spaceWidth > 0);
        view->setTabStopDistance(spaceWidth * 3);
        QCOMPARE(view->textDocument()->tabSettings().m_tabSize, 3);
        view->setTabStopDistance(spaceWidth * 8);
        QCOMPARE(view->textDocument()->tabSettings().m_tabSize, 8);

        // And centring, which a mode turns on for as long as it is driving.
        QVERIFY2(!view->centerOnScroll(), "the view started out centring");
        view->setCursorPosition(0);
        QTextCursor farDown(view->textDocument()->document());
        farDown.setPosition(view->textDocument()->document()->findBlockByNumber(150).position());

        view->setCenterOnScroll(false);
        view->setTextCursor(farDown);
        view->ensureCursorVisible();
        const qreal scrolledToEdge = view->scrollY();

        view->setCursorPosition(0);
        view->ensureCursorVisible();
        view->setCenterOnScroll(true);
        QVERIFY(view->centerOnScroll());
        view->setTextCursor(farDown);
        view->ensureCursorVisible();
        const qreal scrolledToMiddle = view->scrollY();

        // Going down, stopping at the edge scrolls just far enough to bring the
        // caret into view at the bottom; centring keeps going until it is in
        // the middle, so it scrolls further.
        QVERIFY2(scrolledToMiddle > scrolledToEdge,
                 qPrintable(QString("centring scrolled to %1, the edge to %2")
                                .arg(scrolledToMiddle).arg(scrolledToEdge)));
    }

    // Numbering the gutter by distance from the caret - vim's
    // "relativenumber". The widget editor lays a column of numbers over its
    // own; a view that draws its own gutter is told to number it differently,
    // which is what the model answers.
    void testTheGutterCanNumberFromTheCaret()
    {
        Utils::TemporaryDirectory dir("quick-editor-relative-numbers");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QString content;
        for (int i = 1; i <= 40; ++i)
            content += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(content.toUtf8()));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QTRY_VERIFY(view->visibleLineCount() > 5);

        QAbstractItemModel * const rows = view->visibleRows();
        QVERIFY(rows);
        const int displayRole = rows->roleNames().key("displayNumber", -1);
        const int lineRole = rows->roleNames().key("lineNumber", -1);
        QVERIFY2(displayRole >= 0, "the row model does not say what to print");
        QVERIFY(lineRole >= 0);
        const auto numberOfRow = [rows, displayRole](int row) {
            return rows->data(rows->index(row, 0), displayRole).toInt();
        };
        const auto lineOfRow = [rows, lineRole](int row) {
            return rows->data(rows->index(row, 0), lineRole).toInt();
        };

        // Put the caret on the fifth line.
        QTextCursor caret(view->textDocument()->document());
        caret.setPosition(view->textDocument()->document()->findBlockByNumber(4).position());
        view->setTextCursor(caret);
        QCOMPARE(view->cursorLine(), 5);

        // Counting from the top, which is what it does until told otherwise.
        QVERIFY2(!view->relativeLineNumbers(), "the gutter started out counting from the caret");
        for (int row = 0; row < 5; ++row)
            QCOMPARE(numberOfRow(row), lineOfRow(row));

        view->setRelativeLineNumbers(true);
        // The caret's own line keeps its number; the others say how far away
        // they are, in both directions.
        for (int row = 0; row < 8; ++row) {
            const int line = lineOfRow(row);
            const int expected = line == 5 ? 5 : qAbs(line - 5);
            QCOMPARE(numberOfRow(row), expected);
        }

        // And the numbers follow the caret rather than being fixed once.
        caret.setPosition(view->textDocument()->document()->findBlockByNumber(6).position());
        view->setTextCursor(caret);
        QCOMPARE(view->cursorLine(), 7);
        for (int row = 0; row < 8; ++row) {
            const int line = lineOfRow(row);
            const int expected = line == 7 ? 7 : qAbs(line - 7);
            QCOMPARE(numberOfRow(row), expected);
        }

        view->setRelativeLineNumbers(false);
        for (int row = 0; row < 5; ++row)
            QCOMPARE(numberOfRow(row), lineOfRow(row));
    }

    // The numbers as the gutter actually draws them. The role the form reads
    // is looked up by name at run time, so a wrong one is not a compile error
    // and not something qmllint can see: the model would still answer and the
    // gutter would quietly print nothing.
    void testTheGutterDrawsTheNumbersItIsGiven()
    {
        Utils::TemporaryDirectory dir("quick-editor-gutter-drawing");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QString content;
        for (int i = 1; i <= 40; ++i)
            content += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(content.toUtf8()));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        QTRY_VERIFY(view->visibleLineCount() > 6);

        const auto drawnNumbers = [quick](int count) {
            QStringList texts;
            const QList<QQuickItem *> items
                = itemsNamed(quick->rootObject(), "gutterLineNumber");
            for (QQuickItem * const item : items) {
                if (texts.size() == count)
                    break;
                texts << item->property("text").toString();
            }
            return texts;
        };

        QTRY_VERIFY2(!drawnNumbers(1).isEmpty(),
                     "the gutter drew no line numbers at all");

        QTextCursor caret(view->textDocument()->document());
        caret.setPosition(view->textDocument()->document()->findBlockByNumber(4).position());
        view->setTextCursor(caret);
        QCOMPARE(view->cursorLine(), 5);

        // Counting from the top of the file.
        QTRY_COMPARE(drawnNumbers(7),
                     (QStringList{"1", "2", "3", "4", "5", "6", "7"}));

        // And from the caret, which keeps its own number.
        view->setRelativeLineNumbers(true);
        QTRY_COMPARE(drawnNumbers(7),
                     (QStringList{"4", "3", "2", "1", "5", "1", "2"}));

        // Moving the caret redraws them.
        caret.setPosition(view->textDocument()->document()->findBlockByNumber(2).position());
        view->setTextCursor(caret);
        QCOMPARE(view->cursorLine(), 3);
        QTRY_COMPARE(drawnNumbers(7),
                     (QStringList{"2", "1", "3", "1", "2", "3", "4"}));

        view->setRelativeLineNumbers(false);
        QTRY_COMPARE(drawnNumbers(7),
                     (QStringList{"1", "2", "3", "4", "5", "6", "7"}));
    }

    // Holding suggestions off. A modal editing mode does this outside insert
    // mode, and the view had no way to be told - so FakeVim skipped it and a
    // suggestion could appear over vim's command mode.
    void testSuggestionsCanBeHeldOff()
    {
        Utils::TemporaryDirectory dir("quick-editor-suggestion-blocker");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 0);
        QTextDocument * const text = view->textDocument()->document();

        const auto offerOne = [view, text] {
            TextSuggestion::Data data;
            data.position = Utils::Text::Position{1, 0};
            data.range = {Utils::Text::Position{1, 0}, Utils::Text::Position{1, 0}};
            data.text = "alphabet\nbeta\n";
            view->insertSuggestion(std::make_unique<TextSuggestion>(data, text));
        };

        view->setCursorPosition(0);
        QVERIFY2(!view->suggestionsBlocked(), "the view started out holding suggestions off");
        offerOne();
        QVERIFY2(view->currentSuggestion(), "a suggestion was not shown when nothing held it off");

        // Taking the token clears what is shown and refuses what comes next.
        {
            const TextViewport::SuggestionBlocker blocker = view->blockSuggestions();
            QVERIFY(view->suggestionsBlocked());
            QVERIFY2(!view->currentSuggestion(),
                     "taking the block left the suggestion that was already up");
            offerOne();
            QVERIFY2(!view->currentSuggestion(), "a suggestion was shown while held off");
        }

        // And letting go lifts it. Nothing refused meanwhile comes back - the
        // next one offered is what shows.
        QVERIFY2(!view->suggestionsBlocked(), "letting the token go left the block in force");
        QVERIFY(!view->currentSuggestion());
        offerOne();
        QVERIFY2(view->currentSuggestion(), "suggestions were still held off after the token went");
    }

    // Commands only some languages can answer. A factory says which of them
    // its language does with an OptionalActions mask, and the widget editor
    // greys out the rest - so a plain text file is not offered Rename Symbol.
    // The Quick editor registered all of them enabled whatever the mask said.
    void testTheOptionalCommandsFollowTheFactorysMask()
    {
        class MaskedFactory final : public TextEditorFactory
        {
        public:
            MaskedFactory(const Utils::Id &id, uint mask)
            {
                setId(id);
                setDisplayName(id.toString());
                setDocumentCreator([id] { return new TextDocument(id); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
                setOptionalActionMask(mask);
            }
        };

        // What a language answers is the point, so ask for two of the ten and
        // nothing else.
        MaskedFactory some("QuickEditorMaskTestSome",
                           OptionalActions::RenameSymbol | OptionalActions::UnCommentSelection);
        MaskedFactory none("QuickEditorMaskTestNone", OptionalActions::None);

        const std::unique_ptr<Core::IEditor> withSome(some.createEditor());
        const std::unique_ptr<Core::IEditor> withNone(none.createEditor());
        QVERIFY(withSome && withNone);

        const auto actionFor = [](Core::IEditor *editor, const Utils::Id &id) -> QAction * {
            Core::Command * const cmd = Core::ActionManager::command(id);
            if (!cmd)
                return nullptr;
            for (const Utils::Id &each : editor->context()) {
                if (QAction * const a = cmd->actionForContext(each))
                    return a;
            }
            return nullptr;
        };

        // Registered either way: the entry keeps its place and its shortcut,
        // and only says whether the language can answer it.
        QAction * const renameHere = actionFor(withSome.get(), Constants::RENAME_SYMBOL);
        QVERIFY2(renameHere, "Rename Symbol is not registered in the editor's context at all");
        QVERIFY2(renameHere->isEnabled(),
                 "the language asked for Rename Symbol and did not get it");

        QAction * const renameThere = actionFor(withNone.get(), Constants::RENAME_SYMBOL);
        QVERIFY2(renameThere, "Rename Symbol is not registered for the second editor");
        QVERIFY2(!renameThere->isEnabled(),
                 "a language that answers no symbol question is offered Rename Symbol");

        // One the first factory did not ask for either.
        QAction * const usages = actionFor(withSome.get(), Constants::FIND_USAGES);
        QVERIFY(usages);
        QVERIFY2(!usages->isEnabled(),
                 "Find Usages is offered to a language that did not ask for it");

        // And one that edits: being allowed to write is part of the answer,
        // which is what the widget editor asks in updateActions().
        TextViewport * const view = viewportForEditor(withSome.get());
        QVERIFY(view);
        QAction * const comment = actionFor(withSome.get(), Constants::UN_COMMENT_SELECTION);
        QVERIFY(comment);
        QVERIFY2(comment->isEnabled(), "the language asked to comment and did not get it");
        view->setReadOnly(true);
        QVERIFY2(!comment->isEnabled(), "a read-only file is still offered to be commented");
        view->setReadOnly(false);
        QVERIFY(comment->isEnabled());
    }

    // A choice the language offers in the toolbar - which of several ways the
    // file is parsed. Hidden where there is nothing to choose between, which
    // is what the widget editor does with it too.
    void testTheFormDrawsTheLanguagesChoice()
    {
        class TestChoice final : public ToolBarChoice
        {
        public:
            explicit TestChoice(QObject *parent)
                : ToolBarChoice(parent)
            {
                m_model.appendRow(new QStandardItem("Debug"));
                m_model.appendRow(new QStandardItem("Release"));
            }
            QAbstractItemModel *model() const override
            {
                return const_cast<QStandardItemModel *>(&m_model);
            }
            int currentIndex() const override { return m_current; }
            QString toolTip() const override { return "How this file is parsed"; }
            bool isAvailable() const override { return m_available; }
            bool isChosen() const override { return m_chosen >= 0; }
            void choose(int index) override
            {
                m_chosen = index;
                m_current = index;
                emit changed();
            }
            void clearChoice() override
            {
                m_chosen = -1;
                m_current = 0;
                ++m_clears;
                emit changed();
            }
            void offer(bool available) { m_available = available; emit changed(); }

            QStandardItemModel m_model;
            int m_current = 0;
            int m_chosen = -1;
            int m_clears = 0;
            bool m_available = false;
        };

        class ChoiceDocument final : public TextDocument
        {
        public:
            ChoiceDocument()
                : TextDocument("QuickEditorChoiceTest")
                , m_choice(new TestChoice(this))
            {}
            ToolBarChoice *toolBarChoice() const override { return m_choice; }
            TestChoice * const m_choice;
        };

        class ChoiceFactory final : public TextEditorFactory
        {
        public:
            ChoiceFactory()
            {
                setId("QuickEditorChoiceTest");
                setDisplayName("Quick Editor Choice Test");
                setDocumentCreator([] { return new ChoiceDocument; });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        ChoiceFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = static_cast<ChoiceDocument *>(editor->document());

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "parseContextCombo")),
                     "the toolbar drew nothing for the language's choice");
        // Nothing to choose between yet, so nothing to show.
        QVERIFY2(!drawn->isVisible(), "the combo was shown with nothing to choose between");

        document->m_choice->offer(true);
        QTRY_VERIFY2(drawn->isVisible(), "the combo stayed hidden with a choice to make");
        QCOMPARE(drawn->property("currentIndex").toInt(), 0);

        // What the language worked out is not a pick, so there is nothing to
        // undo yet and nothing offering to.
        QQuickItem *clear = nullptr;
        QTRY_VERIFY2((clear = itemNamed(quick->rootObject(), "clearParseContextButton")),
                     "the toolbar drew no way to undo a pick at all");
        QVERIFY2(!clear->isVisible(), "a way back was offered before anything had been picked");

        // And picking one is what the language is told.
        QMetaObject::invokeMethod(drawn, "activated", Q_ARG(int, 1));
        QTRY_COMPARE(document->m_choice->m_chosen, 1);
        QTRY_COMPARE(drawn->property("currentIndex").toInt(), 1);

        // Now there is: a pick is the reader's, and left alone the language
        // would go on choosing the same part, so only they can take it back.
        QTRY_VERIFY2(clear->isVisible(), "picking a parse context offered no way back");
        QMetaObject::invokeMethod(clear, "clicked");
        QTRY_COMPARE(document->m_choice->m_clears, 1);
        QTRY_VERIFY2(!clear->isVisible(), "the way back stayed after the pick was undone");
    }

    // The provider is on the document, and asking it is what turns that into
    // something a user sees. A plain text file gets the document-content
    // provider, which proposes the words already in the file.
    void testTheEditorOffersWhatTheLanguageProposes()
    {
        Utils::TemporaryDirectory dir("quick-editor-completion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("words.txt");
        QVERIFY(file.writeFileContents("alphabetical\nbeta\ngamma\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(document->completionAssistProvider(),
                 "no provider, so there would be nothing to ask");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 2);

        // Typed half of a word that is in the file: the rest of it is on offer.
        QTextDocument * const text = document->document();
        QTextCursor cursor(text);
        cursor.setPosition(text->characterCount() - 1);
        cursor.insertText("alph");
        view->setTextCursor(cursor);

        QCOMPARE(view->completionPrefix(), QString("alph"));

        // The answer comes back on a signal: the provider every text file gets
        // works in a thread, so asking and reading in one breath would read
        // before it had finished.
        QStringList candidates;
        QString prefix;
        connect(view, &TextViewport::completionsAvailable, view,
                [&candidates, &prefix](const QStringList &proposed, const QString &typed) {
                    candidates = proposed;
                    prefix = typed;
                });
        view->requestCompletions();
        QTRY_VERIFY2(!candidates.isEmpty(), "the language proposed nothing at all");
        QVERIFY2(candidates.contains("alphabetical"),
                 qPrintable("proposed: " + candidates.join(", ")));
        QCOMPARE(prefix, QString("alph"));

        // And choosing one replaces what was typed rather than adding to it.
        view->applyCompletion("alphabetical");
        QCOMPARE(text->lastBlock().text(), QString("alphabetical"));
        QVERIFY2(!text->lastBlock().text().contains("alphalph"),
                 "the completion was appended to the prefix instead of replacing it");
    }

    // C++ keeps nothing on its editor factory - CppEditorDocument installs the
    // indenter in its own constructor - so the factory lookup misses it. What
    // it uses instead is codeStyleFactory(), keyed by a language id that a
    // mime type maps to, and that is reachable from anywhere.
    void testTheEditorIndentsCppLikeTheCppEditorDoes()
    {
        Utils::TemporaryDirectory dir("quick-editor-cpp");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main()\n{\n}\n"));

        // The lookup, on its own: a C++ file maps to a language whose style
        // has an indenter, and the editor factory for it has none - which is
        // the whole reason this route exists.
        const Utils::Id language
            = TextEditor::languageId(Utils::mimeTypeForFile(file).name());
        QVERIFY2(language.isValid(), "a C++ file maps to no language id");
        ICodeStylePreferencesFactory * const style = codeStyleFactory(language);
        QVERIFY2(style, "C++ has no code style factory");
        TextEditorFactory * const editorFactory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(editorFactory);
        QVERIFY2(!editorFactory->indenterCreator(),
                 "the C++ editor factory grew an indenter, so this tests the wrong route");

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY(document->indenter());

        // Which indenter it is, before what it does with it: a failure here
        // and a failure below mean different things.
        QVERIFY2(document->indenter()->isElectricCharacter('}'),
                 "'}' is not electric, so this is not a C++ indenter");
        // On the language's code style, not the editor's generic one - which is
        // where the indenter reads its tab settings from.
        QVERIFY(document->codeStyle());
        QCOMPARE(document->codeStyle()->id(), QByteArray("CppGlobal"));

        // A brace opens a block, which the plain indenter would not know.
        QTextDocument * const text = document->document();
        QTextCursor cursor(text);
        cursor.setPosition(text->findBlockByNumber(1).position() + 1);
        cursor.insertText("\n");
        document->autoIndent(cursor);
        const QString opened = cursor.block().text();
        QVERIFY2(opened.startsWith(" ") || opened.startsWith("\t"),
                 qPrintable(QString("the line after '{' was not indented: '%1'").arg(opened)));


    }

    // A hover handler handed to an editor that is already open gets asked
    // what to show. This is how a language server's tooltips arrive: a
    // language registers its own handlers on its editor factory and the view
    // picks them up when it is built, but a Client is created with a project
    // and has to hand its handler to whatever is open at the time.
    //
    // Driven by Alt on its own, which is the "show help tooltips using the
    // keyboard" gesture, so no pointer and no hover timer are involved.
    void testAHoverHandlerAddedLaterIsAsked()
    {
        Utils::TemporaryDirectory dir("quick-editor-hover");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hovered.txt");
        // Four words, because each ask has to be about a different one: the
        // runner short-circuits to the handler that won last time when the
        // document revision and the *word start* both repeat, and then nobody
        // is asked anything.
        QVERIFY(file.writeFileContents("alpha beta gamma delta\n"));

        // A setting the gesture is gated on, put back afterwards: a test that
        // applies one has to un-apply it or the next run reads what this one
        // chose.
        const bool wasOn = globalBehaviorSettings().keyboardTooltips();
        globalBehaviorSettings().keyboardTooltips.setValue(true);
        const QScopeGuard restoreSetting(
            [wasOn] { globalBehaviorSettings().keyboardTooltips.setValue(wasOn); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY2(view, "a text file no longer opens in the Quick editor");
        QTRY_VERIFY(view->visibleLineCount() > 0);

        class CountingHoverHandler final : public BaseHoverHandler
        {
        public:
            int asked = 0;

        protected:
            void identifyMatch(HoverTarget *target, int pos, ReportPriority report) override
            {
                Q_UNUSED(target)
                Q_UNUSED(pos)
                ++asked;
                // Nothing to show - the assertion is that the question was
                // put, not what the answer was. Reporting is not optional:
                // the runner waits for it before asking anyone else.
                report(Priority_None);
            }
        };
        CountingHoverHandler mine;
        CountingHoverHandler other;

        // Alt on its own, about the word containing \a position.
        const auto askAbout = [view](int position) {
            view->setCursorPosition(position);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
            QCoreApplication::sendEvent(view, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &release);
        };

        addHoverHandlerIn(editor, &mine);
        addHoverHandlerIn(editor, &other);
        askAbout(1); // alpha
        QTRY_COMPARE(mine.asked, 1);
        QTRY_COMPARE(other.asked, 1);

        // Adding the same one twice must not ask it twice, the way the widget
        // editor's list does not grow either.
        addHoverHandlerIn(editor, &mine);
        askAbout(7); // beta
        QTRY_COMPARE(other.asked, 2);
        QCOMPARE(mine.asked, 2);

        // And taken back out it stops being asked. "Nothing happens" has no
        // event of its own to wait for, so the handler still registered is
        // the event: they are asked in list order and `mine` was added first,
        // so once `other` has answered a third time, `mine` would already
        // have been asked had it still been there.
        removeHoverHandlerIn(editor, &mine);
        askAbout(12); // gamma
        QTRY_COMPARE(other.asked, 3);
        QCOMPARE(mine.asked, 2);
    }
};

QObject *createQuickTextEditorTest()
{
    return new QuickTextEditorTest;
}

#endif // WITH_TESTS

TextViewport *viewportForEditor(Core::IEditor *editor)
{
    auto * const quick = qobject_cast<QuickTextEditor *>(editor);
    return quick ? quick->viewport() : nullptr;
}

} // namespace TextEditor::Internal

namespace TextEditor {

SymbolRequests *symbolRequestsForEditor(Core::IEditor *editor)
{
    TextViewport * const view = Internal::viewportForEditor(editor);
    return view ? view->symbolRequests() : nullptr;
}

} // namespace TextEditor

#ifdef WITH_TESTS
#include "quicktexteditor.moc"
#endif
