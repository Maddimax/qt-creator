// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quicktexteditor.h"

#include "colorpreviewhoverhandler.h"

#include "codesource.h"
#include "textdocument.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"
#include "codestylepool.h"
#include "autocompleter.h"
#include "codeassist/documentcontentcompletion.h"
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
#include "textviewport.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/codecselector.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>

#include <qtcquick/actionmodel.h>
#include <qtcquick/qtcquickwidget.h>

#include "fontsettings.h"

#include <coreplugin/find/basetextfind.h>
#include <coreplugin/find/ifindsupport.h>

#include <utils/aggregate.h>
#include <utils/textutils.h>
#include <utils/algorithm.h>
#include <utils/theme/theme.h>
#include <utils/mimeutils.h>
#include <utils/temporarydirectory.h>

#include <QDataStream>
#include <QMenu>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScopeGuard>
#include <QSignalSpy>
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
public:
    QuickTextEditor()
        : QuickTextEditor(std::shared_ptr<TextDocument>(new TextDocument(QUICK_TEXT_EDITOR_ID)))
    {}

    // A split view is two editors on one document, so duplicating shares it
    // rather than opening the file again.
    explicit QuickTextEditor(std::shared_ptr<TextDocument> document)
        : m_document(std::move(document))
        , m_source(std::make_unique<AdoptedSource>(m_document.get()))
    {
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
        if (TextViewport * const view = viewport())
            view->setTooltipHost(widget->quickWidget());

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
        const auto followSymbol = [this](Utils::Id id, bool inNextSplit) {
            Core::ActionBuilder(this, id)
                .setContext(Core::Context(m_editorContext))
                .addOnTriggered(this, [this, inNextSplit] {
                    if (TextViewport * const view = viewport())
                        view->followSymbolUnderCursor(view->opensInNextSplit(inNextSplit));
                });
        };
        followSymbol(Constants::FOLLOW_SYMBOL_UNDER_CURSOR, false);
        followSymbol(Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT, true);

        // Ctrl+F reaches an editor by asking its widget for an IFindSupport,
        // so this has to hang off the widget rather than off the editor.
        if (TextViewport * const view = viewport())
            Utils::Aggregation::aggregate({widget, new QuickTextFind(view, widget)});
    }

    Core::IDocument *document() const final { return m_document.get(); }

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

        auto bar = new QtcQuick::QuickWidget;
        bar->quickWidget()->setInitialProperties({{"viewport", QVariant::fromValue(view)}});
        bar->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/EditorToolBar.qml"));
        m_toolBar = bar;
        return m_toolBar;
    }

    Core::IEditor *duplicate() final { return new QuickTextEditor(m_document); }

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
        if (TextViewport * const view = viewport())
            view->gotoLine(line, column, centerLine);
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

    // The form's viewport. Found rather than held: the QML owns it, and it
    // does not exist until the component has been created.
    TextViewport *viewport() const
    {
        auto * const quick = static_cast<QtcQuick::QuickWidget *>(widget());
        QQuickItem * const root = quick->quickWidget()->rootObject();
        return root ? root->findChild<TextViewport *>() : nullptr;
    }

    // Shared because a duplicated editor would show the same document; the
    // editor manager is what decides that, not this.
    std::shared_ptr<TextDocument> m_document;
    QtcQuick::ActionModel m_contextActions;
    // Owned by the toolbar the editor manager puts it in, so a QPointer.
    QPointer<QWidget> m_toolBar;
    QPointer<QAction> m_wrapAction;
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
        setEditorCreator([] { return new QuickTextEditor; });
    }
};

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

class QuickTextEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // The one thing this must not do while it is unfinished: become what a
    // text file opens in. The default for a mime type is the first factory
    // that claims it, and the plain text editor is still offered beside it.
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
};

QObject *createQuickTextEditorTest()
{
    return new QuickTextEditorTest;
}

#endif // WITH_TESTS

} // namespace TextEditor::Internal

#ifdef WITH_TESTS
#include "quicktexteditor.moc"
#endif
