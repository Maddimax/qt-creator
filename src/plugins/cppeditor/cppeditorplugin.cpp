// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangdsettings.h"
#include "cppautocompleter.h"
#include "cppcodemodelinspectordialog.h"
#include "cppcodemodelsettings.h"
#include "cppcodestyleaspects_test.h"
#include "cppcodestylesettingspage.h"
#include "cppeditorconstants.h"
#include "cpppreprocessordialog.h"
#include "cppeditordocument.h"
#include "cppselectionchanger.h"
#include "cppeditoroutline.h"
#include "cppeditortr.h"
#include "cppeditorwidget.h"
#include "cppfunctiondecldeflink.h"
#include "cppfilesettingspage.h"
#include "cppheadersource.h"
#include "cpphighlighter.h"
#include "cppincludehierarchy.h"
#include "cppmodelmanager.h"
#include "cppoutline.h"
#include "cppprojectupdater.h"
#include "cpptoolsreuse.h"
#include "cpplocalrenaming.h"
#include "cppuseselectionsupdater.h"
#include "cpptoolssettings.h"
#include "cpptypehierarchy.h"
#include "mcpsupport.h"
#include "quickfixes/cppquickfix.h"
#include "quickfixes/cppquickfixsettings.h"

#ifdef WITH_TESTS
#include "compileroptionsbuilder_test.h"
#include "cpptoolstestcase.h"

#include <QtTest>
#include "cppcodegen_test.h"
#include "cppcompletion_test.h"
#include "cppdoxygen_test.h"
#include "cppfollowsymbolundercursor.h"
#include "cppincludehierarchy_test.h"
#include "cpplocalsymbols_test.h"
#include "cpplocatorfilter_test.h"
#include "cppmcpsupport_test.h"
#include "cppmodelmanager_test.h"
#include "cpppointerdeclarationformatter_test.h"
#include "cpprenaming_test.h"
#include "cppsourceprocessor_test.h"
#include "cppuseselections_test.h"
#include "fileandtokenactions_test.h"
#include "followsymbol_switchmethoddecldef_test.h"
#include "functionutils.h"
#include "includeutils.h"
#include "projectinfo_test.h"
#include "symbolsearcher_test.h"
#include "typehierarchybuilder_test.h"
#endif

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/icore.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/rawprojectpart.h>
#include <projectexplorer/resourcepreviewhoverhandler.h>

#include <texteditor/codestylepool.h>
#include <texteditor/colorpreviewhoverhandler.h>
#include <texteditor/fontsettings.h>
#include <texteditor/snippets/snippetprovider.h>
#include <texteditor/behaviorsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/symbolrequests.h>
#include <texteditor/texteditorconstants.h>

#include <utils/clangutils.h>
#include <utils/environment.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/hostosinfo.h>
#include <utils/macroexpander.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QFutureWatcher>
#include <QMenu>
#include <QStringList>

using namespace CPlusPlus;
using namespace Core;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {

#ifdef WITH_TESTS
QObject *createCppSelectionExpansionTest();
#endif

// Ctrl+U and Ctrl+Shift+U for C++, for a view that is not a CppEditorWidget.
// The changer is stateful - repeated presses walk further out and moving the
// caret starts the walk again - so there is one of these per editor, which is
// why it is an object rather than a function the factory registers.
class CppSelectionExpander final : public TextEditor::SelectionExpander
{
public:
    CppSelectionExpander(Core::IEditor *editor, CppEditorDocument *document)
        : m_editor(editor)
        , m_document(document)
    {}

    bool grow() override { return change(CppSelectionChanger::ExpandSelection); }

    bool shrink() override { return change(CppSelectionChanger::ShrinkSelection); }

    // The walk is relative to where it started, so a caret the reader moved
    // has to start it again. CppEditorWidget tells its own changer the same.
    void caretMoved()
    {
        if (m_editor)
            m_changer.onCursorPositionChanged(TextEditor::textCursorOf(m_editor));
    }

private:
    // The caret is written back inside the changer's own guard, the way
    // CppEditorWidget does it: outside, the write is a caret move like any
    // other and starts the walk again at every step.
    bool change(CppSelectionChanger::Direction direction)
    {
        if (!m_editor || !m_document
            || !TextEditor::globalBehaviorSettings().data().m_smartSelectionChanging) {
            return false;
        }
        const CPlusPlus::Document::Ptr doc = m_document->semanticInfo().doc;
        if (!doc)
            return false;

        QTextCursor cursor = TextEditor::textCursorOf(m_editor);
        m_changer.startChangeSelection();
        const bool changed = m_changer.changeSelection(direction, cursor, doc);
        if (changed)
            TextEditor::setTextCursorOf(m_editor, cursor);
        m_changer.stopChangeSelection();
        return changed;
    }

    CppSelectionChanger m_changer;
    QPointer<Core::IEditor> m_editor;
    QPointer<CppEditorDocument> m_document;
};


//////////////////////////// CppEditorFactory /////////////////////////////

// Where the symbol under the cursor is defined, for a view that is not a
// CppEditorWidget. The widget keeps its own findLinkAt(): it can offer the
// preprocessor popup and the choice of override, and this cannot.
static void findCppLinkAt(TextEditor::TextDocument *document,
                          const QTextCursor &cursor,
                          const Utils::LinkHandler &processLinkCallback,
                          bool resolveTarget,
                          bool inNextSplit)
{
    if (!CppModelManager::instance())
        return processLinkCallback(Utils::Link());

    // No widget to take the semantic info from, so the snapshot's document is
    // what the builtin backend gets. clangd asks the language server and needs
    // neither.
    const CursorInEditor data(cursor,
                              document->filePath(),
                              nullptr,
                              document,
                              CppModelManager::snapshot().document(document->filePath()));
    CppModelManager::followSymbol(data, processLinkCallback, resolveTarget, inNextSplit,
                                  FollowSymbolMode::Exact);
}

class CppEditorFactory : public TextEditorFactory
{
public:
    CppEditorFactory()
    {
        setId(Constants::CPPEDITOR_ID);
        setDisplayName(Tr::tr("C++ Editor"));
        addMimeType(Utils::Constants::C_SOURCE_MIMETYPE);
        addMimeType(Utils::Constants::C_HEADER_MIMETYPE);
        addMimeType(Utils::Constants::CPP_SOURCE_MIMETYPE);
        addMimeType(Utils::Constants::CPP_HEADER_MIMETYPE);
        addMimeType(Utils::Constants::QDOC_MIMETYPE);
        addMimeType(Utils::Constants::MOC_MIMETYPE);

        addEditorContext(ProjectExplorer::Constants::CXX_LANGUAGE_ID);

        // A C++ file opens in the Qt Quick view. Everything CppEditorWidget
        // adds is reachable without being that widget now: completion, quick
        // fixes, follow symbol, refactoring, renaming both in place and across
        // a project, the uses of the symbol under the caret, the outline and
        // the parse-context chooser in the toolbar. Every CppEditor test class
        // was run once per view and the two answer the same.
        //
        // QTC_WIDGET_CPP_EDITOR is the way back, for a report that says
        // otherwise.
        setUsesQuickEditor(!Utils::qtcEnvironmentVariableIsSet("QTC_WIDGET_CPP_EDITOR"));

        setDocumentCreator([]() { return new CppEditorDocument; });
        setEditorWidgetCreator([]() { return new CppEditorWidget; });
        setAutoCompleterCreator([]() { return new CppAutoCompleter; });
        setLinkFinder(&findCppLinkAt);
        setCommentDefinition(CommentDefinition::CppStyle);
        setCodeFoldingSupported(true);
        setParenthesesMatchingEnabled(true);

        setOptionalActionMask(OptionalActions::Format
                                | OptionalActions::UnCommentSelection
                                | OptionalActions::UnCollapseAll
                                | OptionalActions::FollowSymbolUnderCursor
                                | OptionalActions::FollowTypeUnderCursor
                                | OptionalActions::RenameSymbol
                                | OptionalActions::TypeHierarchy
                                | OptionalActions::FindUsage);
    }
};

class ClangdToolFactory : public DeviceToolAspectFactory
{
public:
    ClangdToolFactory()
    {
        setToolId(Constants::CLANGD_TOOL_ID);
        setToolType(DeviceToolAspect::SourceTool);
        setFilePattern({"clangd"});
        setLabelText(Tr::tr("Clangd executable:"));
        setDisplayName(Tr::tr("Clangd"));
        setChecker([](const DeviceConstRef &, const FilePath &candidate) {
            return checkClangdVersion(candidate);
        });
    }
};

///////////////////////////////// CppEditorPlugin //////////////////////////////////

class CppEditorPluginPrivate : public QObject
{
public:
    void onTaskStarted(Utils::Id type);
    void onAllTasksFinished(Utils::Id type);

    QAction *m_reparseExternallyChangedFiles = nullptr;
    QAction *m_findRefsCategorizedAction = nullptr;

    CppEditorFactory m_cppEditorFactory;

    CppModelManager modelManager;
    ClangdToolFactory clangdToolFactory;
};

class CppEditorPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "CppEditor.json")

public:
    ~CppEditorPlugin() final
    {
        destroyCppQuickFixFactories();
        delete d;
        d = nullptr;
    }

private:
    void initialize() final;
    void extensionsInitialized() final;

    void setupMenus();
    void addPerSymbolActions();
    void addActionsForSelections();
    void addPerFileActions();
    void addGlobalActions();
    void registerVariables();
    void registerTests();

    CppEditorPluginPrivate *d = nullptr;
};

static QFuture<QTextDocument *> highlightCode(const QString &code, const QString &mimeType)
{
    QTextDocument *document = new QTextDocument;
    document->setPlainText(code);

    std::shared_ptr<QPromise<QTextDocument *>> promise
        = std::make_shared<QPromise<QTextDocument *>>();

    promise->start();

    CppHighlighter *highlighter = new CppHighlighter(document);

    QObject::connect(highlighter, &CppHighlighter::finished, document, [document, promise]() {
        promise->addResult(document);
        promise->finish();
    });

    QFutureWatcher<QTextDocument *> *watcher = new QFutureWatcher<QTextDocument *>(document);
    QObject::connect(watcher, &QFutureWatcher<QTextDocument *>::canceled, document, [document]() {
        document->deleteLater();
    });
    watcher->setFuture(promise->future());

    highlighter->setParent(document);
    highlighter->setFontSettings(globalFontSettings().data());
    highlighter->setMimeType(mimeType);
    highlighter->rehighlight();

    return promise->future();
}

void CppEditorPlugin::initialize()
{
    d = new CppEditorPluginPrivate;

    setupCppToolsSettings();
    setupCppQuickFixSettings();
    setupCppCodeModelSettingsPage();
    provideCppSettingsRetriever([](const Project *p) {
        return QVariant::fromValue(CppCodeModelSettings::settingsForProject(p));
    });
    setupCppOutline();
    setupCppCodeStyleSettings();
    setupCppProjectUpdater();

    CppModelManager::registerJsExtension();

    registerMcpTools();

    setupMenus();
    registerVariables();
    createCppQuickFixFactories();
    registerTests();

    SnippetProvider::registerGroup(Constants::CPP_SNIPPETS_GROUP_ID, Tr::tr("C++", "SnippetProvider"),
                                   &decorateCppDocument, "text/x-c++src",
                                   [] { return new CppAutoCompleter; });

    connect(ProgressManager::instance(), &ProgressManager::taskStarted,
            d, &CppEditorPluginPrivate::onTaskStarted);
    connect(ProgressManager::instance(), &ProgressManager::allTasksFinished,
            d, &CppEditorPluginPrivate::onAllTasksFinished);

    auto oldHighlighter = Utils::Text::codeHighlighter();
    Utils::Text::setCodeHighlighter(
        [oldHighlighter](const QString &code, const QString &mimeType) -> QFuture<QTextDocument *> {
            if (mimeType == "text/x-c++src" || mimeType == "text/x-c++hdr"
                || mimeType == "text/x-csrc" || mimeType == "text/x-chdr") {
                return highlightCode(code, mimeType);
            }

            return oldHighlighter(code, mimeType);
        });

    const auto loader = [](const Utils::FilePath &codeStyleFile,
                           const Project &project) -> Result<QVariant> {
        CodeStylePool * const pool = cppCodeStyle()->delegatingPool();
        QTC_ASSERT(pool, return ResultError(Tr::tr("Internal error: No code style pool")));
        if (ICodeStylePreferences * const style
                = pool->loadCodeStyle(codeStyleFile, true, project.projectFilePath()))
            return Id::fromName(style->id()).toSetting();
        return ResultError(Tr::tr("No code style found in file."));
    };
    const auto unloader = [](const QVariant &data) {
        for (const QVariantList &l = data.toList(); const QVariant &id : l) {
            CodeStylePool * const pool = cppCodeStyle()->delegatingPool();
            QTC_ASSERT(pool, return);
            pool->removeAutoImportedCodeStyle(Id::fromSetting(id));
        }
    };
    ProjectManager::registerCustomProjectSettingsHandler(
        {"codestyles", CustomProjectSettingsHandler::FileType::Dir, loader, unloader});
}

void CppEditorPlugin::extensionsInitialized()
{
    setupCppQuickFixProjectPanel();
    setupCppFileSettings(*this);
    setupCppCodeModelProjectSettingsPanel();

    if (CppModelManager::isClangCodeModelActive()) {
        setupClangdProjectSettingsPanel();
        setupClangdSettingsPage();
    }

    // Add the hover handler factories here instead of in initialize()
    // so that the Clang Code Model has a chance to hook in.
    d->m_cppEditorFactory.addHoverHandler(&CppModelManager::cppHoverHandler());
    d->m_cppEditorFactory.addHoverHandler(&colorPreviewHoverHandler());
    d->m_cppEditorFactory.addHoverHandler(&resourcePreviewHoverHandler());

    FileIconProvider::registerIconOverlayForMimeType(
        creatorTheme()->imageFile(Theme::IconOverlayCppSource,
                                  ProjectExplorer::Constants::FILEOVERLAY_CPP),
        Utils::Constants::CPP_SOURCE_MIMETYPE);
    FileIconProvider::registerIconOverlayForMimeType(
        creatorTheme()->imageFile(Theme::IconOverlayCSource,
                                  ProjectExplorer::Constants::FILEOVERLAY_C),
        Utils::Constants::C_SOURCE_MIMETYPE);
    FileIconProvider::registerIconOverlayForMimeType(
        creatorTheme()->imageFile(Theme::IconOverlayCppHeader,
                                  ProjectExplorer::Constants::FILEOVERLAY_H),
        Utils::Constants::CPP_HEADER_MIMETYPE);
}

static void insertIntoMenus(const QList<ActionContainer *> &menus,
                            const std::function<void(ActionContainer *)> &func)
{
    for (ActionContainer * const menu : menus)
        func(menu);
}

static void addActionToMenus(const QList<Id> &menuIds, Id actionId, Id groupId)
{
    for (const Id menuId : menuIds) {
        ActionContainer * const menu = ActionManager::actionContainer(menuId);
        menu->addAction(ActionManager::command(actionId), groupId);
    }
}

void CppEditorPlugin::setupMenus()
{
    ActionContainer * const cppToolsMenu = ActionManager::createMenu(Constants::M_TOOLS_CPP);
    cppToolsMenu->menu()->setTitle(Tr::tr("&C++"));
    cppToolsMenu->menu()->setEnabled(true);
    ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(cppToolsMenu);
    ActionContainer * const contextMenu = ActionManager::createMenu(Constants::M_CONTEXT);

    insertIntoMenus({cppToolsMenu, contextMenu}, [](ActionContainer *menu) {
        menu->insertGroup(Core::Constants::G_DEFAULT_ONE, Constants::G_SYMBOL);
        menu->insertGroup(Core::Constants::G_DEFAULT_ONE, Constants::G_SELECTION);
        menu->insertGroup(Core::Constants::G_DEFAULT_ONE, Constants::G_FILE);
        menu->insertGroup(Core::Constants::G_DEFAULT_ONE, Constants::G_GLOBAL);
        menu->addSeparator(Constants::G_SELECTION);
        menu->addSeparator(Constants::G_FILE);
        menu->addSeparator(Constants::G_GLOBAL);
    });

    addPerSymbolActions();
    addActionsForSelections();
    addPerFileActions();
    addGlobalActions();

    ActionBuilder inspectCppCodeModel(this, Constants::INSPECT_CPP_CODEMODEL);
    inspectCppCodeModel.setText(Tr::tr("Inspect C++ Code Model..."));
    inspectCppCodeModel.setDefaultKeySequence(Tr::tr("Meta+Shift+F12"), Tr::tr("Ctrl+Shift+F12"));
    inspectCppCodeModel.addToContainer(Core::Constants::M_TOOLS_DEBUG);
    inspectCppCodeModel.addOnTriggered(d, &Internal::inspectCppCodeModel);
}

void CppEditorPlugin::addPerSymbolActions()
{
    const QList<Id> menus{Constants::M_TOOLS_CPP, Constants::M_CONTEXT};
    const auto addSymbolActionToMenus = [&menus](Id id) {
        addActionToMenus(menus, id, Constants::G_SYMBOL);
    };
    const Context context(Constants::CPPEDITOR_ID);

    addSymbolActionToMenus(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR);
    Command *cmd = ActionManager::command(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR);
    cmd->setTouchBarText(Tr::tr("Follow", "text on macOS touch bar"));
    ActionContainer * const touchBar = ActionManager::actionContainer(Core::Constants::TOUCH_BAR);
    touchBar->addAction(cmd, Core::Constants::G_TOUCHBAR_NAVIGATION);

    addSymbolActionToMenus(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT);
    addSymbolActionToMenus(TextEditor::Constants::FOLLOW_SYMBOL_TO_TYPE);
    addSymbolActionToMenus(TextEditor::Constants::FOLLOW_SYMBOL_TO_TYPE_IN_NEXT_SPLIT);

    ActionBuilder followToParentImpl(this, "CppEditor.FollowToParentImpl");
    followToParentImpl.setText(Tr::tr("Follow Virtual Function to Base Class Implementation"));
    followToParentImpl.setContext(context);
    followToParentImpl.setScriptable(true);
    followToParentImpl.addToContainers(menus, Constants::G_SYMBOL);
    followToParentImpl.addOnTriggered(this, [] {
        CppEditor::goToParentImpl(EditorManager::currentEditor(), /*inNextSplit*/ false);
    });

    ActionBuilder followToParentImplSplit(this, "CppEditor.FollowToParentImplInNextSplit");
    followToParentImplSplit.setText(
        Tr::tr("Follow Virtual Function to Base Class Implementation in Next Split"));
    followToParentImplSplit.setContext(context);
    followToParentImplSplit.setScriptable(true);
    followToParentImplSplit.addToContainers(menus, Constants::G_SYMBOL);
    followToParentImplSplit.addOnTriggered(this, [] {
        CppEditor::goToParentImpl(EditorManager::currentEditor(), /*inNextSplit*/ true);
    });

    ActionBuilder switchDeclDef(this, Constants::SWITCH_DECLARATION_DEFINITION);
    switchDeclDef.setText(Tr::tr("Switch Between Function Declaration/Definition"));
    switchDeclDef.setContext(context);
    switchDeclDef.setScriptable(true);
    switchDeclDef.setDefaultKeySequence(Tr::tr("Shift+F2"));
    switchDeclDef.setTouchBarText(Tr::tr("Decl/Def", "text on macOS touch bar"));
    switchDeclDef.addToContainers(menus, Constants::G_SYMBOL);
    switchDeclDef.addToContainer(Core::Constants::TOUCH_BAR, Core::Constants::G_TOUCHBAR_NAVIGATION);
    switchDeclDef.addOnTriggered(this, [] {
        CppEditor::switchDeclarationDefinition(EditorManager::currentEditor(),
                                               /*inNextSplit*/ false);
    });

    ActionBuilder openDeclDefSplit(this, Constants::OPEN_DECLARATION_DEFINITION_IN_NEXT_SPLIT);
    openDeclDefSplit.setText(Tr::tr("Open Function Declaration/Definition in Next Split"));
    openDeclDefSplit.setContext(context);
    openDeclDefSplit.setScriptable(true);
    openDeclDefSplit.setDefaultKeySequence(Tr::tr("Meta+E, Shift+F2"), Tr::tr("Ctrl+E, Shift+F2"));
    openDeclDefSplit.addToContainers(menus, Constants::G_SYMBOL);
    openDeclDefSplit.addOnTriggered(this, [] {
        CppEditor::switchDeclarationDefinition(EditorManager::currentEditor(),
                                               /*inNextSplit*/ true);
    });

    addSymbolActionToMenus(TextEditor::Constants::FIND_USAGES);

    ActionBuilder findRefsCategorized(this,  "CppEditor.FindRefsCategorized");
    findRefsCategorized.setText(Tr::tr("Find References With Access Type"));
    findRefsCategorized.setContext(context);
    findRefsCategorized.bindContextAction(&d->m_findRefsCategorizedAction);
    findRefsCategorized.addToContainers(menus, Constants::G_SYMBOL);
    findRefsCategorized.addOnTriggered(this, [] {
        IEditor * const editor = EditorManager::currentEditor();
        if (!editor || !qobject_cast<CppEditorDocument *>(editor->document()))
            return;
        CppCodeModelSettings::setCategorizeFindReferences(true);
        CppEditor::findUsagesOf(editor);
        CppCodeModelSettings::setCategorizeFindReferences(false);
    });

    addSymbolActionToMenus(TextEditor::Constants::RENAME_SYMBOL);

    setupCppTypeHierarchy();

    // The symbol under the caret, highlighted everywhere else it is used.
    // CppEditorWidget owns one of these itself; a view that is not one has
    // nobody to own it, so the plugin does - for the editor's lifetime.
    connect(EditorManager::instance(), &EditorManager::editorOpened, this,
            [](IEditor *editor) {
                if (!editor || TextEditor::TextEditorWidget::fromEditor(editor))
                    return;
                const auto document = qobject_cast<CppEditorDocument *>(editor->document());
                if (!document)
                    return;
                auto * const updater = new CppUseSelectionsUpdater(editor);
                updater->setParent(editor);

                // Renaming a local name happens in the view, and only what is
                // not local goes on to a search - the same order the widget
                // editor takes them in. The view finds this parented to the
                // editor and asks it first, so which backend answers the
                // searches makes no difference to it.
                auto * const renaming = new CppLocalRenaming(editor);
                renaming->setUseSelectionsUpdater(updater);
                connect(updater,
                        &CppUseSelectionsUpdater::selectionsForVariableUnderCursorUpdated,
                        renaming, &CppLocalRenaming::updateSelectionsForVariableUnderCursor);
                connect(document->document(), &QTextDocument::contentsChange, renaming,
                        &CppLocalRenaming::onContentsChangeOfEditorWidgetDocument);
                connect(renaming, &CppLocalRenaming::finished, document,
                        [document] { document->recalculateSemanticInfoDetached(); });
                connect(editor, &IEditor::cursorPositionChanged, updater,
                        [updater] { updater->scheduleUpdate(); });
                // And when the file has been read again: the ranges are
                // positions in a parse, so a new parse is new ranges.
                connect(document, &CppEditorDocument::semanticInfoUpdated, updater,
                        [updater] { updater->scheduleUpdate(); });

                // Find Usages and Rename Symbol. A view that is not a widget
                // asks through a relay rather than by being asked itself, and
                // only the built-in model needs answering here: with clangd in
                // charge the language client answers the same relay, and two
                // answers would be two searches.
                TextEditor::SymbolRequests * const requests
                    = TextEditor::symbolRequestsForEditor(editor);
                if (!requests || CppModelManager::usesClangd(document))
                    return;
                connect(requests, &TextEditor::SymbolRequests::requestUsages, editor,
                        [editor](const QTextCursor &cursor) {
                            CppEditor::findUsagesOf(editor, cursor);
                        });
                connect(requests, &TextEditor::SymbolRequests::requestRename, editor,
                        [editor](const QTextCursor &cursor) {
                            CppEditor::renameUsagesOf(editor, {}, cursor);
                        });

                // Which function the caret is in, which is what the toolbar
                // combo says. CppEditorWidget owns one of these itself; the
                // combo it fills is still a widget, so in a view that is not
                // one the outline is kept up to date and not yet drawn.
                auto * const outline = new CppEditorOutline(editor, document);
                outline->setParent(editor);
                connect(editor, &IEditor::cursorPositionChanged, outline,
                        [outline] { outline->updateIndex(); });

                // What Ctrl+U encloses next. CppEditorWidget answers this by
                // overriding selectBlockUp(), which a view that is not one
                // cannot do - so a C++ file here used to get the plain bracket
                // walk instead of the syntax tree.
                auto * const expander = new CppSelectionExpander(editor, document);
                expander->setParent(editor);
                connect(editor, &IEditor::cursorPositionChanged, expander,
                        [expander] { expander->caretMoved(); });

                // A function whose declaration and definition have drifted
                // apart, and the offer to bring the other one along.
                // CppEditorWidget owns one of these itself.
                auto * const declDefLink = new CppDeclDefLinkController(editor);
                declDefLink->setParent(editor);
                connect(editor, &IEditor::cursorPositionChanged, declDefLink,
                        [declDefLink] { declDefLink->scheduleUpdate(); });
                connect(document->document(), &QTextDocument::contentsChanged, declDefLink,
                        [declDefLink] { declDefLink->scheduleUpdate(); });
                connect(document, &CppEditorDocument::semanticInfoUpdated, declDefLink,
                        [declDefLink] { declDefLink->scheduleUpdate(); });
            });

    addSymbolActionToMenus(TextEditor::Constants::OPEN_TYPE_HIERARCHY);
    addSymbolActionToMenus(TextEditor::Constants::OPEN_CALL_HIERARCHY);

    // Refactoring sub-menu
    Command *sep = ActionManager::actionContainer(Constants::M_CONTEXT)
        ->addSeparator(Constants::G_SYMBOL);
    sep->action()->setObjectName(QLatin1String(Constants::M_REFACTORING_MENU_INSERTION_POINT));
}

void CppEditorPlugin::addActionsForSelections()
{
    const QList<Id> menus{Constants::M_TOOLS_CPP,  Constants::M_CONTEXT};

    addActionToMenus(menus, TextEditor::Constants::AUTO_INDENT_SELECTION, Constants::G_SELECTION);
    addActionToMenus(menus, TextEditor::Constants::UN_COMMENT_SELECTION, Constants::G_SELECTION);
}

void CppEditorPlugin::addPerFileActions()
{
    const QList<Id> menus{Constants::M_TOOLS_CPP, Constants::M_CONTEXT};
    const Context context(Constants::CPPEDITOR_ID);

    ActionBuilder switchAction(this, Constants::SWITCH_HEADER_SOURCE);
    switchAction.setText(Tr::tr("Switch Header/Source"));
    switchAction.setContext(context);
    switchAction.setScriptable(true);
    switchAction.setTouchBarText(Tr::tr("Header/Source", "text on macOS touch bar"));
    switchAction.addToContainers(menus, Constants::G_FILE);
    switchAction.addToContainer(Core::Constants::TOUCH_BAR, Core::Constants::G_TOUCHBAR_NAVIGATION);
    switchAction.setDefaultKeySequence(Qt::Key_F4);
    switchAction.addOnTriggered([] { CppModelManager::switchHeaderSource(false); });

    ActionBuilder switchInNextSplit(this, Constants::OPEN_HEADER_SOURCE_IN_NEXT_SPLIT);
    switchInNextSplit.setText(Tr::tr("Open Corresponding Header/Source in Next Split"));
    switchInNextSplit.setContext(context);
    switchInNextSplit.setScriptable(true);
    switchInNextSplit.setDefaultKeySequence(Tr::tr("Meta+E, F4"), Tr::tr("Ctrl+E, F4"));
    switchInNextSplit.addToContainers(menus, Constants::G_FILE);
    switchInNextSplit.addOnTriggered([] { CppModelManager::switchHeaderSource(true); });

    ActionBuilder openPreprocessor(this, Constants::OPEN_PREPROCESSOR_DIALOG);
    openPreprocessor.setText(Tr::tr("Additional Preprocessor Directives..."));
    openPreprocessor.setContext(context);
    openPreprocessor.setDefaultKeySequence({});
    openPreprocessor.addToContainers(menus, Constants::G_FILE);
    openPreprocessor.addOnTriggered(this, [] {
        Core::IEditor * const editor = EditorManager::currentEditor();
        if (const auto document = qobject_cast<CppEditorDocument *>(
                editor ? editor->document() : nullptr)) {
            document->showPreProcessorDialog();
        }
    });

    ActionBuilder showPreprocessed(this, Constants::SHOW_PREPROCESSED_FILE);
    showPreprocessed.setText(Tr::tr("Show Preprocessed Source"));
    showPreprocessed.setContext(context);
    showPreprocessed.addToContainers(menus, Constants::G_FILE);
    showPreprocessed.addOnTriggered(this, [] { CppModelManager::showPreprocessedFile(false); });

    ActionBuilder showPreprocessedInSplit(this, Constants::SHOW_PREPROCESSED_FILE_SPLIT);
    showPreprocessedInSplit.setText(Tr::tr("Show Preprocessed Source in Next Split"));
    showPreprocessedInSplit.setContext(context);
    showPreprocessedInSplit.addToContainers(menus, Constants::G_FILE);
    showPreprocessedInSplit.addOnTriggered([] { CppModelManager::showPreprocessedFile(true); });

    ActionBuilder foldComments(this, "CppTools.FoldCommentBlocks");
    foldComments.setText(Tr::tr("Fold All Comment Blocks"));
    foldComments.setContext(context);
    foldComments.addToContainers(menus, Constants::G_FILE);
    foldComments.addOnTriggered(this, [] { CppModelManager::foldComments(); });

    ActionBuilder unfoldComments(this, "CppTools.UnfoldCommentBlocks");
    unfoldComments.setText(Tr::tr("Unfold All Comment Blocks"));
    unfoldComments.setContext(context);
    unfoldComments.addToContainers(menus, Constants::G_FILE);
    unfoldComments.addOnTriggered(this, [] { CppModelManager::unfoldComments(); });

    ActionBuilder foldInactiveRegions(this, "CppTools.FoldInactiveRegions");
    foldInactiveRegions.setText(Tr::tr("Fold All Inactive Code"));
    foldInactiveRegions.setContext(context);
    foldInactiveRegions.addToContainers(menus, Constants::G_FILE);
    foldInactiveRegions.addOnTriggered(this, [] {
        CppModelManager::foldOrUnfoldInactiveRegions(true);
    });

    ActionBuilder unfoldInactiveRegions(this, "CppTools.UnfoldInactiveRegions");
    unfoldInactiveRegions.setText(Tr::tr("Unfold All Inactive Code"));
    unfoldInactiveRegions.setContext(context);
    unfoldInactiveRegions.addToContainers(menus, Constants::G_FILE);
    unfoldInactiveRegions.addOnTriggered(this, [] {
        CppModelManager::foldOrUnfoldInactiveRegions(false);
    });

    setupCppIncludeHierarchy();
}

void CppEditorPlugin::addGlobalActions()
{
    const QList<Id> menus{Constants::M_TOOLS_CPP, Constants::M_CONTEXT};

    ActionBuilder findUnusedFunctions(this, "CppTools.FindUnusedFunctions");
    findUnusedFunctions.setText(Tr::tr("Find Unused Functions"));
    findUnusedFunctions.addToContainers(menus, Constants::G_GLOBAL);
    findUnusedFunctions.addOnTriggered(this, [] { CppModelManager::findUnusedFunctions({}); });

    ActionBuilder findUnusedFunctionsSubProject(this, "CppTools.FindUnusedFunctionsInSubProject");
    findUnusedFunctionsSubProject.setText(Tr::tr("Find Unused C/C++ Functions"));
    for (ActionContainer *const projectContextMenu :
         {ActionManager::actionContainer(ProjectExplorer::Constants::M_SUBPROJECTCONTEXT),
          ActionManager::actionContainer(ProjectExplorer::Constants::M_PROJECTCONTEXT)}) {
        projectContextMenu->addSeparator(ProjectExplorer::Constants::G_PROJECT_TREE);
        projectContextMenu->addAction(findUnusedFunctionsSubProject.command(),
                                      ProjectExplorer::Constants::G_PROJECT_TREE);
    }
    findUnusedFunctionsSubProject.addOnTriggered(this, [] {
        if (const Node *const node = ProjectTree::currentNode(); node && node->asFolderNode())
            CppModelManager::findUnusedFunctions(node->directory());
    });

    ActionBuilder reparseChangedFiles(this,  Constants::UPDATE_CODEMODEL);
    reparseChangedFiles.setText(Tr::tr("Reparse Externally Changed Files"));
    reparseChangedFiles.bindContextAction(&d->m_reparseExternallyChangedFiles);
    reparseChangedFiles.addToContainers(menus, Constants::G_GLOBAL);
    reparseChangedFiles.addOnTriggered(CppModelManager::instance(),
                                       &CppModelManager::updateModifiedSourceFiles);
}

void CppEditorPlugin::registerVariables()
{
    MacroExpander * const expander = globalMacroExpander();

    // TODO: Per-project variants of these three?
    expander->registerVariable("Cpp:LicenseTemplate",
        Tr::tr("The license template."),
        [] { return globalCppFileSettings().licenseTemplate(); });
    expander->registerFileVariables("Cpp:LicenseTemplatePath",
        Tr::tr("The configured path to the license template"),
        [] { return globalCppFileSettings().licenseTemplatePath(); });
    expander->registerVariable(
        "Cpp:PragmaOnce",
        //: %1=#pragma once, %2=#ifndef
        Tr::tr("Insert \"%1\" instead of \"%2\" include guards into header file").arg("#pragma once", "#ifndef"),
        [] { return globalCppFileSettings().headerPragmaOnce() ? QString("true") : QString(); });
}

void CppEditorPlugin::registerTests()
{
    registerHighlighterTests(*this);
#ifdef WITH_TESTS
    addTest<CodegenTest>();
    addTest<CompilerOptionsBuilderTest>();
    addTest<CompletionTest>();
    addTest<CppMcpSupportTest>();
    addTestCreator(createFindParentImplTest);
    addTestCreator(createVirtualFunctionProposalTest);
    addTest<FunctionUtilsTest>();
    addTest<HeaderPathFilterTest>();
    addTestCreator(createCppCodeStyleAspectsTest);
    addTestCreator(createCppHeaderSourceTest);
    addTestCreator(createSymbolJumpTest);
    addTestCreator(createUseSelectionsTest);
    addTestCreator(createDeclDefLinkTest);
    addTestCreator(createIncludeGroupsTest);
    addTestCreator(createCppPreProcessorDialogTest);
    addTestCreator(createCppCodeModelInspectorTest);
    addTestCreator(createClangdSettingsTest);
    addTestCreator(createCppQuickFixSettingsTest);
    addTestCreator(createQuickFixAssistTest);
    addTestCreator(createCppOutlineTest);
    addTestCreator(createCodeWarningsTest);
    addTestCreator(createCppIncludeHierarchyWidgetTest);
    addTestCreator(createCppEditorOutlineTest);
    addTestCreator(createCppTypeHierarchyTest);
    addTestCreator(createCppSelectionExpansionTest);
    addTest<LocalSymbolsTest>();
    addTest<LocatorFilterTest>();
    addTest<ModelManagerTest>();
    addTest<PointerDeclarationFormatterTest>();
    addTest<ProjectFileCategorizerTest>();
    addTest<ProjectInfoGeneratorTest>();
    addTest<ProjectPartChooserTest>();
    addTest<SourceProcessorTest>();
    addTest<SymbolSearcherTest>();
    addTest<TypeHierarchyBuilderTest>();
    addTest<Tests::AutoCompleterTest>();
    addTest<Tests::DoxygenTest>();
    addTest<Tests::FileAndTokenActionsTest>();
    addTest<Tests::FollowSymbolTest>();
    addTest<Tests::IncludeHierarchyTest>();
    addTest<Tests::GlobalRenamingTest>();
    addTest<Tests::SelectionsTest>();
#endif
}

void CppEditorPluginPrivate::onTaskStarted(Id type)
{
    if (type == Constants::TASK_INDEX) {
        ActionManager::command(TextEditor::Constants::FIND_USAGES)->action()->setEnabled(false);
        ActionManager::command(TextEditor::Constants::RENAME_SYMBOL)->action()->setEnabled(false);
        m_reparseExternallyChangedFiles->setEnabled(false);
    }
}

void CppEditorPluginPrivate::onAllTasksFinished(Id type)
{
    if (type == Constants::TASK_INDEX) {
        ActionManager::command(TextEditor::Constants::FIND_USAGES)->action()->setEnabled(true);
        ActionManager::command(TextEditor::Constants::RENAME_SYMBOL)->action()->setEnabled(true);
        m_reparseExternallyChangedFiles->setEnabled(true);
    }
}

#ifdef WITH_TESTS

// Ctrl+U in a C++ file grows the selection along the syntax tree. The widget
// editor gets that from a CppEditorWidget override; a view that is not one has
// to be given it, or the reader silently gets the bracket walk instead.
class CppSelectionExpansionTest : public QObject
{
    Q_OBJECT

    static Utils::FilePath fileWithANestedExpression(CppEditor::Tests::TemporaryDir &dir)
    {
        return dir.createFile("expand.cpp",
                              "int compute(int a, int b)\n"
                              "{\n"
                              "    return a + b * 2;\n"
                              "}\n");
    }

    // On \a b in "b * 2" - inside an expression that is nested several nodes
    // deep inside the function body, so the syntax walk and the bracket walk
    // give visibly different answers.
    static bool putCaretOnTheInnerOperand(Core::IEditor *editor)
    {
        editor->gotoLine(3, 15);
        const QTextCursor cursor = TextEditor::textCursorOf(editor);
        QTextCursor probe = cursor;
        probe.select(QTextCursor::WordUnderCursor);
        return probe.selectedText() == "b";
    }

    static Core::IEditor *openWith(const Utils::FilePath &file, bool quick)
    {
        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        if (!factory)
            return nullptr;
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore(
            [factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        return Core::EditorManager::openEditor(file);
    }

private slots:
    // Four presses of Ctrl+U from the same caret, as text. The syntax walk and
    // the bracket walk disagree on the very first one, so this reads as the
    // whole difference between them rather than as a threshold.
    static QStringList walkFrom(Core::IEditor *editor)
    {
        QStringList walk;
        for (int i = 0; i < 4; ++i) {
            TextEditor::growSelectionIn(editor);
            walk << TextEditor::textCursorOf(editor).selectedText();
        }
        return walk;
    }

    static QStringList theSyntaxWalk()
    {
        return {"b", "b * 2", "a + b * 2", "return a + b * 2;"};
    }

private slots:
    void testGrowingASelection_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // Ctrl+U walks the syntax tree in both views. Against the widget editor
    // this is the fixture's control: it says the file, the caret and the
    // expected walk are ones the C++ model really produces, so a failure in
    // the Quick row is about the view and not about the expectation.
    void testGrowingASelection()
    {
        QFETCH(bool, quick);

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        CppEditor::Tests::TemporaryDir dir;
        QVERIFY(dir.isValid());
        const Utils::FilePath file = fileWithANestedExpression(dir);
        QVERIFY(!file.isEmpty());

        Core::IEditor * const editor = openWith(file, quick);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        QTRY_VERIFY2(document->semanticInfo().doc,
                     "the code model never parsed the file, so there is no tree to walk");

        QVERIFY2(putCaretOnTheInnerOperand(editor), "the caret is not where the test needs it");
        QCOMPARE(walkFrom(editor), theSyntaxWalk());

        // Ctrl+Shift+U retraces the walk it is in the middle of, which is the
        // part that needs the walk's own state to have survived: re-deriving
        // it from the selection would give the enclosing node again, not the
        // step before.
        QStringList back;
        for (int i = 0; i < 3; ++i) {
            TextEditor::shrinkSelectionIn(editor);
            back << TextEditor::textCursorOf(editor).selectedText();
        }
        QStringList retraced = theSyntaxWalk();
        retraced.removeLast();
        std::reverse(retraced.begin(), retraced.end());
        QCOMPARE(back, retraced);
    }

    // The setting the reader has for this, in the view that used to ignore it:
    // turning the syntax walk off has to leave the bracket walk, not leave the
    // syntax walk running because nothing consults the setting any more.
    void testTheSettingStillTurnsItOff()
    {
        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        auto &smart = TextEditor::globalBehaviorSettings().smartSelectionChanging;
        const bool wasSmart = smart();
        const QScopeGuard restoreSmart([&smart, wasSmart] { smart.setValue(wasSmart); });

        CppEditor::Tests::TemporaryDir dir;
        QVERIFY(dir.isValid());
        const Utils::FilePath file = fileWithANestedExpression(dir);
        QVERIFY(!file.isEmpty());

        Core::IEditor * const editor = openWith(file, true);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        QTRY_VERIFY(document->semanticInfo().doc);

        smart.setValue(false);
        QVERIFY(putCaretOnTheInnerOperand(editor));
        const QStringList walk = walkFrom(editor);
        QVERIFY2(walk.first() != theSyntaxWalk().first(),
                 "the syntax walk ran with the setting turned off");
        QVERIFY2(walk.first().contains("return"),
                 qPrintable("expected the bracket walk to take the whole body, got "
                            + walk.first()));
    }
};

QObject *createCppSelectionExpansionTest()
{
    return new CppSelectionExpansionTest;
}

#endif // WITH_TESTS

} // CppEditor::Internal

#include "cppeditorplugin.moc"
