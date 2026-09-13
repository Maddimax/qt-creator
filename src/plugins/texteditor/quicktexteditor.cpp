// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quicktexteditor.h"

#include "codebuffer.h"

#include "colorpreviewhoverhandler.h"

#include "codesource.h"
#include "textdocument.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"
#include "textindenter.h"
#include "codestylepool.h"
#include "autocompleter.h"
#include "codeassist/assistproposalitem.h"
#include "codeassist/completionassistprovider.h"
#include "codeassist/assistinterface.h"
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
#include "formattexteditor.h"
#include "highlighterhelper.h"
#include "textdocumentlayout.h"
#include "refactoringchanges.h"
#include "suggestionhost.h"
#include "symbolrequests.h"
#include "textsuggestion.h"
#include "typehierarchy.h"
#include "linenumberfilter.h"
#include "textviewport.h"
#include "circularclipboard.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/locator/locatormanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/helpitem.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <QMainWindow>
#include <QScrollBar>
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
#include <utils/environment.h>
#include <utils/changeset.h>
#include <utils/textutils.h>
#include <utils/algorithm.h>
#include <utils/theme/theme.h>
#include <utils/mimeutils.h>
#include <utils/searchresultitem.h>
#include <utils/guitest.h>
#include <utils/infobar.h>
#include <utils/minimizableinfobars.h>
#include <utils/macroexpander.h>
#include <utils/temporarydirectory.h>
#include <utils/tooltip/tooltip.h>

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
#include <QApplication>
#include <QHBoxLayout>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
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
    explicit AdoptedSource(const TextDocumentPtr &document)
        : m_document(document)
    {}

    TextDocument *textDocument() const final { return m_document.get(); }

private:
    // A share rather than a pointer: the view asks the document on the way out
    // - clearing its scope highlight reads the revision - and a host whose own
    // reference goes first would leave it asking freed memory.
    const TextDocumentPtr m_document;
};

const char QUICK_FIND_HIGHLIGHTS[] = "TextEditor.QuickTextEditor.FindResults";
const char QUICK_FIND_SCOPE[] = "TextEditor.QuickTextEditor.FindScope";

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
        // "Selection only" searches part of the file, and the reader has to be
        // able to see which part. A widget editor paints it from the same
        // signal; here it is a range drawn over the text like any other.
        connect(this, &Core::BaseTextFindBase::findScopeChanged,
                this, &QuickTextFind::showScope);
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

    void showScope(const Utils::MultiTextCursor &scope)
    {
        TextDocument * const doc = m_viewport && m_viewport->document()
                                       ? m_viewport->document()->textDocument()
                                       : nullptr;
        if (!doc)
            return;
        const QTextCharFormat format = doc->fontSettings().toTextCharFormat(C_SEARCH_SCOPE);
        QList<TextViewport::Highlight> marks;
        for (const QTextCursor &part : scope) {
            if (part.hasSelection())
                marks.append({part.selectionStart(), part.selectionEnd(), format});
        }
        // Cleared by the same route: clearFindScope() says so with an empty one.
        m_viewport->setHighlights(QUICK_FIND_SCOPE, marks);
    }

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
                             uint optionalActions = OptionalActions::None,
                             Utils::Id contextMenuId = {},
                             const Core::Context &languageContext = {},
                             TextEditorFactory *factory = nullptr)
        : m_document(std::move(document))
        , m_source(std::make_unique<AdoptedSource>(m_document))
        , m_gate(new OptionalActionGate(this, [this] { return viewport(); }))
        , m_jumps(new JumpRecorder(this, [this] { return saveState(); }))
        , m_contextMenuId(contextMenuId)
        , m_languageContext(languageContext)
        , m_factory(factory)
    {
        m_gate->setOptionalActions(optionalActions);
        // duplicate() answers, so say so: the editor manager asks this rather
        // than trying, and an editor it thinks cannot be duplicated is moved
        // between split views instead of copied - which loses the tab the
        // first view was keeping for it.
        setDuplicateSupported(true);

        // What a right click offers. Taken from the same place the widget
        // editor takes it, and asked again each time the menu opens, because
        // the ActionManager's containers gain entries as plugins register
        // them.
        m_contextActions.setProvider([this] {
            const auto actionsOf = [](Utils::Id menuId) {
                Core::ActionContainer * const container
                    = menuId.isValid() ? Core::ActionManager::actionContainer(menuId) : nullptr;
                return container && container->menu() ? container->menu()->actions()
                                                      : QList<QAction *>();
            };
            // The language's own entries first, the way the widget editor puts
            // them before what every text editor offers.
            QList<QAction *> actions = actionsOf(m_contextMenuId);
            // Then what the document offers for this click. A container holds
            // what a language registered once; these may exist only while the
            // menu is open, which is what the quick fixes for a cursor are.
            // Not asked before there is a place to ask about: the entries a
            // document offers are the ones for a particular cursor, and a null
            // one is not a place.
            TextViewport * const view = viewport();
            if (TextDocument * const doc = view ? m_document.get() : nullptr) {
                const QTextCursor cursor = view->textCursor();
                if (!cursor.isNull())
                    actions += doc->contextMenuActions(cursor);
            }
            const QList<QAction *> standard = actionsOf(Constants::M_STANDARDCONTEXTMENU);
            for (QAction * const action : standard) {
                if (!actions.contains(action))
                    actions.append(action);
            }
            return actions;
        });

        QtcQuick::QuickWidget * const widget
            = Internal::createQuickTextView(m_source.get(), &m_contextActions);
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
            // And the other half: a selection can change without the caret
            // moving, so following the caret alone misses it.
            connect(view, &TextViewport::selectionChanged,
                    this, &Core::IEditor::selectionChanged);
            connect(view, &TextViewport::cursorPositionChanged,
                    m_jumps, &JumpRecorder::caretMoved);
        }

        // Work the document put off until a view is looking. Done now if this
        // one is, and remembered for its next show if it is not - which is the
        // usual case, because a file is parsed before its editor is shown.
        connect(m_document.data(), &TextDocument::pendingUpdatesRequested, this, [this] {
            QWidget * const host = Core::IEditor::widget();
            if (host && host->isVisible())
                m_document->triggerPendingUpdates();
            else
                m_updatesWaitingForAShow = true;
        });

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
        const auto pushDisplaySettings = [widget, self = this] {
            QQuickItem * const form = widget->quickWidget()->rootObject();
            if (!form)
                return;
            form->setProperty("wrapLines", displaySettings().textWrapping());
            form->setProperty("showLineNumbers", displaySettings().displayLineNumbers());
            form->setProperty("showFoldMarkers", self->foldMarkersWanted());
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
        // A file reread from disk is the same file, so the reader should be
        // left looking at the same place in it - a rebase or a formatter run
        // should not throw them back to the top. Saved and put back the way
        // the widget editor does it in documentAboutToBeReloaded().
        connect(m_document.get(), &Core::IDocument::aboutToReload, this, [this] {
            m_stateBeforeReload = saveState();
        });
        connect(m_document.get(), &Core::IDocument::reloadFinished, this, [this](bool success) {
            if (success)
                restoreState(m_stateBeforeReload);
            m_stateBeforeReload.clear();
        });

        // A file whose bytes did not decode is not safe to edit: what is in
        // the buffer is not what is on disk, and saving would write the
        // buffer over it. Say so, and refuse the edit until the reader picks
        // an encoding - which is what the widget editor does in
        // updateCannotDecodeInfo() and updateReadOnlyState().
        connect(m_document.get(), &TextDocument::openFinishedSuccessfully,
                this, &QuickTextEditor::updateCannotDecodeInfo);
        connect(m_document.get(), &TextDocument::conflictedChanged,
                this, &QuickTextEditor::updateCannotDecodeInfo);

        connect(m_document.get(), &Core::IDocument::filePathChanged, this, [this] {
            configureHighlighter();
            configureLanguageServices();
        });

        // Where the reader last changed something, which is what Go to Last
        // Edit goes to. TextEditorWidget sets this from its own cursor handler,
        // off a flag its code calls too heavy; an edit on the document says the
        // same thing without one.
        //
        connect(m_document->document(), &QTextDocument::contentsChanged, this, [this] {
            if (Core::EditorManager::currentEditor() == this)
                Core::EditorManager::setLastEditLocation(this);
        });
        configureHighlighter();
        configureLanguageServices();

        // Three contexts. C_TEXTEDITOR is what "a text editor is current"
        // means to the rest of Creator, and other plugins register against it
        // rather than against any one editor - the macro recorder's four
        // commands are registered there and were dead here for want of it.
        // BaseTextEditor adds it in its constructor for the same reason.
        //
        // Then the shared one, which is what a command meant for "any editor
        // of this kind" uses, and one of this editor's own. A per-editor
        // action has to be registered against the last, or the next editor of
        // the same kind collides with it - the widget editor generates an id
        // per instance for exactly this reason.
        // What "a text editor is current" means to the rest of Creator - the
        // macro recorder registers its commands against it - and the shared
        // one a command meant for "any editor of this kind" uses.
        //
        // C_TEXTEDITOR also carries handler-less entries for commands this
        // editor answers itself: the menu text and shortcut that
        // texteditorplugin.cpp registers. CommandPrivate::setCurrentContext()
        // takes the first context that has an action at all, so this editor's
        // own context has to be looked at before C_TEXTEDITOR or every
        // handler is shadowed by an entry that does nothing.
        //
        // That is what the widget-attached context below is for: contexts on
        // the focus widget come before the editor's own. The widget editor has
        // had this shape all along - see TextEditorWidgetPrivate's ctor.
        setContext(Core::Context(QUICK_TEXT_EDITOR_ID, Constants::C_TEXTEDITOR));
        // Added rather than set: the two above are what every Quick editor
        // needs, and the language's are on top of them. Done here rather than
        // by whoever built this, because duplicate() builds one too.
        if (!m_languageContext.isEmpty()) {
            Core::Context contexts = context();
            contexts.add(m_languageContext);
            setContext(contexts);
        }

        auto * const perEditor = new Core::IContext(this);
        perEditor->setWidget(widget);
        perEditor->setContext(Core::Context(m_editorContext));
        Core::ICore::addContextObject(perEditor);
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
                OptionalActions::AutoIndentSelection);
        command(Constants::AUTO_FORMAT_SELECTION, &TextViewport::autoFormat,
                OptionalActions::AutoFormatSelection);

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

        // Find comes with the view; createQuickTextView() aggregates it.
        if (TextViewport * const view = viewport()) {
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
        if (event->type() == QEvent::Show && m_updatesWaitingForAShow) {
            m_updatesWaitingForAShow = false;
            m_document->triggerPendingUpdates();
        }

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

    void gate(QAction *action, uint needs) { m_gate->gate(action, needs); }

    // What the language turned out to be able to do, on top of what its
    // factory said up front - a language server only knows once it has
    // answered its initialize.
    void addOptionalActions(uint optionalActions) { m_gate->addOptionalActions(optionalActions); }

    void updateOptionalActions() { m_gate->update(); }

    // The editor's own part of the toolbar row. Built on demand and once: the
    // editor manager asks whenever this editor becomes current, and a new one
    // each time would drop whatever the previous one was showing.
    QWidget *toolBar() final
    {
        // The language says it has no row of its own, because its text view
        // sits inside another editor's UI.
        if (m_factory && !m_factory->toolBarVisible())
            return nullptr;
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

        m_toolBar = Internal::createQuickTextToolBar(view, &m_toolBarActions, outline,
                                                     m_document->toolBarChoice());
        return m_toolBar;
    }

    // A split view is a duplicate, so it has to be built the way this one
    // was: with the language's mask, its right-click menu and its contexts.
    // A bare one is a half whose language commands are greyed out and whose
    // menu has nothing of the language in it - which the widget editor avoids
    // by building its duplicate through the factory a second time.
    Core::IEditor *duplicate() final
    {
        auto * const copy = new QuickTextEditor(m_document, m_gate->optionalActions(),
                                                m_contextMenuId, m_languageContext, m_factory);
        // And what the language puts on the editor rather than in its text.
        // The factory ran this over the first half; nothing but this runs it
        // over the second.
        if (m_factory)
            m_factory->decorateEditor(copy);
        return copy;
    }

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
    void selectTo(int line, int column) final
    {
        TextViewport * const view = viewport();
        QTextDocument * const text = m_document ? m_document->document() : nullptr;
        if (!view || !text)
            return;
        const int position = Utils::Text::positionInText(text, line, column);
        if (position < 0)
            return;
        QTextCursor cursor = view->textCursor();
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        view->setTextCursor(cursor);
    }

    void gotoLine(int line, int column, bool centerLine) final
    {
        if (TextViewport * const view = viewport()) {
            view->gotoLine(line, column, centerLine);
            rememberThisAsSomewhereJumpedTo();
        }
    }

    void rememberThisAsSomewhereJumpedTo() { m_jumps->jumped(); }

    // Where the reader was, so that closing and reopening - or stepping back
    // through the navigation history - returns them to it. A private format:
    // only this editor is ever handed it back.
    QByteArray saveState() const final
    {
        TextViewport * const view = viewport();
        if (!view)
            return {};

        // Which blocks are folded, as well as where the caret and the view
        // were. What an editor remembers is what comes back after a reload,
        // after a session is restored, and in the second half of a split -
        // and folds are the part of that a reader has arranged by hand.
        QSet<int> folded;
        if (QTextDocument * const text = m_document->document()) {
            for (QTextBlock block = text->firstBlock(); block.isValid();
                 block = block.next()) {
                if (TextBlockUserData::isFolded(block))
                    folded.insert(block.blockNumber());
            }
        }

        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << kStateVersion << view->cursorPosition() << view->scrollY() << folded
               << view->scrollX();
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
            const auto fold = [doc] { doc->foldOnFirstOpen(); };
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
        QSet<int> folded;
        qreal scrollX = 0;
        stream >> position >> scrollY >> folded >> scrollX;

        // The folds first: putting them back changes where every line below
        // them is, so a caret restored before them would be left pointing at
        // the wrong one. They can only be put back once the highlighter has
        // said which lines fold at all, which for a language that has one is
        // after this returns.
        if (!folded.isEmpty()) {
            TextDocument * const doc = m_document.get();
            const auto unfoldWhatWasFolded = [doc, folded] {
                QTextDocument * const text = doc->document();
                auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
                bool changed = false;
                for (const int number : folded) {
                    const QTextBlock block = text->findBlockByNumber(number);
                    if (block.isValid() && TextBlockUserData::canFold(block)
                        && !TextBlockUserData::isFolded(block)) {
                        TextBlockUserData::doFoldOrUnfold(block, /*unfold=*/false);
                        changed = true;
                    }
                }
                if (changed && layout)
                    layout->requestUpdate();
            };
            if (!doc->singleShotAfterHighlightingDone(unfoldWhatWasFolded))
                unfoldWhatWasFolded();
        }

        view->setCursorPosition(position);
        // Both offsets, because neither follows from the caret: with wrapping
        // off a long line runs off the right, and a caret at the left margin
        // does not mean that is where the reader was looking.
        //
        // The order is not load-bearing, whatever the comment here used to
        // say: setCursorPosition() moves the caret without scrolling to it -
        // it is setTextCursor() that calls ensureCursorVisible() - so putting
        // the scroll back first would answer the same. Measured, after a
        // control that would not bite.
        view->setScrollY(scrollY);
        view->setScrollX(scrollX);

        // Arriving here is a jump like any other, and Go Back is how the
        // reader usually arrives: without this, going back and then moving
        // around leaves Back with nothing to return to, and it walks on
        // through the older entries instead. The widget editor ends
        // restoreState() the same way.
        rememberThisAsSomewhereJumpedTo();
    }

private:
    static constexpr int kStateVersion = 3;

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

    // The fold column costs width, so it is shown only where the language has
    // something to fold - the rule the widget editor applies in
    // updateCodeFoldingVisible(). A plain text file has no folding and the
    // widget editor shows no column for it.
    bool foldMarkersWanted() const
    {
        return m_codeFoldingSupported && displaySettings().displayFoldingMarkers();
    }

    QQuickItem *quickForm() const
    {
        auto * const quick = qobject_cast<QtcQuick::QuickWidget *>(widget());
        return quick ? quick->quickWidget()->rootObject() : nullptr;
    }

    void updateCannotDecodeInfo()
    {
        TextViewport * const view = viewport();
        if (!view)
            return;
        view->setReadOnly(m_document->isConflicted() || m_document->hasDecodingError());

        Utils::InfoBar * const infoBar = m_document->infoBar();
        const Utils::Id selectEncoding(Constants::SELECT_ENCODING);
        if (!m_document->hasDecodingError()) {
            infoBar->removeInfo(selectEncoding);
            return;
        }
        if (!infoBar->canInfoBeAdded(selectEncoding))
            return;
        Utils::InfoBarEntry info(
            selectEncoding,
            Tr::tr("<b>Error:</b> Could not decode \"%1\" with \"%2\"-encoding. "
                   "Editing not possible.")
                .arg(m_document->displayName(), m_document->encoding().displayName()));
        info.addCustomButton(Tr::tr("Select Encoding"),
                             [view] { view->selectEncoding(); });
        infoBar->addInfo(info);
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
        // Only where nothing has one yet. TextEditorFactory's editor creator
        // already did this for a document it built, and a language may have
        // chosen its own since - CppEditorDocument installs the C++ one when
        // it learns its mime type, and setting a provider here clears that.
        if (!m_document->completionAssistProvider()) {
            static DocumentContentCompletionProvider wordsInTheDocument;
            CompletionAssistProvider * const provider = factory->completionAssistProvider();
            m_document->setCompletionAssistProvider(provider ? provider : &wordsInTheDocument);
        }

        if (TextViewport * const view = viewport()) {
            if (const Utils::CommentDefinition comment = factory->commentDefinition();
                comment.isValid()) {
                view->setCommentDefinition(comment);
            }
        }

        m_codeFoldingSupported = factory->codeFoldingSupported();
        if (QQuickItem * const form = quickForm())
            form->setProperty("showFoldMarkers", foldMarkersWanted());

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
    TextViewport *viewport() const { return Internal::viewportIn(widget()); }

public:
    // IContext keeps the widget in a QPointer and does not own it, and the
    // editor manager only takes it out of the layout it put it in. So this is
    // what deletes it, as BaseTextEditor does for the widget editor - without
    // it every editor ever opened leaves its view, its scene and everything
    // they are connected to behind. The tool bar is a second widget, kept the
    // same way and just as unowned.
    ~QuickTextEditor() override
    {
        // The tool bar first: its scene is bound to the view inside the other
        // widget, so the other order leaves it re-evaluating its bindings
        // against an object that is already gone.
        // The tool bar first: its scene is bound to the view inside the other
        // widget, so the other order leaves it re-evaluating its bindings
        // against an object that is already gone.
        delete m_toolBar;
        delete widget();
    }

private:





    // Shared because a duplicated editor would show the same document; the
    // editor manager is what decides that, not this. The same handle the
    // widget editor uses, so that whoever wants the document need not know
    // which kind of view is showing it.
    TextDocumentPtr m_document;

    QtcQuick::ActionModel m_contextActions;
    const Utils::Id m_contextMenuId;
    const Core::Context m_languageContext;
    // The factory outlives every editor it builds: it is a plugin's, made
    // once at startup. Null for an editor nothing built - the plain Qt Quick
    // one, and the views the tests make by hand.
    TextEditorFactory * const m_factory = nullptr;
    QtcQuick::ActionModel m_toolBarActions;
    bool m_outlineUpdateScheduled = false;
    // Owned by the toolbar the editor manager puts it in, so a QPointer.
    QPointer<QWidget> m_toolBar;
    QPointer<QAction> m_wrapAction;
    // Enabled only while there is a suggestion to take.
    QList<QPointer<QAction>> m_suggestionActions;
    OptionalActionGate * const m_gate;
    JumpRecorder * const m_jumps;
    // See pendingUpdatesRequested(): asked for while nobody was looking.
    bool m_updatesWaitingForAShow = false;
    QPointer<QAction> m_whitespaceAction;
    // This editor alone, so that a per-editor action does not collide with
    // the same action on the next one.
    bool m_codeFoldingSupported = false;
    QByteArray m_stateBeforeReload;
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

JumpRecorder::JumpRecorder(Core::IEditor *editor, const std::function<QByteArray()> &state)
    : QObject(editor)
    , m_editor(editor)
    , m_state(state)
{}

void JumpRecorder::jumped()
{
    m_stateOfAJumpNotYetLeft = m_state ? m_state() : QByteArray();
    m_jumpedHereAndStillOnIt = true;
}

void JumpRecorder::caretMoved()
{
    if (!m_jumpedHereAndStillOnIt)
        return;
    m_jumpedHereAndStillOnIt = false;
    // The manager records "the current position", so a view the reader is not
    // in would push somewhere they never were. Same guard as the one on a jump
    // inside one file.
    if (Core::EditorManager::currentEditor() == m_editor)
        Core::EditorManager::addCurrentPositionToNavigationHistory(m_stateOfAJumpNotYetLeft);
}

OptionalActionGate::OptionalActionGate(QObject *parent,
                                       const std::function<TextViewport *()> &viewport)
    : QObject(parent)
    , m_viewport(viewport)
{}

void OptionalActionGate::gate(QAction *action, uint needs)
{
    if (needs != OptionalActions::None)
        m_gated.append({action, needs});
}

void OptionalActionGate::setOptionalActions(uint optionalActions)
{
    m_optionalActions = optionalActions;
    update();
}

void OptionalActionGate::addOptionalActions(uint optionalActions)
{
    m_optionalActions |= optionalActions;
    update();
}

void OptionalActionGate::update()
{
    TextViewport * const view = m_viewport ? m_viewport() : nullptr;
    const bool writable = view && view->canEdit();
    for (const auto &[action, needs] : std::as_const(m_gated)) {
        if (!action)
            continue;
        const bool edits = needs & (OptionalActions::Format
                                    | OptionalActions::UnCommentSelection);
        action->setEnabled((m_optionalActions & needs) && (!edits || writable));
    }
}

void addOptionalActionsIn(Core::IEditor *editor, uint optionalActions)
{
    // Whoever owns one, rather than only the Qt Quick editor: an editor made
    // of more than one pane registers its own commands and gates them the
    // same way.
    if (!editor)
        return;
    if (auto * const gate = editor->findChild<OptionalActionGate *>())
        gate->addOptionalActions(optionalActions);
}

void setOptionalActionsIn(Core::IEditor *editor, uint optionalActions)
{
    if (!editor)
        return;
    if (auto * const gate = editor->findChild<OptionalActionGate *>())
        gate->setOptionalActions(optionalActions);
}

uint optionalActionsIn(Core::IEditor *editor)
{
    if (!editor)
        return OptionalActions::None;
    if (auto * const gate = editor->findChild<OptionalActionGate *>())
        return gate->optionalActions();
    return OptionalActions::None;
}

// Item coordinates from the viewport, turned global through the item itself -
// the QQuickWidget's position inside its window is the item's business to know.
static QRect globallyFrom(TextViewport *view, const QRectF &rect)
{
    if (!view || rect.isEmpty())
        return {};
    return QRect(view->mapToGlobal(rect.topLeft()).toPoint(), rect.size().toSize());
}

QRect globalRectForPositionIn(Core::IEditor *editor, int position)
{
    TextViewport * const view = viewportForEditor(editor);
    return view ? globallyFrom(view, view->rectangleAt(position)) : QRect();
}

QRect globalRectForBlocksIn(Core::IEditor *editor, int firstBlock, int lastBlock)
{
    TextViewport * const view = viewportForEditor(editor);
    return view ? globallyFrom(view, view->rectangleForBlocks(firstBlock, lastBlock)) : QRect();
}

QtcQuick::QuickWidget *createQuickTextView(CodeSource *source,
                                           QtcQuick::ActionModel *contextActions)
{
    auto * const widget = new QtcQuick::QuickWidget;
    // Set before the source: the form's root property is required, and a
    // required property has to be there when the component is created.
    widget->quickWidget()->setInitialProperties(
        {{"source", QVariant::fromValue(source)},
         {"contextActions", QVariant::fromValue(contextActions)},
         {"wrapLines", displaySettings().textWrapping()},
         {"showLineNumbers", displaySettings().displayLineNumbers()},
         // Corrected once the file - and so the language - is known, by
         // whoever knows it: the editor does it in configureLanguageServices().
         {"showFoldMarkers", false},
         {"highlightCurrentLine", displaySettings().highlightCurrentLine()},
         {"showAnnotations", displaySettings().displayAnnotations()}});
    widget->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/MainEditor.qml"));

    // Ctrl+F reaches a view by asking its widget for an IFindSupport, so this
    // belongs to the view rather than to whoever hosts it - an editor made of
    // two panes wants Ctrl+F in the text one without arranging it itself.
    if (TextViewport * const view = viewportIn(widget))
        Utils::Aggregation::aggregate({widget, new QuickTextFind(view, widget)});
    return widget;
}

QtcQuick::QuickWidget *createQuickTextToolBar(TextViewport *view,
                                              QtcQuick::ActionModel *languageActions,
                                              ToolBarOutline *outline,
                                              ToolBarChoice *choice)
{
    auto * const bar = new QtcQuick::QuickWidget;
    bar->quickWidget()->setInitialProperties(
        {{"viewport", QVariant::fromValue(view)},
         {"languageActions", QVariant::fromValue(languageActions)},
         {"outline", QVariant::fromValue(outline)},
         {"choice", QVariant::fromValue(choice)}});
    bar->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/EditorToolBar.qml"));
    return bar;
}

QtcQuick::QuickWidget *createQuickTextViewOver(const TextDocumentPtr &document,
                                               QtcQuick::ActionModel *contextActions)
{
    QTC_ASSERT(document, return nullptr);
    // Parented to the view: the source is an implementation detail of showing
    // this document, and nobody outside should have to keep it alive.
    auto * const source = new AdoptedSource(document);
    QtcQuick::QuickWidget * const widget = createQuickTextView(source, contextActions);
    source->setParent(widget);
    return widget;
}

TextViewport *viewportIn(QWidget *host)
{
    auto * const quick = qobject_cast<QtcQuick::QuickWidget *>(host);
    QQuickItem * const root = quick ? quick->quickWidget()->rootObject() : nullptr;
    return root ? root->findChild<TextViewport *>() : nullptr;
}

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
                                     uint optionalActions,
                                     Utils::Id contextMenuId,
                                     TextEditorFactory *factory)
{
    return new QuickTextEditor(document, optionalActions, contextMenuId, context, factory);
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

// A layout that asks for another layout is a warning and nothing else, so a
// test has to listen for it. Qt says it twice: once the first time an item
// calls polish() from inside its own updatePolish(), and again once it has
// happened often enough to look like a loop. The first is the one worth
// catching - it needs a single pass rather than a thousand.

// A Repeater's delegates are visual children of the item and QObject children
// of somewhere else, so findChild() never sees one. See
// textviewport_test.cpp, which walks the tree the same way.
// Every item drawn under \a root with that name, top to bottom. itemNamed()
// answers the first one, which is no use for a column of them.
// How often \a action is in \a toolBar's row: an action inserted directly is
// an entry of its own, and one wrapped in a QToolButton is that button's
// default action.
static int timesDrawnIn(QToolBar *toolBar, QAction *action)
{
    int drawn = 0;
    for (QAction * const entry : toolBar->actions()) {
        auto * const button = qobject_cast<QToolButton *>(toolBar->widgetForAction(entry));
        if (entry == action || (button && button->defaultAction() == action))
            ++drawn;
    }
    return drawn;
}

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

// Every context this editor's commands can be registered against, in the order
// CommandPrivate::setCurrentContext() would look at them: those attached to the
// view's widget first, then the editor's own. A view that is not a widget keeps
// its per-editor commands on the first and C_TEXTEDITOR is on the second, and
// C_TEXTEDITOR has a handler-less entry for several of the same commands - so
// looking at the editor's contexts alone finds the entry that does nothing.
static Core::Context commandContextsOf(Core::IEditor *editor)
{
    Core::Context all;
    if (editor->widget()) {
        for (Core::IContext * const each : Core::ICore::contextObjects(editor->widget())) {
            for (const Utils::Id &id : each->context())
                all.add(id);
        }
    }
    for (const Utils::Id &id : editor->context())
        all.add(id);
    return all;
}

// A Quick editor whose language answers exactly the given mask.
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
        const Core::Context context = commandContextsOf(editor);
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
        // Plain text on purpose, as the assertions below say: this is about
        // the relay, which is what a file whose language answers nothing has.
        // A .cpp file reaches CppEditor's own answers - see
        // testFollowingATypeAsksTheLanguageBeforeTheRelay.
        const Utils::FilePath file = dir.filePath("code.txt");
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
        const Core::Context context = commandContextsOf(editor);
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

        const Core::Context context = commandContextsOf(editor);
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

        const Core::Context context = commandContextsOf(editor);
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

    // Taking a completion that is a snippet, through the whole path this view
    // uses: the provider answers, QML is handed the words, one is taken, and
    // the item writes it in.
    //
    // This is the join three entries of the plan called out as untested.
    // ViewportAssistTarget is private to textviewport.cpp, so the two overrides
    // that make snippets work here - handing the holes to the view, and asking
    // the language to lay out what was inserted - were reachable only by
    // driving a real proposal. This drives one.
    void testTakingASnippetCompletionOffersItsHoles()
    {
        // A provider whose one offer is a snippet, which is what Creator's own
        // C++ snippets are in a completion list. An item is a snippet when its
        // data is a string - see AssistProposalItem::isSnippet().
        class SnippetProcessor final : public IAssistProcessor
        {
        public:
            IAssistProposal *perform() override
            {
                auto * const item = new AssistProposalItem;
                item->setText("cls");
                item->setData(QString("class $name$ : public $base$ {};"));
                // Several lines, so that laying out what was inserted is
                // something the file can be looked at to see.
                auto * const multi = new AssistProposalItem;
                multi->setText("multi");
                multi->setData(QString("if ($cond$) {\nreturn;\n}"));
                QSharedPointer<GenericProposalModel> model(new GenericProposalModel);
                model->loadContent({item, multi});
                return new GenericProposal(interface()->position(), model);
            }
        };

        class SnippetProvider final : public CompletionAssistProvider
        {
        public:
            IAssistProcessor *createProcessor(const AssistInterface *) const override
            {
                return new SnippetProcessor;
            }
        };

        Utils::TemporaryDirectory dir("quick-editor-snippet-completion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("code.cpp");
        QVERIFY(file.writeFileContents(""));

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

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        SnippetProvider provider;
        document->setCompletionAssistProvider(&provider);

        QVERIFY2(!view->hasSnippetPlaceholders(), "the view began with holes in it");

        QSignalSpy answered(view, &TextViewport::completionsAvailable);
        view->requestCompletions();
        QTRY_VERIFY2(!answered.isEmpty(), "the provider was never asked");
        QCOMPARE(answered.last().first().toStringList(), QStringList({"cls", "multi"}));

        view->applyCompletion("cls");

        // The markup is gone, which says the item went through the view's own
        // target rather than being pasted in as it stands.
        const QString text = document->document()->toPlainText();
        QVERIFY2(!text.contains('$'),
                 qPrintable(QString("the snippet's markup reached the file: %1").arg(text)));
        QCOMPARE(text, QString("class name : public base {};"));

        // And the holes were handed to the view, which is the override this
        // test exists for: without it the text would be right and Tab would
        // have nowhere to go.
        QVERIFY2(view->hasSnippetPlaceholders(),
                 "the view was never given the snippet's holes");
        QCOMPARE(view->highlights("TextEditor.SnippetPlaceholders").size(), 2);

        // The caret stands on the first hole, ready to be typed over.
        QCOMPARE(view->textCursor().position(), int(QString("class ").size()));

        // And several lines, which is where the other override shows: what was
        // put in is laid out the way the code style asks. The snippet carries
        // no indentation of its own - "return;" is written hard against the
        // margin - and the line it goes on had four spaces on it already.
        view->clearSnippetPlaceholders();
        document->document()->setPlainText("    ");
        view->setCursorPosition(4);
        answered.clear();
        view->requestCompletions();
        QTRY_VERIFY(!answered.isEmpty());
        view->applyCompletion("multi");

        // Laid out: the body is indented and the line it started on is put
        // where the style wants it rather than where the caret happened to be.
        // Without that the file would read "    if (cond) {" and "return;".
        QCOMPARE(document->document()->toPlainText(),
                 QString("if (cond) {\n    return;\n}"));
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

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 0);

        // And the colours reach what is drawn, which is the half a highlighter
        // being present does not prove. Asked of JSON, the language this file
        // still is: it is one of the definitions this repository ships, so
        // this holds on a checkout that has downloaded nothing.
        const auto colours = [viewport] {
            QSet<QRgb> found;
            const QVariantList ranges = viewport->visibleLine(0).value("formats").toList();
            for (const QVariant &range : ranges)
                found.insert(range.toMap().value("foreground").value<QColor>().rgba());
            return found;
        };
        QTRY_VERIFY2(colours().size() > 1,
                     qPrintable(QString("the line is drawn in %1 colour(s)").arg(colours().size())));

        // Saving under a different name changes the language. TextDocument
        // reads its mime type when it *opens* and never again, so this is the
        // editor's job - the same reason the widget one reconfigures on
        // filePathChanged. What the new language colours is its own business
        // and not checked here; Python is not a definition this repository
        // ships, and the question is whether the rename is noticed at all.
        const Utils::FilePath renamed = dir.filePath("sample.py");
        const QString expected
            = Utils::mimeTypeForFile(renamed, Utils::MimeMatchMode::MatchDefaultAndRemote).name();
        QVERIFY2(expected != document->mimeType(),
                 "both names have the same mime type, so renaming proves nothing");

        document->setFilePath(renamed);
        QCOMPARE(document->mimeType(), expected);
        QVERIFY(document->syntaxHighlighter());
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
        // Java, not C++: the comment marker is the same and its definition is
        // one of the 50 this repository ships, so the run does not depend on
        // what the machine has downloaded. See bundledDefinition().
        const Utils::FilePath file = dir.filePath("commented.java");
        QVERIFY(file.writeFileContents("// a b c\nint x = 1;\n"));

        // Asked for rather than assumed. What this checks is how this view
        // draws a comment; which view a C++ file opens in by default is a
        // different question.
        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restoreView([factory, wasQuick] {
            factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

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
        for (const Utils::Id context : commandContextsOf(editor)) {
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
        for (const Utils::Id context : commandContextsOf(second)) {
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

    // A kind the document holds under a name this plugin does not know is
    // drawn by both views. The Qt Quick view draws every kind the document
    // has; the widget knew five names and drew only those, so a client asking
    // for an id of its own - CocoLanguageClient does - was drawn in one view
    // and not the other. That is a difference between the views rather than a
    // rule about them.
    void testBothViewsDrawAKindTheyDoNotKnowTheNameOf()
    {
        Utils::TemporaryDirectory dir("selection-kind-by-any-name");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("marked.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        const Utils::Id ours("SelectionKindTest.OfOurOwn");
        QTextCharFormat marked;
        marked.setBackground(QColor(Qt::green));

        // The widget first, because that is the half this changes.
        Core::IEditor * const widgetEditor = Core::EditorManager::openEditor(
            file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY(widgetEditor);
        auto * const base = qobject_cast<BaseTextEditor *>(widgetEditor);
        QVERIFY2(base, "the plain text editor is not a BaseTextEditor any more");
        TextEditorWidget * const widget = base->editorWidget();
        TextDocument * const document = base->textDocument();
        QVERIFY(widget && document);
        QVERIFY2(widget->extraSelections(ours).isEmpty(),
                 "something had already drawn under this name");

        QTextCursor over(document->document());
        over.setPosition(0);
        over.setPosition(5, QTextCursor::KeepAnchor);
        document->setExtraSelections(ours, {{over, marked}});

        const QList<QTextEdit::ExtraSelection> drawn = widget->extraSelections(ours);
        QVERIFY2(drawn.size() == 1,
                 "the widget drew nothing for a kind it does not know the name of");
        QCOMPARE(drawn.first().cursor.selectionStart(), 0);
        QCOMPARE(drawn.first().cursor.selectionEnd(), 5);

        // Emptied on the document is taken back, or a stale mark outlives
        // whoever put it there.
        document->setExtraSelections(ours, {});
        QVERIFY2(widget->extraSelections(ours).isEmpty(),
                 "the widget kept drawing a kind the document had given up");
        Core::EditorManager::closeEditors({widgetEditor}, false);

        // And the same file in the other view, which already did this - here
        // so that the two halves are written down in one place.
        Core::IEditor * const quickEditor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(quickEditor);
        const QScopeGuard closeQuick(
            [quickEditor] { Core::EditorManager::closeEditors({quickEditor}, false); });
        TextViewport * const view = viewportForEditor(quickEditor);
        QVERIFY(view);
        auto * const quickDocument = qobject_cast<TextDocument *>(quickEditor->document());
        QVERIFY(quickDocument);

        QTextCursor there(quickDocument->document());
        there.setPosition(0);
        there.setPosition(5, QTextCursor::KeepAnchor);
        quickDocument->setExtraSelections(ours, {{there, marked}});
        QTRY_VERIFY2(view->highlights(ours).size() == 1,
                     "the Qt Quick view drew nothing for a kind of its own name");
    }

    // Ranges that do not arrive in document order must still settle. The view
    // keeps what it is given sorted, and used to compare that against the
    // caller's order - so a producer that hands them over in any other order
    // looked different on every pass. updatePolish() sets these, so the layout
    // asked for another layout and never stopped: FollowSymbolTest took
    // 1 298 540 ms in this editor against the widget editor's 41 598 ms, and
    // failed thirteen cases whose waits the loop was starving.
    //
    // A language server's diagnostics are the producer that does not sort;
    // everything else in the tree walks the document forwards and was
    // accidentally safe, which is why no suite saw this.
    void testRangesGivenOutOfOrderDoNotLoopTheLayout()
    {
        Utils::TemporaryDirectory dir("out-of-order-highlights");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("marked.txt");
        QVERIFY(file.writeFileContents("alpha beta gamma delta\nsecond line here\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const view = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(view);
        QTRY_VERIFY2(view->visibleLineCount() > 0, "the view never laid anything out");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const auto rangeAt = [document](int from, int to) {
            QTextCursor cursor(document->document());
            cursor.setPosition(from);
            cursor.setPosition(to, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setBackground(QColor(Qt::red));
            return TextDocument::ExtraSelection{cursor, format};
        };

        // The later range first, which is what a server that reports its
        // complaints in its own order does.
        const QList<TextDocument::ExtraSelection> outOfOrder{rangeAt(12, 17), rangeAt(0, 5)};

        Utils::GuiTest::CollectedWarnings warnings({"updatePolish", "polish() loop"});
        QSignalSpy laidOut(view, &TextViewport::metricsChanged);
        document->setExtraSelections(TextEditorWidget::CodeWarningsSelection, outOfOrder);

        // Waiting for a layout rather than for a length of time: the warning
        // is printed from inside one, so there is nothing to see until one has
        // happened.
        QTRY_VERIFY2(laidOut.count() > 0, "the view never laid out again");

        QVERIFY2(warnings.hits().isEmpty(),
                 qPrintable(QString("the layout asked for another layout: %1")
                                .arg(warnings.hits().value(0))));

        // And the ranges are there, in the order the view keeps them, so this
        // did not settle by dropping them.
        const QList<TextViewport::Highlight> drawn
            = view->highlights(TextEditorWidget::CodeWarningsSelection);
        QCOMPARE(drawn.size(), 2);
        QCOMPARE(drawn.first().start, 0);
        QCOMPARE(drawn.at(1).start, 12);
    }

    // Joining lines with the caret at the very start of the file. The shared
    // joinLines() works out the last character *of* the selection as
    // selectionEnd() - 1, which with no selection at position zero is -1 - a
    // position Qt refuses, with a warning, leaving the cursor where it was.
    //
    // The join is right either way, because the line count below it is clamped
    // to at least one. What is wrong is the warning: a log with warnings
    // nobody means in it is a log nobody reads, and a polish loop printing 350
    // times a run went unnoticed for exactly that reason.
    void testJoiningAtTheStartOfTheFileSaysNothingAboutPositionMinusOne()
    {
        Utils::TemporaryDirectory dir("join-at-start");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("two.txt");
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
        view->setCursorPosition(0);

        Utils::GuiTest::CollectedWarnings warnings({"out of range"});
        view->joinLines();

        QVERIFY2(warnings.hits().isEmpty(),
                 qPrintable(QString("joining asked for a position that is not there: %1")
                                .arg(warnings.hits().value(0))));

        // And it still joined, so this did not go quiet by doing nothing.
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->document()->findBlockByNumber(0).text(), QString("first second"));
    }

    // Typing the closing half of a bracket the view put in itself. Both views
    // insert the closing one when "(" is typed; only the widget stepped over
    // it when ")" followed, so a C++ file in this editor answered "(" with
    // "()" and then ")" with "())" - every bracket, brace and quote a reader
    // types, doubled.
    void testTypingAClosingBracketStepsOverTheOneTheViewAdded_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTypingAClosingBracketStepsOverTheOneTheViewAdded()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("closing-bracket");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("b.cpp");
        QVERIFY(file.writeFileContents("int f\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const auto type = [editor](Qt::Key key, const QString &text) {
            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY2(target, "nothing takes keys for this editor");
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &press);
        };

        editor->gotoLine(1, 5);

        // The view puts the closing half in for us.
        type(Qt::Key_ParenLeft, "(");
        QCOMPARE(document->document()->toPlainText().trimmed(), QString("int f()"));

        // And typing that half goes over it rather than adding another.
        type(Qt::Key_ParenRight, ")");
        QCOMPARE(document->document()->toPlainText().trimmed(), QString("int f()"));

        // The caret is past the bracket, not still inside it, so the next
        // thing typed lands after the call rather than in it.
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 7);
    }

    // Scripts of keys typed into each view, and the two files compared. Every
    // other test here sets a cursor, invokes a command or sends one key; none
    // types a *sequence*, and three separate bugs have now been found in that
    // gap: a doubled closing bracket, the closers that nest behind it, and
    // Return between braces leaving "x;}" on one line with no indent.
    //
    // The closers nest: "{" leaves "}", then "(" leaves ")}", then a quote
    // leaves "\"" in front of both. Stepping over one has to leave the rest,
    // and putting one in has to join what is already waiting. Getting either
    // wrong leaves the line ending in ")}" - which is what typing this line
    // into this editor did.
    void testAMarkOnTheTextFollowsAnEditItDidNotMake_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // What is drawn over a range of the file - a search result, the other uses
    // of the name under the caret, a diagnostic, a refactoring marker - marks
    // *text*, not two numbers. A widget editor keeps a QTextCursor per range
    // and the document carries it; this view keeps positions, and positions
    // slide off what they were put on at the next edit.
    void testAMarkOnTheTextFollowsAnEditItDidNotMake()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("marks-follow");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("hello world\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        QTextCursor over(document->document());
        over.setPosition(6);
        over.setPosition(11, QTextCursor::KeepAnchor);
        QCOMPARE(over.selectedText(), QString("world"));
        QTextCharFormat format;
        format.setBackground(QColor(Qt::magenta));
        TextEditor::setViewSelections(editor, "Test.Marks", {{over, format}});

        const auto marked = [editor] {
            const QList<TextDocument::ExtraSelection> marks
                = TextEditor::viewSelections(editor, "Test.Marks");
            return marks.size() == 1 ? marks.first().cursor : QTextCursor();
        };
        QCOMPARE(marked().selectedText(), QString("world"));

        // Written through the document: a quick fix, a language server.
        QTextCursor elsewhere(document->document());
        elsewhere.setPosition(0);
        elsewhere.insertText("XY");
        QCOMPARE(marked().selectionStart(), 8);
        QCOMPARE(marked().selectedText(), QString("world"));

        // And typed into the view, which is the path the caret is deliberately
        // not carried on - nothing writes these back afterwards, so they are.
        QTextCursor toStart(document->document());
        toStart.setPosition(0);
        TextEditor::setTextCursorOf(editor, toStart);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, "Z");
        QCoreApplication::sendEvent(target, &press);
        QCOMPARE(marked().selectionStart(), 9);
        QCOMPARE(marked().selectedText(), QString("world"));

        // Taking the text away again brings it back where it started.
        QTextCursor undo(document->document());
        undo.setPosition(0);
        undo.setPosition(3, QTextCursor::KeepAnchor);
        undo.removeSelectedText();
        QCOMPARE(marked().selectionStart(), 6);
        QCOMPARE(marked().selectedText(), QString("world"));
    }

    // Where the view thinks the caret is. The last sighting of the flake
    // pointed here: the script's own ")" landed inside the string, which is a
    // caret several characters behind the document.
    static int caretPositionOf(Core::IEditor *editor)
    {
        if (TextViewport * const view = Internal::viewportForEditor(editor))
            return view->cursorPosition();
        if (TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor))
            return widget->textCursor().position();
        return -1;
    }

    // Sends a script key by key and records what each one did. The trace is
    // kept rather than printed: this has failed three times in about fifty
    // suite runs, never twice the same way, and printing as it goes perturbs
    // it enough that sixteen instrumented runs found nothing.
    static bool typeScript(Core::IEditor *editor, const QString &script,
                           QStringList *trace, QList<int> *carets)
    {
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QObject * const target = TextEditor::keyTargetOf(editor);
        if (!document || !target)
            return false;

        for (int i = 0; i < script.size(); ++i) {
            const QChar ch = script.at(i);
            int key = Qt::Key_unknown;
            Qt::KeyboardModifiers modifiers = Qt::NoModifier;
            if (ch == '<') {
                const int close = script.indexOf('>', i);
                if (close <= i)
                    return false;
                const QString name = script.mid(i + 1, close - i - 1);
                i = close;
                if (name == "SelectAll") {
                    QTextCursor all(document->document());
                    all.select(QTextCursor::Document);
                    TextEditor::setTextCursorOf(editor, all);
                    continue;
                }
                if (name == "Home") {
                    key = Qt::Key_Home;
                } else if (name == "Insert") {
                    key = Qt::Key_Insert;
                } else if (name == "Backtab") {
                    key = Qt::Key_Backtab;
                    modifiers = Qt::ShiftModifier;
                } else {
                    return false;
                }
            } else if (ch == '\n') {
                key = Qt::Key_Return;
            } else if (ch == '\b') {
                key = Qt::Key_Backspace;
            } else if (ch == '\t') {
                key = Qt::Key_Tab;
            }
            const QString text = key == Qt::Key_unknown ? QString(ch) : QString();
            QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
            QCoreApplication::sendEvent(target, &press);

            const int caret = caretPositionOf(editor);
            if (carets)
                *carets << caret;
            if (trace) {
                *trace << QString("%1 -> %2 | caret %3")
                              .arg(text.isEmpty() ? QString::number(key, 16) : text,
                                   document->document()->toPlainText(),
                                   QString::number(caret));
            }
        }
        return true;
    }

    static QList<std::tuple<QString, QString, QString>> typingScripts()
    {
        const QList<std::tuple<QString, QString, QString>> scripts{
            {"a line", "void f() { g(\"a\"); }", "void f() { g(\"a\"); }"},
            {"a bracket inside a string", "g(\"(\")", "g(\"(\")"},
            {"return between braces", "void f() {\nx;", "void f() {\n    x;\n}"},
            {"backspace over a pair", "f(\b", "f"},
            {"return in a line comment", "// hi\nx;", "// hi\nx;"},
            {"return in a block comment", "/* hi\nx;", "/* hi\n * x;"},
            {"return after a doxygen opening", "/**\n", "/**\n * "},
            {"a bracket around a selection", "word<SelectAll>(", "(word)"},
            {"tab at the start of a line", "x;<Home>\t", "    x;"},
            {"backtab on an indented line", "\tx;<Home><Backtab>", "x;"},
            {"overwriting two characters", "hello<Home><Insert>ab", "abllo"},
            {"overwriting past the line", "hi<Home><Insert>xyz", "xyz"},
            {"overwriting with a bracket", "hello<Home><Insert>(", "(ello"},
            {"backspace while overwriting", "hello<Home><Insert>\b", "hello"},
            {"return while overwriting", "hello<Home><Insert>\n", "\nhello"},
        };
        return scripts;
    }

    void testTypingALineOfCppLeavesTheSameFileInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<QString>("script");
        QTest::addColumn<QString>("expected");

        // "\n" is Return, "\b" Backspace and "\t" Tab; <Name> is a key that
        // has no character, or - for <SelectAll> - the selection a reader
        // makes with the mouse. Everything else is typed.
        //
        // Every row expects the same file from both views, so the widget row
        // is the control on what the expectation should be.
        const QList<std::tuple<QString, QString, QString>> scripts = typingScripts();
        for (const auto &[what, script, expected] : scripts) {
            QTest::newRow(qPrintable(QString("widget: %1").arg(what)))
                << false << script << expected;
            QTest::newRow(qPrintable(QString("quick: %1").arg(what)))
                << true << script << expected;
        }
    }

    void testTypingALineOfCppLeavesTheSameFileInEitherView()
    {
        QFETCH(bool, quick);
        QFETCH(QString, script);
        QFETCH(QString, expected);

        Utils::TemporaryDirectory dir("probe-typing");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        // Both are global and both are turned off and back on by tests that
        // run before this one. Left off, the text comes out wrong in a way
        // that reads as a defect in the typing: with no auto-insert there are
        // no closing characters at all, and with no stepping over there are
        // two of each. Said here so that the next time it happens the failure
        // names the cause rather than showing the damage.
        QVERIFY2(globalCompletionSettings().autoInsertBrackets(),
                 "a test left auto-insert turned off");
        QVERIFY2(globalCompletionSettings().skipAutoCompletedText(),
                 "a test left stepping over an inserted character turned off");

        QStringList trace;

        QVERIFY(typeScript(editor, script, &trace, nullptr));

        QVERIFY2(document->document()->toPlainText() == expected,
                 qPrintable(QString("got %1, wanted %2, key by key:\n  %3")
                                .arg(document->document()->toPlainText(), expected,
                                     trace.join("\n  "))));
    }

    void testClosingAnEditorLetsGoOfItsDocument_data()
    {
        // A name rather than a suffix: one of these editors is chosen by the
        // whole file name.
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("contents");
        // Which view to ask for, where the factory offers a choice. -1 leaves
        // it alone, for an editor that is only ever drawn one way.
        QTest::addColumn<int>("quick");
        QTest::newRow("c++ in the widget view") << "t.cpp" << "int i;\n" << 0;
        QTest::newRow("c++ in the quick view") << "t.cpp" << "int i;\n" << 1;
        QTest::newRow("markdown") << "t.md" << "# hi\n" << -1;
        QTest::newRow("qrc") << "t.qrc" << "<RCC/>\n" << -1;
        QTest::newRow("android manifest")
            << "AndroidManifest.xml"
            << "<?xml version=\"1.0\"?><manifest package=\"a.b\"/>\n" << -1;
        // The wizard's own template with its placeholders filled in. Entry
        // 230 tried a fabricated one and the model editor aborted, taking the
        // process with it, so this is the shape it really expects.
        QTest::newRow("model") << "t.qmodel" << QString::fromLatin1(R"QMODEL(<?xml version="1.0" encoding="UTF-8"?>
<qmt>
 <project>
  <uid>{11111111-1111-1111-1111-111111111111}</uid>
  <root-package>
   <instance>
    <MPackage>
     <base-MObject>
      <MObject>
       <base-MElement>
        <MElement>
         <uid>{22222222-2222-2222-2222-222222222222}</uid>
        </MElement>
       </base-MElement>
       <name>Probe</name>
       <children>
        <handles>
         <handles>
          <qlist>
           <item>
            <handle>
             <uid>{33333333-3333-3333-3333-333333333333}</uid>
             <target>
              <instance type="MCanvasDiagram">
               <MCanvasDiagram>
                <base-MDiagram>
                 <MDiagram>
                  <base-MObject>
                   <MObject>
                    <base-MElement>
                     <MElement>
                      <uid>{33333333-3333-3333-3333-333333333333}</uid>
                     </MElement>
                    </base-MElement>
                    <name>Probe</name>
                   </MObject>
                  </base-MObject>
                 </MDiagram>
                </base-MDiagram>
               </MCanvasDiagram>
              </instance>
             </target>
            </handle>
           </item>
          </qlist>
         </handles>
        </handles>
       </children>
      </MObject>
     </base-MObject>
    </MPackage>
   </instance>
  </root-package>
 </project>
</qmt>
)QMODEL") << -1;
        QTest::newRow("git commit message")
            << "COMMIT_EDITMSG" << "a commit message\n" << -1;
        // A Chrome Trace, which is JSON recognised by its magic rather than
        // by the suffix - the one trace format that is writable as text.
        QTest::newRow("chrome trace")
            << "t.json" << "{\"traceEvents\":[],\"displayTimeUnit\":\"ms\"}\n" << -1;
        QTest::newRow("svg") << "t.svg"
                             << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                "width=\"1\" height=\"1\"/>\n" << -1;
    }

    // A view that outlives the editor it was made for keeps watching the
    // document, and a document that nothing is looking at any more is what
    // says the view is gone. Tests here open and close an editor apiece, so
    // one that held on would sit in the next test's process reacting to a
    // file it has no business with.
    void testClosingAnEditorLetsGoOfItsDocument()
    {
        QFETCH(QString, fileName);
        QFETCH(QString, contents);
        QFETCH(int, quick);

        Utils::TemporaryDirectory dir("close-lets-go");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath(fileName);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        std::optional<QScopeGuard<std::function<void()>>> restore;
        if (quick >= 0) {
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            QVERIFY(factory);
            const bool wasQuick = factory->usesQuickEditor();
            restore.emplace([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
            factory->setUsesQuickEditor(quick > 0);
        }

        // The widget, because that is what every editor has and what nothing
        // owns: Core::IContext keeps it in a QPointer, so the editor is the
        // only thing that can free it.
        QPointer<QWidget> widget;
        QPointer<QWidget> toolBar;
        QPointer<Core::IDocument> document;
        {
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            if (!editor)
                QSKIP("Nothing here opens this kind of file");
            widget = editor->widget();
            QVERIFY(widget);
            // Asked for, because it is made on demand and the editor manager
            // asks for it too. A second widget, kept the same unowned way.
            toolBar = editor->toolBar();
            document = editor->document();
            QVERIFY(document);
            Core::EditorManager::closeEditors({editor}, false);
        }

        // Deleted later rather than at once, so this waits rather than asks.
        QTRY_VERIFY2(widget.isNull(), "the editor's widget outlived the editor");
        if (toolBar)
            QTRY_VERIFY2(toolBar.isNull(), "the editor's tool bar outlived the editor");
        QTRY_VERIFY2(document.isNull(), "something is still holding the document");
    }

    // The same scripts as the typing test, but watching where the caret is
    // after every key rather than what the file says at the end. A view whose
    // caret falls behind the text still types the right characters for a
    // while and puts the later ones in the wrong place - one sighting of the
    // intermittent failure had the script's own ")" inside the string. The
    // widget editor is a cursor in the document and cannot fall behind, so it
    // says where each key should leave the caret.
    //
    // One editor per view for all of the scripts, not one per script. Opening
    // and closing thirty of them measured nothing this test is about - what
    // closing an editor leaves behind is testClosingAnEditorLetsGoOfItsDocument's
    // question, asked there deliberately and over more kinds of editor - and
    // it is where the suite has twice died of heap corruption. That is not a
    // fix: entry 244 could not find the bug with four instruments, and this
    // takes the test out of its way rather than getting it out of everyone's.
    void testTheCaretLandsWhereTheWidgetEditorsDoes()
    {
        Utils::TemporaryDirectory dir("caret-lockstep");
        QVERIFY(dir.isValid());

        const auto openOne = [&dir](bool quick) -> Core::IEditor * {
            const Utils::FilePath file
                = dir.filePath(quick ? QString("quick.cpp") : QString("widget.cpp"));
            if (!file.writeFileContents(""))
                return nullptr;
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            if (!factory)
                return nullptr;
            const bool was = factory->usesQuickEditor();
            const QScopeGuard restore([factory, was] { factory->setUsesQuickEditor(was); });
            factory->setUsesQuickEditor(quick);
            return Core::EditorManager::openEditor(file);
        };

        Core::IEditor * const widgetEditor = openOne(false);
        QVERIFY(widgetEditor);
        Core::IEditor * const quickEditor = openOne(true);
        QVERIFY(quickEditor);
        const QScopeGuard closeThem([widgetEditor, quickEditor] {
            Core::EditorManager::closeEditors({widgetEditor, quickEditor}, false);
        });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(quickEditor),
                 "the quick row opened in a widget editor, so this compares one view with itself");

        // Emptied and put back to the start, so each script types into the
        // same file the last one did and starts where it did.
        // Emptied, put back to the start, and *said to be* - one editor runs
        // every script now, so a script that leaves something behind would
        // show up as a failure in the next one. Checked rather than assumed:
        // the text, the caret, and the range the view keeps for what it
        // inserted, which is the state that steps a closing character over.
        const auto reset = [](Core::IEditor *editor) -> QString {
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            if (!document)
                return "the editor has no text document";
            QTextCursor all(document->document());
            all.select(QTextCursor::Document);
            all.removeSelectedText();
            QTextCursor start(document->document());
            start.setPosition(0);
            TextEditor::setTextCursorOf(editor, start);

            const QString left = document->document()->toPlainText();
            if (!left.isEmpty())
                return "the document still holds \"" + left + "\"";
            if (caretPositionOf(editor) != 0)
                return "the caret is at " + QString::number(caretPositionOf(editor));
            // Overwrite mode, which five of the scripts toggle once each and
            // none toggles back. Sharing one editor let the second of those
            // start with it already on, so its own <Insert> turned it *off*
            // and it stopped testing overwriting at all - invisibly, because
            // both views alternated together and this test compares the two.
            if (TextViewport * const view = Internal::viewportForEditor(editor)) {
                if (view->autoCompletedRange().hasSelection()) {
                    return "the view still has "
                           + view->autoCompletedRange().selectedText() + " pending";
                }
                view->setOverwriteMode(false);
                if (view->overwriteMode())
                    return "the view is still overwriting";
            } else if (TextEditorWidget * const widget
                       = TextEditor::TextEditorWidget::fromEditor(editor)) {
                widget->setOverwriteMode(false);
                if (widget->overwriteMode())
                    return "the widget is still overwriting";
            }
            return {};
        };

        QStringList diverged;
        for (const auto &[what, script, expected] : typingScripts()) {
            const QString widgetReset = reset(widgetEditor);
            QVERIFY2(widgetReset.isEmpty(), qPrintable(what + ", widget view: " + widgetReset));
            QList<int> widgetCarets;
            QVERIFY2(typeScript(widgetEditor, script, nullptr, &widgetCarets),
                     qPrintable(what + ": the widget view would not take the script"));

            const QString quickReset = reset(quickEditor);
            QVERIFY2(quickReset.isEmpty(), qPrintable(what + ", quick view: " + quickReset));
            const bool overwrites = script.contains("<Insert>");
            QList<int> quickCarets;
            QVERIFY2(typeScript(quickEditor, script, nullptr, &quickCarets),
                     qPrintable(what + ": the quick view would not take the script"));

            // A script with one <Insert> in it must end overwriting, or it
            // did not test what its name says. This is the only thing that
            // can tell: the comparison below is view against view, and two
            // views both left in the wrong mode agree with each other.
            for (Core::IEditor * const editor : {widgetEditor, quickEditor}) {
                bool over = false;
                if (TextViewport * const view = Internal::viewportForEditor(editor))
                    over = view->overwriteMode();
                else if (TextEditorWidget * const w
                         = TextEditor::TextEditorWidget::fromEditor(editor))
                    over = w->overwriteMode();
                QVERIFY2(over == overwrites,
                         qPrintable(what + (overwrites
                                                ? ": ended in insert mode, so it overwrote nothing"
                                                : ": ended overwriting, which it never asked for")));
            }

            // Collected rather than asserted here: which scripts diverge is
            // worth knowing. Ten of them never type a character that gets a
            // closer put in after it, so a change that breaks that breaks
            // five - and "five, these five" says more than "the first one".
            if (quickCarets != widgetCarets) {
                const auto joined = [](const QList<int> &carets) {
                    return Utils::transform<QStringList>(
                               carets, [](int c) { return QString::number(c); })
                        .join(",");
                };
                diverged << QString("%1: quick %2, widget %3")
                                .arg(what, joined(quickCarets), joined(widgetCarets));
            }
        }
        QVERIFY2(diverged.isEmpty(), qPrintable("\n  " + diverged.join("\n  ")));
    }

    // In TextEditor rather than beside the other C++ editor checks because
    // reaching a TextViewport needs Qt Quick, which CppEditor does not link.
    void testCompletionAnswersInTheQuickViewWithCppMembers()
    {
        Utils::TemporaryDirectory dir("quick-cpp-completion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("members.cpp");
        QVERIFY(file.writeFileContents("struct Probe { int alpha; int beta; };\n"
                                       "void use(Probe p)\n"
                                       "{\n"
                                       "    p.\n"
                                       "}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY2(view, "the C++ file did not open in the Qt Quick view");

        // Just after the "p.", which is where the members are on offer.
        const int dot = document->document()->toPlainText().indexOf("p.") + 2;
        QVERIFY(dot > 1);
        QTextCursor caret(document->document());
        caret.setPosition(dot);
        TextEditor::setTextCursorOf(editor, caret);

        // Asked until it answers with something: the model parses the file on
        // its own thread and an early request is answered with nothing, which
        // is not a failure - it is the language still reading.
        QStringList offered;
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                QSignalSpy answered(view, &TextViewport::completionsAvailable);
                view->requestCompletions();
                if (!answered.wait(2000))
                    return false;
                offered = answered.last().at(0).toStringList();
                return offered.contains("alpha") && offered.contains("beta");
            }(),
            30000);

        QVERIFY2(offered.contains("alpha") && offered.contains("beta"),
                 qPrintable("offered: " + offered.join(", ")));

        // And not the other words in the file. Both member names are written
        // in it, so a completer that only knows the words it has seen offers
        // them too and would pass the check above; what it cannot do is leave
        // out the ones that are not members of Probe.
        //
        // "Probe" is not one of those: a class injects its own name into its
        // scope, so "p.Probe" names the type and the model offers it. Asked
        // of a free function and a keyword instead, which nothing reachable
        // through a "." can be.
        for (const QString &notAMember : {QString("use"), QString("struct")}) {
            QVERIFY2(!offered.contains(notAMember),
                     qPrintable("\"" + notAMember + "\" was offered after \"p.\", so this is "
                                "completing from the words in the file: " + offered.join(", ")));
        }
    }

    // The companion to the completion check above, for the other half of what
    // the C++ editor configures. This one can go all the way: a quick fix is
    // offered, applied, and the text it produced is read back.
    void testAQuickFixIsOfferedAndAppliedInTheQuickView()
    {
        Utils::TemporaryDirectory dir("quick-cpp-quickfix");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("split.cpp");
        QVERIFY(file.writeFileContents("void use(bool alpha, bool beta)\n"
                                       "{\n"
                                       "    if (alpha && beta)\n"
                                       "        return;\n"
                                       "}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY2(view, "the C++ file did not open in the Qt Quick view");

        const int op = document->document()->toPlainText().indexOf("&&");
        QVERIFY(op > 0);
        QTextCursor caret(document->document());
        caret.setPosition(op);
        TextEditor::setTextCursorOf(editor, caret);

        // The fixes come back on the signal, which this path may emit before
        // requestQuickFixes() has returned - so the record is read whether it
        // is already there or still to arrive.
        const QString wanted = "Split if Statement";
        QStringList offered;
        const auto askOnce = [&] {
            QSignalSpy answered(view, &TextViewport::quickFixesAvailable);
            view->requestQuickFixes();
            if (answered.isEmpty() && !answered.wait(1000))
                return false;
            offered = answered.last().at(0).toStringList();
            return offered.contains(wanted);
        };
        QTRY_VERIFY_WITH_TIMEOUT(askOnce(), 30000);
        QVERIFY2(offered.contains(wanted),
                 qPrintable("offered: " + offered.join(", ")));

        view->applyQuickFix(offered.indexOf(wanted));

        // What the fix says it does, asked of the text rather than of the
        // list: the one condition became two, and the operator joining them
        // is gone. Written this way because the fix chooses its own
        // indentation and asserting that would be asserting the formatter.
        const QString after = document->document()->toPlainText();
        QVERIFY2(!after.contains("&&"), qPrintable(after));
        QCOMPARE(after.count("if ("), 2);
        QVERIFY2(after.contains("alpha") && after.contains("beta"), qPrintable(after));
    }

    // The change bar: a line untouched this session draws none, one edited
    // draws it in the changed colour, and one edited before the file was
    // written draws it in the saved colour. The rule is a binding in
    // EditorGutter.qml - the C++ side only says which of the three a line is -
    // so it is asked of the drawn bars rather than of the model.
    void testTheChangeBarSaysWhetherALineWasEditedAndSaved()
    {
        // The gutter is drawn only where line numbers or fold markers are, and
        // both are global settings other tests write.
        DisplaySettings &display = displaySettings();
        const bool wasShowing = display.displayLineNumbers();
        const QScopeGuard restoreSetting([&display, wasShowing] {
            display.displayLineNumbers.setValue(wasShowing); });
        display.displayLineNumbers.setValue(true);

        Utils::TemporaryDirectory dir("quick-editor-change-bar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("changed.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

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

        const std::function<void(QQuickItem *, QList<QQuickItem *> &)> collect =
            [&collect](QQuickItem *item, QList<QQuickItem *> &found) {
                if (item->objectName() == QLatin1String("gutterChangeBar"))
                    found << item;
                const QList<QQuickItem *> children = item->childItems();
                for (QQuickItem * const child : children)
                    collect(child, found);
            };
        const auto bars = [form, &collect] {
            QList<QQuickItem *> found;
            collect(form, found);
            return found;
        };

        QTRY_VERIFY2(bars().size() > 2, "the gutter draws no change bars at all");

        // Nothing edited yet, so nothing is marked. Asserted first: "not
        // drawn" below has to mean a bar stopped being drawn rather than
        // never having been there.
        for (QQuickItem * const bar : bars())
            QVERIFY2(!bar->isVisible(), "a line nobody touched is marked as changed");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QTextCursor cursor(document->document());
        cursor.setPosition(document->document()->findBlockByNumber(1).position());
        cursor.insertText("X");

        const auto barForLine = [&bars](int line) -> QQuickItem * {
            for (QQuickItem * const bar : bars()) {
                if (bar->property("row").toInt() == line)
                    return bar;
            }
            return nullptr;
        };
        QTRY_VERIFY(barForLine(1) && barForLine(1)->isVisible());
        QCOMPARE(barForLine(1)->property("color").value<QColor>(), viewport->changedLineColor());
        QVERIFY2(barForLine(0) && !barForLine(0)->isVisible(),
                 "editing one line marked its neighbour too");

        // Written since: the same line, in the other colour. That is the whole
        // point of the second colour - the edit is still this session's.
        QVERIFY(document->save(file));
        QTRY_COMPARE(barForLine(1)->property("color").value<QColor>(),
                     viewport->savedLineColor());
        QVERIFY2(barForLine(1)->isVisible(),
                 "saving took the mark away, so nothing says the line was edited");
    }

    // A gutter nobody asked for takes no room. Turning off the line numbers
    // and the fold markers has to leave the text where it would have been
    // without a gutter at all, rather than indented by an invisible one - and
    // that is a binding in CodeViewport.qml, not something the C++ side says.
    void testAGutterNobodyAskedForTakesNoRoom()
    {
        DisplaySettings &display = displaySettings();
        const bool wasNumbers = display.displayLineNumbers();
        const bool wasFolds = display.displayFoldingMarkers();
        const QScopeGuard restoreSettings([&display, wasNumbers, wasFolds] {
            display.displayLineNumbers.setValue(wasNumbers);
            display.displayFoldingMarkers.setValue(wasFolds); });
        display.displayLineNumbers.setValue(true);
        display.displayFoldingMarkers.setValue(true);

        Utils::TemporaryDirectory dir("quick-editor-gutter-room");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("gutter.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

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
        auto * const gutter = form->findChild<QQuickItem *>("codeGutter");
        QVERIFY2(gutter, "the form has no gutter at all");

        // Asked for: it is drawn and it takes room. Asserted first, so that
        // "no room" below means room was given up rather than never taken.
        QTRY_VERIFY2(gutter->isVisible(), "the gutter is not drawn where it was asked for");
        QVERIFY2(gutter->width() > 0, "the gutter is drawn and takes no room");
        const qreal textStartsAt = viewport->x();

        // Neither asked for: not drawn, and no room.
        display.displayLineNumbers.setValue(false);
        display.displayFoldingMarkers.setValue(false);
        QTRY_VERIFY2(!gutter->isVisible(), "the gutter stayed after both columns were turned off");
        QTRY_COMPARE(gutter->width(), qreal(0));
        QVERIFY2(viewport->x() < textStartsAt,
                 "the text stayed where the gutter had put it, so the room was never given back");

        // And back again, so that what is asserted above is a binding that
        // follows the setting rather than a value read once at build time.
        // Line numbers rather than fold markers: this document's language does
        // not fold, so the fold column is not shown for it whatever the
        // setting says.
        display.displayLineNumbers.setValue(true);
        QTRY_VERIFY2(gutter->isVisible(), "the gutter did not come back when it was asked for again");
        QVERIFY2(gutter->width() > 0, "the gutter came back and takes no room");
        QTRY_COMPARE(viewport->x(), textStartsAt);
    }

    // Where the current-line band is drawn. Whether it is drawn at all is
    // covered by testTheEditorDrawsWhatTheDisplaySettingsAskFor; this is the
    // rest of it, which is arithmetic in CodeViewport.qml over the caret
    // rectangle. The band and the gutter's numbers are placed by different
    // sums - the numbers from the visible-row model - so asking that they
    // agree says more than asking either on its own.
    void testTheCurrentLineHighlightSitsOnTheLineItMarks()
    {
        DisplaySettings &display = displaySettings();
        const bool wasNumbers = display.displayLineNumbers();
        const bool wasCurrent = display.highlightCurrentLine();
        const QScopeGuard restoreSettings([&display, wasNumbers, wasCurrent] {
            display.displayLineNumbers.setValue(wasNumbers);
            display.highlightCurrentLine.setValue(wasCurrent); });
        display.displayLineNumbers.setValue(true);
        display.highlightCurrentLine.setValue(true);

        Utils::TemporaryDirectory dir("quick-editor-current-line");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("current.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\nfour\nfive\nsix\n"));

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
        QTRY_VERIFY(viewport->visibleLineCount() > 4);
        auto * const highlight = form->findChild<QQuickItem *>("currentLineHighlight");
        QVERIFY2(highlight, "the form has no current-line highlight at all");
        auto * const gutter = form->findChild<QQuickItem *>("codeGutter");
        QVERIFY(gutter);

        const std::function<void(QQuickItem *, QList<QQuickItem *> &)> collect =
            [&collect](QQuickItem *item, QList<QQuickItem *> &found) {
                if (item->objectName() == QLatin1String("gutterLineNumber"))
                    found << item;
                const QList<QQuickItem *> children = item->childItems();
                for (QQuickItem * const child : children)
                    collect(child, found);
            };
        const auto numberTop = [form, &collect](int row) -> std::optional<qreal> {
            QList<QQuickItem *> found;
            collect(form, found);
            for (QQuickItem * const number : found) {
                if (number->property("row").toInt() == row)
                    return number->mapToItem(form, QPointF(0, 0)).y();
            }
            return std::nullopt;
        };
        const auto bandTop = [form, highlight] {
            return highlight->mapToItem(form, QPointF(0, 0)).y();
        };

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        QTRY_VERIFY2(highlight->isVisible(), "the current line is not marked at all");
        QVERIFY2(highlight->x() >= gutter->x() + gutter->width(),
                 "the band runs through the gutter instead of starting at the text");

        for (const int line : {0, 2, 3}) {
            viewport->setCursorPosition(
                document->document()->findBlockByNumber(line).position());
            // cursorLine counts from one.
            QTRY_COMPARE(viewport->property("cursorLine").toInt(), line + 1);

            const std::optional<qreal> top = numberTop(line);
            QVERIFY2(top, "the gutter draws no number for the line the cursor is on");
            // Both are inset by the viewport's own offset. Leaving that out of
            // one of them moves the band by less than half a line, so it still
            // lands nearer its own line than any other: close enough to pass a
            // test that only asked which line was nearest, and still wrong on
            // screen.
            QTRY_COMPARE(bandTop(), *top);

            const std::optional<qreal> below = numberTop(line + 1);
            QVERIFY(below);
            QVERIFY2(bandTop() + highlight->height() <= *below,
                     "the band reaches into the line below the one it marks");
        }
    }

    void testAnEditElsewhereKeepsWhatTheViewPutIn_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Typing the closing half of a pair the view inserted steps over it. The
    // view remembers what it put in as a range in the document, and something
    // writing to that document from elsewhere - a quick fix, a language
    // server, the code model - moves the caret without the reader touching it.
    // That must not be read as the reader having walked away from the pair.
    void testAnEditElsewhereKeepsWhatTheViewPutIn()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("pending-pair");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        const auto type = [target](const QString &text) {
            for (const QChar &ch : text) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
                QCoreApplication::sendEvent(target, &press);
            }
        };

        type("f(");
        QCOMPARE(document->document()->toPlainText(), QString("f()"));

        // Somebody else writes to the file, before the caret, the way a quick
        // fix or a language server does.
        QTextCursor elsewhere(document->document());
        elsewhere.setPosition(0);
        elsewhere.insertText("x");
        QCOMPARE(document->document()->toPlainText(), QString("xf()"));

        // The closing bracket the view put in is still the one being typed.
        type(")");
        QCOMPARE(document->document()->toPlainText(), QString("xf()"));
    }

    void testAnEditFromElsewhereCarriesTheCaret_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Not everything that writes to the file goes through the view: a quick
    // fix, a refactoring, a language server, the comment the language writes
    // when Enter is pressed. The caret has to end up on the same character it
    // was on, and a widget editor's does because it *is* a cursor in the
    // document. The Quick view keeps a position, and a position has to be
    // carried.
    void testAnEditFromElsewhereCarriesTheCaret()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("edit-from-elsewhere");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("hello world\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        const auto editElsewhere = [document](int position, int remove, const QString &insert) {
            QTextCursor other(document->document());
            other.setPosition(position);
            if (remove > 0)
                other.setPosition(position + remove, QTextCursor::KeepAnchor);
            other.insertText(insert);
        };
        const auto putCaretAt = [editor, document](int position) {
            QTextCursor caret(document->document());
            caret.setPosition(position);
            TextEditor::setTextCursorOf(editor, caret);
        };

        // Between "hello" and the space.
        putCaretAt(5);
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 5);

        editElsewhere(0, 0, "XY");
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 7);

        editElsewhere(0, 2, {});
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 5);

        // At the caret rather than before it, which is what the language does
        // writing a comment continuation under Enter: the caret belongs after
        // what was written, not in front of it.
        editElsewhere(5, 0, "ZZ");
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 7);

        // And a selection is two positions, both of which move.
        QTextCursor selection(document->document());
        selection.setPosition(2);
        selection.setPosition(5, QTextCursor::KeepAnchor);
        TextEditor::setTextCursorOf(editor, selection);
        editElsewhere(0, 0, "XY");
        const QTextCursor moved = TextEditor::textCursorOf(editor);
        QCOMPARE(moved.selectionStart(), 4);
        QCOMPARE(moved.selectionEnd(), 7);
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

        // Asked for rather than assumed, as above.
        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restoreView([factory, wasQuick] {
            factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

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

        // Either view says so when a link has been opened. Following a symbol
        // is answered asynchronously by the language, so this signal is the
        // only event a test has to wait for - and a view that stays silent
        // makes every such test wait out its timeout instead.
        QSignalSpy announced(Core::EditorManager::instance(),
                             &Core::EditorManager::linkOpened);
        if (quick) {
            TextViewport * const view = viewportForEditor(editor);
            QVERIFY(view);
            QVERIFY(view->openLink(here, false));
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            QVERIFY(widget->openLink(here, false));
        }
        QCOMPARE(announced.count(), 1);
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

    // Every command the widget editor answers for a file, this one answers
    // too. A census rather than a list of ids, because the list is 120 long
    // and would be churn: what is written down is the *difference*, so a
    // command that stops reaching this editor shows up here rather than in a
    // bug report.
    //
    // This is the net that found the macro recorder. Its four commands are
    // registered against C_TEXTEDITOR - "a text editor is current" - which
    // this editor did not declare, so they were greyed out over a file it was
    // showing while the machinery behind them already worked.
    void testTheQuickEditorAnswersEveryCommandTheWidgetOneDoes()
    {
        Utils::TemporaryDirectory dir("quick-editor-command-census");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main() { return 0; }\n"));

        // Which commands have an action registered for any of \a contexts.
        // actionForContext() rather than triggering: what is being counted is
        // that the command reaches the editor at all, and that needs no
        // context to be active.
        const auto commandsFor = [](const Core::Context &contexts) {
            QSet<QString> found;
            for (const Utils::Id &one : contexts) {
                for (Core::Command * const cmd : Core::ActionManager::commands()) {
                    if (cmd->actionForContext(one))
                        found.insert(cmd->id().toString());
                }
            }
            return found;
        };

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });

        factory->setUsesQuickEditor(true);
        Core::IEditor * const quickEditor = Core::EditorManager::openEditor(file);
        QVERIFY(quickEditor);
        QVERIFY2(!TextEditorWidget::fromEditor(quickEditor),
                 "the file opened in a widget editor, so this compares nothing");
        const QSet<QString> quick = commandsFor(commandContextsOf(quickEditor));
        Core::EditorManager::closeEditors({quickEditor}, false);

        factory->setUsesQuickEditor(false);
        Core::IEditor * const widgetEditor = Core::EditorManager::openEditor(file);
        QVERIFY(widgetEditor);
        TextEditorWidget * const widgetView = TextEditorWidget::fromEditor(widgetEditor);
        QVERIFY(widgetView);

        // The widget's own contexts as well as the editor's: its per-instance
        // commands are registered against the widget, which is not the editor.
        Core::Context widgetContexts = widgetEditor->context();
        for (Core::IContext * const each : Core::ICore::contextObjects(widgetView)) {
            for (const Utils::Id &id : each->context())
                widgetContexts.add(id);
        }
        const QSet<QString> widget = commandsFor(widgetContexts);
        Core::EditorManager::closeEditors({widgetEditor}, false);

        QVERIFY2(widget.size() > 100,
                 "the widget editor answers almost nothing, so the fixture is wrong");

        // What is still missing, written down with why, so that the list
        // shrinking is a deliberate act and the list growing is a failure.
        //
        // Print *is* a gap, and this comment used to say it was not. Core puts
        // the entry in the File menu disabled and with no handler, which is
        // true and is only half of it: TextEditorWidget registers Print in its
        // own editor context with a working handler, so File > Print prints a
        // file open in a widget editor and does nothing for one open in this
        // view. Measured, not read - registered=1 enabled=1 against
        // registered=0.
        //
        // Left open rather than closed, and the reason is this branch:
        // printing needs QPrinter, QPrinter needs Qt::PrintSupport, and
        // "utils-drop-printsupport" exists to remove that dependency.
        // Implementing Print here would add back what the branch is for.
        // Whether the text editor keeps printing at all is a decision for
        // whoever owns it, not for a test comment.
        const QStringList knownMissing{"QtCreator.Print"};
        QStringList missing = QStringList((widget - quick).begin(), (widget - quick).end());
        missing.sort();
        QCOMPARE(missing, knownMissing);
    }

    // Back and Forward over a whole trail, as one list, because the places
    // they disagree are several steps in: arriving somewhere by Go Back is
    // itself a jump, and a view that does not record it leaves Back with
    // nothing to return to and walking on through older entries instead.
    //
    // Against the widget row this is the control on the fixture - the trail is
    // what Creator really produces - and against the quick row it is the test.
    void testBackAndForwardWalkTheSameTrail_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testBackAndForwardWalkTheSameTrail()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-restorestate");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("lines.cpp");
        QString text;
        for (int i = 0; i < 60; ++i)
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

        const auto here = [editor] { return textCursorOf(editor).blockNumber(); };
        const auto wander = [keyTarget] {
            for (int i = 0; i < 3; ++i)
                QTest::keyClick(keyTarget, Qt::Key_Down);
        };

        QStringList trace;
        editor->gotoLine(10, 0); wander(); trace << QString("A=%1").arg(here());
        editor->gotoLine(30, 0); wander(); trace << QString("B=%1").arg(here());
        Core::EditorManager::goBackInNavigationHistory(); trace << QString("back1=%1").arg(here());
        Core::EditorManager::goBackInNavigationHistory(); trace << QString("back2=%1").arg(here());
        wander(); trace << QString("wander=%1").arg(here());
        Core::EditorManager::goBackInNavigationHistory(); trace << QString("back3=%1").arg(here());
        Core::EditorManager::goForwardInNavigationHistory(); trace << QString("fwd=%1").arg(here());

        // Written down rather than derived: every step is a place a reader
        // would recognise, and a change to any of them should be read.
        QCOMPARE(trace,
                 QStringList({"A=12", "B=32", "back1=29", "back2=9", "wander=12",
                              "back3=9", "fwd=12"}));
    }

    void testTheViewSaysWhenItScrolled_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Anything drawn over the text - the debugger's value tooltip - has to
    // move with it, and only the view knows when it moved. One has a scroll
    // bar to watch and the other scrolls without one, which is why asking is
    // a seam rather than a connect at the call site.
    //
    // The widget row is the control on the fixture: it says a file this long
    // really does scroll when told to.
    void testTheViewSaysWhenItScrolled()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-scrolled");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("long.cpp");
        QString text;
        for (int i = 0; i < 400; ++i)
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

        auto listener = std::make_unique<QObject>();
        int told = 0;
        whenScrolled(editor, listener.get(), [&told] { ++told; });
        QCOMPARE(told, 0);

        // Scrolled the way the reader would, by going somewhere far enough
        // down that the view has to follow.
        editor->gotoLine(300, 0);
        QTRY_VERIFY2(told > 0, "the view scrolled and said nothing");

        // And the listener going away takes the connection with it, which is
        // what stops a closed tooltip hearing about an editor that is still
        // open. The editor outlives it here, which is the way round that bites.
        const int afterFirst = told;
        listener.reset();
        editor->gotoLine(1, 0);
        QCOMPARE(told, afterFirst);
    }

    void testFormattedTextIsAppliedInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Formatting rewrites the whole file and then puts the new text back. That
    // used to be done through a TextEditorWidget, so format on save did
    // nothing at all for a C++ file - which opens in this editor.
    //
    // What is checked is the putting back: the text arrives, and the caret is
    // still where the reader left it rather than at the top.
    void testFormattedTextIsAppliedInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-formatted");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("unformatted.cpp");
        const QString before = "int f()\n{\n        int alpha = 1;\n    return alpha;\n}\n";
        const QString after = "int f()\n{\n    int alpha = 1;\n    return alpha;\n}\n";
        QVERIFY(file.writeFileContents(before.toUtf8()));

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
        QCOMPARE(document->plainText(), before);

        // On "alpha" in the line the reformatting *does* change, so that the
        // caret has to survive text going away in front of it rather than only
        // whole lines moving.
        editor->gotoLine(3, 12);
        QCOMPARE(textCursorOf(editor).blockNumber(), 2);
        QCOMPARE(textCursorOf(editor).positionInBlock(), 12);

        updateEditorText(editor, after);

        QCOMPARE(document->plainText(), after);
        QCOMPARE(textCursorOf(editor).blockNumber(), 2);
        QCOMPARE(textCursorOf(editor).block().text(), QString("    int alpha = 1;"));
        // Four spaces went from in front of it, so it moved four to the left
        // and is still on the same character.
        QCOMPARE(textCursorOf(editor).positionInBlock(), 8);
    }

    void testEveryCaretIsReachedInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Scripting asks for *every* caret, not just the one last moved - the Lua
    // binding's cursor()/setCursor() are MultiTextCursor. Both views keep one;
    // the binding read it off a TextEditorWidget, so a C++ file had none.
    void testEveryCaretIsReachedInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("multi-cursor-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("carets.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint beta = 2;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Two carets, one on each line, which is the state a script sets up
        // before inserting at both at once.
        QTextCursor first(document->document());
        first.setPosition(4);
        QTextCursor second(document->document());
        second.setPosition(document->plainText().indexOf("beta"));
        setMultiTextCursorOf(editor, Utils::MultiTextCursor({first, second}));

        const Utils::MultiTextCursor read = multiTextCursorOf(editor);
        QCOMPARE(read.cursors().size(), 2);
        QCOMPARE(read.cursors().at(0).position(), first.position());
        QCOMPARE(read.cursors().at(1).position(), second.position());
        QVERIFY2(read.hasMultipleCursors(),
                 "the view kept only one caret, so a multi-caret edit would miss the others");
    }

    // A widget put among the text. The widget editor registers it with the
    // document layout and moves it to the block's geometry; a view that is not
    // one had no answer at all, so a Lua script embedding anything into a C++
    // file was told it needed a widget editor.
    void testAWidgetCanBeEmbeddedInEitherView()
    {
        Utils::TemporaryDirectory dir("embed-in-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("embed.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint beta = 2;\nint gamma = 3;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(view->rowGaps().isEmpty(), "the view had already given room to something");

        // On the second line, which is where a script would put a note about
        // the code under it.
        const int position = document->plainText().indexOf("beta");
        QPointer<QWidget> embedded = new QWidget;
        embedded->setFixedHeight(37);

        const std::unique_ptr<EmbeddedWidgetInterface> handle
            = insertWidgetIn(editor, embedded, position);
        QVERIFY2(handle.get(), "the view refused to embed anything");

        // It lives over the editor's own widget - the QQuickWidget the scene
        // is in - and the view has given up room for it so it covers no text.
        QCOMPARE(embedded->parentWidget(), editor->widget());
        const QList<TextViewport::Gap> gaps = view->rowGaps();
        QCOMPARE(gaps.size(), 1);
        QCOMPARE(gaps.first().height, qreal(37));

        // And closing gives the room back and takes the widget with it.
        handle->close();
        QVERIFY2(view->rowGaps().isEmpty(), "the room was not given back");
        QTRY_VERIFY2(embedded.isNull(), "the widget outlived the handle that closed it");
    }

    // Escape takes an embedded widget back. The widget editor claims Escape
    // while it has one and emits embeddedWidgetsShouldClose(); a view that is
    // not one did neither, so a Lua script's widget could not be dismissed in
    // a C++ file - and the key went to the shortcut bound to it instead.
    void testEscapeSendsBackAWidgetEmbeddedInTheView()
    {
        Utils::TemporaryDirectory dir("embed-escape");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("embed.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint beta = 2;\nint gamma = 3;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Nothing embedded yet, so Escape is not the view's to take: it means
        // other things elsewhere and taking it always would be taking it from
        // them. Without this the claim below would pass on a view that always
        // claimed Escape.
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QVERIFY2(!view->wantsKeyBeforeShortcuts(&escape),
                 "the view claimed Escape with nothing to dismiss");

        const int position = document->plainText().indexOf("beta");
        QPointer<QWidget> embedded = new QWidget;
        embedded->setFixedHeight(37);
        const std::unique_ptr<EmbeddedWidgetInterface> handle
            = insertWidgetIn(editor, embedded, position);
        QVERIFY2(handle.get(), "the view refused to embed anything");

        // Now it is: otherwise the shortcut bound to Escape takes the key and
        // the view is never asked at all.
        QVERIFY2(view->wantsKeyBeforeShortcuts(&escape),
                 "Escape was left to the shortcut system with a widget embedded");

        // And the key reaches whoever put the widget there, rather than the
        // view closing something it does not own.
        QSignalSpy asked(handle.get(), &EmbeddedWidgetInterface::shouldClose);
        QCoreApplication::sendEvent(view, &escape);
        QVERIFY2(asked.count() == 1, "Escape never reached the widget's owner");
        QVERIFY2(escape.isAccepted(), "the view let Escape travel on after dismissing");
        QVERIFY2(!embedded.isNull(),
                 "the view closed the widget itself instead of asking its owner to");

        // The owner closing it is what takes it away, and the view stops
        // claiming Escape once there is nothing left to dismiss.
        handle->close();
        QTRY_VERIFY2(embedded.isNull(), "the widget outlived the handle that closed it");
        QVERIFY2(!view->wantsKeyBeforeShortcuts(&escape),
                 "the view went on claiming Escape after the widget was gone");
    }

    // A snippet's holes in a view with no overlay: drawn so the reader can see
    // where they are, and Tab moves between them. The batch before this one
    // stopped the markup reaching the file; this is the half that makes the
    // placeholders usable. No mirroring yet - filling one hole does not fill
    // the others with the same name.
    void testTabMovesBetweenASnippetsHoles()
    {
        Utils::TemporaryDirectory dir("snippet-holes");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("snip.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);

        // Nothing yet, so Tab is still indentation and Escape is not the
        // view's to take. Without this the assertions below would pass on a
        // view that always thought it had a snippet.
        QVERIFY2(!view->hasSnippetPlaceholders(), "the view began with holes in it");

        // Two holes and a final one, the shape Snippet::parse gives back.
        view->setSnippetPlaceholders({});
        QCOMPARE(view->hasSnippetPlaceholders(), false);

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        document->document()->setPlainText("class name : public base {};");
        const int nameStart = 6;
        const int baseStart = int(QString("class name : public ").size());
        view->setSnippetPlaceholders({{nameStart, nameStart + 4, 0, false},
                                      {baseStart, baseStart + 4, 1, true}});
        QVERIFY2(view->hasSnippetPlaceholders(), "the view kept none of the holes");

        // Drawn, so the reader can see where to type.
        const QList<TextViewport::Highlight> drawn
            = view->highlights("TextEditor.SnippetPlaceholders");
        QVERIFY2(drawn.size() == 2,
                 "the holes were not drawn, so the reader cannot see where to type");
        QCOMPARE(drawn.first().start, nameStart);

        // Escape is claimed while they are offered, or the shortcut bound to
        // it takes the key and the view never hears it.
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QVERIFY2(view->wantsKeyBeforeShortcuts(&escape),
                 "Escape was left to the shortcut system with a snippet open");

        // The caret starts on the first hole, and Tab takes it to the next -
        // selected, so that typing replaces what is there.
        QTextCursor caret = view->textCursor();
        caret.setPosition(nameStart);
        view->setTextCursor(caret);
        QVERIFY2(view->goToSnippetPlaceholder(true), "Tab found no hole to go to");
        QCOMPARE(view->textCursor().selectionStart(), baseStart);
        QVERIFY2(view->textCursor().hasSelection(),
                 "the hole was moved to but not selected, so typing would not replace it");

        // That one was the last, so the snippet is over and Tab is ordinary
        // indentation again.
        QVERIFY2(!view->hasSnippetPlaceholders(),
                 "the view went on offering holes after the final one was reached");
    }

    // Typing in one hole fills every other hole standing for the same name.
    // The batch before this one drew the holes and tabbed between them; this
    // is what makes a snippet with a name used twice worth having.
    void testTypingInOneHoleFillsTheOthersWithTheSameName()
    {
        Utils::TemporaryDirectory dir("snippet-mirrors");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("snip.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const QString before = "class name : public base { name(); };";
        document->document()->setPlainText(before);
        const int first = before.indexOf("name");
        const int second = before.indexOf("name", first + 1);
        const int baseAt = before.indexOf("base");
        QVERIFY(first >= 0 && second > first && baseAt > first);

        // Two holes for the same name, and one for a different one - which is
        // what says the copying follows the name rather than every hole.
        view->setSnippetPlaceholders({{first, first + 4, 0, false},
                                      {baseAt, baseAt + 4, 1, false},
                                      {second, second + 4, 0, false}});
        QVERIFY(view->hasSnippetPlaceholders());

        // The hole selected, the way Tab leaves it, so typing replaces it.
        QTextCursor caret = view->textCursor();
        caret.setPosition(first);
        caret.setPosition(first + 4, QTextCursor::KeepAnchor);
        view->setTextCursor(caret);

        QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier, "X");
        QCoreApplication::sendEvent(view, &typed);

        QCOMPARE(document->document()->toPlainText(),
                 QString("class X : public base { X(); };"));

        // And the other name was left alone.
        QVERIFY2(document->document()->toPlainText().contains("public base"),
                 "the copy reached a hole standing for a different name");

        // One undo takes the typing and every copy of it, because the copies
        // join the edit the reader made rather than being edits of their own.
        document->document()->undo();
        QCOMPARE(document->document()->toPlainText(), before);
    }

    // A hole asked to be written differently from what was typed. The C++
    // Q_PROPERTY snippet is the one shipped snippet that does this - "WRITE
    // set$name:c$" - so filling the name in with "foo" has to leave "setFoo"
    // behind and not "setfoo". Applied when the snippet is done with, which is
    // when the widget editor's overlay applies them.
    void testAHoleIsWrittenTheWayItAskedToBe()
    {
        // Written here rather than reached for: the parser keeps one of each
        // mangler privately, and what is under test is that the view uses
        // whichever it was handed.
        class Capitalise final : public NameMangler
        {
        public:
            Utils::Id id() const override { return "Test.Capitalise"; }
            QString mangle(const QString &unmangled) const override
            {
                QString result = unmangled;
                if (!result.isEmpty())
                    result[0] = result.at(0).toUpper();
                return result;
            }
        };

        Utils::TemporaryDirectory dir("snippet-manglers");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("snip.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        Capitalise capitalise;
        const QString before = "name WRITE setname";
        document->document()->setPlainText(before);
        const int nameAt = 0;
        const int setAt = before.indexOf("setname") + 3;

        // Both stand for the same name; only the second asks for a capital.
        view->setSnippetPlaceholders({{nameAt, nameAt + 4, 0, false, nullptr},
                                      {setAt, setAt + 4, 0, false, &capitalise}});
        QVERIFY(view->hasSnippetPlaceholders());

        QTextCursor caret = view->textCursor();
        caret.setPosition(nameAt);
        caret.setPosition(nameAt + 4, QTextCursor::KeepAnchor);
        view->setTextCursor(caret);

        QKeyEvent typed(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier, "f");
        QCoreApplication::sendEvent(view, &typed);

        // While it is still open the copy is what was typed, exactly as the
        // widget editor shows it: the capital is not applied per keystroke.
        QCOMPARE(document->document()->toPlainText(), QString("f WRITE setf"));

        // And on the way out it is written the way it asked to be.
        view->clearSnippetPlaceholders();
        QCOMPARE(document->document()->toPlainText(), QString("f WRITE setF"));
        QVERIFY2(!view->hasSnippetPlaceholders(), "the snippet was left open");
    }

    // The caret being taken out of a snippet ends it, the way the widget
    // editor's overlay accepts when the cursor leaves.
    void testLeavingASnippetGivesUpItsHoles()
    {
        Utils::TemporaryDirectory dir("snippet-leave");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("snip.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        document->document()->setPlainText("class name : public base {};\nint elsewhere = 1;\n");
        const int nameStart = 6;
        view->setSnippetPlaceholders({{nameStart, nameStart + 4, 0, false},
                                      {20, 24, 1, true}});
        QVERIFY(view->hasSnippetPlaceholders());

        // Still inside: moving between the holes is ordinary editing.
        QTextCursor caret = view->textCursor();
        caret.setPosition(nameStart + 2);
        view->setTextCursor(caret);
        QVERIFY2(view->hasSnippetPlaceholders(),
                 "moving within the snippet gave up its holes");

        // And out of it, which is what says the reader is done.
        caret.setPosition(document->document()->toPlainText().indexOf("elsewhere"));
        view->setTextCursor(caret);
        QVERIFY2(!view->hasSnippetPlaceholders(),
                 "the holes were still offered after the caret left the snippet");
        QVERIFY2(view->highlights("TextEditor.SnippetPlaceholders").isEmpty(),
                 "the holes were still drawn after the caret left the snippet");
    }

    // What a right click offers. The widget editor puts the language's own
    // container in front of what every text editor offers - CppEditorWidget
    // names CppEditor.ContextMenu in its contextMenuEvent() - and a view that
    // is not a widget cannot name anything, so a C++ file here offered the
    // plain text menu and nothing of C++ at all.
    void testTheContextMenuOffersTheLanguagesOwnEntries()
    {
        // A container of its own, so that what is asserted below is what this
        // test put there rather than whatever a plugin happens to register.
        const Utils::Id menuId("QuickEditorContextMenuTest.Menu");
        Core::ActionContainer * const container = Core::ActionManager::createMenu(menuId);
        QVERIFY(container);
        QAction ours(QString("Only Here"));
        Core::Command * const command
            = Core::ActionManager::registerAction(&ours,
                                                  "QuickEditorContextMenuTest.Action",
                                                  Core::Context(Core::Constants::C_GLOBAL));
        QVERIFY(command);
        container->addAction(command);
        const QScopeGuard unregister([] {
            Core::ActionManager::unregisterAction(nullptr,
                                                  "QuickEditorContextMenuTest.Action");
        });

        class MenuFactory final : public TextEditorFactory
        {
        public:
            explicit MenuFactory(Utils::Id menuId)
            {
                setId("QuickEditorContextMenuTest");
                setDisplayName("Quick Editor Context Menu Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorContextMenuTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
                setContextMenuId(menuId);
            }
        };

        MenuFactory factory(menuId);
        QCOMPARE(factory.contextMenuId(), menuId);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");

        QWidget * const host = editor->widget();
        QVERIFY(host);
        auto * const quick = host->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());

        auto * const model
            = quick->rootObject()->property("contextActions").value<QtcQuick::ActionModel *>();
        QVERIFY2(model, "the form was given no context actions at all");
        model->refresh();

        const auto listed = [model] {
            QStringList texts;
            for (int row = 0; row < model->rowCount({}); ++row) {
                texts << model->data(model->index(row, 0), QtcQuick::ActionModel::TextRole)
                             .toString();
            }
            return texts;
        };

        QVERIFY2(listed().contains("Only Here"),
                 "the right-click menu offered nothing of the language's own");

        // And what every text editor offers is still there, or the language's
        // entries would have replaced them rather than been put in front.
        Core::ActionContainer * const standard
            = Core::ActionManager::actionContainer(Constants::M_STANDARDCONTEXTMENU);
        QVERIFY(standard && standard->menu());
        const QList<QAction *> shared = standard->menu()->actions();
        if (!shared.isEmpty()) {
            const QString anyStandard = shared.first()->text();
            QVERIFY2(listed().contains(anyStandard),
                     "the language's entries took the place of the standard ones");
        }
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
    // Which *factories* are Qt Quick, as opposed to which file names happen
    // to open in one. Entry 59 of the migration plan got this wrong and wrote
    // the wrong answer down: it counted setUsesQuickEditor() calls, found the
    // single production one in CppEditorFactory, and concluded that C++ was
    // the only thing in the Qt Quick editor. It is not.
    //
    // QuickTextEditorFactory is not a TextEditorFactory at all, so it has no
    // setUsesQuickEditor() to count. It claims text/plain and is registered
    // ahead of the widget plain text editor, and preferredFactoryFor() walks a
    // mime type's parents - so every text file with no editor of its own is
    // already in the Qt Quick editor. Rust, Go, YAML, shell.
    void testWhichFactoriesAreQuick()
    {
        QStringList quick;
        for (Core::IEditorFactory * const factory : Core::IEditorFactory::allEditorFactories()) {
            const bool isQuick
                = factory->id() == Utils::Id(QUICK_TEXT_EDITOR_ID)
                  || (dynamic_cast<TextEditorFactory *>(factory)
                      && static_cast<TextEditorFactory *>(factory)->usesQuickEditor());
            if (isQuick)
                quick << factory->id().toString();
        }
        quick.sort();

        // Moving a language is a deliberate act; moving one should change this
        // line in the same commit.
        QStringList expected{QString(QUICK_TEXT_EDITOR_ID), QString("CppEditor.C++Editor"),
                             QString("QmlJSEditor.QMLJSEditor"),
                             QString("Editors.Json"), QString("Qt4.proFileEditor"),
                             QString("CMakeProject.CMakeEditor"),
                             QString("PythonEditor.PythonEditor"),
                             QString("Vcpkg.VcpkgManifestEditor"),
                             QString("QT4.FilesEditor"), QString("java.editor"),
                             QString("CompilationDatabase.CompilationDatabaseEditor"),
                             QString("Nim.NimEditor"),
                             QString("GLSLEditor.GLSLEditor")};
        expected.sort();
        QCOMPARE(quick, expected);
    }

    void testWhichLanguagesOpenInTheQuickEditor()
    {
        Utils::TemporaryDirectory dir("quick-editor-census");
        QVERIFY(dir.isValid());

        struct Expectation { const char *name; bool quick; };
        const QList<Expectation> expected{
            {"main.cpp", true},
            {"header.h", true},
            {"notes.txt", true},
            // A language with no editor of its own lands on the Qt Quick
            // factory's text/plain claim. These are not an accident of the
            // list: they are what a generic language server is configured for,
            // and every one of them is in the Qt Quick editor today.
            {"main.rs", true},
            {"main.go", true},
            {"deploy.yaml", true},
            {"build.sh", true},
            // And a language that has one does not, however plain its text.
            {"script.py", true},
            // Its text pane is the Qt Quick view now; its preview is not.
            {"README.md", true},
            // Moved last of all: the outline pane, the uses, the code model
            // facts, the design-mode answer and the context pane each went
            // first, and each has a test that runs over both views.
            {"Thing.qml", true},
            {"project.pro", true},
            {"CMakeLists.txt", true},
            // Moved. DevContainerPlugin used to decorate this file through a
            // TextEditorWidget; it puts its button in through the editor now,
            // which is what had to be true before this row could change.
            {"devcontainer.json", true},
            // Its own factory, because of the two buttons a manifest gets -
            // which are an editor decorator now, so the factory no longer
            // needs a widget to hang them on.
            {"vcpkg.json", true},
            // Three factories that configured nothing only a widget could
            // read, and so had nothing to port.
            {"project.files", true},
            {"Main.java", true},
            {"compile_commands.json", true},
            {"module.nim", true},
            {"shader.frag", true},
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

    // The eleven languages that moved were moved one at a time, and every gap
    // since has been found by somebody noticing: C++ went three entries with
    // no outline, QML files five without being parsed at all. This asks for
    // all of them at once, and asks the *factory* rather than a list of
    // features - whatever a factory configures has to arrive in the view it
    // asked for.
    //
    // Driven by the factories, so a language that turns setUsesQuickEditor()
    // on without adding a file here fails rather than being left out quietly.
    void testEveryQuickLanguageGetsWhatItsFactoryConfigures()
    {
        // A file of each language. Only the name matters: what it opens in is
        // decided by the mime type, which is half of what is checked below.
        const QHash<QString, QString> sampleFor{
            {"CppEditor.C++Editor", "main.cpp"},
            {"QmlJSEditor.QMLJSEditor", "Thing.qml"},
            {"Editors.Json", "settings.json"},
            {"Qt4.proFileEditor", "project.pro"},
            {"CMakeProject.CMakeEditor", "CMakeLists.txt"},
            {"PythonEditor.PythonEditor", "script.py"},
            {"Vcpkg.VcpkgManifestEditor", "vcpkg.json"},
            {"QT4.FilesEditor", "project.files"},
            {"java.editor", "Main.java"},
            {"CompilationDatabase.CompilationDatabaseEditor", "compile_commands.json"},
            {"Nim.NimEditor", "module.nim"},
            {"GLSLEditor.GLSLEditor", "shader.frag"},
        };

        Utils::TemporaryDirectory dir("quick-language-census");
        QVERIFY(dir.isValid());
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        QStringList wrong;
        int checked = 0;
        for (Core::IEditorFactory * const base : Core::IEditorFactory::allEditorFactories()) {
            auto * const factory = dynamic_cast<TextEditorFactory *>(base);
            if (!factory || !factory->usesQuickEditor())
                continue;
            const QString id = factory->id().toString();
            const auto complain = [&wrong, &id](const QString &what) { wrong << id + ": " + what; };

            const QString name = sampleFor.value(id);
            if (name.isEmpty()) {
                complain("moved to the Qt Quick editor with no file here to check it with");
                continue;
            }
            const Utils::FilePath file = dir.filePath(name);
            QVERIFY(file.writeFileContents(""));
            if (TextEditorFactory::preferredFactoryFor(file) != factory) {
                complain(name + " belongs to another factory, so this row measures that one");
                continue;
            }

            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            if (!editor) {
                complain("nothing opened " + name);
                continue;
            }
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            ++checked;

            TextViewport * const view = viewportForEditor(editor);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            if (!view || !document) {
                complain("did not open in the Qt Quick view at all");
                continue;
            }

            // A language whose document installs its own completion provider
            // after the factory has finished with it, which is what
            // CppEditorDocument does when it learns its mime type. Asking the
            // factory alone skips exactly the language this work is for.
            //
            // Completion only: the indenter arrives the same way, but a C++
            // document cannot be made to indent as plain text - the code
            // style factory hands one to every document, and taking that away
            // is a null indenter and a crash rather than a fallback. Nothing
            // to guard against, so nothing is asserted.
            static const QSet<QString> completesThroughItsDocument{"CppEditor.C++Editor"};
            const bool ownDocument = completesThroughItsDocument.contains(id);

            // Completions. A document may have chosen its own since - that is
            // what CppEditorDocument does once it knows its mime type - so
            // what is wrong is falling back to the words already in the file.
            if ((factory->completionAssistProvider() || ownDocument)
                && dynamic_cast<DocumentContentCompletionProvider *>(
                    document->completionAssistProvider())) {
                complain("completes from the words in the file, not from the language");
            }

            // Indenting, which a language registers either on the factory or
            // on its code style. Either way it is not the plain one.
            if (factory->indenterCreator()
                && (!document->indenter()
                    || typeid(*document->indenter()) == typeid(PlainTextIndenter))) {
                complain("indents as plain text");
            }

            // Closing a bracket as it is typed: the base class only knows how
            // to take a pair apart again.
            if (factory->autoCompleterCreator()
                && (!view->autoCompleter()
                    || typeid(*view->autoCompleter()) == typeid(AutoCompleter))) {
                complain("has no auto completer of its own");
            }

            if (factory->commentDefinition().isValid() && !view->commentDefinition().isValid())
                complain("does not know what a comment looks like in it");

            if (factory->linkFinder() && !TextEditorFactory::linkFinderFor(document))
                complain("answers no Follow Symbol");

            if (factory->typeFinder() && !TextEditorFactory::typeFinderFor(document))
                complain("answers no Follow Symbol to Type");

            // And the fold column, which is the one of these the reader sees
            // without doing anything.
            if (factory->codeFoldingSupported() && displaySettings().displayFoldingMarkers()) {
                auto * const quick = editor->widget()->findChild<QQuickWidget *>();
                QVERIFY(quick);
                QTRY_VERIFY(quick->rootObject());
                if (!quick->rootObject()->property("showFoldMarkers").toBool())
                    complain("folds, but draws no fold markers");
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join("; ")));
        // The list above is not empty on any build this runs on, and a census
        // of nothing passes.
        QCOMPARE(checked, sampleFor.size());
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

    // Follow Symbol to Type had the same shape one step later: CppEditor
    // answered it by overriding findTypeAt() on its widget, so the Qt Quick
    // editor fell through to the relay and asked whichever language server
    // was running - a bare typeDefinition instead of CppModelManager, which
    // knows both the server and the built-in model.
    void testFollowingATypeAsksTheLanguageBeforeTheRelay()
    {
        Utils::TemporaryDirectory dir("quick-editor-follow-type");
        QVERIFY(dir.isValid());
        const Utils::FilePath cpp = dir.filePath("typed.cpp");
        QVERIFY(cpp.writeFileContents("struct S {};\n\nS value;\n"));
        const Utils::FilePath txt = dir.filePath("plain.txt");
        QVERIFY(txt.writeFileContents("alpha beta\n"));

        // The fixture only means anything if the two differ in exactly the way
        // under test: one language answers Follow Type, the other does not.
        QVERIFY2(TextEditorFactory::typeFinderFor(nullptr) == nullptr,
                 "a null document was given a type finder");
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        TextEditorFactory * const cppFactory = TextEditorFactory::preferredFactoryFor(cpp);
        QVERIFY(cppFactory);
        const bool wasQuick = cppFactory->usesQuickEditor();
        const QScopeGuard restore(
            [cppFactory, wasQuick] { cppFactory->setUsesQuickEditor(wasQuick); });
        cppFactory->setUsesQuickEditor(true);

        Core::IEditor * const cppEditor = Core::EditorManager::openEditor(cpp);
        QVERIFY(cppEditor);
        QVERIFY2(!TextEditorWidget::fromEditor(cppEditor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        TextViewport * const cppView = viewportForEditor(cppEditor);
        QVERIFY(cppView);
        SymbolRequests * const cppRelay = symbolRequestsForEditor(cppEditor);
        QVERIFY(cppRelay);

        // The language answers, so the relay is never asked.
        QSignalSpy cppRelayAsked(cppRelay, &SymbolRequests::requestTypeAt);
        cppEditor->gotoLine(3, 1);
        cppView->followTypeUnderCursor(false);
        QCOMPARE(cppRelayAsked.count(), 0);
        QVERIFY2(TextEditorFactory::typeFinderFor(
                     qobject_cast<TextDocument *>(cppEditor->document())),
                 "no C++ factory answers Follow Symbol to Type");

        // And where no language answers, the relay still is - that is what it
        // is for, and a language server is the only thing that knows.
        Core::IEditor * const txtEditor
            = Core::EditorManager::openEditor(txt, QUICK_TEXT_EDITOR_ID);
        QVERIFY(txtEditor);
        TextViewport * const txtView = viewportForEditor(txtEditor);
        SymbolRequests * const txtRelay = symbolRequestsForEditor(txtEditor);
        QVERIFY(txtView && txtRelay);
        QVERIFY2(!TextEditorFactory::typeFinderFor(
                     qobject_cast<TextDocument *>(txtEditor->document())),
                 "the plain text factory grew a type finder");
        QSignalSpy txtRelayAsked(txtRelay, &SymbolRequests::requestTypeAt);
        txtEditor->gotoLine(1, 1);
        txtView->followTypeUnderCursor(false);
        QCOMPARE(txtRelayAsked.count(), 1);
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

    // And a split of a *language's* editor is still that language. The factory
    // configures the first half - the mask of commands the language answers,
    // the container its right-click entries live in, the contexts those
    // commands are registered against - and duplicate() did not go back to
    // the factory for the second. Splitting a C++ file gave a half with
    // Rename Symbol greyed out and nothing of C++ in its context menu.
    void testASplitKeepsWhatTheFactoryConfigured()
    {
        // A container of this test's own, so what is asserted below is what it
        // put there rather than whatever a plugin happens to register.
        const Utils::Id menuId("QuickEditorSplitTest.Menu");
        Core::ActionContainer * const container = Core::ActionManager::createMenu(menuId);
        QVERIFY(container);
        QAction ours(QString("Only In This Language"));
        Core::Command * const command
            = Core::ActionManager::registerAction(&ours, "QuickEditorSplitTest.Action",
                                                  Core::Context(Core::Constants::C_GLOBAL));
        QVERIFY(command);
        container->addAction(command);
        const QScopeGuard unregister([] {
            Core::ActionManager::unregisterAction(nullptr, "QuickEditorSplitTest.Action");
        });

        const Utils::Id languageContext("QuickEditorSplitTest.Language");
        MaskedFactory factory("QuickEditorSplitTest", OptionalActions::RenameSymbol);
        factory.setContextMenuId(menuId);
        factory.addEditorContext(languageContext);

        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        // The three only mean anything on the second half if the first has
        // them, which is the factory's doing and not this test's.
        QVERIFY2(editor->context().contains(languageContext),
                 "the factory gave the first half no language context, so this tests nothing");

        const std::unique_ptr<Core::IEditor> split(editor->duplicate());
        QVERIFY2(split.get(), "the editor cannot be split");
        QCOMPARE(split->document(), editor->document());

        QVERIFY2(split->context().contains(languageContext),
                 "the split half is not in the language's context, so its commands miss it");
        QVERIFY2(split->context().contains(factory.id()),
                 "the split half is not in the factory's context");

        Core::Command * const rename = Core::ActionManager::command(Constants::RENAME_SYMBOL);
        QVERIFY(rename);
        QAction *renameThere = nullptr;
        for (const Utils::Id &each : commandContextsOf(split.get())) {
            if (QAction * const action = rename->actionForContext(each)) {
                renameThere = action;
                break;
            }
        }
        QVERIFY2(renameThere, "Rename Symbol is not registered for the split half at all");
        QVERIFY2(renameThere->isEnabled(),
                 "the split half is not offered a command the language asked for");

        QWidget * const host = split->widget();
        QVERIFY(host);
        auto * const quick = host->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());
        auto * const model
            = quick->rootObject()->property("contextActions").value<QtcQuick::ActionModel *>();
        QVERIFY2(model, "the split half was given no context actions at all");
        model->refresh();
        QStringList offered;
        for (int row = 0; row < model->rowCount({}); ++row) {
            offered << model->data(model->index(row, 0), QtcQuick::ActionModel::TextRole)
                           .toString();
        }
        QVERIFY2(offered.contains("Only In This Language"),
                 "the split half's right-click menu offers nothing of the language's own");
    }

    // Everything asked about a split so far has been asked of duplicate()
    // directly. The editor manager's own path does more: it emits editorOpened
    // for the copy - which is where C++ hangs its outline, its use selections
    // and its Ctrl+U walk - and it asks the copy for a tool bar row. Two
    // batches running found live defects in duplication, and neither of the
    // tests that found them went through this path.
    void testARealSplitOfACppFileIsTheSameEditorTwice()
    {
        Utils::TemporaryDirectory dir("real-split");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int alpha;\nint beta;\nint gamma;\n"));

        // A split outlives closeAllEditors(), so the suite would run the rest
        // of its tests in a split window.
        const QScopeGuard unsplit([] {
            if (Core::Command * const cmd
                = Core::ActionManager::command(Core::Constants::REMOVE_ALL_SPLITS)) {
                if (QAction * const action = cmd->action())
                    action->trigger();
            }
            Core::EditorManager::closeAllEditors(false);
        });

        Core::IEditor * const first = Core::EditorManager::openEditor(file);
        QVERIFY(first);
        QVERIFY2(!TextEditorWidget::fromEditor(first),
                 "the C++ file opened in a widget editor, so this tests nothing");
        TextViewport * const firstView = viewportForEditor(first);
        QVERIFY(firstView);
        QTRY_VERIFY(firstView->visibleLineCount() > 0);
        first->gotoLine(3, 4);
        // The first half has to have what the second is asked for, or this
        // measures the fixture.
        QVERIFY2(first->findChild<ToolBarOutline *>(),
                 "the first half has no outline, so this tests nothing");
        QVERIFY2(first->toolBar(), "the first half has no tool bar row, so this tests nothing");

        Core::EditorManager::split();

        Core::IEditor *second = nullptr;
        const QList<Core::IEditor *> all
            = Core::DocumentModel::editorsForDocument(first->document());
        for (Core::IEditor * const each : all) {
            if (each != first)
                second = each;
        }
        QVERIFY2(second, "splitting made no second editor for the document");

        TextViewport * const secondView = viewportForEditor(second);
        QVERIFY2(secondView, "the split half is not a Qt Quick view");
        QCOMPARE(second->document(), first->document());
        // Where the reader was looking, rather than the top of the file.
        QCOMPARE(secondView->cursorPosition(), firstView->cursorPosition());

        // What the language hung on the copy when the editor manager announced
        // it, and what the copy was built with.
        QVERIFY2(second->findChild<ToolBarOutline *>(),
                 "the split half has no outline of its own");
        QVERIFY2(second->toolBar(), "the split half has no tool bar row");
        QCOMPARE(second->context(), first->context());

        // And a command only this language answers, which is the factory's
        // mask arriving: registered either way, enabled only where the
        // language said it can answer.
        Core::Command * const rename = Core::ActionManager::command(Constants::RENAME_SYMBOL);
        QVERIFY(rename);
        QAction *renameThere = nullptr;
        for (const Utils::Id &each : commandContextsOf(second)) {
            if (QAction * const action = rename->actionForContext(each)) {
                renameThere = action;
                break;
            }
        }
        QVERIFY2(renameThere, "Rename Symbol is not registered for the split half at all");
        QVERIFY2(renameThere->isEnabled(),
                 "the split half is not offered a command C++ answers");
    }

    void testASplitKeepsWhatTheLanguageAddsToAnEditor_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("quick") << true;
        QTest::newRow("widget") << false;
    }

    // The other half of what a factory configures: not what a language does to
    // its text, but what it adds to an *editor* - Python's REPL menu and its
    // interpreter picker, vcpkg's two buttons, what F1 answers over CMake.
    // The factory runs both over the editor it builds, and neither duplicate
    // path goes back through it, so a split view had neither. In the widget
    // editor as much as in this one, which is why both rows are here.
    void testASplitKeepsWhatTheLanguageAddsToAnEditor()
    {
        QFETCH(bool, quick);

        class DecoratedFactory final : public TextEditorFactory
        {
        public:
            explicit DecoratedFactory(bool quick)
            {
                setId("QuickEditorDecoratorTest");
                setDisplayName("Quick Editor Decorator Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorDecoratorTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
                setEditorDecorator([](Core::IEditor *editor) {
                    editor->setProperty("theLanguageWasHere", true);
                });
                setContextHelpProvider(
                    [](Core::IEditor *, const Core::IContext::HelpCallback &callback) {
                        callback(Core::HelpItem(QString("QuickEditorDecoratorTest.Topic")));
                    });
            }
        };

        DecoratedFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QCOMPARE(bool(viewportForEditor(editor.get())), quick);
        // Both only mean anything on the second half if the first has them,
        // and that is the factory's doing rather than this test's.
        QVERIFY2(editor->property("theLanguageWasHere").toBool(),
                 "the factory decorated nothing, so this tests nothing");
        QString first;
        editor->contextHelp(
            [&first](const Core::HelpItem &item) { first = item.helpIds().value(0); });
        QCOMPARE(first, QString("QuickEditorDecoratorTest.Topic"));

        const std::unique_ptr<Core::IEditor> split(editor->duplicate());
        QVERIFY2(split.get(), "the editor cannot be split");
        QCOMPARE(split->document(), editor->document());

        QVERIFY2(split->property("theLanguageWasHere").toBool(),
                 "the split half is missing what the language adds to an editor");

        QString topic;
        split->contextHelp(
            [&topic](const Core::HelpItem &item) { topic = item.helpIds().value(0); });
        QVERIFY2(topic == QString("QuickEditorDecoratorTest.Topic"),
                 qPrintable(QString("F1 in the split half answers \"%1\", not the language")
                                .arg(topic)));
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
        // Folding is the one setting a language can veto, and plain text does:
        // it has nothing to fold, so the column stays off however the setting
        // is set. Checked after a setting that *does* arrive, so that the
        // absence is read once the push has demonstrably happened rather than
        // before it - see testTheFoldColumnIsShownOnlyWhereTheLanguageFolds
        // for the language that says yes.
        displaySettings().displayFoldingMarkers.setValue(true);
        displaySettings().displayAnnotations.setValue(true);
        QTRY_COMPARE(form->property("showAnnotations").toBool(), true);
        QVERIFY2(!form->property("showFoldMarkers").toBool(),
                 "a plain text file was given a fold column it can never fill");
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
        // Java: its definition is shipped here and folds block comments,
        // which is what the header being foldable rests on. A C++ file would
        // need definitions this repository does not ship.
        const Utils::FilePath file = dir.filePath("licensed.java");
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
        // Java, for the reason the folding test beside this one gives.
        const Utils::FilePath file = dir.filePath("licensed.java");
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
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("pasted"));
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

    // An action that carries a menu is a button that opens it, not one that
    // fires. clang-tools' "Analyze File..." is exactly that, and in the widget
    // editor it is a QToolButton set to InstantPopup - so the Qt Quick toolbar
    // has to draw the same shape rather than a flat button that does nothing.
    void testTheFormDrawsAnActionsMenu()
    {
        class MenuDocument final : public TextDocument
        {
        public:
            MenuDocument()
                : TextDocument("QuickEditorToolBarMenuTest")
                , m_menu(new QMenu)
                , m_action(new QAction("Analyze", this))
            {
                m_first = m_menu->addAction("Clang-Tidy");
                m_second = m_menu->addAction("Clazy");
                m_action->setMenu(m_menu);
            }
            ~MenuDocument() override { delete m_menu; }
            QList<QAction *> ownToolBarActions() const override { return {m_action}; }
            QMenu * const m_menu;
            QAction * const m_action;
            QAction *m_first = nullptr;
            QAction *m_second = nullptr;
        };

        class MenuFactory final : public TextEditorFactory
        {
        public:
            MenuFactory()
            {
                setId("QuickEditorToolBarMenuTest");
                setDisplayName("Quick Editor Tool Bar Menu Test");
                setDocumentCreator([] { return new MenuDocument; });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        MenuFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = static_cast<MenuDocument *>(editor->document());
        QVERIFY(document);

        QWidget * const bar = editor->toolBar();
        QVERIFY(bar);
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the toolbar drew nothing for the language's action");

        // The menu is there and carries what the action's menu carries.
        QObject * const menu = drawn->findChild<QObject *>("languageToolBarMenu");
        QVERIFY2(menu, "the button drew no menu for an action that has one");
        QTRY_COMPARE(menu->property("count").toInt(), 2);
        QVERIFY2(!menu->property("visible").toBool(),
                 "the menu opened itself instead of waiting to be asked");

        // Pressing opens it rather than triggering the action, which would run
        // whichever tool the button is standing in for without asking.
        QSignalSpy fired(document->m_action, &QAction::triggered);
        QMetaObject::invokeMethod(drawn, "clicked");
        QTRY_VERIFY2(menu->property("visible").toBool(), "pressing the button opened nothing");
        QVERIFY2(fired.isEmpty(), "pressing the button ran the action instead of opening its menu");
    }

    // A tool bar action that is checkable rather than one that just fires.
    // GLSL's Vulkan switch is the one in the tree, and Markdown wants one for
    // its preview - neither could have had it, because the button the form
    // draws decided for itself whether it was checkable, from its role.
    void testACheckableActionShowsItsStateInTheToolBar()
    {
        Utils::TemporaryDirectory dir("checkable-toolbar-action");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("notes.txt");
        QVERIFY(file.writeFileContents("one\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor), "not the Qt Quick editor");

        // Checked before the tool bar ever sees it, so that what is drawn is
        // the action's state rather than a default that happens to match.
        QAction vulkan;
        vulkan.setText("Vulkan");
        vulkan.setCheckable(true);
        vulkan.setChecked(true);
        TextEditor::insertExtraToolBarActionIn(editor, TextEditorWidget::Left, &vulkan);

        QWidget * const bar = editor->toolBar();
        QVERIFY(bar);
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the toolbar drew nothing for the action");
        QVERIFY2(drawn->property("checkable").toBool(),
                 "the button does not consider itself checkable");
        QTRY_VERIFY2(drawn->property("checked").toBool(),
                     "the button was drawn unchecked for an action that is checked");

        // The action changing elsewhere reaches the button.
        vulkan.setChecked(false);
        QTRY_VERIFY2(!drawn->property("checked").toBool(),
                     "turning the action off left the button looking on");

        // And pressing the button is what turns the action back on - the
        // button does not keep a state of its own that the action never hears
        // about.
        QMetaObject::invokeMethod(drawn, "clicked");
        QTRY_VERIFY2(vulkan.isChecked(), "pressing the button did not check the action");
        QTRY_VERIFY2(drawn->property("checked").toBool(),
                     "the action is on but the button is not");
    }

    // A right click offers what the language registered once - its
    // ActionManager container - and, since this batch, what the document
    // offers for *this* click. The second kind cannot live in a container: the
    // quick fixes proposed for a cursor exist only while the menu is open.
    void testADocumentsContextMenuActionsAreOfferedByEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testADocumentsContextMenuActionsAreOfferedByEitherView()
    {
        QFETCH(bool, quick);

        // A document that proposes something different every time it is asked,
        // which is the whole point of the seam.
        class ClickDocument final : public TextDocument
        {
        public:
            ClickDocument()
                : TextDocument("QuickEditorContextMenuTest")
                , m_action(new QAction("Fix It", this))
            {}

            QList<QAction *> contextMenuActions(const QTextCursor &cursor) final
            {
                ++m_timesAsked;
                // The place asked about, so that a caller which loses the
                // cursor is caught rather than merely the count changing.
                m_action->setText(QString("Fix It %1 at %2")
                                      .arg(m_timesAsked)
                                      .arg(cursor.isNull() ? -1 : cursor.position()));
                return {m_action};
            }

            QAction * const m_action;
            mutable int m_timesAsked = 0;
        };

        class ClickFactory final : public TextEditorFactory
        {
        public:
            explicit ClickFactory(bool quick)
            {
                setId("QuickEditorContextMenuTest");
                setDisplayName("Context Menu Test");
                setDocumentCreator([] { return new ClickDocument; });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        ClickFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = static_cast<ClickDocument *>(editor->document());
        QVERIFY(document);

        // What each view would list, asked the way that view asks - the widget
        // builds a QMenu, the Qt Quick form reads a model.
        QtcQuick::ActionModel *model = nullptr;
        if (quick) {
            QWidget * const host = editor->widget();
            QVERIFY(host);
            auto * const form = host->findChild<QQuickWidget *>();
            QVERIFY(form);
            QTRY_VERIFY(form->rootObject());
            model = form->rootObject()->property("contextActions")
                        .value<QtcQuick::ActionModel *>();
            QVERIFY2(model, "the form was given no context actions at all");
        }

        const auto offered = [&editor, model]() -> QStringList {
            if (model) {
                model->refresh();
                QStringList texts;
                for (int row = 0; row < model->rowCount({}); ++row) {
                    texts << model->data(model->index(row, 0),
                                         QtcQuick::ActionModel::TextRole).toString();
                }
                return texts;
            }
            TextEditorWidget * const w = TextEditorWidget::fromEditor(editor.get());
            QMenu menu;
            w->appendContextMenuActions(&menu, {});
            return Utils::transform(menu.actions(), [](QAction *a) { return a->text(); });
        };

        const auto ours = [](const QStringList &texts) {
            return Utils::filtered(texts,
                                   [](const QString &t) { return t.startsWith("Fix It"); });
        };

        // Somewhere in particular, so that the place asked about is checkable.
        if (TextViewport * const view = Internal::viewportForEditor(editor.get()))
            view->setCursorPosition(0);

        const QStringList first = ours(offered());
        QCOMPARE(first.size(), 1);
        QVERIFY2(!first.first().endsWith("at -1"),
                 qPrintable("the view asked about nowhere: " + first.first()));

        // Asked again and answered again, with a different text: a container
        // would have given the same entry both times. That is the whole reason
        // this is a question rather than a stored list.
        const QStringList second = ours(offered());
        QCOMPARE(second.size(), 1);
        QVERIFY2(second.first() != first.first(),
                 qPrintable("the document was not asked again: " + second.first()));
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

    // Which of the two answers for a key comes first. A document answers for
    // the file - CppEditorDocument::handleKeyPress() splits a string or
    // continues a doxygen comment on Enter - and a handler answers for a caret
    // that is in the middle of something spanning several places, which is
    // what an in-place rename is.
    //
    // The handler goes first, the way CppEditorWidget::keyPressEvent() asks
    // CppLocalRenaming before the document. Enter is the one key both want:
    // to a rename it means "done", and a rename that cannot be ended with it
    // stays on screen editing every use of the name at once. Reachable because
    // a rename ends on a content change outside its selection and not on a
    // caret move, so the caret can be walked into a comment while it lasts.
    void testTheViewAsksAHandlerBeforeTheLanguageForAKey()
    {
        // Stands in for a language that splits strings and comments on Enter.
        class ClaimingDocument final : public TextDocument
        {
        public:
            using TextDocument::TextDocument;

            bool handleKeyPress(QKeyEvent *event, const QTextCursor &) override
            {
                if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
                    return false;
                ++m_asked;
                return true;
            }

            int m_asked = 0;
        };

        // And for the rename, which needs Enter to mean "done".
        class OrderedHandler final : public EditHandler
        {
        public:
            OrderedHandler(Core::IEditor *editor, ClaimingDocument *language)
                : EditHandler(editor)
                , m_language(language)
            {}

            bool handleKeyPress(QKeyEvent *event, const std::function<void()> &) override
            {
                if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
                    return false;
                ++m_asked;
                if (m_language->m_asked > 0)
                    m_languageWentFirst = true;
                return m_claim;
            }

            ClaimingDocument * const m_language;
            bool m_claim = true;
            bool m_languageWentFirst = false;
            int m_asked = 0;
        };

        class ClaimingFactory final : public TextEditorFactory
        {
        public:
            ClaimingFactory()
            {
                setId("QuickEditorKeyOrderTest");
                setDisplayName("Quick Editor Key Order Test");
                setDocumentCreator([] { return new ClaimingDocument("QuickEditorKeyOrderTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        ClaimingFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = static_cast<ClaimingDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = viewportForEditor(editor.get());
        QVERIFY2(view, "the factory did not build a Qt Quick editor, so this tests nothing");

        document->document()->setPlainText("alpha\n");
        QTextCursor caret(document->document());
        caret.movePosition(QTextCursor::EndOfLine);
        view->setTextCursor(caret);

        auto * const handler = new OrderedHandler(editor.get(), document);

        // Enter, which both of them want.
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
        QCoreApplication::sendEvent(view, &enter);

        QVERIFY2(handler->m_asked == 1,
                 "the caret's own handler was never asked for the key");
        QVERIFY2(!handler->m_languageWentFirst,
                 "the language answered for the file before the caret's own handler");
        QVERIFY2(document->m_asked == 0,
                 "the language was asked as well, although the handler took the key");
        QCOMPARE(document->document()->toPlainText(), QString("alpha\n"));

        // And when the handler has nothing to say - no rename is running - the
        // language still gets the key, so Enter in a doxygen comment keeps
        // writing the block. Without this the order could be "fixed" by never
        // asking the language at all.
        handler->m_claim = false;
        QCoreApplication::sendEvent(view, &enter);
        QCOMPARE(handler->m_asked, 2);
        QVERIFY2(document->m_asked == 1,
                 "the language never got the key the handler declined");
        QVERIFY2(document->document()->toPlainText() == QString("alpha\n"),
                 "the view inserted a newline although the language took the key");
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

    // The way back from a minimized info bar. CppEditorDocument is the only
    // thing in the tree that offers one, so this is a C++ button in practice:
    // minimize "no project configuration" and a warning sign in the toolbar is
    // all that can bring it back. MinimizableInfoBars hands the widget toolbar
    // a QToolButton wrapping its action; this view wants the action itself,
    // and without it minimizing the bar is a one-way door.
    void testTheFormOffersTheWayBackFromAMinimizedInfoBar()
    {
        class PlainFactory final : public TextEditorFactory
        {
        public:
            PlainFactory()
            {
                setId("QuickEditorInfoBarTest");
                setDisplayName("Quick Editor Info Bar Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorInfoBarTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        PlainFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const Utils::Id entryId("QuickEditorInfoBarTestEntry");
        Utils::MinimizableInfoBars * const bars = document->minimizableInfoBars();
        bars->setSettingsGroup("QuickEditorInfoBarTest");
        bars->setPossibleInfoBarEntries({Utils::InfoBarEntry(entryId, "No project configuration")});

        // What the document offers is the info bars' own action, not a copy:
        // whatever flips it has to flip the button drawn for it.
        const QList<QAction *> owned = bars->showInfoBarActions();
        QCOMPARE(owned.size(), 1);
        QVERIFY2(document->toolBarActions().contains(owned.first()),
                 "the document does not offer the info bar's own action");

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());

        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the toolbar drew nothing for the info bar's action");
        // The action carries a warning sign and no text at all, so a button
        // bound only to the text would be there and empty.
        QVERIFY2(!drawn->property("iconSource").toString().isEmpty(),
                 "the button drew neither text nor icon");
        // Nothing is minimized yet, so the way back must not be offered. Without
        // this the test would pass on a toolbar that always shows the button.
        QVERIFY2(!drawn->isVisible(),
                 "the toolbar offered the way back before anything was minimized");

        // The bar itself, and then the reader minimizing it - the only route
        // that reaches the state this button exists to undo. removeCancelButton()
        // leaves exactly one button on the entry, which is that one.
        bars->setInfoVisible(entryId, true);
        QTRY_VERIFY2(document->infoBar()->containsInfo(entryId),
                     "the info bar never showed the entry");
        const QList<Utils::InfoBarEntry> entries = document->infoBar()->entries();
        const auto shown = std::find_if(entries.cbegin(), entries.cend(),
                                        [entryId](const Utils::InfoBarEntry &entry) {
                                            return entry.id() == entryId;
                                        });
        QVERIFY(shown != entries.cend());
        const QList<Utils::InfoBarEntry::Button> buttons = shown->buttons();
        QCOMPARE(buttons.size(), 1);
        document->infoBar()->triggerButton(entryId, buttons.first());

        // Minimizing runs queued, so this is the first moment it can be true.
        QTRY_VERIFY2(drawn->isVisible(),
                     "minimizing the info bar left no way to bring it back");

        // And pressing it puts the bar back, which is the whole point of the
        // button - and leaves the setting as it was found.
        QMetaObject::invokeMethod(drawn, "clicked");
        QTRY_VERIFY2(document->infoBar()->containsInfo(entryId),
                     "pressing the button did not bring the info bar back");
        QTRY_VERIFY2(!drawn->isVisible(),
                     "the way back stayed offered after the bar came back");
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
        // The caret rectangles are produced when the item renders, and a
        // platform with no screen renders only when asked - so ask, the way a
        // visible window's render loop would. Without this the width reads 0
        // here and 7.2 immediately after a grab.
        const auto caretWidth = [view] {
            if (QQuickWindow * const window = view->window())
                window->grabWindow();
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

        // Shift+Insert is paste and must not be mistaken for the toggle. What
        // it pastes is whatever the clipboard holds, and another test in this
        // process may have filled it - so the text and the caret are put back
        // afterwards, leaving this phase about the toggle alone.
        type(Qt::Key_Insert, {}, Qt::ShiftModifier);
        QVERIFY2(!view->overwriteMode(), "Shift+Insert was taken for the overwrite toggle");
        QTextCursor whole(text);
        whole.select(QTextCursor::Document);
        whole.insertText("Xabcdef\n");
        view->setCursorPosition(1);

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
    // The census test above says which commands are *registered* in each
    // editor. It says nothing about what they do, and a command that reaches
    // this view and then edits differently is a worse bug than one that does
    // not reach it at all: nothing looks wrong until the file is.
    //
    // So: the same file, the same selection, every command that both views
    // expose, and the document and the carets compared afterwards. Written as
    // one test rather than one per command for the reason the registration
    // census gives - the set is too big to keep as a written-down list, and
    // what is worth keeping is the *difference*.
    void testEveryEditingCommandEditsTheSame()
    {
        struct Command
        {
            const char *name;
            std::function<void(TextViewport *)> onView;
            std::function<void(TextEditorWidget *)> onWidget;
        };
        // Every command both views expose that edits or moves. The names
        // differ where this view named its own; the pairs are what a reader
        // reaches with one keystroke either way.
        const QList<Command> commands{
            {"deleteLine", [](TextViewport *v) { v->deleteLine(); },
             [](TextEditorWidget *w) { w->deleteLine(); }},
            {"copyLineUp", [](TextViewport *v) { v->copyLineUp(); },
             [](TextEditorWidget *w) { w->copyLineUp(); }},
            {"copyLineDown", [](TextViewport *v) { v->copyLineDown(); },
             [](TextEditorWidget *w) { w->copyLineDown(); }},
            {"moveLineUp", [](TextViewport *v) { v->moveLineUp(); },
             [](TextEditorWidget *w) { w->moveLineUp(); }},
            {"moveLineDown", [](TextViewport *v) { v->moveLineDown(); },
             [](TextEditorWidget *w) { w->moveLineDown(); }},
            {"duplicateSelection", [](TextViewport *v) { v->duplicateSelection(); },
             [](TextEditorWidget *w) { w->duplicateSelection(); }},
            {"insertLineAbove", [](TextViewport *v) { v->insertLineAbove(); },
             [](TextEditorWidget *w) { w->insertLineAbove(); }},
            {"insertLineBelow", [](TextViewport *v) { v->insertLineBelow(); },
             [](TextEditorWidget *w) { w->insertLineBelow(); }},
            {"joinLines", [](TextViewport *v) { v->joinLines(); },
             [](TextEditorWidget *w) { w->joinLines(); }},
            {"indent", [](TextViewport *v) { v->indent(); },
             [](TextEditorWidget *w) { w->indent(); }},
            {"unindent", [](TextViewport *v) { v->unindent(); },
             [](TextEditorWidget *w) { w->unindent(); }},
            {"uppercaseSelection", [](TextViewport *v) { v->uppercaseSelection(); },
             [](TextEditorWidget *w) { w->uppercaseSelection(); }},
            {"lowercaseSelection", [](TextViewport *v) { v->lowercaseSelection(); },
             [](TextEditorWidget *w) { w->lowercaseSelection(); }},
            {"sortLines", [](TextViewport *v) { v->sortLines(); },
             [](TextEditorWidget *w) { w->sortLines(); }},
            {"cleanWhitespace", [](TextViewport *v) { v->cleanWhitespace(); },
             [](TextEditorWidget *w) { w->cleanWhitespace(); }},
            {"rewrapParagraph", [](TextViewport *v) { v->rewrapParagraph(); },
             [](TextEditorWidget *w) { w->rewrapParagraph(); }},
            {"deleteEndOfWord", [](TextViewport *v) { v->deleteEndOfWord(); },
             [](TextEditorWidget *w) { w->deleteEndOfWord(); }},
            {"deleteStartOfWord", [](TextViewport *v) { v->deleteStartOfWord(); },
             [](TextEditorWidget *w) { w->deleteStartOfWord(); }},
            {"deleteEndOfLine", [](TextViewport *v) { v->deleteEndOfLine(); },
             [](TextEditorWidget *w) { w->deleteEndOfLine(); }},
            {"deleteStartOfLine", [](TextViewport *v) { v->deleteStartOfLine(); },
             [](TextEditorWidget *w) { w->deleteStartOfLine(); }},
            {"selectWordUnderCursor", [](TextViewport *v) { v->selectWordUnderCursor(); },
             [](TextEditorWidget *w) { w->selectWordUnderCursor(); }},
            {"addCaretsToLineEnds", [](TextViewport *v) { v->addCaretsToLineEnds(); },
             [](TextEditorWidget *w) { w->addCursorsToLineEnds(); }},
            {"addCaretAtNextMatch", [](TextViewport *v) { v->addCaretAtNextMatch(); },
             [](TextEditorWidget *w) { w->addSelectionNextFindMatch(); }},
        };

        // Leading and trailing spaces, a short line, an empty one: the things
        // the whitespace and line commands differ over if they are going to.
        const QString content = "  aa bb\nccc Bb  \n\nd bb\n";

        const auto answerOf = [&content](const Command &command, bool quick) {
            Utils::TemporaryDirectory dir("command-census");
            if (!dir.isValid())
                return QString("<no directory>");
            const Utils::FilePath file = dir.filePath("t.cpp");
            if (!file.writeFileContents(content.toUtf8()))
                return QString("<not written>");
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            if (!factory)
                return QString("<no factory>");
            const bool wasQuick = factory->usesQuickEditor();
            const QScopeGuard restore([factory, wasQuick] {
                factory->setUsesQuickEditor(wasQuick); });
            factory->setUsesQuickEditor(quick);
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            if (!editor)
                return QString("<not opened>");
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            if (!document)
                return QString("<no document>");

            // "c Bb " on the second line: mixed case and a trailing space, so
            // that a command which changes case or trims whitespace shows a
            // difference at all. A selection of "bb" would make several of
            // these commands indistinguishable from each other.
            QTextCursor pick(document->document());
            pick.setPosition(10);
            pick.setPosition(15, QTextCursor::KeepAnchor);
            TextEditor::setTextCursorOf(editor, pick);

            if (quick)
                command.onView(Internal::viewportForEditor(editor));
            else
                command.onWidget(TextEditorWidget::fromEditor(editor));

            QString text = document->document()->toPlainText();
            text.replace(QLatin1Char('\n'), QLatin1Char('~'));
            QStringList carets;
            for (const QTextCursor &c : TextEditor::multiTextCursorOf(editor)) {
                carets << (c.hasSelection() ? QString("%1..%2").arg(c.selectionStart())
                                                  .arg(c.selectionEnd())
                                            : QString::number(c.position()));
            }
            return QString("|%1| carets=%2").arg(text, carets.join(","));
        };

        QStringList differ;
        int changedTheFile = 0;
        for (const Command &command : commands) {
            const QString fromWidget = answerOf(command, false);
            const QString fromQuick = answerOf(command, true);
            if (fromWidget != fromQuick) {
                differ << QString("%1\n    widget %2\n    quick  %3")
                              .arg(QString::fromLatin1(command.name), fromWidget, fromQuick);
            }
            if (!fromWidget.startsWith("|" + QString(content).replace(QLatin1Char('\n'),
                                                                     QLatin1Char('~')) + "|"))
                ++changedTheFile;
        }

        // A guard on the fixture: if the selection or the file were such that
        // nothing any command did showed up, every answer would match and this
        // would pass without comparing anything.
        // Measured: 19 of the 23 change the file with this fixture, and the
        // rest only move carets, which is compared too.
        QVERIFY2(changedTheFile > 15,
                 qPrintable(QString("only %1 of %2 commands changed the file, so this "
                                    "fixture is not exercising them")
                                .arg(changedTheFile).arg(commands.size())));
        QVERIFY2(differ.isEmpty(), qPrintable("\n" + differ.join("\n")));
    }

    void testTextDroppedOnTheEditorLandsWhereItWasDropped_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Text dragged in from somewhere else - another editor, another
    // application - goes in at the point it was dropped on, and what arrived
    // is what is left selected. Neither view had been asked.
    void testTextDroppedOnTheEditorLandsWhereItWasDropped()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("drop-text");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("alpha beta gamma\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Just before "beta".
        const int at = 6;
        QCOMPARE(document->plainText().mid(at, 4), QString("beta"));

        if (quick) {
            auto * const host = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(host);
            TextViewport * const view = Internal::viewportForEditor(editor);
            QVERIFY(view);
            host->resize(600, 300);
            host->show();
            QVERIFY(QTest::qWaitForWindowExposed(host));
            QTRY_VERIFY(view->visibleLineCount() > 0);
            // What CodeViewport.qml's DropArea calls when something is
            // dropped on it. Qt Quick's drag machinery is not what is being
            // compared here.
            const QRectF where = view->rectangleAt(at);
            view->dropText("XY", where.center().x(), where.center().y(), false);
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            widget->resize(600, 300);
            widget->show();
            QVERIFY(QTest::qWaitForWindowExposed(widget));
            QTextCursor c(document->document());
            c.setPosition(at);
            const QPoint p = widget->Utils::PlainTextEdit::cursorRect(c).center();

            // A drop is not one event: without the drag entering first the
            // widget has no drop state and dropEvent() leaves the file alone,
            // which reads exactly like the drop being broken.
            widget->viewport()->setAcceptDrops(true);
            QMimeData mime;
            mime.setText("XY");
            QDragEnterEvent enter(p, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(widget->viewport(), &enter);
            QVERIFY2(enter.isAccepted(), "the widget refused the drag, so nothing is being tested");
            QDropEvent drop(QPointF(p), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(widget->viewport(), &drop);
        }

        QCOMPARE(document->plainText(), QString("alpha XYbeta gamma\n"));
        QCOMPARE(TextEditor::textCursorOf(editor).selectedText(), QString("XY"));
    }

    // And a selection dragged within the file moves rather than copies: taken
    // out of where it was and put in where it was dropped. There is no widget
    // row - that path asks whether the drag came from its own viewport, and a
    // QDropEvent's source cannot be set by hand, only by a real QDrag whose
    // exec() blocks.
    void testASelectionDraggedWithinTheFileMoves()
    {
        Utils::TemporaryDirectory dir("drop-move");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("alpha beta gamma\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        auto * const host = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(host);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);
        host->resize(600, 300);
        host->show();
        QVERIFY(QTest::qWaitForWindowExposed(host));
        QTRY_VERIFY(view->visibleLineCount() > 0);

        // What 6..10 is, checked once here rather than inside the lambda
        // below: QCOMPARE expands to a bare return, which a lambda that
        // answers a QString cannot use.
        QCOMPARE(document->plainText().mid(6, 4), QString("beta"));

        const auto dropSelectionOn = [&](int onto) -> QString {
            QTextCursor all(document->document());
            all.select(QTextCursor::Document);
            all.insertText("alpha beta gamma\n");
            QTextCursor pick(document->document());
            pick.setPosition(6);
            pick.setPosition(10, QTextCursor::KeepAnchor);
            view->setTextCursor(pick);
            const QRectF where = view->rectangleAt(onto);
            view->dropText(pick.selectedText(), where.center().x(), where.center().y(), true);
            return document->plainText();
        };

        // To the end, and to the start: taken out of the middle either way.
        QCOMPARE(dropSelectionOn(16), QString("alpha  gammabeta\n"));
        QCOMPARE(dropSelectionOn(0), QString("betaalpha  gamma\n"));

        // And onto itself, which moves it nowhere: taking the text out first
        // and putting it back would be a deletion followed by an insertion,
        // and a reader who changes their mind mid-drag would lose the text.
        QCOMPARE(dropSelectionOn(8), QString("alpha beta gamma\n"));
    }

    void testDraggingSelectsWhatItPassesOver_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Dragging with the mouse is how most selections are made and neither view
    // had been asked about it. Driving one needs a pixel for a document
    // position, and the two views answer that question with different APIs -
    // which is where an earlier attempt went wrong: TextEditorWidget adds a
    // cursorRect(int) that answers in *global* coordinates and hides the base
    // cursorRect(QTextCursor) that answers in the viewport's. Mapping the
    // wrong one back gave points that named no position at all, and the
    // comparison underneath was meaningless in both views.
    void testDraggingSelectsWhatItPassesOver()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("drag-select");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("alpha beta\ngamma delta\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        QWidget *surface = nullptr;
        std::function<QPoint(int)> pointOf;
        std::function<int(const QPoint &)> positionOf;
        if (quick) {
            auto * const host = editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(host);
            TextViewport * const view = Internal::viewportForEditor(editor);
            QVERIFY(view);
            host->resize(600, 300);
            host->show();
            QVERIFY(QTest::qWaitForWindowExposed(host));
            QTRY_VERIFY(view->visibleLineCount() > 1);
            view->forceActiveFocus();
            surface = host;
            pointOf = [view](int at) {
                return view->mapToScene(view->rectangleAt(at).center()).toPoint();
            };
            positionOf = [view](const QPoint &p) {
                const QPointF in = view->mapFromScene(QPointF(p));
                return view->positionAt(in.x(), in.y());
            };
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            widget->resize(600, 300);
            widget->show();
            QVERIFY(QTest::qWaitForWindowExposed(widget));
            surface = widget->viewport();
            pointOf = [widget](int at) {
                QTextCursor c(widget->document());
                c.setPosition(at);
                return widget->Utils::PlainTextEdit::cursorRect(c).center();
            };
            positionOf = [widget](const QPoint &p) {
                return widget->cursorForPosition(p).position();
            };
        }

        // The control on the drive, before it is used for anything: a point
        // asked for a position has to name that position back. Without this
        // the comparisons below can agree while measuring nothing.
        for (const int at : {0, 5, 10, 17}) {
            QCOMPARE(positionOf(pointOf(at)), at);
        }

        const auto dragFrom = [&](int from, int to) {
            QTextCursor start(document->document());
            start.setPosition(from);
            TextEditor::setTextCursorOf(editor, start);
            const QPoint a = pointOf(from);
            const QPoint b = pointOf(to);
            QTest::mousePress(surface, Qt::LeftButton, {}, a);
            QTest::mouseMove(surface, QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2));
            QTest::mouseMove(surface, b);
            QTest::mouseRelease(surface, Qt::LeftButton, {}, b);
            QString got = TextEditor::textCursorOf(editor).selectedText();
            return got.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        };

        QCOMPARE(dragFrom(0, 10), QString("alpha beta"));
        QCOMPARE(dragFrom(6, 17), QString("beta\ngamma "));
        // Backwards over the same run: the same text, whichever end it started
        // from.
        QCOMPARE(dragFrom(10, 0), QString("alpha beta"));
    }

    void testReplacingInsideASelectionStaysInsideIt_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // "Selection only" in the Replace bar searches part of the file. The
    // restriction itself is BaseTextFindBase's and both views inherit it;
    // nothing had checked that the view underneath answers well enough for it
    // to work.
    void testReplacingInsideASelectionStaysInsideIt()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("find-scope");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("x one\nx two\nx three\nx four\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        auto * const find = Utils::Aggregation::query<Core::IFindSupport>(editor->widget());
        QVERIFY2(find, "the editor offers no way to search it at all");

        // The middle two lines.
        QTextCursor scope(document->document());
        scope.setPosition(6);
        scope.setPosition(20, QTextCursor::KeepAnchor);
        QCOMPARE(scope.selectedText().left(5), QString("x two"));
        TextEditor::setTextCursorOf(editor, scope);
        find->defineFindScope();

        QCOMPARE(find->replaceAll("x", "y", {}), 2);
        QCOMPARE(document->plainText(), QString("x one\ny two\ny three\nx four\n"));
    }

    // And the reader has to be able to see which part is being searched. A
    // widget editor paints the scope; this view was told about it and drew
    // nothing, so "Selection only" looked exactly like searching the file.
    void testTheLanguageServerIsAskedWhereASymbolIsWhenNothingElseKnows()
    {
        class LinkFactory final : public TextEditorFactory
        {
        public:
            LinkFactory()
            {
                setId("QuickEditorLinkFallbackTest");
                setDisplayName("Quick Editor Link Fallback Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorLinkFallbackTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(true);
            }
        };

        LinkFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        document->setPlainText("one two three\n");
        TextViewport * const view = Internal::viewportForEditor(editor.get());
        QVERIFY(view);

        int asked = 0;
        int askedAt = -1;
        bool askedToResolve = false;
        connect(view->symbolRequests(), &SymbolRequests::requestLinkAt, this,
                [&](const QTextCursor &cursor, const Utils::LinkHandler &, bool resolveTarget,
                    bool) {
                    ++asked;
                    askedAt = cursor.position();
                    askedToResolve = resolveTarget;
                });

        // Nothing registered a link finder for this language, so the view
        // cannot answer where a symbol is defined and has to ask.
        QVERIFY2(!TextEditorFactory::linkFinderFor(document),
                 "a link finder was registered, so this tests nothing");

        // The view cannot say where the symbol is, so it asks - which is the
        // only way "Follow Symbol Under Cursor" can work for a language whose
        // only answer comes from a language server.
        QTextCursor at(document->document());
        at.setPosition(4);
        view->findLinkAt(at, [](const Utils::Link &) {}, /*resolveTarget=*/false,
                         /*inNextSplit=*/false);
        QCOMPARE(asked, 1);
        QCOMPARE(askedAt, 4);
        QVERIFY2(!askedToResolve,
                 "the question was passed on with the wrong resolveTarget");

        // But the click is not taken: nothing is underlined here yet, and
        // Ctrl+click has to go on placing the caret in a file whose language
        // nobody can answer for.
        QVERIFY2(!view->followSymbolAt(4),
                 "the click was swallowed although nothing could follow it");

        // And a language that does know answers for itself, without the
        // question going out - the order the widget editor uses.
        int found = 0;
        document->setLinkFinder([&found](const TextDocument *, const QTextCursor &,
                                         const Utils::LinkHandler &callback, bool, bool) {
            ++found;
            callback({});
        });
        QVERIFY(TextEditorFactory::linkFinderFor(document));
        QVERIFY(view->followSymbolAt(4));
        QCOMPARE(found, 1);
        QCOMPARE(asked, 1);
    }

    void testTheSuggestionToolBarIsShownOverAViewThatIsNotAWidget()
    {
        Utils::TemporaryDirectory dir("suggestion-toolbar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("int one = 1;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);

        // The view has to be able to hand over what it is showing, which is
        // what the tooltip drives.
        SuggestionHost * const host = view->suggestionHost();
        QVERIFY2(host, "the Qt Quick view offered no suggestion host at all");

        const auto alternative = [](const QString &text) {
            TextSuggestion::Data data;
            data.range = {Utils::Text::Position{1, 0}, Utils::Text::Position{1, 12}};
            data.position = {1, 12};
            data.text = text;
            return data;
        };
        const QList<TextSuggestion::Data> both{alternative("int one = 1; // first\n"),
                                               alternative("int one = 1; // second\n")};
        view->setCursorPosition(0);
        host->insertSuggestion(
            std::make_unique<CyclicSuggestion>(both, document->document(), 0));
        QVERIFY2(host->suggestionVisible(), "the view was given a suggestion and shows none");

        Utils::ToolTip::hideImmediately();
        QVERIFY(!Utils::ToolTip::isVisible());

        // Driven the way the editor drives it: ask what is here, then show it.
        BaseHoverHandler &handler = suggestionHoverHandler();
        int reported = BaseHoverHandler::Priority_None;
        handler.checkPriority(view, 0, [&reported](int priority) { reported = priority; });
        QCOMPARE(reported, int(BaseHoverHandler::Priority_Suggestion));

        handler.showToolTip(view, view->mapToGlobal(QPointF(0, 0)).toPoint());
        QVERIFY2(Utils::ToolTip::isVisible(),
                 "no suggestion tool bar was shown over the Qt Quick view");
        Utils::ToolTip::hideImmediately();
    }

    void testTheLanguagesOwnBracketRulesApplyInEitherView()
    {
        Utils::TemporaryDirectory dir("language-autocompleter");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("int a = ;\n// a note here\n"));

        CompletionSettings &completion = globalCompletionSettings();
        const bool wasQuotes = completion.autoInsertQuotes();
        const QScopeGuard restoreSetting([&completion, wasQuotes] {
            completion.autoInsertQuotes.setValue(wasQuotes);
        });
        completion.autoInsertQuotes.setValue(true);

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        const auto typeAQuote = [target] {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_QuoteDbl, Qt::NoModifier, "\"");
            QCoreApplication::sendEvent(target, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_QuoteDbl, Qt::NoModifier, "\"");
            QCoreApplication::sendEvent(target, &release);
        };
        const auto caretTo = [editor, document](int line, int column) {
            QTextCursor c(document->document());
            c.setPosition(document->document()->findBlockByNumber(line).position() + column);
            TextEditor::setTextCursorOf(editor, c);
        };

        // In code the language closes the quote for the reader. The plain
        // AutoCompleter never does - it answers no to every context - so this
        // is the language's own rules arriving or not arriving.
        caretTo(0, 8);
        typeAQuote();
        QCOMPARE(document->document()->findBlockByNumber(0).text(), QString("int a = \"\";"));

        // And in a comment it does not, which is the half that says the rules
        // are C++'s rather than just "always close a quote".
        caretTo(1, 9);
        typeAQuote();
        QCOMPARE(document->document()->findBlockByNumber(1).text(), QString("// a note\" here"));
    }

    void testTheFoldColumnIsShownOnlyWhereTheLanguageFolds()
    {
        Utils::TemporaryDirectory dir("fold-column");
        QVERIFY(dir.isValid());

        DisplaySettings &display = displaySettings();
        const bool wasShowing = display.displayFoldingMarkers();
        const QScopeGuard restoreSetting([&display, wasShowing] {
            display.displayFoldingMarkers.setValue(wasShowing);
        });
        display.displayFoldingMarkers.setValue(true);

        const auto foldColumnOf = [&dir](const QString &name) {
            const Utils::FilePath file = dir.filePath(name);
            file.writeFileContents("one\ntwo\n");
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            if (!editor)
                return QVariant();
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            if (TextEditorWidget::fromEditor(editor))
                return QVariant(); // a widget editor, which is not what this asks
            auto * const quick = editor->widget()->findChild<QQuickWidget *>();
            if (!quick || !quick->rootObject())
                return QVariant();
            return quick->rootObject()->property("showFoldMarkers");
        };

        // C++ folds, so the column is worth its width.
        const QVariant cpp = foldColumnOf("t.cpp");
        QVERIFY2(cpp.isValid(), "the C++ file did not open in the Qt Quick editor");
        QVERIFY2(cpp.toBool(), "a C++ file was given no fold column");

        // Plain text does not, and the widget editor shows no column for it -
        // showing one here is width spent on markers that never appear.
        const QVariant plain = foldColumnOf("notes.txt");
        QVERIFY2(plain.isValid(), "the text file did not open in the Qt Quick editor");
        QVERIFY2(!plain.toBool(), "a plain text file was given a fold column");

        // And the setting still reaches an editor that is already open, for
        // the language that folds.
        const Utils::FilePath open = dir.filePath("open.cpp");
        QVERIFY(open.writeFileContents("int f()\n{\n}\n"));
        Core::IEditor * const editor = Core::EditorManager::openEditor(open);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor), "not the Qt Quick editor");
        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());
        QCOMPARE(quick->rootObject()->property("showFoldMarkers").toBool(), true);
        display.displayFoldingMarkers.setValue(false);
        QTRY_COMPARE(quick->rootObject()->property("showFoldMarkers").toBool(), false);
    }

    void testCommentingUsesTheLanguagesOwnMarkerInEitherView()
    {
        Utils::TemporaryDirectory dir("comment-marker");
        QVERIFY(dir.isValid());

        // C++ is the case that matters and the one that had none: it is
        // highlighted by CppHighlighter rather than by a generic definition,
        // so nothing derived from the highlighter can say what a comment
        // looks like. The factory is the only thing that knows.
        const Utils::FilePath cpp = dir.filePath("t.cpp");
        QVERIFY(cpp.writeFileContents("int one = 1;\nint two = 2;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(cpp);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(cpp);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);

        QTextCursor firstLine(document->document());
        firstLine.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        TextEditor::setTextCursorOf(editor, firstLine);
        view->unCommentSelection();
        const QString commented = document->document()->findBlockByNumber(0).text();
        QVERIFY2(commented.startsWith("//"), qPrintable("nothing was commented: " + commented));
        QVERIFY(commented.contains("int one = 1;"));

        // And back off again, which is the same definition read a second time.
        view->unCommentSelection();
        QCOMPARE(document->document()->findBlockByNumber(0).text(), QString("int one = 1;"));

        // The same text through the widget editor, which has always had the
        // language's definition. Comparing the two is what says this is the
        // C++ marker rather than merely *a* marker.
        factory->setUsesQuickEditor(false);
        const Utils::FilePath same = dir.filePath("w.cpp");
        QVERIFY(same.writeFileContents("int one = 1;\nint two = 2;\n"));
        Core::IEditor * const widgetEditor = Core::EditorManager::openEditor(same);
        QVERIFY(widgetEditor);
        const QScopeGuard closeWidget(
            [widgetEditor] { Core::EditorManager::closeEditors({widgetEditor}, false); });
        TextEditorWidget * const w = TextEditorWidget::fromEditor(widgetEditor);
        QVERIFY2(w, "the comparison editor is not a widget one");
        QTextCursor wFirst(w->document());
        wFirst.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        w->setTextCursor(wFirst);
        w->unCommentSelection();
        QCOMPARE(w->document()->findBlockByNumber(0).text(), commented);

        // A language whose marker comes from its highlighting definition
        // still gets that one - the factory answer must not replace it.
        const Utils::FilePath shell = dir.filePath("run.sh");
        QVERIFY(shell.writeFileContents("echo one\n"));
        Core::IEditor * const shellEditor = Core::EditorManager::openEditor(shell);
        QVERIFY(shellEditor);
        const QScopeGuard closeShell(
            [shellEditor] { Core::EditorManager::closeEditors({shellEditor}, false); });
        TextViewport * const shellView = Internal::viewportForEditor(shellEditor);
        QVERIFY2(shellView, "the shell script did not open in the Qt Quick editor");
        QCOMPARE(shellView->commentDefinition().singleLine, QString("#"));
    }

    void testAHoverTooltipObeysTheModifierTheSettingsAskFor()
    {
        BehaviorSettings &behavior = globalBehaviorSettings();
        const bool wasConstrained = behavior.constrainHoverTooltips();
        const QScopeGuard restoreSetting([&behavior, wasConstrained] {
            behavior.constrainHoverTooltips.setValue(wasConstrained);
        });
        behavior.constrainHoverTooltips.setValue(true);

        class CountingHoverHandler final : public BaseHoverHandler
        {
        public:
            int asked = 0;
            void identifyMatch(HoverTarget *, int, ReportPriority report) override
            {
                ++asked;
                setToolTip("something");
                report(Priority_Tooltip);
            }
            void operateTooltip(HoverTarget *, const QPoint &) override {}
        };

        Utils::TemporaryDirectory dir("hover-modifier");
        QVERIFY(dir.isValid());

        // Two editors, because one view has one hover timer: a second hover
        // into the same view would restart it rather than let the first have
        // its turn, and "it was superseded" is not "it was refused".
        struct Fixture
        {
            Core::IEditor *editor = nullptr;
            TextViewport *view = nullptr;
            CountingHoverHandler handler;
        };
        const auto open = [&dir](const QString &name, Fixture &f) {
            const Utils::FilePath file = dir.filePath(name);
            QVERIFY(file.writeFileContents("alpha beta gamma"));
            f.editor = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
            QVERIFY(f.editor);
            auto * const quick = f.editor->widget()->findChild<QQuickWidget *>();
            QVERIFY(quick);
            QTRY_VERIFY(quick->rootObject());
            f.view = quick->rootObject()->findChild<TextViewport *>();
            QVERIFY(f.view);
            f.view->setTooltipHost(quick);
            f.view->setHoverHandlers({&f.handler});
            QTRY_VERIFY(f.view->lineHeight() > 0);
        };

        Fixture refused;
        Fixture allowed;
        open("refused.txt", refused);
        open("allowed.txt", allowed);
        const QScopeGuard closeThem([&refused, &allowed] {
            Core::EditorManager::closeEditors({refused.editor, allowed.editor}, false);
        });

        const auto hover = [](Fixture &f, int position, Qt::KeyboardModifiers modifiers) {
            const QPointF at(f.view->rectangleAt(position).center().x(),
                             f.view->lineHeight() / 2);
            QHoverEvent move(QEvent::HoverMove, at, at, at, modifiers);
            QCoreApplication::sendEvent(f.view, &move);
        };

        // Started first, so its timer would fire first if it were started at
        // all. Plain hover, and the setting says Shift is required.
        hover(refused, 2, Qt::NoModifier);
        // And this one is allowed, by holding what the setting asks for.
        hover(allowed, 2, Qt::ShiftModifier);

        QTRY_VERIFY2(allowed.handler.asked > 0,
                     "a Shift+hover was refused although the setting asks for Shift");
        QCOMPARE(refused.handler.asked, 0);

        // Control belongs to following links, in either setting.
        behavior.constrainHoverTooltips.setValue(false);
        hover(refused, 8, Qt::ControlModifier);
        hover(allowed, 8, Qt::NoModifier);
        QTRY_VERIFY(allowed.handler.asked > 1);
        QCOMPARE(refused.handler.asked, 0);
    }

    void testDeletingAWordDeletesTheSameWordInEitherView_data()
    {
        QTest::addColumn<bool>("camelCase");
        QTest::addColumn<bool>("forward");
        QTest::newRow("backwards") << false << false;
        QTest::newRow("forwards") << false << true;
        QTest::newRow("backwards by humps") << true << false;
        QTest::newRow("forwards by humps") << true << true;
    }

    void testDeletingAWordDeletesTheSameWordInEitherView()
    {
        QFETCH(bool, camelCase);
        QFETCH(bool, forward);

        BehaviorSettings &behavior = globalBehaviorSettings();
        const bool wasCamel = behavior.camelCaseNavigation();
        const QScopeGuard restoreSetting([&behavior, wasCamel] {
            behavior.camelCaseNavigation.setValue(wasCamel);
        });
        behavior.camelCaseNavigation.setValue(camelCase);

        Utils::TemporaryDirectory dir("delete-word");
        QVERIFY(dir.isValid());

        // The key sequence rather than the command: these are what Ctrl (or
        // Alt) with Backspace and Delete send, and the widget editor picks
        // them out of its key handler rather than binding a shortcut.
        const QKeySequence sequence(forward ? QKeySequence::DeleteEndOfWord
                                            : QKeySequence::DeleteStartOfWord);
        QVERIFY2(sequence.count() > 0, "this platform binds no key to deleting a word");
        const QKeyCombination combination = sequence[0];

        const auto afterDeleting = [&dir, &combination](bool quick, QString *result) {
            const Utils::FilePath file = dir.filePath(quick ? QString("q.cpp")
                                                            : QString("w.cpp"));
            QVERIFY(file.writeFileContents("oneTwoThree fourFive\n"));
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            QVERIFY(factory);
            const bool was = factory->usesQuickEditor();
            const QScopeGuard restore([factory, was] { factory->setUsesQuickEditor(was); });
            factory->setUsesQuickEditor(quick);

            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            // Inside the first word, so that both directions have something
            // to take and neither is standing on a space.
            QTextCursor at(document->document());
            at.setPosition(3);
            TextEditor::setTextCursorOf(editor, at);

            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);
            QKeyEvent press(QEvent::KeyPress, combination.key(),
                            combination.keyboardModifiers());
            QCoreApplication::sendEvent(target, &press);
            *result = document->plainText();
        };

        // Out-parameters rather than a return: QVERIFY inside a lambda
        // expands to a bare return, which a value-returning one cannot have.
        QString widgetText;
        QString quickText;
        afterDeleting(false, &widgetText);
        afterDeleting(true, &quickText);

        // Something was taken, or the comparison below would pass on two
        // editors that both did nothing.
        QVERIFY2(widgetText != "oneTwoThree fourFive\n",
                 "the widget editor deleted nothing, so this compares nothing");
        QCOMPARE(quickText, widgetText);
    }

    void testBreakingALineStartsWhereEitherViewWouldStartIt_data()
    {
        QTest::addColumn<bool>("autoIndent");
        QTest::newRow("the language lays the line out") << true;
        QTest::newRow("the reader keeps their own indent") << false;
    }

    void testBreakingALineStartsWhereEitherViewWouldStartIt()
    {
        QFETCH(bool, autoIndent);

        TypingSettings &typing = globalTypingSettings();
        const bool wasAuto = typing.autoIndent();
        const QScopeGuard restoreSetting([&typing, wasAuto] {
            typing.autoIndent.setValue(wasAuto);
        });
        typing.autoIndent.setValue(autoIndent);

        Utils::TemporaryDirectory dir("return-indent");
        QVERIFY(dir.isValid());

        // The caret sits at the end of an indented line that opens a brace,
        // which is where the language's indenter and the line's own
        // indentation give different answers: one level in, or the same
        // level again.
        const auto lineAfterReturn = [&dir](bool quick, QString *result) {
            const Utils::FilePath file = dir.filePath(quick ? QString("q.cpp")
                                                            : QString("w.cpp"));
            QVERIFY(file.writeFileContents("void f()\n{\n    if (x) {\n"));
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            QVERIFY(factory);
            const bool was = factory->usesQuickEditor();
            const QScopeGuard restore([factory, was] { factory->setUsesQuickEditor(was); });
            factory->setUsesQuickEditor(quick);

            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            const QTextBlock openingBrace = document->document()->findBlockByNumber(2);
            QTextCursor at(document->document());
            at.setPosition(openingBrace.position() + openingBrace.text().length());
            TextEditor::setTextCursorOf(editor, at);

            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
            QCoreApplication::sendEvent(target, &press);
            *result = document->document()->findBlockByNumber(3).text();
        };

        QString widgetLine;
        QString quickLine;
        lineAfterReturn(false, &widgetLine);
        lineAfterReturn(true, &quickLine);

        // The two answers really are different, or this row is comparing the
        // setting against itself.
        const int indent = widgetLine.length();
        QCOMPARE(indent, autoIndent ? 8 : 4);
        QCOMPARE(quickLine, widgetLine);
    }

    // The languages that are in the Qt Quick editor because it claims
    // text/plain and the mime lookup walks parents - Rust, Go, YAML, shell -
    // and were moved there without anyone deciding it. C++ was moved on the
    // strength of eighteen plugins compared both ways; these had nothing.
    void testOrdinaryEditingIsTheSameInEitherView_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("start");
        QTest::addColumn<QString>("comment");

        QTest::newRow("rust") << "a.rs" << "fn main() {\n    let x = 1;\n}\n" << "//";
        QTest::newRow("go") << "a.go" << "func main() {\n    x := 1\n}\n" << "//";
        QTest::newRow("yaml") << "a.yaml" << "root:\n    key: 1\n" << "#";
        QTest::newRow("shell") << "a.sh" << "if true; then\n    echo hi\nfi\n" << "#";
        // Nothing knows how to comment plain text, and nothing should invent
        // a marker for it.
        QTest::newRow("plain text") << "a.txt" << "alpha\n    beta\ngamma\n" << "";
    }

    void testOrdinaryEditingIsTheSameInEitherView()
    {
        QFETCH(QString, name);
        QFETCH(QString, start);
        QFETCH(QString, comment);

        // Commenting takes its marker from the language's generic-highlighter
        // definition, and this repository ships only the 50 under
        // src/libs/3rdparty/syntax-highlighting - yaml and shell are there,
        // Rust and Go are not. Where the language is the question the row
        // asks, a machine without its definition cannot answer it; saying so
        // beats asserting on whichever editor happens to be installed.
        if (!comment.isEmpty()
            && HighlighterHelper::definitionsForFileName(Utils::FilePath::fromString(name))
                   .isEmpty()) {
            QSKIP(qPrintable("no syntax definition installed for " + name
                             + "; Help > Update Highlighting Definitions fetches them"));
        }

        Utils::TemporaryDirectory dir("languages-both-ways");
        QVERIFY(dir.isValid());

        // Break a line, type, take a word back, comment it, break again and
        // take another word back: the operations the last four batches
        // changed, in the order a reader would do them.
        const auto edited = [&dir, &name, &start](bool quick, QString *out) {
            const Utils::FilePath file = dir.filePath((quick ? "q_" : "w_") + name);
            QVERIFY(file.writeFileContents(start.toUtf8()));
            Core::IEditor * const editor = Core::EditorManager::openEditor(
                file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                            : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            QTextCursor at(document->document());
            at.setPosition(document->document()->findBlockByNumber(1).position() + 4);
            TextEditor::setTextCursorOf(editor, at);
            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);

            for (const QChar ch : QString("\nvalue_here#\nzz_")) {
                if (ch == '#') {
                    // Asked of the view rather than through the command: a
                    // command goes to the editor with focus and neither of
                    // these has any, so triggering it did nothing in both
                    // views and compared nothing.
                    if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor))
                        w->unCommentSelection();
                    else if (TextViewport * const v = Internal::viewportForEditor(editor))
                        v->unCommentSelection();
                    continue;
                }
                if (ch == '_') {
                    const QKeySequence seq(QKeySequence::DeleteStartOfWord);
                    QVERIFY(seq.count() > 0);
                    const QKeyCombination combination = seq[0];
                    QKeyEvent press(QEvent::KeyPress, combination.key(),
                                    combination.keyboardModifiers());
                    QCoreApplication::sendEvent(target, &press);
                    continue;
                }
                int key = Qt::Key_unknown;
                QString text(ch);
                if (ch == '\n') { key = Qt::Key_Return; text = "\r"; }
                else if (ch == '\b') { key = Qt::Key_Backspace; text.clear(); }
                else if (ch == '\t') { key = Qt::Key_Tab; text.clear(); }
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
                QCoreApplication::sendEvent(target, &press);
            }
            *out = document->plainText();
        };

        QString widgetText;
        QString quickText;
        edited(false, &widgetText);
        edited(true, &quickText);

        // The script did something, and did the language-specific part of it:
        // without this the comparison below passes on two editors that both
        // ignored every key.
        QVERIFY2(widgetText != start, "the widget editor was left unchanged");
        QVERIFY2(widgetText.contains("here"), "nothing was typed");
        QVERIFY2(!widgetText.contains("value"),
                 "deleting a word before the caret took nothing");
        if (comment.isEmpty()) {
            QVERIFY2(!widgetText.contains("//") && !widgetText.contains('#'),
                     qPrintable("a marker was invented for plain text: " + widgetText));
        } else {
            QVERIFY2(widgetText.contains(comment + " here") || widgetText.contains(comment + "here"),
                     qPrintable("the language's comment marker is missing: " + widgetText));
        }

        QCOMPARE(quickText, widgetText);
    }

    // The rest of ordinary editing, one operation per row so that a failure
    // names the operation rather than handing back one blob of text.
    void testEveryEditingKeyDoesTheSameInEitherView_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("start");
        QTest::addColumn<QString>("script");
        QTest::addColumn<QString>("expected");

        struct Op { const char *what; const char *script; };
        const QList<Op> ops{
            {"undo", "abcU"},
            {"redo", "abcUR"},
            {"paste", "P"},
            {"home then type", "EHx"},
            {"unindent", "|"},
            {"tab", "\t"},
        };
        struct Lang { const char *name; const char *start; };
        const QList<Lang> langs{
            {"a.rs", "fn main() {\n    let x = 1;\n}\n"},
            {"a.yaml", "root:\n    key: 1\n"},
            {"a.txt", "alpha\n    beta\ngamma\n"},
        };
        // What the widget editor does, written out rather than taken from the
        // widget at run time: two views that both did nothing would otherwise
        // agree, and the point of the row is the operation happening.
        const QHash<QString, QString> expected{
            {"a.rs|undo", "fn main() {\n    let x = 1;\n}\n"},
            {"a.rs|redo", "fn main() {\n    abclet x = 1;\n}\n"},
            {"a.rs|paste", "fn main() {\n    PASTEDlet x = 1;\n}\n"},
            {"a.rs|home then type", "fn main() {\n    xlet x = 1;\n}\n"},
            {"a.rs|unindent", "fn main() {\nlet x = 1;\n}\n"},
            {"a.rs|tab", "fn main() {\n        let x = 1;\n}\n"},
            {"a.yaml|undo", "root:\n    key: 1\n"},
            {"a.yaml|redo", "root:\n    abckey: 1\n"},
            {"a.yaml|paste", "root:\n    PASTEDkey: 1\n"},
            {"a.yaml|home then type", "root:\n    xkey: 1\n"},
            {"a.yaml|unindent", "root:\nkey: 1\n"},
            {"a.yaml|tab", "root:\n        key: 1\n"},
            {"a.txt|undo", "alpha\n    beta\ngamma\n"},
            {"a.txt|redo", "alpha\n    abcbeta\ngamma\n"},
            {"a.txt|paste", "alpha\n    PASTEDbeta\ngamma\n"},
            {"a.txt|home then type", "alpha\n    xbeta\ngamma\n"},
            {"a.txt|unindent", "alpha\nbeta\ngamma\n"},
            {"a.txt|tab", "alpha\n        beta\ngamma\n"},
        };
        for (const Lang &l : langs) {
            for (const Op &o : ops) {
                const QString key = QString("%1|%2").arg(l.name, o.what);
                QTest::newRow(qPrintable(key))
                    << QString::fromLatin1(l.name) << QString::fromLatin1(l.start)
                    << QString::fromLatin1(o.script) << expected.value(key);
            }
        }
    }

    void testEveryEditingKeyDoesTheSameInEitherView()
    {
        QFETCH(QString, name);
        QFETCH(QString, start);
        QFETCH(QString, script);
        QFETCH(QString, expected);
        QVERIFY2(!expected.isEmpty(), "this row has no expected text");

        Utils::TemporaryDirectory dir("editing-keys");
        QVERIFY(dir.isValid());
        QGuiApplication::clipboard()->setText("PASTED");

        const auto edited = [&dir, &name, &start, &script](bool quick, QString *out) {
            const Utils::FilePath file = dir.filePath((quick ? "q_" : "w_") + name);
            QVERIFY(file.writeFileContents(start.toUtf8()));
            Core::IEditor * const editor = Core::EditorManager::openEditor(
                file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                            : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);
            QTextCursor at(document->document());
            at.setPosition(document->document()->findBlockByNumber(1).position() + 4);
            TextEditor::setTextCursorOf(editor, at);
            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);
            const auto standard = [target](QKeySequence::StandardKey which) {
                const QKeySequence seq(which);
                QVERIFY2(seq.count() > 0, "this platform binds no key to that");
                const QKeyCombination combination = seq[0];
                QKeyEvent press(QEvent::KeyPress, combination.key(),
                                combination.keyboardModifiers());
                QCoreApplication::sendEvent(target, &press);
            };
            for (const QChar ch : script) {
                if (ch == 'U') { standard(QKeySequence::Undo); continue; }
                if (ch == 'R') { standard(QKeySequence::Redo); continue; }
                if (ch == 'P') { standard(QKeySequence::Paste); continue; }
                if (ch == 'H') { standard(QKeySequence::MoveToStartOfLine); continue; }
                if (ch == 'E') { standard(QKeySequence::MoveToEndOfLine); continue; }
                int key = Qt::Key_unknown;
                QString text(ch);
                Qt::KeyboardModifiers modifiers = Qt::NoModifier;
                if (ch == '\t') { key = Qt::Key_Tab; text.clear(); }
                else if (ch == '|') { key = Qt::Key_Backtab; text.clear();
                                      modifiers = Qt::ShiftModifier; }
                QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
                QCoreApplication::sendEvent(target, &press);
            }
            *out = document->plainText();
        };

        QString widgetText;
        QString quickText;
        edited(false, &widgetText);
        edited(true, &quickText);

        QCOMPARE(widgetText, expected);
        QCOMPARE(quickText, widgetText);
    }

    // Editing with more than one caret. Driven through setMultiTextCursorOf()
    // rather than a modifier drag: the seam is what every caller uses, and a
    // mouse would be testing the scene rather than the editing.
    void testEditingWithTwoCaretsIsTheSameInEitherView_data()
    {
        QTest::addColumn<QString>("script");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("carets");

        QTest::newRow("type") << "xy" << "alxypha one\nbexyta two\ngamma\n" << "4,16";
        QTest::newRow("backspace") << "\b" << "apha one\nbta two\ngamma\n" << "1,10";
        QTest::newRow("break the line") << "\n" << "al\npha one\nbe\nta two\ngamma\n" << "3,14";
        QTest::newRow("paste") << "P" << "alPpha one\nbePta two\ngamma\n" << "3,14";
        QTest::newRow("delete a word") << "_" << "pha one\nta two\ngamma\n" << "0,8";
        QTest::newRow("type then undo")
            << "xyU" << "alxpha one\nbexta two\ngamma\n" << "3,14";
    }

    void testEditingWithTwoCaretsIsTheSameInEitherView()
    {
        QFETCH(QString, script);
        QFETCH(QString, expected);
        QFETCH(QString, carets);

        Utils::TemporaryDirectory dir("multi-caret");
        QVERIFY(dir.isValid());
        QGuiApplication::clipboard()->setText("P");

        const auto edited = [&dir, &script](bool quick, QString *text, QString *at) {
            const Utils::FilePath file
                = dir.filePath(quick ? QString("q.txt") : QString("w.txt"));
            QVERIFY(file.writeFileContents("alpha one\nbeta two\ngamma\n"));
            Core::IEditor * const editor = Core::EditorManager::openEditor(
                file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                            : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            // One caret on each of the first two lines, at column 2.
            QTextCursor first(document->document());
            first.setPosition(document->document()->findBlockByNumber(0).position() + 2);
            QTextCursor second(document->document());
            second.setPosition(document->document()->findBlockByNumber(1).position() + 2);
            TextEditor::setMultiTextCursorOf(editor, Utils::MultiTextCursor({first, second}));
            QCOMPARE(TextEditor::multiTextCursorOf(editor).cursorCount(), 2);

            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);
            const auto standard = [target](QKeySequence::StandardKey which) {
                const QKeySequence seq(which);
                QVERIFY2(seq.count() > 0, "this platform binds no key to that");
                const QKeyCombination combination = seq[0];
                QKeyEvent press(QEvent::KeyPress, combination.key(),
                                combination.keyboardModifiers());
                QCoreApplication::sendEvent(target, &press);
            };
            for (const QChar ch : script) {
                if (ch == 'P') { standard(QKeySequence::Paste); continue; }
                if (ch == '_') { standard(QKeySequence::DeleteStartOfWord); continue; }
                if (ch == 'U') { standard(QKeySequence::Undo); continue; }
                int key = Qt::Key_unknown;
                QString typed(ch);
                if (ch == '\n') { key = Qt::Key_Return; typed = "\r"; }
                else if (ch == '\b') { key = Qt::Key_Backspace; typed.clear(); }
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, typed);
                QCoreApplication::sendEvent(target, &press);
            }
            *text = document->plainText();
            QStringList positions;
            for (const QTextCursor &c : TextEditor::multiTextCursorOf(editor))
                positions << QString::number(c.position());
            *at = positions.join(',');
        };

        QString widgetText;
        QString widgetCarets;
        QString quickText;
        QString quickCarets;
        edited(false, &widgetText, &widgetCarets);
        edited(true, &quickText, &quickCarets);

        // The widget editor is the reference, and it really did the operation.
        QCOMPARE(widgetText, expected);
        QCOMPARE(widgetCarets, carets);

        QCOMPARE(quickText, widgetText);
        QCOMPARE(quickCarets, widgetCarets);
    }

    // A file whose bytes do not decode with the encoding chosen for it. The
    // buffer then holds something other than what is on disk, so editing it
    // and saving would write the replacement characters over the original
    // bytes. Both views have to refuse, and say why.
    void testAFileThatDidNotDecodeIsNotEditableInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAFileThatDidNotDecodeIsNotEditableInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("decoding-error");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath(quick ? "q.txt" : "w.txt");
        // Not valid UTF-8 by any reading.
        QVERIFY(file.writeFileContents(QByteArray("ok\n\xff\xfe\x80\x81 bad\n")));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                        : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const Utils::Id selectEncoding(Constants::SELECT_ENCODING);
        // Read as UTF-8 explicitly, rather than relying on what the default
        // happens to be: the point of the row is the failure to decode.
        document->reload(Utils::TextEncoding::Utf8);
        QVERIFY2(document->hasDecodingError(),
                 "the bytes decoded after all, so this tests nothing");

        QVERIFY2(document->infoBar()->containsInfo(selectEncoding),
                 "nothing said the file could not be decoded");

        const auto readOnly = [editor] {
            if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor))
                return w->isReadOnly();
            TextViewport * const v = Internal::viewportForEditor(editor);
            return v && v->isReadOnly();
        };
        QVERIFY2(readOnly(), "a file that did not decode was left editable");

        // And the offer to fix it is there to be taken, not just to be read.
        const QList<Utils::InfoBarEntry> entries = document->infoBar()->entries();
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [selectEncoding](const Utils::InfoBarEntry &e) {
                                            return e.id() == selectEncoding;
                                        });
        QVERIFY(entry != entries.end());
        QVERIFY2(!entry->buttons().isEmpty(), "the error offered no way to choose an encoding");
    }

    // A file reread from disk - a rebase, a formatter, a build system rewrite
    // - is the same file. The reader should be left where they were in it.
    void testAReloadLeavesTheReaderWhereTheyWere_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAReloadLeavesTheReaderWhereTheyWere()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("reload-place");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath(quick ? "q.txt" : "w.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\ndelta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                        : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const QTextBlock third = document->document()->findBlockByNumber(2);
        QTextCursor away(document->document());
        away.setPosition(third.position() + 2);
        TextEditor::setTextCursorOf(editor, away);
        const int before = TextEditor::textCursorOf(editor).position();
        QVERIFY2(before > 0, "the caret never left the start, so this tests nothing");

        // Rereading with a different encoding is a reload like any other, and
        // one a test can ask for without waiting on the file system.
        QVERIFY(document->reload(Utils::TextEncoding("ISO-8859-1")));
        QCOMPARE(document->encoding().displayName(), QString("ISO-8859-1"));

        QCOMPARE(TextEditor::textCursorOf(editor).position(), before);
    }

    // Code the preprocessor left out. CppEditorDocument marks those blocks
    // and the widget editor paints them with C_DISABLED_CODE; this view drew
    // nothing for them at all, so an inactive #if branch looked like live
    // code.
    void testCodeTheProcessorLeftOutIsDrawnAsSuch()
    {
        Utils::TemporaryDirectory dir("disabled-code");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("int live = 1;\nint out = 2;\nint alsoOut = 3;\n"
                                       "int liveAgain = 4;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);

        const Utils::Id kind("TextEditor.TextViewport.DisabledCode");
        const auto drawn = [view, kind] { return view->highlights(kind); };
        QVERIFY2(drawn().isEmpty(), "something was greyed out before anything was left out");

        // What the language does when it works out which branch is live.
        // Marked here rather than driven through the code model: the view's
        // job starts once the blocks carry the flag, and that is what this is
        // about.
        QTextBlock second = document->document()->findBlockByNumber(1);
        QTextBlock third = document->document()->findBlockByNumber(2);
        TextBlockUserData::setIfdefedOut(second);
        TextBlockUserData::setIfdefedOut(third);
        view->updateDisabledCode();

        // Two adjacent lines are one range, not two: a run of left-out code
        // reads as one block.
        QCOMPARE(drawn().size(), 1);
        QCOMPARE(drawn().first().start, second.position());
        QCOMPARE(drawn().first().end, third.position() + third.length() - 1);
        QCOMPARE(drawn().first().format,
                 document->fontSettings().toTextCharFormat(C_DISABLED_CODE));

        // And it goes when the branch becomes live again.
        TextBlockUserData::clearIfdefedOut(second);
        TextBlockUserData::clearIfdefedOut(third);
        view->updateDisabledCode();
        QVERIFY2(drawn().isEmpty(), "the greying outlived the #if that caused it");
    }

    // The languages still in the widget editor whose own factory can be
    // flipped. Entry 69 measured three costs to moving them - Python and JSON
    // indenting differently on Return, .pro commenting in a different place.
    // Re-measured here, and asserted, because a cost recorded once and never
    // rechecked is how this file has been wrong before.
    void testMovingALanguageWouldChangeNothing_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("start");
        QTest::newRow("python") << "m.py" << "def f():\n    x = 1\n";
        QTest::newRow("json") << "m.json" << "{\n    \"a\": 1\n}\n";
        QTest::newRow("qmake project") << "m.pro" << "TEMPLATE = app\n    SOURCES = a.cpp\n";
        // Recognised by its name rather than its suffix, which is why each
        // view gets a directory of its own below instead of a prefix.
        QTest::newRow("cmake") << "CMakeLists.txt" << "project(x)\n    set(A 1)\n";
    }

    void testMovingALanguageWouldChangeNothing()
    {
        QFETCH(QString, name);
        QFETCH(QString, start);

        Utils::TemporaryDirectory dir("move-costs");
        QVERIFY(dir.isValid());

        const auto edited = [&dir, &name, &start](bool quick, QString *out) {
            // A directory per view rather than a prefix on the file: a name
            // like CMakeLists.txt is matched whole, and "w_CMakeLists.txt" is
            // plain text. That prefix is what made entry 69 record cmake as
            // agreeing when neither editor had opened it.
            const Utils::FilePath sub = dir.filePath(QString::fromLatin1(quick ? "quick" : "widget"));
            QVERIFY(sub.ensureWritableDir());
            const Utils::FilePath file = sub / name;
            QVERIFY(file.writeFileContents(start.toUtf8()));
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            QVERIFY(factory);
            const bool was = factory->usesQuickEditor();
            const QScopeGuard restore([factory, was] { factory->setUsesQuickEditor(was); });
            factory->setUsesQuickEditor(quick);

            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            // Flipping the factory has to be what decides the view, or the row
            // is measuring two editors of the same kind.
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            QTextCursor at(document->document());
            at.setPosition(document->document()->findBlockByNumber(1).position() + 4);
            TextEditor::setTextCursorOf(editor, at);
            QObject * const target = TextEditor::keyTargetOf(editor);
            QVERIFY(target);

            for (const QChar ch : QString("\nvalue_here#\nzz_")) {
                if (ch == '#') {
                    if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor))
                        w->unCommentSelection();
                    else if (TextViewport * const v = Internal::viewportForEditor(editor))
                        v->unCommentSelection();
                    continue;
                }
                if (ch == '_') {
                    const QKeySequence seq(QKeySequence::DeleteStartOfWord);
                    QVERIFY(seq.count() > 0);
                    const QKeyCombination combination = seq[0];
                    QKeyEvent press(QEvent::KeyPress, combination.key(),
                                    combination.keyboardModifiers());
                    QCoreApplication::sendEvent(target, &press);
                    continue;
                }
                int key = Qt::Key_unknown;
                QString typed(ch);
                if (ch == '\n') { key = Qt::Key_Return; typed = "\r"; }
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, typed);
                QCoreApplication::sendEvent(target, &press);
            }
            *out = document->plainText();
        };

        QString widgetText;
        QString quickText;
        edited(false, &widgetText);
        edited(true, &quickText);

        // A QVERIFY inside the lambda returns from the lambda alone, so an
        // early bail leaves the string empty and two empties compare equal.
        QVERIFY2(!widgetText.isEmpty(), "the widget editor produced nothing");
        QVERIFY2(!quickText.isEmpty(), "the Qt Quick editor produced nothing");
        QVERIFY2(widgetText != start, "nothing was edited, so this compares nothing");

        QCOMPARE(quickText, widgetText);
    }

    // Something that is not the language putting a button in the toolbar -
    // DevContainer's "restart the container" on a devcontainer.json is the
    // one in the tree. It reached for the widget, so it would have vanished
    // the moment JSON moved.
    void testAnActionPutInTheToolBarArrivesInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAnActionPutInTheToolBarArrivesInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("toolbar-action");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.txt");
        QVERIFY(file.writeFileContents("alpha\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, quick ? Utils::Id(QUICK_TEXT_EDITOR_ID)
                        : Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID));
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Each view is asked where it actually keeps them: the widget editor
        // puts the action straight into its own tool bar, and a view that is
        // not one builds its tool bar from what the document carries.
        const auto shown = [editor, document]() -> QList<QAction *> {
            if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor)) {
                QWidget * const bar = w->toolBar();
                return bar ? bar->actions() : QList<QAction *>();
            }
            return document->toolBarActions();
        };
        const int before = shown().size();

        QAction restart;
        restart.setText("Start or Restart");
        TextEditor::insertExtraToolBarActionIn(editor, TextEditorWidget::Side::Left, &restart);

        const QList<QAction *> after = shown();
        QCOMPARE(after.size(), before + 1);
        QVERIFY2(after.contains(&restart),
                 "the action was not put anywhere this view's toolbar reads");
    }

    // A language whose right-click menu was named only by its widget
    // subclass overriding contextMenuEvent(). The Qt Quick view is handed the
    // factory's menu id instead, so a language that names it in only one of
    // the two places loses the menu when it moves.
    void testALanguagesContextMenuSurvivesMovingToTheQuickView()
    {
        Utils::TemporaryDirectory dir("promenu");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.pro");
        QVERIFY(file.writeFileContents("TEMPLATE = app\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        QCOMPARE(factory->id(), Utils::Id("Qt4.proFileEditor"));
        QVERIFY2(factory->contextMenuId().isValid(),
                 "the .pro factory names no context menu, so a Qt Quick view has none");

        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY(quick->rootObject());
        auto * const model
            = quick->rootObject()->property("contextActions").value<QtcQuick::ActionModel *>();
        QVERIFY2(model, "the form was given no context actions at all");
        model->refresh();

        QStringList texts;
        for (int row = 0; row < model->rowCount({}); ++row) {
            texts << model->data(model->index(row, 0), QtcQuick::ActionModel::TextRole)
                         .toString();
        }
        // The entry qmake puts in its own menu, rather than one every editor
        // has: this is about the language's menu arriving, not about a menu.
        QVERIFY2(Utils::anyOf(texts, [](const QString &t) { return t.contains("Add Library"); }),
                 qPrintable("the language's own entries are missing: " + texts.join(", ")));
    }

    // F1 over a word. A language that knows better than the hover handlers do
    // used to say so on a BaseTextEditor subclass, which only one of the two
    // views is - so moving the language took its help away with it.
    void testTheLanguagesContextHelpAnswersInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheLanguagesContextHelpAnswersInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("context-help");
        QVERIFY(dir.isValid());
        // A directory per view: CMakeLists.txt is matched by name.
        const Utils::FilePath sub = dir.filePath(QString::fromLatin1(quick ? "q" : "w"));
        QVERIFY(sub.ensureWritableDir());
        const Utils::FilePath file = sub / QString("CMakeLists.txt");
        QVERIFY(file.writeFileContents("project(x)\nset(A 1)\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        QCOMPARE(factory->id(), Utils::Id("CMakeProject.CMakeEditor"));
        QVERIFY2(factory->contextHelpProvider(),
                 "the CMake factory offers no context help, so a Qt Quick view has none");

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

        // Inside "set" on the second line, which is a CMake command and so
        // has a page of its own - the word under the caret alone would not
        // tell the two views apart.
        QTextCursor onSet(document->document());
        onSet.setPosition(document->document()->findBlockByNumber(1).position() + 1);
        TextEditor::setTextCursorOf(editor, onSet);

        QStringList answered;
        bool called = false;
        editor->contextHelp([&answered, &called](const Core::HelpItem &item) {
            called = true;
            answered = item.helpIds();
        });
        QTRY_VERIFY2(called, "F1 was never answered at all");
        QVERIFY2(answered.contains("command/set"),
                 qPrintable("the language's own help is missing: " + answered.join(", ")));
    }

    // What a language adds to the tool bar. Python's REPL button and its
    // interpreter picker were QToolButtons put in with
    // insertExtraToolBarWidget(), which only the widget editor has, so they
    // would have vanished when Python moved. As actions carrying menus,
    // either tool bar can draw them.
    void testALanguagesToolBarButtonsArriveInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testALanguagesToolBarButtonsArriveInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("python-toolbar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.py");
        QVERIFY(file.writeFileContents("x = 1\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        QCOMPARE(factory->id(), Utils::Id("PythonEditor.PythonEditor"));

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

        // Each view is asked where it keeps them, as in the toolbar action
        // test: the widget editor puts them straight into its own bar.
        const auto shown = [editor, document]() -> QList<QAction *> {
            if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor)) {
                QToolBar * const bar = w->toolBar();
                return bar ? bar->actions() : QList<QAction *>();
            }
            return document->toolBarActions();
        };

        QAction *repl = nullptr;
        for (QAction * const action : shown()) {
            if (action->text() == "REPL")
                repl = action;
        }
        QVERIFY2(repl, "the REPL button is not in this view's tool bar");
        // It is a menu, not a command: REPL, REPL Import File, REPL Import *.
        QVERIFY2(repl->menu(), "the REPL button carries no menu, so it can only trigger");
        QVERIFY(repl->menu()->actions().size() >= 3);

        // And the interpreter picker, which is built on the first update
        // rather than in the constructor.
        QVERIFY2(Utils::anyOf(shown(), [repl](QAction *a) { return a != repl && a->menu(); }),
                 "the interpreter picker is missing");
    }

    // A Qt Quick text view with no editor behind it. Everything else in this
    // file builds a QuickTextEditor and looks inside it; this asks whether the
    // view stands on its own, which is what an editor made of more than one
    // pane would need - Markdown's text beside its preview.
    // setLanguageSettingsId() is a TextEditorWidget function: a widget is
    // told which language's code style its document should use, and a view
    // that is not one cannot be told. Either view derives it from the mime
    // type instead. C++ is where both happen - CppEditorWidget still names
    // it - so it is where the two answers can be compared.
    void testDerivingACodeStyleAgreesWithBeingToldIt()
    {
        Utils::TemporaryDirectory dir("code-style-either-view");
        QVERIFY(dir.isValid());

        const auto styleIn = [&dir](bool quick, ICodeStylePreferences **style) {
            const Utils::FilePath viewDir
                = dir.filePath(quick ? QString("quick") : QString("widget"));
            QVERIFY(viewDir.ensureWritableDir());
            const Utils::FilePath file = viewDir / "main.cpp";
            QVERIFY(file.writeFileContents("int main() { return 0; }\n"));

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
            *style = document->codeStyle();
        };

        ICodeStylePreferences *told = nullptr;
        ICodeStylePreferences *derived = nullptr;
        styleIn(false, &told);
        styleIn(true, &derived);

        QVERIFY2(told, "the widget editor left the document without a code style");
        QVERIFY2(told != &globalCodeStyle(),
                 "the widget editor used the generic style, so this compares nothing");
        QCOMPARE(derived, told);
    }

    // And the derivation is only as good as the mapping behind it. A language
    // that registered one gets its own style in the Qt Quick view without any
    // widget having named it - which is what lets its widget subclass go.
    void testALanguageThatRegisteredItsMimeTypeGetsItsOwnStyle()
    {
        Utils::TemporaryDirectory dir("registered-code-style");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("module.nim");
        QVERIFY(file.writeFileContents("echo \"hello\"\n"));

        const Utils::Id language
            = TextEditor::languageId(Utils::mimeTypeForFile(file).name());
        QVERIFY2(language.isValid(), "the language registered no mime type mapping");
        ICodeStylePreferencesFactory * const styleFactory = codeStyleFactory(language);
        QVERIFY2(styleFactory, "the language registered no code style factory");
        ICodeStylePreferences * const expected = styleFactory->globalCodeStyle();
        QVERIFY2(expected && expected != &globalCodeStyle(),
                 "the language's style is the generic one, so this asserts nothing");

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor), "it opened in a widget editor");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->codeStyle(), expected);
    }

    void testALanguageWithoutItsOwnHighlighterIsColouredInEitherView_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("CMake") << "CMakeLists.txt" << QByteArray("project(demo)\n");
        QTest::newRow("JSON") << "settings.json" << QByteArray("{\"a\": 1}\n");
    }

    void testALanguageWithoutItsOwnHighlighterIsColouredInEitherView()
    {
        QFETCH(QString, fileName);
        QFETCH(QByteArray, contents);

        Utils::TemporaryDirectory dir("generic-highlighting");
        QVERIFY(dir.isValid());

        const auto definitionIn = [&dir, &fileName, &contents](bool quick, QString *name) {
            // A directory each rather than a prefixed name: these languages
            // are matched on the file name, so two views cannot share one.
            const Utils::FilePath viewDir = dir.filePath(quick ? QString("quick") : QString("widget"));
            QVERIFY(viewDir.ensureWritableDir());
            const Utils::FilePath file = viewDir / fileName;
            QVERIFY(file.writeFileContents(contents));

            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            QVERIFY(factory);
            QVERIFY2(factory->useGenericHighlighter(),
                     "this language does not ask for the generic highlighter");
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
            *name = HighlighterHelper::definitionForDocument(document).name();
        };

        QString widgetDefinition;
        QString quickDefinition;
        definitionIn(false, &widgetDefinition);
        definitionIn(true, &quickDefinition);

        QVERIFY2(!widgetDefinition.isEmpty(),
                 "the widget editor found no definition either, so this compares nothing");
        QCOMPARE(quickDefinition, widgetDefinition);
    }

    void testAViewCanBeBuiltWithoutAnEditor()
    {
        // Text that is in no file and no editor.
        CodeBuffer source;
        source.setText("alpha\nbeta\ngamma\n");
        TextDocument * const document = source.textDocument();
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("alpha\nbeta\ngamma\n"));

        // No context actions: a view that offers no right-click menu is a
        // thing a caller may legitimately want.
        const std::unique_ptr<QWidget> host(Internal::createQuickTextView(&source, nullptr));
        QVERIFY2(host.get(), "no view was built at all");
        host->resize(400, 300);
        // Laying text out needs a window: the view measures against a real
        // font and a real width. Shown and closed again by the guard below.
        host->show();
        const QScopeGuard hideIt([&host] { host->hide(); });

        TextViewport *view = nullptr;
        QTRY_VERIFY2(view = Internal::viewportIn(host.get()),
                     "the widget holds no viewport, so the form never loaded");

        // It is showing that document, and laying it out - not merely holding
        // a pointer to it.
        QCOMPARE(view->textDocument(), document);
        QTRY_VERIFY2(view->lineHeight() > 0, "the view never laid the text out");
        QCOMPARE(view->textDocument()->plainText(), QString("alpha\nbeta\ngamma\n"));

        // And it edits, without an editor to route anything through.
        view->setCursorPosition(0);
        QTextCursor typed = view->textCursor();
        typed.insertText("x");
        view->setTextCursor(typed);
        QCOMPARE(document->plainText(), QString("xalpha\nbeta\ngamma\n"));
    }

    // Ctrl+F reaches an editor by asking its *widget* for an IFindSupport, so
    // whoever builds the widget has to put one there. An editor made of two
    // panes should not have to know that.
    // An editor made of two panes gives each its own context, so that a
    // command registered for one is live only while that pane has focus -
    // Markdown does exactly this with its text and its preview. Whether a Qt
    // Quick view can be one of those panes is this.
    void testTwoViewsEachKeepTheirOwnContext()
    {
        CodeBuffer leftSource;
        leftSource.setText("left\n");
        CodeBuffer rightSource;
        rightSource.setText("right\n");

        // One window holding both, so that focus moves between the panes
        // rather than between windows.
        const std::unique_ptr<QWidget> window(new QWidget);
        auto * const layout = new QHBoxLayout(window.get());
        auto * const leftView = Internal::createQuickTextView(&leftSource, nullptr);
        auto * const rightView = Internal::createQuickTextView(&rightSource, nullptr);
        layout->addWidget(leftView);
        layout->addWidget(rightView);

        const Utils::Id leftId("Test.QuickPane.Left");
        const Utils::Id rightId("Test.QuickPane.Right");
        Core::IContext::attach(leftView, Core::Context(leftId));
        Core::IContext::attach(rightView, Core::Context(rightId));

        window->resize(800, 300);
        window->show();
        const QScopeGuard hideIt([&window] { window->hide(); });
        window->activateWindow();
        QApplication::setActiveWindow(window.get());

        const auto activeContexts = [] {
            QStringList out;
            for (Core::IContext * const context : Core::ICore::currentContextObjects()) {
                for (const Utils::Id id : context->context())
                    out << id.toString();
            }
            return out;
        };

        // The inner QQuickWidget is what a click focuses - the wrapper around
        // it takes no focus of its own - and the context is attached to the
        // wrapper, so this also says the walk up from the focus widget finds
        // it.
        rightView->quickWidget()->setFocus(Qt::MouseFocusReason);
        QTRY_VERIFY2(activeContexts().contains(rightId.toString()),
                     qPrintable("focusing the right pane gave: " + activeContexts().join(", ")));
        QVERIFY2(!activeContexts().contains(leftId.toString()),
                 "the left pane's context was live while the right pane had focus");

        // And they swap, which is what makes them two panes rather than one.
        leftView->quickWidget()->setFocus(Qt::MouseFocusReason);
        QTRY_VERIFY2(activeContexts().contains(leftId.toString()),
                     qPrintable("focusing the left pane gave: " + activeContexts().join(", ")));
        QVERIFY2(!activeContexts().contains(rightId.toString()),
                 "the right pane's context stayed live after focus left it");
    }

    // The row above the view, built for a host that is not the Qt Quick
    // editor. An editor made of two panes wants the same row over its text
    // pane, and should not have to know which QML file it lives in.
    // A button whose label *is* the emphasis - Markdown's italic "i". The
    // action carries the font; the row takes the weight and the slant from it
    // and keeps its own size and family.
    void testAnActionsEmphasisReachesTheQuickToolBar()
    {
        CodeBuffer source;
        source.setText("text\n");
        const std::unique_ptr<QWidget> viewHost(
            Internal::createQuickTextView(&source, nullptr));
        QVERIFY(viewHost.get());
        viewHost->resize(400, 300);
        viewHost->show();
        const QScopeGuard hideView([&viewHost] { viewHost->hide(); });
        TextViewport *view = nullptr;
        QTRY_VERIFY(view = Internal::viewportIn(viewHost.get()));

        QAction glyph("i");
        QtcQuick::ActionModel actions;
        actions.setActions({&glyph});

        const std::unique_ptr<QWidget> bar(
            Internal::createQuickTextToolBar(view, &actions, nullptr, nullptr));
        QVERIFY(bar.get());
        bar->resize(600, 40);
        bar->show();
        const QScopeGuard hideBar([&bar] { bar->hide(); });

        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the row drew nothing for the action");

        const auto drawnFont = [drawn] {
            return drawn->property("emphasisedLabelFont").value<QFont>();
        };

        // Before and after on the same button, so that what is compared is the
        // emphasis and not the row's own type scale - whose weight for this
        // kind of button is already heavy.
        const QFont plain = drawnFont();
        QVERIFY2(!plain.italic(), "the row draws every label italic");

        glyph.setFont([&glyph] { QFont f = glyph.font(); f.setItalic(true); return f; }());
        QTRY_VERIFY2(drawnFont().italic(),
                     "the action turned italic and the row kept drawing it upright");

        // Its emphasis, not its whole font: the size and family are the row's.
        const QFont emphasised = drawnFont();
        QCOMPARE(emphasised.family(), plain.family());
        QCOMPARE(emphasised.pixelSize(), plain.pixelSize());
        QCOMPARE(emphasised.pointSize(), plain.pointSize());
        QCOMPARE(emphasised.weight(), plain.weight());
    }

    // An editor's document is opened by the editor manager before the editor
    // exists, so a view for one cannot own its text the way CodeBuffer does
    // nor open the file the way CodeDocument does. This is the third case, and
    // it is what an editor made of two panes needs for its text pane.
    // Go Back returning to where a jump landed, for an editor that is not the
    // Qt Quick one. The rule is the awkward part: the entry is recorded when
    // the reader *leaves* the destination, not when they arrive on it.
    void testAnyEditorCanRecordWhereAJumpLanded()
    {
        Utils::TemporaryDirectory dir("jump-recorder");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("long.md");
        QString text;
        for (int i = 0; i < 40; ++i)
            text += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(text.toUtf8()));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!qobject_cast<QuickTextEditor *>(editor),
                 "this opened in the Qt Quick editor, so it tests the old path");
        QCOMPARE(Core::EditorManager::currentEditor(), editor);

        auto * const jumps = new JumpRecorder(
            editor, [editor] { return editor->saveState(); });

        // A jump lands, and the reader is still standing on it.
        editor->gotoLine(20, 0);
        QCOMPARE(editor->currentLine(), 20);
        jumps->jumped();

        // Nothing is recorded yet: an entry made on arrival would be in front
        // of the caret that has not moved off it.
        editor->gotoLine(5, 0);
        QCOMPARE(editor->currentLine(), 5);
        Core::EditorManager::goBackInNavigationHistory();
        QVERIFY2(editor->currentLine() != 20,
                 "the jump was recorded on arrival rather than on leaving");

        // Now the same jump, left properly.
        editor->gotoLine(30, 0);
        jumps->jumped();
        editor->gotoLine(8, 0);
        jumps->caretMoved();
        Core::EditorManager::goBackInNavigationHistory();
        QCOMPARE(editor->currentLine(), 30);
    }

    // TextDocument::toolBarActions() calls itself "what a view should ask
    // for". Only one of the two views was asking: a language that describes
    // its tool bar on its document was drawn by the Qt Quick view and ignored
    // by the widget one.
    void testADocumentsToolBarActionIsDrawnByEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testADocumentsToolBarActionIsDrawnByEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("document-toolbar-action");
        QVERIFY(dir.isValid());
        // A .cpp rather than a .txt: plain text is claimed by the Qt Quick
        // factory directly, so flipping the text editor factory's switch would
        // not give a widget editor to compare against.
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main() { return 0; }\n"));

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

        // Where each view actually draws from.
        const auto drawn = [editor, document]() -> QList<QAction *> {
            if (TextEditorWidget * const w = TextEditorWidget::fromEditor(editor)) {
                QWidget * const bar = w->toolBar();
                return bar ? bar->actions() : QList<QAction *>();
            }
            return document->toolBarActions();
        };
        QAction restart("Start or Restart");
        QVERIFY2(!drawn().contains(&restart), "the action was there before it was added");

        // On the document, not through insertExtraToolBarActionIn(), which
        // would put it on the widget and prove nothing.
        document->addToolBarAction(&restart);
        QTRY_VERIFY2(drawn().contains(&restart),
                     "the view draws nothing for an action the document offers");

        // And taking it back takes it out of the row.
        document->removeToolBarAction(&restart);
        QTRY_VERIFY2(!drawn().contains(&restart),
                     "the action stayed in the row after the document dropped it");
    }

    // addOptionalActionsIn() is how a language says what it turned out to be
    // able to do - a language server only knows once it has answered its
    // initialize. It used to reach the Qt Quick editor and nothing else, so an
    // editor made of more than one pane could register commands and never hear
    // that they were answerable.
    void testAnEditorThatIsNeitherKindCanGateItsCommands()
    {
        // Neither a widget editor nor the Qt Quick one, which is what an
        // editor made of two panes is while only one of them is text.
        class PaneEditor final : public Core::IEditor
        {
        public:
            PaneEditor()
                : m_document(new TextDocument)
                , m_widget(new QWidget)
            {
                setWidget(m_widget.get());
            }
            Core::IDocument *document() const final { return m_document.get(); }
            QWidget *toolBar() final { return nullptr; }

        private:
            const std::unique_ptr<TextDocument> m_document;
            const std::unique_ptr<QWidget> m_widget;
        };

        PaneEditor editor;
        QVERIFY2(!TextEditorWidget::fromEditor(&editor),
                 "this editor has a widget pane, so the widget answer would win");

        QAction follow;
        auto * const gate = new OptionalActionGate(
            &editor, []() -> TextViewport * { return nullptr; });
        gate->gate(&follow, OptionalActions::FollowSymbolUnderCursor);
        gate->update();
        QVERIFY2(!follow.isEnabled(),
                 "a command nobody has answered for was offered anyway");

        // A different bit does not turn it on, or the assertion below would
        // pass for anything at all.
        TextEditor::addOptionalActionsIn(&editor, OptionalActions::FindUsage);
        QVERIFY2(!follow.isEnabled(), "an unrelated answer enabled the command");

        TextEditor::addOptionalActionsIn(&editor, OptionalActions::FollowSymbolUnderCursor);
        QVERIFY2(follow.isEnabled(),
                 "the language's answer never reached this editor's gate");
    }

    void testAViewCanBeBuiltOverADocumentAnEditorOwns()
    {
        Utils::TemporaryDirectory dir("adopted-document");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("shared.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        const std::unique_ptr<QWidget> host(
            Internal::createQuickTextViewOver(document->sharedFromThis(), nullptr));
        QVERIFY2(host.get(), "no view was built over the editor's document");
        host->resize(400, 300);
        host->show();
        const QScopeGuard hideIt([&host] { host->hide(); });

        TextViewport *view = nullptr;
        QTRY_VERIFY2(view = Internal::viewportIn(host.get()),
                     "the widget holds no viewport, so the form never loaded");

        // The source belongs to the view, so a caller holding only the widget
        // is holding everything.
        QVERIFY2(host->findChild<CodeSource *>(),
                 "the source is not owned by the view, so it leaks or dangles");

        // The editor's document, not a copy of its text.
        QCOMPARE(view->textDocument(), document);
        QCOMPARE(view->textDocument()->plainText(), QString("alpha\nbeta\n"));

        // And editing through this view is editing that document, which is
        // what makes two panes over one file two views rather than two copies.
        QVERIFY(!document->isModified());
        view->setCursorPosition(0);
        QTextCursor typed = view->textCursor();
        typed.insertText("x");
        view->setTextCursor(typed);
        QCOMPARE(document->plainText(), QString("xalpha\nbeta\n"));
        QVERIFY2(document->isModified(),
                 "the editor's document did not notice the edit");
    }

    void testAToolBarCanBeBuiltWithoutAnEditor()
    {
        CodeBuffer source;
        source.setText("alpha\nbeta\n");
        const std::unique_ptr<QWidget> viewHost(
            Internal::createQuickTextView(&source, nullptr));
        QVERIFY(viewHost.get());
        viewHost->resize(400, 300);
        viewHost->show();
        const QScopeGuard hideView([&viewHost] { viewHost->hide(); });
        TextViewport *view = nullptr;
        QTRY_VERIFY2(view = Internal::viewportIn(viewHost.get()),
                     "the view never loaded, so there is nothing to put a row above");

        // One action, the way a language offers one.
        QAction bold("Bold");
        QtcQuick::ActionModel actions;
        actions.setActions({&bold});

        // No outline and no choice: a caller may legitimately have neither.
        const std::unique_ptr<QWidget> bar(
            Internal::createQuickTextToolBar(view, &actions, nullptr, nullptr));
        QVERIFY2(bar.get(), "no toolbar was built at all");
        bar->resize(600, 40);
        bar->show();
        const QScopeGuard hideBar([&bar] { bar->hide(); });

        auto * const quick = bar->findChild<QQuickWidget *>();
        QVERIFY(quick);
        QQuickItem *drawn = nullptr;
        QTRY_VERIFY2((drawn = itemNamed(quick->rootObject(), "languageToolBarButton")),
                     "the row drew nothing for the action it was given");
        QCOMPARE(drawn->property("text").toString(), QString("Bold"));

        // And pressing it runs that action, so the row is wired to what it
        // was handed rather than merely drawing its text.
        QSignalSpy fired(&bold, &QAction::triggered);
        QMetaObject::invokeMethod(drawn, "clicked");
        QTRY_VERIFY2(!fired.isEmpty(), "the button drew but did nothing");
    }

    void testAViewBuiltWithoutAnEditorCanBeSearched()
    {
        CodeBuffer source;
        source.setText("alpha\nbeta\nalpha again\n");
        QVERIFY(source.textDocument());

        const std::unique_ptr<QWidget> host(Internal::createQuickTextView(&source, nullptr));
        QVERIFY(host.get());
        host->resize(400, 300);
        host->show();
        const QScopeGuard hideIt([&host] { host->hide(); });

        TextViewport *view = nullptr;
        QTRY_VERIFY2(view = Internal::viewportIn(host.get()),
                     "the widget holds no viewport, so the form never loaded");

        auto * const find = Utils::Aggregation::query<Core::IFindSupport>(host.get());
        QVERIFY2(find, "nothing on the view's widget answers Ctrl+F");

        // Found and not found, so that the answer is the search rather than a
        // stub that always agrees.
        QCOMPARE(find->findStep("beta", {}), Core::IFindSupport::Found);
        QCOMPARE(find->findStep("nowhere", {}), Core::IFindSupport::NotFound);

        // And it moved the caret to what it found rather than only saying so.
        QCOMPARE(find->findStep("alpha again", {}), Core::IFindSupport::Found);
        QCOMPARE(view->textCursor().selectedText(), QString("alpha again"));
    }

    void testAWatcherHearsTheCaretMoveInEitherView()
    {
        Utils::TemporaryDirectory dir("caret-watcher");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });

        // The same watcher, over both views, so that a caller which has only
        // an IEditor can be written once.
        for (const bool quick : {false, true}) {
            factory->setUsesQuickEditor(quick);
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);

            int moves = 0;
            const QMetaObject::Connection conn
                = TextEditor::whenCursorMoved(editor, this, [&moves] { ++moves; });
            QVERIFY2(conn, quick ? "no watcher on the Quick view" : "no watcher on the widget");

            QTextCursor to(document->document());
            to.setPosition(document->document()->findBlockByNumber(2).position());
            TextEditor::setTextCursorOf(editor, to);
            QTRY_VERIFY(moves > 0);

            // And it stops when the caller says so, which is what a watcher
            // that can be turned off needs the connection back for.
            QObject::disconnect(conn);
            const int afterDisconnect = moves;
            QTextCursor back(document->document());
            back.setPosition(0);
            TextEditor::setTextCursorOf(editor, back);
            QCOMPARE(TextEditor::lineColumnOf(editor).line, 1);
            QCOMPARE(moves, afterDisconnect);
        }
    }

    void testTheSearchScopeIsDrawn()
    {
        Utils::TemporaryDirectory dir("find-scope-drawn");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("x one\nx two\nx three\nx four\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);

        auto * const find = Utils::Aggregation::query<Core::IFindSupport>(editor->widget());
        QVERIFY(find);

        const Utils::Id kind("TextEditor.QuickTextEditor.FindScope");
        const auto drawn = [view, kind] { return view->highlights(kind); };
        QVERIFY2(drawn().isEmpty(), "a scope was drawn before one was defined");

        QTextCursor scope(document->document());
        scope.setPosition(6);
        scope.setPosition(20, QTextCursor::KeepAnchor);
        TextEditor::setTextCursorOf(editor, scope);
        find->defineFindScope();

        QCOMPARE(drawn().size(), 1);
        QCOMPARE(drawn().first().start, 6);
        QCOMPARE(drawn().first().end, 20);
        QCOMPARE(drawn().first().format,
                 document->fontSettings().toTextCharFormat(C_SEARCH_SCOPE));

        // And it goes when the search does.
        find->clearFindScope();
        QVERIFY2(drawn().isEmpty(), "the scope outlived the search it was for");
    }

    void testWhatIsCopiedGoesOnTheClipboardHistory_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Circular Paste offers the things copied before this one, and a widget
    // editor puts every copy and cut on that list as it happens. This view put
    // nothing there, so the list only ever held what Circular Paste itself had
    // collected - the reader's own copies were not among the things offered.
    void testWhatIsCopiedGoesOnTheClipboardHistory()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("clipboard-history");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("one two three\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // One list for the whole program, so a count only means something
        // from a state this test set.
        Internal::CircularClipboard * const history = Internal::CircularClipboard::instance();
        history->clear();
        QCOMPARE(history->size(), 0);

        const auto copyRange = [&](int from, int to) {
            QTextCursor pick(document->document());
            pick.setPosition(from);
            pick.setPosition(to, QTextCursor::KeepAnchor);
            TextEditor::setTextCursorOf(editor, pick);
            if (quick)
                Internal::viewportForEditor(editor)->copy();
            else
                TextEditorWidget::fromEditor(editor)->copy();
        };

        copyRange(0, 3);
        QCOMPARE(history->size(), 1);

        // A second, different thing: the list is what Circular Paste chooses
        // between, so it has to have both on it.
        copyRange(4, 7);
        QCOMPARE(history->size(), 2);

        // The same thing again is not a second entry - it moves to the front.
        copyRange(0, 3);
        QCOMPARE(history->size(), 2);
    }

    // The other half of what an editor remembers. With wrapping off - which is
    // the default - a long line runs off the right, and where the reader had
    // scrolled to is as much a part of where they were as which line it was.
    // The widget editor writes it into its state:
    //
    //     stream << verticalScrollBar()->value();
    //     stream << horizontalScrollBar()->value();
    //
    // and this one wrote only the downward half, so a reload came back at the
    // left margin.
    //
    // One editor rather than the usual two rows. Driving a widget editor
    // sideways needs a gesture this harness cannot make stick: its horizontal
    // scroll bar has range, but setting a value on it is undone again before
    // anything reads it, because the caret is at the left and the view keeps
    // going back to it. The widget's half is read from the two lines above
    // instead of driven, and said so here rather than asserted badly.
    void testTheSidewaysScrollComesBackToo()
    {
        Utils::TemporaryDirectory dir("sideways-state");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("wide.cpp");
        // One line far wider than any window, so there is somewhere to scroll.
        QString wide = "int x = ";
        for (int i = 0; i < 200; ++i)
            wide += QString("someLongIdentifier%1 + ").arg(i);
        QVERIFY(file.writeFileContents((wide + "0;\n").toUtf8()));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY2(view, "the file opened in a widget editor, so this tests nothing");
        QTRY_VERIFY(view->visibleLineCount() > 0);

        // The caret stays at the left while the view is scrolled away from it:
        // that is what tells restoring the scroll apart from restoring the
        // caret and letting the view follow it there.
        view->setCursorPosition(0);
        QTRY_COMPARE(view->scrollX(), qreal(0));
        view->setScrollX(300);
        QTRY_VERIFY2(view->scrollX() > 0, "there was nowhere to scroll sideways");
        const qreal was = view->scrollX();

        const QByteArray state = editor->saveState();
        QVERIFY(!state.isEmpty());

        view->setScrollX(0);
        QTRY_COMPARE(view->scrollX(), qreal(0));

        editor->restoreState(state);
        QTRY_COMPARE(view->scrollX(), was);
    }

    void testTheEditorSaysWhenTheSelectionChanges_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // A selection can change without the caret moving - something selects the
    // word the caret is already at the end of - and whoever is following the
    // editor has to hear about it. IEditor said when the caret moved and
    // nothing else, so anyone who wanted the other half reached for the
    // widget's own signal and got nothing in this view.
    void testTheEditorSaysWhenTheSelectionChanges()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("editor-selection-signal");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("selected.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        // The caret at the end of "alpha", with nothing selected.
        editor->gotoLine(1, 9);
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 9);
        QVERIFY(TextEditor::textCursorOf(editor).selectedText().isEmpty());

        QSignalSpy selection(editor, &Core::IEditor::selectionChanged);

        // Select back over "alpha" without moving the caret: the anchor moves
        // and the position does not, which is the case that has no signal of
        // its own.
        QTextCursor selecting = TextEditor::textCursorOf(editor);
        selecting.setPosition(4);
        selecting.setPosition(9, QTextCursor::KeepAnchor);
        TextEditor::setTextCursorOf(editor, selecting);

        QCOMPARE(TextEditor::textCursorOf(editor).selectedText(), QString("alpha"));
        QCOMPARE(TextEditor::textCursorOf(editor).position(), 9);
        QTRY_VERIFY2(selection.count() > 0, "the editor never said the selection changed");

        // Note what is *not* asserted here: that cursorPositionChanged did not
        // fire. A widget editor emits it for any change to its cursor, an
        // anchor-only one included, so its count says nothing about whether
        // the caret moved. What says so is the position above, which is 9
        // before and after.
    }

    void testASearchResultOpensWithTheMatchSelected_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Clicking a hit in the Search Results pane - Find in Files, Find Usages,
    // a grep - opens the file and *selects the match*, so it stands out
    // instead of leaving a caret at its start. The editor manager does that by
    // asking the editor's widget for a Utils::PlainTextEdit, which a Qt Quick
    // editor has none of, so every hit in a C++ file arrived unselected.
    void testASearchResultOpensWithTheMatchSelected()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("search-result-selection");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hit.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint needle = 2;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        // "needle" on the second line, at column four, six characters long.
        Utils::SearchResultItem item;
        item.setFilePath(file);
        item.setLineText("int needle = 2;");
        item.setMainRange(2, 4, 6);

        Core::EditorManager::openEditorAtSearchResult(item);
        Core::IEditor * const editor = Core::EditorManager::currentEditor();
        QVERIFY(editor);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);

        QTRY_COMPARE(TextEditor::textCursorOf(editor).selectedText(), QString("needle"));
    }

    void testFoldsSurviveASaveAndRestore_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // What an editor remembers is what comes back after the file is reloaded,
    // after a session is restored, and in the second half of a split. The
    // widget editor writes the folded blocks into its state; the Qt Quick one
    // wrote a caret and a scroll offset, so every fold was lost.
    void testFoldsSurviveASaveAndRestore()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("fold-state");
        QVERIFY(dir.isValid());
        // A C++ file rather than plain text: the Qt Quick factory claims
        // text/plain ahead of the plain text editor, so a .txt opens in this
        // view whatever a TextEditorFactory says, and the widget row would
        // measure the wrong editor.
        const Utils::FilePath file = dir.filePath("folded.cpp");
        QVERIFY(file.writeFileContents("void f()\n{\n    int a;\n    int b;\n    int c;\n}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(bool(Internal::viewportForEditor(editor)), quick);

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QTextDocument * const text = document->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);

        // Whichever line the highlighter says is foldable - which one that is
        // depends on the language, and this test is about what is remembered
        // rather than about where a fold starts.
        int foldable = -1;
        QTRY_VERIFY([&] {
            for (QTextBlock b = text->firstBlock(); b.isValid(); b = b.next()) {
                if (TextBlockUserData::canFold(b)) {
                    foldable = b.blockNumber();
                    return true;
                }
            }
            return false;
        }());

        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(foldable), /*unfold=*/false);
        layout->requestUpdate();
        QVERIFY2(TextBlockUserData::isFolded(text->findBlockByNumber(foldable)),
                 "the fixture did not fold anything");

        const QByteArray state = editor->saveState();
        QVERIFY2(!state.isEmpty(), "the editor remembered nothing at all");

        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(foldable), /*unfold=*/true);
        layout->requestUpdate();
        QVERIFY2(!TextBlockUserData::isFolded(text->findBlockByNumber(foldable)),
                 "the fold was not undone, so this proves nothing");

        editor->restoreState(state);
        QTRY_VERIFY2(TextBlockUserData::isFolded(text->findBlockByNumber(foldable)),
                     "the fold did not come back");

        // And a state written before the folds were in it is refused rather
        // than read as though it had them: the fields after the version differ
        // now, so misreading one would put the caret somewhere arbitrary.
        if (quick) {
            editor->gotoLine(1, 1);
            const int before = TextEditor::textCursorOf(editor).position();
            QByteArray older;
            QDataStream out(&older, QIODevice::WriteOnly);
            out << 1 << 12 << qreal(34);
            editor->restoreState(older);
            QCOMPARE(TextEditor::textCursorOf(editor).position(), before);
        }
    }

    void testDoubleClickTakesTheWordUnderIt_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<int>("position");
        QTest::addColumn<QString>("selected");

        // "void f(int a) { g(1); }" then "  x  y" then an empty line: a word,
        // a word touching a bracket, a lone brace, a digit, a run of spaces
        // between two words, and the words either side of it.
        const QList<std::tuple<QString, int, QString>> places{
            {"inside a word", 1, "void"},
            {"just after an opening bracket", 7, "int"},
            {"inside the word after it", 8, "int"},
            {"on a brace", 15, "{"},
            {"on a digit", 18, "1"},
            {"between two words", 25, "  "},
            {"on a word after leading spaces", 27, "x"},
            {"on the last word of a line", 30, "y"},
        };
        for (const auto &[what, at, selected] : places) {
            QTest::newRow(qPrintable(QString("widget: %1").arg(what)))
                << false << at << selected;
            QTest::newRow(qPrintable(QString("quick: %1").arg(what))) << true << at << selected;
        }
    }

    // What a double click takes. A widget editor gets it from
    // PlainTextEdit and then corrects the case of a click between two
    // spaces, which selects the word *before* them or nothing at all; this
    // view does the correction in selectWordAt(), which is what its QML calls.
    // Nothing had compared the two.
    void testDoubleClickTakesTheWordUnderIt()
    {
        QFETCH(bool, quick);
        QFETCH(int, position);
        QFETCH(QString, selected);

        Utils::TemporaryDirectory dir("double-click-word");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("void f(int a) { g(1); }\n  x  y\n\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // The caret goes there first, which is what the click before the
        // second one does.
        QTextCursor caret(document->document());
        caret.setPosition(position);
        TextEditor::setTextCursorOf(editor, caret);

        if (quick) {
            // What CodeViewport.qml calls when a double tap arrives. QTest
            // cannot double-click a Quick item, and driving the QML handler is
            // not what is being compared anyway.
            TextViewport * const view = Internal::viewportForEditor(editor);
            QVERIFY(view);
            view->selectWordAt(position);
        } else {
            TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
            QVERIFY(widget);
            const QPoint at = widget->viewport()->mapFromGlobal(
                widget->cursorRect(position).center());
            QTest::mouseDClick(widget->viewport(), Qt::LeftButton, {}, at);
        }

        QCOMPARE(TextEditor::textCursorOf(editor).selectedText(), selected);
    }

    void testRedoPutsBackWhatUndoTookAway_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<QString>("script");
        QTest::addColumn<int>("steps");
        const QList<std::tuple<QString, QString, int>> scripts{
            {"a word", "abc", 1},
            {"a word, a space, a word", "abc def", 1},
            {"an auto-closed bracket", "f(", 2},
            {"typing, moving, typing", "abc<Home>x", 2},
        };
        for (const auto &[what, script, steps] : scripts) {
            QTest::newRow(qPrintable(QString("widget: %1").arg(what))) << false << script << steps;
            QTest::newRow(qPrintable(QString("quick: %1").arg(what))) << true << script << steps;
        }
    }

    // Redo is undo read backwards and had no test at all. It has to come back
    // in the same steps, or the grouping the entry before this one gave undo
    // would be true one way round and not the other.
    void testRedoPutsBackWhatUndoTookAway()
    {
        QFETCH(bool, quick);
        QFETCH(QString, script);
        QFETCH(int, steps);

        Utils::TemporaryDirectory dir("redo-steps");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        for (int i = 0; i < script.size(); ++i) {
            const QChar ch = script.at(i);
            int key = Qt::Key_unknown;
            if (ch == '<') {
                const int close = script.indexOf('>', i);
                QVERIFY2(close > i, qPrintable("unterminated <> in " + script));
                QCOMPARE(script.mid(i + 1, close - i - 1), QString("Home"));
                key = Qt::Key_Home;
                i = close;
            }
            const QString text = key == Qt::Key_unknown ? QString(ch) : QString();
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &press);
        }
        const QString typed = document->plainText();
        QVERIFY(!typed.isEmpty());

        const auto view = [editor] { return Internal::viewportForEditor(editor); };
        const auto widget = [editor] { return TextEditorWidget::fromEditor(editor); };

        int undos = 0;
        while (document->document()->isUndoAvailable() && undos < 20) {
            quick ? view()->undo() : widget()->undo();
            ++undos;
        }
        QCOMPARE(undos, steps);
        QCOMPARE(document->plainText(), QString());

        int redos = 0;
        while (document->document()->isRedoAvailable() && redos < 20) {
            quick ? view()->redo() : widget()->redo();
            ++redos;
        }
        QCOMPARE(redos, steps);
        QCOMPARE(document->plainText(), typed);
    }

    void testTheClipboardSeesEveryCaret_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Several carets select several runs of text, and the clipboard is one
    // thing. A widget editor puts them all on it a line each, takes them all
    // away on a cut, and hands a line back to each caret on a paste. This view
    // asked textCursor() for all three, so it saw only the main caret: copying
    // a column of names copied one name.
    void testTheClipboardSeesEveryCaret()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("clipboard-carets");
        QVERIFY(dir.isValid());

        const auto openOne = [&](const QString &name, const QString &content) {
            const Utils::FilePath file = dir.filePath(name);
            if (!file.writeFileContents(content.toUtf8()))
                return static_cast<Core::IEditor *>(nullptr);
            TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
            if (!factory)
                return static_cast<Core::IEditor *>(nullptr);
            factory->setUsesQuickEditor(quick);
            return Core::EditorManager::openEditor(file);
        };
        TextEditorFactory * const factory
            = TextEditorFactory::preferredFactoryFor(dir.filePath("t.cpp"));
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });

        // Each view's own operation. cutIn()/pasteIn() exist as seams because
        // an EditHandler needs them; copy has none, so all three are called
        // the same way here rather than two of one kind and one of another.
        const auto copyFrom = [quick](Core::IEditor *editor) {
            quick ? Internal::viewportForEditor(editor)->copy()
                  : TextEditorWidget::fromEditor(editor)->copy();
        };
        const auto cutFrom = [quick](Core::IEditor *editor) {
            quick ? Internal::viewportForEditor(editor)->cutNormally()
                  : TextEditorWidget::fromEditor(editor)->TextEditorWidget::cut();
        };
        const auto pasteInto = [quick](Core::IEditor *editor) {
            quick ? Internal::viewportForEditor(editor)->pasteNormally()
                  : TextEditorWidget::fromEditor(editor)->TextEditorWidget::paste();
        };

        const auto threeSelections = [](Core::IEditor *editor, TextDocument *document) {
            Utils::MultiTextCursor cursors;
            for (const int at : {0, 6, 11}) {
                QTextCursor one(document->document());
                one.setPosition(at);
                one.setPosition(at + 4, QTextCursor::KeepAnchor);
                cursors.addCursor(one);
            }
            TextEditor::setMultiTextCursorOf(editor, cursors);
        };

        // Copied: all three, a line each.
        {
            Core::IEditor * const editor = openOne("copy.cpp", "alpha\nbeta\ngamma\n");
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);
            threeSelections(editor, document);
            QGuiApplication::clipboard()->clear();
            copyFrom(editor);
            QCOMPARE(QGuiApplication::clipboard()->text(), QString("alph\nbeta\ngamm"));
        }

        // Cut: all three go.
        {
            Core::IEditor * const editor = openOne("cut.cpp", "alpha\nbeta\ngamma\n");
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);
            threeSelections(editor, document);
            cutFrom(editor);
            QCOMPARE(document->plainText(), QString("a\n\na\n"));
        }

        // Pasted: as many lines as carets means one line each.
        {
            Core::IEditor * const editor = openOne("paste.cpp", "alpha\nbeta\ngamma\n");
            QVERIFY(editor);
            const QScopeGuard closeIt(
                [editor] { Core::EditorManager::closeEditors({editor}, false); });
            auto * const document = qobject_cast<TextDocument *>(editor->document());
            QVERIFY(document);
            Utils::MultiTextCursor cursors;
            for (const int at : {5, 10, 16}) {
                QTextCursor one(document->document());
                one.setPosition(at);
                cursors.addCursor(one);
            }
            TextEditor::setMultiTextCursorOf(editor, cursors);
            QGuiApplication::clipboard()->setText("1\n2\n3");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("1\n2\n3"));
            pasteInto(editor);
            QCOMPARE(document->plainText(), QString("alpha1\nbeta2\ngamma3\n"));
        }
    }

    void testAPastedBlockIsLaidOutWhereItLands_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Text on the clipboard carries the indentation of wherever it was
    // written, which is not where it is going. A widget editor hands a paste
    // to the language's indenter; this view put it in at the column it was
    // copied from, so a block pasted into a nested one started at column zero.
    void testAPastedBlockIsLaidOutWhereItLands()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("paste-indent");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("void f() {\n    x;\n}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // Just after "    x", inside the braces.
        QTextCursor caret(document->document());
        caret.setPosition(15);
        TextEditor::setTextCursorOf(editor, caret);

        // Written at column zero, as it would be if it came from elsewhere.
        QGuiApplication::clipboard()->setText("\nif (a) {\ny;\n}");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("\nif (a) {\ny;\n}"));
        if (quick)
            Internal::viewportForEditor(editor)->pasteNormally();
        else
            TextEditorWidget::fromEditor(editor)->TextEditorWidget::paste();

        QCOMPARE(document->plainText(),
                 QString("void f() {\n    \n    if (a) {\n    y;\n    }x;\n}\n"));
    }

    void testTypingIsTakenBackInTheStepsThatMadeIt_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<QString>("script");
        QTest::addColumn<int>("steps");

        // "\n" is Return and "\b" is Backspace; <Home> moves the caret.
        // Both views expect the same count, so the widget row is the control
        // on what the count should be.
        const QList<std::tuple<QString, QString, int>> scripts{
            {"a word", "abc", 1},
            {"a word, a space, a word", "abc def", 1},
            {"an auto-closed bracket", "f(", 2},
            {"typing, moving, typing", "abc<Home>x", 2},
            {"a backspace", "abc\b", 2},
        };
        for (const auto &[what, script, steps] : scripts) {
            QTest::newRow(qPrintable(QString("widget: %1").arg(what))) << false << script << steps;
            QTest::newRow(qPrintable(QString("quick: %1").arg(what))) << true << script << steps;
        }
    }

    // Ctrl+Z takes back a thing the reader did, not an edit the editor made.
    // A widget editor gets that from QTextDocument, which joins consecutive
    // typing into one undo step - and it only stops joining where the editor
    // opens an edit block, which it does when a keystroke turns into more than
    // one edit. This view opened one round every keystroke, so taking back a
    // word took a press per letter.
    void testTypingIsTakenBackInTheStepsThatMadeIt()
    {
        QFETCH(bool, quick);
        QFETCH(QString, script);
        QFETCH(int, steps);

        Utils::TemporaryDirectory dir("undo-grouping");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        for (int i = 0; i < script.size(); ++i) {
            const QChar ch = script.at(i);
            int key = Qt::Key_unknown;
            if (ch == '<') {
                const int close = script.indexOf('>', i);
                QVERIFY2(close > i, qPrintable("unterminated <> in " + script));
                const QString name = script.mid(i + 1, close - i - 1);
                i = close;
                if (name == "Home")
                    key = Qt::Key_Home;
                else
                    QFAIL(qPrintable("no such key in a script: " + name));
            } else if (ch == '\n') {
                key = Qt::Key_Return;
            } else if (ch == '\b') {
                key = Qt::Key_Backspace;
            }
            const QString text = key == Qt::Key_unknown ? QString(ch) : QString();
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &press);
        }
        QVERIFY2(!document->document()->toPlainText().isEmpty(),
                 "the script typed nothing, so counting its undo steps proves nothing");

        int taken = 0;
        while (document->document()->isUndoAvailable() && taken < 20) {
            if (quick)
                Internal::viewportForEditor(editor)->undo();
            else
                TextEditorWidget::fromEditor(editor)->undo();
            ++taken;
        }
        QCOMPARE(taken, steps);
        QCOMPARE(document->document()->toPlainText(), QString());
    }

    void testBreakingALineComesBackWithWhatWasTypedAfterIt_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Breaking a line is one thing the reader did even though the language
    // indents the new line afterwards, and it is a *different* thing from the
    // typing on either side of it. Both halves of that are the edit block:
    // without one, the break and the indent come back separately; with one
    // round every keystroke instead, the letters before it come back one at a
    // time.
    void testBreakingALineComesBackWithWhatWasTypedAfterIt()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("undo-break");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        // Braces, so that the language has something to indent the new line
        // to. On a flat file the break is a single edit and this proves
        // nothing.
        QVERIFY(file.writeFileContents("void f() {}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        QTextCursor caret(document->document());
        caret.setPosition(10);
        TextEditor::setTextCursorOf(editor, caret);

        const auto type = [target](const QString &text) {
            for (const QChar ch : text) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
                QCoreApplication::sendEvent(target, &press);
            }
        };
        type("abc");
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        type("x");
        // Both views produce this, and it is what makes the case worth
        // counting: the break came with an indent, so it is more than one edit.
        QCOMPARE(document->plainText(), QString("void f() {abc\n         x}\n"));

        const auto undoOnce = [editor, quick] {
            if (quick)
                Internal::viewportForEditor(editor)->undo();
            else
                TextEditorWidget::fromEditor(editor)->undo();
        };

        // One press takes back the break and the indent and the "x" together,
        // because they are what the reader last did.
        undoOnce();
        QCOMPARE(document->plainText(), QString("void f() {abc}\n"));

        // And the next takes back the word, not a letter of it.
        undoOnce();
        QCOMPARE(document->plainText(), QString("void f() {}\n"));
        QVERIFY2(!document->document()->isUndoAvailable(),
                 "there was more to take back than the reader did");
    }

    void testACommandIsOneThingToTakeBack_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // The other side of the same rule: a command that takes several edits is
    // still one thing the reader did. Leaving the edit block out where there
    // is nothing to group must not reach these.
    void testACommandIsOneThingToTakeBack()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("undo-commands");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = quick ? Internal::viewportForEditor(editor) : nullptr;
        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        QVERIFY(view || widget);

        const QList<std::pair<QString, std::function<void()>>> commands{
            {"delete a line", [&] { view ? view->deleteLine() : widget->deleteLine(); }},
            {"move a line down", [&] { view ? view->moveLineDown() : widget->moveLineDown(); }},
            {"duplicate a selection",
             [&] { view ? view->duplicateSelection() : widget->duplicateSelection(); }},
            {"indent", [&] { view ? view->indent() : widget->indent(); }},
            {"insert a line below",
             [&] { view ? view->insertLineBelow() : widget->insertLineBelow(); }},
            {"upper-case a selection",
             [&] { view ? view->uppercaseSelection() : widget->uppercaseSelection(); }},
            // Breaking a line is the break plus whatever the language indents
            // afterwards, and it comes through a key rather than a command.
            {"break a line", [&] {
                 QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                 QCoreApplication::sendEvent(TextEditor::keyTargetOf(editor), &press); }},
        };

        for (const auto &[what, run] : commands) {
            // "beta", so that the ones working on a selection have one.
            QTextCursor caret(document->document());
            caret.setPosition(6);
            caret.setPosition(10, QTextCursor::KeepAnchor);
            TextEditor::setTextCursorOf(editor, caret);

            const QString before = document->document()->toPlainText();
            run();
            QVERIFY2(document->document()->toPlainText() != before,
                     qPrintable(what + " changed nothing, so it has nothing to take back"));

            int taken = 0;
            while (document->document()->isUndoAvailable() && taken < 20) {
                if (view)
                    view->undo();
                else
                    widget->undo();
                ++taken;
            }
            QCOMPARE(taken, 1);
            QCOMPARE(document->document()->toPlainText(), before);
        }
    }

    void testASuggestionEndsWhenTheReaderGoesElsewhere_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // A suggestion is an offer about what the reader was going to type next,
    // so it stops meaning anything once they are typing somewhere else. A
    // widget editor drops it in focusOutEvent(); this view had no focus
    // handling at all and went on offering it.
    void testASuggestionEndsWhenTheReaderGoesElsewhere()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("suggestion-focus");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("f()\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        TextViewport * const view = quick ? viewportForEditor(editor) : nullptr;
        QVERIFY(widget || view);

        auto suggestion = std::make_unique<CyclicSuggestion>(
            QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "f()x"}},
            document->document(), 0);
        if (widget) {
            widget->setFocus();
            widget->insertSuggestion(std::move(suggestion));
        } else {
            view->forceActiveFocus();
            QVERIFY2(view->hasActiveFocus(), "the viewport never took focus");
            view->insertSuggestion(std::move(suggestion));
        }
        QVERIFY2(TextEditor::currentSuggestionIn(editor), "nothing was offered to begin with");

        if (widget) {
            // Delivered rather than arranged: this widget lives in a main
            // window that is not active during a test, so it never had the
            // focus to lose - and focusOutEvent() is the code being compared.
            QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(widget, &out);
        } else {
            view->setFocus(false);
            QVERIFY(!view->hasActiveFocus());
        }
        QCoreApplication::processEvents();
        QVERIFY2(!TextEditor::currentSuggestionIn(editor),
                 "the suggestion was still being offered after the reader left");
    }

    // The exception: a tooltip taking the focus is not the reader going
    // anywhere, so what is on offer stays on offer. Without it, reading the
    // tooltip about a suggestion would be what dismissed it.
    //
    // The reference is TextEditorWidget::focusOutEvent(), which asks
    // ToolTip::isVisible() the same way, but there is no widget row here: that
    // widget never has the focus during a test, and a focus-out delivered by
    // hand goes through PlainTextEdit::focusOutEvent() first, which takes the
    // tooltip down before the question is asked.
    void testASuggestionOutlivesTheTooltipThatTookTheFocus()
    {
        Utils::TemporaryDirectory dir("suggestion-tooltip");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("f()\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        const QScopeGuard putTheTooltipAway([] { Utils::ToolTip::hideImmediately(); });

        const auto offer = [&] {
            auto suggestion = std::make_unique<CyclicSuggestion>(
                QList<TextSuggestion::Data>{{{{1, 0}, {1, 3}}, {1, 3}, "f()x"}},
                document->document(), 0);
            view->forceActiveFocus();
            view->insertSuggestion(std::move(suggestion));
        };
        const auto goElsewhere = [&] {
            QVERIFY(view->hasActiveFocus());
            view->setFocus(false);
            QCoreApplication::processEvents();
        };

        offer();
        QVERIFY(TextEditor::currentSuggestionIn(editor));

        Utils::ToolTip::show(QPoint(0, 0), QString("what this suggestion is"),
                             view->tooltipParent());
        // A guard, not a step: with no tooltip up this test would pass whatever
        // the exception did.
        QVERIFY2(Utils::ToolTip::isVisible(), "no tooltip was shown, so nothing is being excepted");

        goElsewhere();
        QVERIFY2(TextEditor::currentSuggestionIn(editor),
                 "the tooltip about the suggestion is what dismissed it");

        // And once it is gone, going elsewhere means what it usually means.
        // hideImmediately(), because hide() is on a timer and the check below
        // would still see a tooltip up.
        Utils::ToolTip::hideImmediately();
        QVERIFY(!Utils::ToolTip::isVisible());
        // Focused again first: dropping focus that is already gone changes
        // nothing and would leave this passing for the wrong reason.
        view->forceActiveFocus();
        goElsewhere();
        QVERIFY2(!TextEditor::currentSuggestionIn(editor),
                 "with the tooltip gone the suggestion was still offered");
    }

    void testWhatTheCompleterPutInStopsBeingNewWhenTheReaderLeaves_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // The bracket the completer closed for you is not text you wrote, and both
    // editors keep a range saying so: it is what a typed ")" steps over
    // instead of doubling, and what gets drawn differently while it is still
    // the last thing that happened. It stops being that the moment the reader
    // goes somewhere else - typing into it does not count, because the text is
    // pushed along and the caret stays at the front of it.
    void testWhatTheCompleterPutInStopsBeingNewWhenTheReaderLeaves()
    {
        QFETCH(bool, quick);

        const bool was = globalCompletionSettings().highlightAutoComplete();
        const QScopeGuard restoreSetting([was] {
            globalCompletionSettings().highlightAutoComplete.setValue(was); });
        globalCompletionSettings().highlightAutoComplete.setValue(true);

        Utils::TemporaryDirectory dir("completer-put-in");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        for (const QChar ch : QString("f(")) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
            QCoreApplication::sendEvent(target, &press);
        }
        QCOMPARE(document->document()->toPlainText(), QString("f()"));

        // The closing bracket, and only it.
        const QTextCursor put = TextEditor::autoCompleteHighlightOf(editor);
        QCOMPARE(put.selectedText(), QString(")"));
        QCOMPARE(put.selectionStart(), 2);

        // The widget editor decides this from a queued call, so what is being
        // waited for is that turn of the loop and not a timeout.
        QCoreApplication::processEvents();
        QVERIFY2(!TextEditor::autoCompleteHighlightOf(editor).isNull(),
                 "sitting where the completer left the caret dropped it");

        // One character to the left is somewhere else.
        QTextCursor back(document->document());
        back.setPosition(1);
        TextEditor::setTextCursorOf(editor, back);
        QCoreApplication::processEvents();
        QVERIFY2(TextEditor::autoCompleteHighlightOf(editor).isNull(),
                 "what the completer put in was still new after the reader left it");
    }

    // And what that range is *for*, in the view that had it and drew nothing:
    // the text is coloured as C_AUTOCOMPLETE until it stops being new. The
    // setting turns the drawing off without taking the range away, because the
    // range is also what steps over a typed closing bracket.
    void testTheViewDrawsWhatTheCompleterPutIn()
    {
        const bool was = globalCompletionSettings().highlightAutoComplete();
        const QScopeGuard restoreSetting([was] {
            globalCompletionSettings().highlightAutoComplete.setValue(was); });
        globalCompletionSettings().highlightAutoComplete.setValue(true);

        Utils::TemporaryDirectory dir("completer-drawn");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents(""));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = viewportForEditor(editor);
        QVERIFY(view);
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);

        const auto drawn = [view] {
            return view->highlights(Utils::Id("TextEditor.AutoCompleted"));
        };
        const auto typeABracket = [target] {
            for (const QChar ch : QString("f(")) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
                QCoreApplication::sendEvent(target, &press);
            }
        };
        const auto putCaretAt = [editor, document](int position) {
            QTextCursor caret(document->document());
            caret.setPosition(position);
            TextEditor::setTextCursorOf(editor, caret);
        };

        QVERIFY2(drawn().isEmpty(), "something was drawn before anything was typed");
        typeABracket();

        QCOMPARE(drawn().size(), 1);
        // Over the bracket itself. The range is set while the edit is still
        // open, so this is also what says it was not carried a second time by
        // the change that closed it.
        QCOMPARE(drawn().first().start, 2);
        QCOMPARE(drawn().first().end, 3);
        QCOMPARE(drawn().first().format,
                 document->fontSettings().toTextCharFormat(C_AUTOCOMPLETE));

        putCaretAt(1);
        QVERIFY2(drawn().isEmpty(), "the colour outlived what it was saying");

        // Off: nothing is drawn, and the range is still there to step over.
        globalCompletionSettings().highlightAutoComplete.setValue(false);
        const Utils::FilePath other = dir.filePath("second.cpp");
        QVERIFY(other.writeFileContents(""));
        Core::IEditor * const second = Core::EditorManager::openEditor(other);
        QVERIFY(second);
        const QScopeGuard closeSecond(
            [second] { Core::EditorManager::closeEditors({second}, false); });
        TextViewport * const secondView = viewportForEditor(second);
        QVERIFY(secondView);
        QObject * const secondTarget = TextEditor::keyTargetOf(second);
        QVERIFY(secondTarget);
        for (const QChar ch : QString("f(")) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
            QCoreApplication::sendEvent(secondTarget, &press);
        }
        QVERIFY2(secondView->highlights(Utils::Id("TextEditor.AutoCompleted")).isEmpty(),
                 "the setting was off and the text was coloured anyway");
        QVERIFY2(!TextEditor::autoCompleteHighlightOf(second).isNull(),
                 "turning the colour off took away the range that steps over a bracket");
    }

    void testEveryOptionalActionBitReachesItsCommands_data()
    {
        QTest::addColumn<uint>("mask");
        QTest::addColumn<QStringList>("enabled");

        // What TextEditorWidgetPrivate::updateOptionalActions() enables for
        // each bit, which is the answer this editor has to give as well.
        QTest::newRow("Format") << uint(OptionalActions::Format)
            << QStringList{Constants::AUTO_INDENT_SELECTION, Constants::AUTO_FORMAT_SELECTION};
        QTest::newRow("UnCommentSelection") << uint(OptionalActions::UnCommentSelection)
            << QStringList{Constants::UN_COMMENT_SELECTION};
        QTest::newRow("UnCollapseAll") << uint(OptionalActions::UnCollapseAll)
            << QStringList{Constants::UNFOLD_ALL};
        QTest::newRow("FollowSymbolUnderCursor") << uint(OptionalActions::FollowSymbolUnderCursor)
            << QStringList{Constants::FOLLOW_SYMBOL_UNDER_CURSOR,
                           Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT};
        QTest::newRow("FollowTypeUnderCursor") << uint(OptionalActions::FollowTypeUnderCursor)
            << QStringList{Constants::FOLLOW_SYMBOL_TO_TYPE,
                           Constants::FOLLOW_SYMBOL_TO_TYPE_IN_NEXT_SPLIT};
        QTest::newRow("JumpToFileUnderCursor") << uint(OptionalActions::JumpToFileUnderCursor)
            << QStringList{Constants::JUMP_TO_FILE_UNDER_CURSOR,
                           Constants::JUMP_TO_FILE_UNDER_CURSOR_IN_NEXT_SPLIT};
        QTest::newRow("RenameSymbol") << uint(OptionalActions::RenameSymbol)
            << QStringList{Constants::RENAME_SYMBOL};
        QTest::newRow("FindUsage") << uint(OptionalActions::FindUsage)
            << QStringList{Constants::FIND_USAGES};
        QTest::newRow("CallHierarchy") << uint(OptionalActions::CallHierarchy)
            << QStringList{Constants::OPEN_CALL_HIERARCHY};
        QTest::newRow("TypeHierarchy") << uint(OptionalActions::TypeHierarchy)
            << QStringList{Constants::OPEN_TYPE_HIERARCHY};
    }

    // One bit at a time, against every command any bit governs: asking for a
    // bit has to enable its own commands and leave the other nine alone. The
    // test below checks the rule and the seam; this checks the table, which is
    // where a bit goes missing quietly.
    void testEveryOptionalActionBitReachesItsCommands()
    {
        QFETCH(uint, mask);
        QFETCH(QStringList, enabled);

        static const QStringList governed{
            Constants::AUTO_INDENT_SELECTION, Constants::AUTO_FORMAT_SELECTION,
            Constants::UN_COMMENT_SELECTION, Constants::UNFOLD_ALL,
            Constants::FOLLOW_SYMBOL_UNDER_CURSOR,
            Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT,
            Constants::FOLLOW_SYMBOL_TO_TYPE, Constants::FOLLOW_SYMBOL_TO_TYPE_IN_NEXT_SPLIT,
            Constants::JUMP_TO_FILE_UNDER_CURSOR,
            Constants::JUMP_TO_FILE_UNDER_CURSOR_IN_NEXT_SPLIT,
            Constants::RENAME_SYMBOL, Constants::FIND_USAGES,
            Constants::OPEN_CALL_HIERARCHY, Constants::OPEN_TYPE_HIERARCHY};

        MaskedFactory factory(Utils::Id("QuickEditorOneBit").withSuffix(int(mask)), mask);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY(editor);

        QStringList on;
        for (const QString &id : governed) {
            Core::Command * const cmd = Core::ActionManager::command(Utils::Id::fromString(id));
            QVERIFY2(cmd, qPrintable(id + " is not a command at all"));
            QAction *action = nullptr;
            for (const Utils::Id &each : commandContextsOf(editor.get())) {
                if (QAction * const a = cmd->actionForContext(each)) {
                    action = a;
                    break;
                }
            }
            QVERIFY2(action, qPrintable(id + " is not registered in the editor's context"));
            if (action->isEnabled())
                on << id;
        }
        QCOMPARE(on, enabled);
    }

    void testTheOptionalCommandsFollowTheFactorysMask()
    {
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
            for (const Utils::Id &each : commandContextsOf(editor)) {
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

        // And what a language turns out to support after the fact. A factory's
        // mask is what it can say before anything has run; a language server
        // knows more once it has answered its initialize, and used to have
        // nowhere to put it in this editor - so Open Call Hierarchy stayed
        // greyed out over a C++ file however capable clangd was.
        QVERIFY2(!usages->isEnabled(), "the fixture starts with Find Usages already offered");
        QVERIFY2(!TextEditorWidget::fromEditor(withSome.get()),
                 "the fixture is a widget editor, so this proves nothing about the Quick one");
        // Through the seam the language client reaches for, not the internal one.
        TextEditor::addOptionalActionsIn(withSome.get(), OptionalActions::FindUsage);
        QVERIFY2(usages->isEnabled(), "the language said it can find usages and was not heard");

        // Widening, not replacing: what the factory asked for is still there.
        QVERIFY2(renameHere->isEnabled(), "widening the mask dropped what the factory asked for");
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

    // The provider above is the one every text file gets, which proposes the
    // words already in the file. A language that hands its factory a fixed
    // keyword list is a different route to the same view: the factory's
    // provider has to reach the document, and the words have to be the
    // language's rather than the file's.
    void testAFactorysKeywordsAreOfferedInTheQuickView()
    {
        Utils::TemporaryDirectory dir("quick-editor-keywords");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Main.java");
        // "impl" is in no word of this file, so anything proposed for it came
        // from the keyword list rather than from the text.
        QVERIFY(file.writeFileContents("class Main {\n}\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "nothing opened a .java file");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the .java file opened in a widget editor");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(document->completionAssistProvider(),
                 "the factory's keywords never reached the document");

        TextViewport * const view = Internal::viewportForEditor(editor);
        QVERIFY(view);

        QTextDocument * const text = document->document();
        QTextCursor cursor(text);
        cursor.setPosition(text->characterCount() - 1);
        cursor.insertText("impl");
        view->setTextCursor(cursor);
        QCOMPARE(view->completionPrefix(), QString("impl"));

        QStringList candidates;
        connect(view, &TextViewport::completionsAvailable, view,
                [&candidates](const QStringList &proposed, const QString &) {
                    candidates = proposed;
                });
        view->requestCompletions();
        QTRY_VERIFY2(!candidates.isEmpty(), "the language proposed nothing at all");
        QVERIFY2(candidates.contains("implements"),
                 qPrintable("proposed: " + candidates.join(", ")));
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

    // Where the text is drawn, asked of the editor: the one question the Qt
    // Quick context pane work still needed a TextEditorWidget for. Global
    // coordinates, because a popup floating over the editor is what asks.
    void testWhereTheTextIsDrawnIsAskableInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testWhereTheTextIsDrawnIsAskableInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("quick-editor-geometry");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("geometry.py");
        //                                 0123456789012345678901
        QVERIFY(file.writeFileContents("alpha bravo\ncharlie\ndelta\n"));

        // A language whose factory drives both views, opened through the
        // editor manager: an editor shown as a window of its own and deleted
        // behind the manager's back leaves the find toolbar holding its view.
        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore(
            [factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(viewportForEditor(editor) != nullptr, quick);

        // Laid out, because only what is laid out has a place on screen.
        editor->widget()->resize(400, 300);
        editor->widget()->show();
        const QScopeGuard hideIt([editor] { editor->widget()->hide(); });
        if (quick) {
            TextViewport * const view = viewportForEditor(editor);
            QTRY_VERIFY(view->visibleLineCount() > 2);
        }

        // Qualified: this test lives in Internal, whose own half of the seam
        // answers for the Qt Quick view only. The public one picks the view.
        const QRect alpha = TextEditor::globalRectForPositionIn(editor, 0);
        QVERIFY2(!alpha.isEmpty(), "the view cannot say where its first character is");

        // Later on the same line is to the right of it, on the same row.
        const QRect bravo = TextEditor::globalRectForPositionIn(editor, 6);
        QVERIFY2(bravo.left() > alpha.left(), "'bravo' is not drawn right of 'alpha'");
        QCOMPARE(bravo.top(), alpha.top());

        // The next line is below it.
        const QRect charlie = TextEditor::globalRectForPositionIn(editor, 12);
        QVERIFY2(charlie.top() > alpha.top(), "'charlie' is not drawn below 'alpha'");

        // The first two blocks as one rectangle: it covers both their rows and
        // not the third's, which is what says the range means the range.
        const QRect firstTwo = TextEditor::globalRectForBlocksIn(editor, 0, 1);
        QVERIFY2(!firstTwo.isEmpty(), "the view cannot say where its first two lines are");
        QVERIFY2(firstTwo.intersects(alpha), "the block rect misses the first line");
        QVERIFY2(firstTwo.intersects(charlie), "the block rect misses the second line");
        const QRect delta = TextEditor::globalRectForPositionIn(editor, 20);
        QVERIFY2(!delta.isEmpty(), "the view cannot say where its third line is");
        QVERIFY2(!firstTwo.intersects(delta),
                 "a rect for the first two blocks reaches into the third");
    }

    // What a folded row stands in for its text is the document's answer - QML
    // labels a folded object with its id - and the row the view lays out has
    // to carry it. It carried a hard-coded "..." instead, so every language's
    // folds said the same thing in this view.
    void testAFoldedRowCarriesTheDocumentsReplacement()
    {
        class LabellingDocument final : public TextDocument
        {
        public:
            using TextDocument::TextDocument;
            QString foldReplacementText(const QTextBlock &block) const override
            {
                return QString("line %1 folded").arg(block.blockNumber());
            }
        };

        class LabellingFactory final : public TextEditorFactory
        {
        public:
            LabellingFactory()
            {
                setId("QuickEditorFoldLabelTest");
                setDisplayName("Quick Editor Fold Label Test");
                setDocumentCreator([] { return new LabellingDocument("QuickEditorFoldLabelTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setCodeFoldingSupported(true);
                setUsesQuickEditor(true);
            }
        };

        LabellingFactory factory;
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        document->setPlainText("one {\n    two\n    three\n}\n");
        // A fold exists where the folding indent deepens, and the highlighter
        // that would say so is the language's - this document is nobody's, so
        // it says so itself.
        QTextBlock block = document->document()->firstBlock();
        for (int indent : {0, 1, 1, 0, 0}) {
            TextBlockUserData::setFoldingIndent(block, indent);
            block = block.next();
        }

        editor->widget()->resize(400, 300);
        editor->widget()->show();
        const QScopeGuard hideIt([&editor] { editor->widget()->hide(); });
        TextViewport * const view = viewportForEditor(editor.get());
        QVERIFY(view);
        QTRY_VERIFY(view->visibleLineCount() > 2);

        view->toggleFold(1);
        const auto foldedRow = [view]() -> QVariantMap {
            for (int i = 0; i < view->visibleLineCount(); ++i) {
                const QVariantMap row = view->visibleLine(i);
                if (row.value("folded").toBool())
                    return row;
            }
            return {};
        };
        QTRY_VERIFY2(!foldedRow().isEmpty(), "nothing folded");
        QVERIFY2(foldedRow().value("foldReplacement").toString().contains("line 0 folded"),
                 qPrintable(QString("the folded row does not carry the document's answer: '%1'")
                                .arg(foldedRow().value("foldReplacement").toString())));
    }

    // An action that carries a menu is a button that opens it, in either row.
    // The widget row builds a QToolButton for the action and a QToolButton
    // opens a menu only on click-and-hold unless told otherwise, so a click
    // triggered the action instead - which for one of these does nothing at
    // all, because opening the menu is the whole point of it.
    void testAMenuCarryingToolBarActionOpensItInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAMenuCarryingToolBarActionOpensItInEitherView()
    {
        QFETCH(bool, quick);

        class MenuActionFactory final : public TextEditorFactory
        {
        public:
            explicit MenuActionFactory(bool quick)
            {
                setId("QuickEditorToolBarMenuTest");
                setDisplayName("Quick Editor Tool Bar Menu Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorToolBarMenuTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        MenuActionFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QCOMPARE(viewportForEditor(editor.get()) != nullptr, quick);

        auto * const withMenu = new QAction("Analyze", editor->document());
        auto * const menu = new QMenu;
        const QScopeGuard dropMenu([menu] { delete menu; });
        menu->addAction("Tidy");
        withMenu->setMenu(menu);
        insertExtraToolBarActionIn(editor.get(), TextEditorWidget::Left, withMenu);

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");

        if (quick) {
            auto * const view = bar->findChild<QQuickWidget *>();
            QVERIFY(view);
            QTRY_VERIFY(view->rootObject());
            QQuickItem *drawn = nullptr;
            QTRY_VERIFY2((drawn = itemNamed(view->rootObject(), "languageToolBarButton")),
                         "the row drew nothing for the action");
            // The row asks the action for its menu and draws one of its own
            // over it; having one is what says a click will open it.
            QVERIFY2(drawn->property("actionMenu").value<QObject *>(),
                     "the row drew a button that does not know the action has a menu");
        } else {
            QToolButton *drawn = nullptr;
            const QList<QToolButton *> buttons = bar->findChildren<QToolButton *>();
            for (QToolButton * const button : buttons) {
                if (button->defaultAction() == withMenu)
                    drawn = button;
            }
            QVERIFY2(drawn, "the row drew nothing for the action");
            QVERIFY2(drawn->popupMode() != QToolButton::DelayedPopup,
                     "a click on the button triggers the action instead of opening its menu");
        }
    }

    // A document says which bars it can minimize when it likes, and for a
    // language that decides late the tool bar row is already built. Neither
    // view re-reads the list on its own, so without being told the way back
    // never appears - and minimizing the bar is a one-way door in both.
    void testAnInfoBarEntrySetAfterTheRowReachesIt_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAnInfoBarEntrySetAfterTheRowReachesIt()
    {
        QFETCH(bool, quick);

        class LateFactory final : public TextEditorFactory
        {
        public:
            explicit LateFactory(bool quick)
            {
                setId("QuickEditorLateInfoBarTest");
                setDisplayName("Quick Editor Late Info Bar Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorLateInfoBarTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        LateFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(viewportForEditor(editor.get()) != nullptr, quick);

        // Built first, which is the whole point: a row that is made after the
        // entries exist reads them itself and says nothing about being told.
        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");
        QToolBar *toolBar = nullptr;
        QQuickItem *root = nullptr;
        if (quick) {
            auto * const view = bar->findChild<QQuickWidget *>();
            QVERIFY(view);
            QTRY_VERIFY(view->rootObject());
            root = view->rootObject();
            QVERIFY2(itemsNamed(root, "languageToolBarButton").isEmpty(),
                     "the row already drew a language button with nothing to draw");
        } else {
            toolBar = bar->findChild<QToolBar *>();
            QVERIFY(toolBar);
        }
        QVERIFY2(document->toolBarActions().isEmpty(),
                 "the document offers something before anything was set on it");

        const Utils::Id entryId("QuickEditorLateInfoBarEntry");
        Utils::MinimizableInfoBars * const bars = document->minimizableInfoBars();
        bars->setSettingsGroup("QuickEditorLateInfoBarTest");
        bars->setPossibleInfoBarEntries({Utils::InfoBarEntry(entryId, "Set after the row")});

        const QList<QAction *> ways = bars->showInfoBarActions();
        QCOMPARE(ways.size(), 1);
        QVERIFY2(document->toolBarActions().contains(ways.first()),
                 "the document does not offer the way back it just gained");

        if (quick) {
            QTRY_VERIFY2(itemsNamed(root, "languageToolBarButton").size() == 1,
                         "the Qt Quick row never drew the way back it was told about");
        } else {
            QTRY_COMPARE(timesDrawnIn(toolBar, ways.first()), 1);
        }
    }

    // The same count over a real language rather than a made-up factory, which
    // is where the two channels met: MinimizableInfoBars handed the widget
    // toolbar a QToolButton wrapping its action, and the document offered the
    // same action for a view that draws actions - which the widget row draws
    // too. So a C++ file had the way back from a minimized info bar in its
    // toolbar twice, one entry per channel, both backed by the same QAction.
    void testACppFilesToolBarRowDrawsEachActionOnce_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testACppFilesToolBarRowDrawsEachActionOnce()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("cpp-toolbar-once");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main() {}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore(
            [factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(viewportForEditor(editor) != nullptr, quick);

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        // The file is in no project, so the way back from the "not part of any
        // project" bar is there to be drawn. Without this the count below
        // would be over a list that does not contain the interesting one.
        const QList<QAction *> ways = document->minimizableInfoBars()->showInfoBarActions();
        QCOMPARE(ways.size(), 1);
        QVERIFY2(document->toolBarActions().contains(ways.first()),
                 "the document does not offer the way back from a minimized bar");

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "a C++ file has no toolbar row at all");

        if (quick) {
            auto * const view = bar->findChild<QQuickWidget *>();
            QVERIFY(view);
            QTRY_VERIFY(view->rootObject());
            // One button per action: the Qt Quick row builds from the same
            // list, so drawing one twice would show up here as well.
            QList<QQuickItem *> drawn;
            QTRY_VERIFY2((drawn = itemsNamed(view->rootObject(), "languageToolBarButton")).size()
                             == document->toolBarActions().size(),
                         "the Qt Quick row does not draw one button per action");
        } else {
            // What the row holds, not what is parented to it: QToolBar keeps
            // the button of an action it has removed, so findChildren() counts
            // a row that was rebuilt as one that drew everything twice.
            auto * const toolBar = bar->findChild<QToolBar *>();
            QVERIFY(toolBar);
            for (QAction * const action : document->toolBarActions())
                QCOMPARE(timesDrawnIn(toolBar, action), 1);
        }
    }

    // A census of the tool bar row: what a language puts there, counted in
    // each view. Both views take the language's part of the row from
    // TextDocument::toolBarActions() - the widget row inserts each as an
    // action, the Qt Quick row draws a button per row of the model - so each
    // action has to be drawn exactly once, and the count has to match.
    void testTheToolBarRowDrawsEachLanguageActionOnce_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheToolBarRowDrawsEachLanguageActionOnce()
    {
        QFETCH(bool, quick);

        class CensusFactory final : public TextEditorFactory
        {
        public:
            explicit CensusFactory(bool quick)
            {
                setId("QuickEditorToolBarCensus");
                setDisplayName("Quick Editor Tool Bar Census");
                setDocumentCreator([] { return new TextDocument("QuickEditorToolBarCensus"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        CensusFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(viewportForEditor(editor.get()) != nullptr, quick);

        // The two ways a language reaches the row: an entry it can minimize,
        // and an action put on the document by something that is not the
        // document - a language client does the second.
        const Utils::Id entryId("QuickEditorToolBarCensusEntry");
        Utils::MinimizableInfoBars * const bars = document->minimizableInfoBars();
        bars->setSettingsGroup("QuickEditorToolBarCensus");
        bars->setPossibleInfoBarEntries({Utils::InfoBarEntry(entryId, "Census entry")});
        auto * const added = new QAction("Census", document);
        document->addToolBarAction(added);

        const QList<QAction *> offered = document->toolBarActions();
        QCOMPARE(offered.size(), 2);

        QWidget * const bar = editor->toolBar();
        QVERIFY2(bar, "the editor puts nothing in the toolbar row");

        if (quick) {
            auto * const view = bar->findChild<QQuickWidget *>();
            QVERIFY(view);
            QTRY_VERIFY(view->rootObject());
            // One button per action the document offers, found by name rather
            // than by text: the info bar's action carries an icon and no text.
            QList<QQuickItem *> drawn;
            QTRY_VERIFY2((drawn = itemsNamed(view->rootObject(), "languageToolBarButton")).size()
                             == offered.size(),
                         "the Qt Quick row does not draw one button per language action");
        } else {
            auto * const toolBar = bar->findChild<QToolBar *>();
            QVERIFY(toolBar);
            for (QAction * const action : offered)
                QCOMPARE(timesDrawnIn(toolBar, action), 1);
        }
    }

    // A text view embedded in another editor's UI - Designer's form source,
    // SCXML's source pane - has no tool bar row of its own, because the editor
    // around it draws one. Saying so was a BaseTextEditor override returning
    // null, which is a class the Qt Quick path never builds, so that view
    // would have drawn a row the widget view does not.
    void testAFactoryCanSayItsLanguageHasNoToolBarRow_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAFactoryCanSayItsLanguageHasNoToolBarRow()
    {
        QFETCH(bool, quick);

        class BarFactory final : public TextEditorFactory
        {
        public:
            BarFactory(bool quick, bool bar)
            {
                setId("QuickEditorToolBarVisibleTest");
                setDisplayName("Quick Editor Tool Bar Visible Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorToolBarVisibleTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
                setToolBarVisible(bar);
            }
        };

        // The default first, so that the assertion below is about the setting
        // rather than about this view having no row to begin with.
        BarFactory withBar(quick, true);
        QCOMPARE(withBar.toolBarVisible(), true);
        const std::unique_ptr<Core::IEditor> loud(withBar.createEditor());
        QVERIFY2(loud.get(), "the factory built nothing");
        QCOMPARE(viewportForEditor(loud.get()) != nullptr, quick);
        QVERIFY2(loud->toolBar(), "an ordinary editor has no toolbar row at all");

        BarFactory withoutBar(quick, false);
        QCOMPARE(withoutBar.toolBarVisible(), false);
        const std::unique_ptr<Core::IEditor> quiet(withoutBar.createEditor());
        QVERIFY2(quiet.get(), "the factory built nothing");
        QCOMPARE(viewportForEditor(quiet.get()) != nullptr, quick);
        QVERIFY2(!quiet->toolBar(),
                 "the editor drew a toolbar row for a language that said it has none");
    }

    // Whether a file belongs in Design mode is a property of the file, so both
    // views have to give the editor manager the same answer for it. The three
    // editors in the tree that said yes said it from a BaseTextEditor
    // subclass, which is a class the Quick path never builds - so a .ui, a
    // .scxml or a .qml opened in this view would have stayed in Edit mode.
    void testEitherViewPrefersDesignModeForTheSameDocument_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testEitherViewPrefersDesignModeForTheSameDocument()
    {
        QFETCH(bool, quick);

        class DesignDoc final : public TextDocument
        {
        public:
            using TextDocument::TextDocument;
            bool isDesignModePreferred() const override { return true; }
        };

        class DesignFactory final : public TextEditorFactory
        {
        public:
            explicit DesignFactory(bool quick)
            {
                setId("QuickEditorDesignModeTest");
                setDisplayName("Quick Editor Design Mode Test");
                setDocumentCreator([] { return new DesignDoc("QuickEditorDesignModeTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        DesignFactory factory(quick);
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QCOMPARE(viewportForEditor(editor.get()) != nullptr, quick);
        QVERIFY2(editor->isDesignModePreferred(),
                 "the editor does not pass on that its document belongs in Design mode");

        // The other half of the same question, so that an editor answering
        // yes to everything would fail here: the default document does not
        // ask for Design mode and neither view may invent it.
        class PlainFactory final : public TextEditorFactory
        {
        public:
            explicit PlainFactory(bool quick)
            {
                setId("QuickEditorEditModeTest");
                setDisplayName("Quick Editor Edit Mode Test");
                setDocumentCreator([] { return new TextDocument("QuickEditorEditModeTest"); });
                setEditorWidgetCreator([] { return new TextEditorWidget; });
                setUsesQuickEditor(quick);
            }
        };

        PlainFactory plain(quick);
        const std::unique_ptr<Core::IEditor> plainEditor(plain.createEditor());
        QVERIFY2(plainEditor.get(), "the factory built nothing");
        QVERIFY2(!plainEditor->isDesignModePreferred(),
                 "an editor over an ordinary document asked for Design mode");
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
