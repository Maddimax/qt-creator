// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaengine.h"
#include "luapluginspec.h"
#include "luatr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/jsexpander.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <texteditor/texteditor.h>

#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QDebug>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QMenu>
#include <QToolBar>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <coreplugin/editormanager/ieditor.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>

using namespace Core;
using namespace Utils;
using namespace ExtensionSystem;

namespace Lua::Internal {

#ifdef WITH_TESTS
QObject *createLuaTextEditorTest();
#endif

const char M_SCRIPT[] = "Lua.Script";
const char G_SCRIPTS[] = "Lua.Scripts";
const char ACTION_SCRIPTS_BASE[] = "Lua.Scripts.";
const char ACTION_NEW_SCRIPT[] = "Lua.NewScript";

void setupActionModule();
void setupCoreModule();
void setupDeviceModule();
void setupFetchModule();
void setupGuiModule();
void setupHookModule();
void setupInstallModule();
void setupJsonModule();
void setupLocalSocketModule();
void setupMacroModule();
void setupMenuModule();
void setupMessageManagerModule();
void setupModeModule();
void setupProcessModule();
void setupProjectModule();
void setupQtModule();
void setupSettingsModule();
void setupTaskHubModule();
void setupTextEditorModule();
void setupTranslateModule();
void setupUtilsModule();

void setupLuaExpander(MacroExpander *expander);

class LuaJsExtension : public QObject
{
    Q_OBJECT

public:
    explicit LuaJsExtension(QObject *parent = nullptr)
        : QObject(parent)
    {}

    Q_INVOKABLE QString metaFolder() const
    {
        return Core::ICore::resourcePath("lua/meta").toFSPathString();
    }
};

class ItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(
        QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto label = new QLabel(parent);
        const QString text = index.data().toString();
        label->setText(text.startsWith("__ERROR__") ? text.mid(9) : text);
        label->setFont(option.font);
        label->setTextInteractionFlags(
            Qt::TextInteractionFlag::TextSelectableByMouse
            | Qt::TextInteractionFlag::TextSelectableByKeyboard);
        label->setAutoFillBackground(true);
        label->setSelection(0, text.size());
        return label;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
        const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        bool isError = opt.text.startsWith("__ERROR__");

        if (isError)
            opt.text = opt.text.mid(9);

        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
            painter->setPen(opt.palette.highlightedText().color());
        } else if (isError) {
            painter->setPen(creatorColor(Theme::Token_Notification_Danger_Default));
        } else {
            painter->setPen(opt.palette.text().color());
        }

        painter->drawText(opt.rect, opt.displayAlignment, opt.text);
    }
};

class LuaReplView : public QListView
{
    Q_OBJECT

    std::unique_ptr<LuaState> m_luaState;
    sol::function m_readCallback;

    QStringListModel m_model;

public:
    LuaReplView(QWidget *parent = nullptr)
        : QListView(parent)
    {
        setModel(&m_model);
        setItemDelegate(new ItemDelegate(this));
    }

    void showEvent(QShowEvent *) override
    {
        if (m_luaState) {
            return;
        }
        resetTerminal();
    }

    void handleRequestResult(const QString &result)
    {
        auto cb = m_readCallback;
        m_readCallback = {};
        cb(result);
    }

    void resetTerminal()
    {
        m_model.setStringList({});
        m_readCallback = {};

        QFile f(":/lua/scripts/ilua.lua");
        QTC_CHECK(f.open(QIODevice::ReadOnly));
        const auto ilua = QString::fromUtf8(f.readAll());
        m_luaState = runScript(ilua, "ilua.lua", [this](sol::state &lua) {
            lua["print"] = [this](sol::variadic_args va) {
                const QString msgs = variadicToStringList(va).join("\t").replace("\r\n", "\n");
                m_model.setStringList(m_model.stringList() << msgs);
                scrollToBottom();
            };
            lua["LuaCopyright"] = LUA_COPYRIGHT;

            sol::table async = lua.script("return require('async')", "_ilua_").get<sol::table>();
            sol::function wrap = async["wrap"];

            lua["readline_cb"] = [this](const QString &prompt, sol::function callback) {
                scrollToBottom();
                emit inputRequested(prompt);
                m_readCallback = callback;
            };

            lua["readline"] = wrap(lua["readline_cb"]);
        });

        QListView::reset();
    }

signals:
    void inputRequested(const QString &prompt);
};

class LineEdit : public FancyLineEdit
{
public:
    using FancyLineEdit::FancyLineEdit;
};

class LuaPane : public Core::IOutputPane
{
    Q_OBJECT

protected:
    QWidget *m_ui{nullptr};
    LuaReplView *m_terminal{nullptr};

public:
    LuaPane(QObject *parent = nullptr)
        : Core::IOutputPane(parent)
    {
        setId("LuaPane");
        setDisplayName(Tr::tr("Lua"));
        setPriorityInStatusBar(-20);
    }

    QWidget *outputWidget(QWidget *parent) override
    {
        using namespace Layouting;

        if (!m_ui && parent) {
            m_terminal = new LuaReplView;
            LineEdit *inputEdit = new LineEdit;
            QLabel *prompt = new QLabel;

            // clang-format off
            m_ui = Column {
                noMargin,
                spacing(0),
                m_terminal,
                Row { prompt, inputEdit },
            }.emerge();
            // clang-format on

            inputEdit->setReadOnly(true);
            inputEdit->setHistoryCompleter(Utils::Key("LuaREPL.InputHistory"), false, 200);

            // We need to use a QueuedConnection here so that we don't interfere with the history
            // completer. Otherwise it will get out of sync between selecting an item and copying
            // it into the text input field.
            connect(
                inputEdit,
                &QLineEdit::returnPressed,
                this,
                [this, inputEdit] {
                    inputEdit->setReadOnly(true);
                    m_terminal->handleRequestResult(inputEdit->text());
                    inputEdit->clear();
                },
                Qt::QueuedConnection);

            connect(
                m_terminal,
                &LuaReplView::inputRequested,
                this,
                [prompt, inputEdit](const QString &p) {
                    prompt->setText(p);
                    inputEdit->setReadOnly(false);
                });
        }

        return m_ui;
    }

    void visibilityChanged(bool) override {};

    void clearContents() override
    {
        if (m_terminal)
            m_terminal->resetTerminal();
    }
    void setFocus() override { outputWidget(nullptr)->setFocus(); }
    bool hasFocus() const override { return true; }
    bool canFocus() const override { return true; }

    bool canNavigate() const override { return false; }
    bool canNext() const override { return false; }
    bool canPrevious() const override { return false; }
    void goToNext() override {}
    void goToPrev() override {}

    QList<QWidget *> toolBarWidgets() const override { return {}; }
};

class LuaPlugin : public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Lua.json")

private:
    LuaPane *m_pane = nullptr;
    std::unique_ptr<FilePathWatcher> m_userScriptsWatcher;

public:
    LuaPlugin() {}

    void initialize() final
    {
#ifdef WITH_TESTS
        addTestCreator(createLuaTextEditorTest);
#endif
        IOptionsPage::registerCategory(
            "ZY.Lua", Tr::tr("Lua"), ":/lua/images/settingscategory_lua.png");

        setupLuaEngine(this);

        registerProvider("async", ":/lua/scripts/async.lua");
        registerProvider("inspect", ":/lua/scripts/inspect.lua");

        setupActionModule();
        setupCoreModule();
        setupDeviceModule();
        setupFetchModule();
        setupGuiModule();
        setupHookModule();
        setupInstallModule();
        setupJsonModule();
        setupLocalSocketModule();
        setupMacroModule();
        setupMenuModule();
        setupMessageManagerModule();
        setupModeModule();
        setupProcessModule();
        setupProjectModule();
        setupQtModule();
        setupSettingsModule();
        setupTaskHubModule();
        setupTextEditorModule();
        setupTranslateModule();
        setupUtilsModule();

        Core::JsExpander::registerGlobalObject("Lua", [] { return new LuaJsExtension(); });

        setupLuaExpander(globalMacroExpander());

        pluginSpecsFromArchiveFactories().push_back([](const FilePath &path) -> QList<PluginSpec *> {
            if (path.isFile()) {
                if (path.suffix() == "lua") {
                    Utils::Result<PluginSpec *> spec = loadPlugin(path);
                    QTC_CHECK_RESULT(spec);
                    if (spec)
                        return {*spec};
                }
                return {};
            }

            QList<PluginSpec *> plugins;
            const FilePaths dirs = path.dirEntries(DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot);
            for (const auto &dir : dirs) {
                const auto specFilePath = dir / (dir.fileName() + ".lua");
                if (specFilePath.exists()) {
                    Utils::Result<PluginSpec *> spec = loadPlugin(specFilePath);
                    QTC_CHECK_RESULT(spec);
                    if (spec)
                        plugins.push_back(*spec);
                }
            }
            return plugins;
        });

        m_pane = new LuaPane(this);

        //register actions
        ActionContainer *toolsContainer = ActionManager::actionContainer(Core::Constants::M_TOOLS);

        ActionContainer *scriptContainer = ActionManager::createMenu(M_SCRIPT);

        Command *newScriptCommand = ActionBuilder(this, ACTION_NEW_SCRIPT)
                                        .setScriptable(true)
                                        .setText(Tr::tr("New Script..."))
                                        .addToContainer(M_SCRIPT)
                                        .addOnTriggered([]() {
                                            auto command = Core::ActionManager::command(Utils::Id("Wizard.Impl.Q.QCreatorScript"));
                                            if (command && command->action())
                                                command->action()->trigger();
                                            else
                                                qWarning("Failed to get wizard command. UI changed?");
                                        })
                                        .command();

        scriptContainer->addAction(newScriptCommand);
        scriptContainer->addSeparator();
        scriptContainer->appendGroup(G_SCRIPTS);

        scriptContainer->menu()->setTitle(Tr::tr("Scripting"));
        toolsContainer->addMenu(scriptContainer);

        const Utils::FilePath userScriptsPath = Core::ICore::userResourcePath("scripts");
        userScriptsPath.ensureWritableDir();
        if (auto watch = userScriptsPath.watch()) {
            m_userScriptsWatcher.swap(*watch);
            connect(
                m_userScriptsWatcher.get(),
                &FilePathWatcher::pathChanged,
                this,
                &LuaPlugin::scanForScripts);
        }

        scanForScripts();

        connect(
            EditorManager::instance(),
            &EditorManager::editorOpened,
            this,
            &LuaPlugin::onEditorOpened);

        ActionBuilder(this, Id(ACTION_SCRIPTS_BASE).withSuffix("current"))
            .setText(Tr::tr("Run Current Script"))
            .addOnTriggered([]() {
                if (auto textEditor = TextEditor::BaseTextEditor::currentTextEditor()) {
                    const FilePath path = textEditor->document()->filePath();
                    if (path.isChildOf(Core::ICore::userResourcePath("scripts")))
                        runScript(path);
                }
            });
    }

    bool delayedInitialize() final
    {
        scanForPlugins(PluginManager::pluginPaths());
        return true;
    }

    void scanForPlugins(const FilePaths &pluginPaths)
    {
        QSet<PluginSpec *> plugins;
        for (const FilePath &path : pluginPaths) {
            const FilePaths folders =
                path.dirEntries(FileFilter({}, DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot));

            for (const FilePath &folder : folders) {
                FilePath script = folder / (folder.baseName() + ".lua");
                if (!script.exists()) {
                    const FilePaths contents =
                        folder.dirEntries(DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot);
                    if (contents.empty())
                        continue;

                    for (const FilePath &subfolder : contents) {
                        script = subfolder / (subfolder.baseName() + ".lua");
                        if (!script.exists()) {
                            script.clear();
                            continue;
                        }
                        break;
                    }
                }

                if (script.isEmpty()) {
                    continue;
                }

                const Result<LuaPluginSpec *> result = loadPlugin(script);

                if (!result) {
                    qWarning() << "Failed to load plugin" << script << ":" << result.error();
                    MessageManager::writeFlashing(Tr::tr("Failed to load plugin %1: %2")
                                                      .arg(script.toUserOutput())
                                                      .arg(result.error()));
                    continue;
                }

                plugins.insert(*result);
            }
        }

        PluginManager::addPlugins({plugins.begin(), plugins.end()});
        PluginManager::loadPluginsAtRuntime(plugins);
    }

    void scanForScripts()
    {
        const FilePath userScriptsPath = Core::ICore::userResourcePath("scripts");
        if (userScriptsPath.exists())
            scanForScriptsIn(userScriptsPath);

        const FilePath scriptsPath = Core::ICore::resourcePath("lua/scripts");
        if (scriptsPath.exists())
            scanForScriptsIn(scriptsPath);
    }

    void scanForScriptsIn(const FilePath &scriptsPath)
    {
        ActionContainer *scriptContainer = ActionManager::actionContainer(M_SCRIPT);

        const FilePaths scripts = scriptsPath.dirEntries(FileFilter({"*.lua"}, DirFilterFlag::Files));
        for (const FilePath &script : scripts) {
            const Id base = Id(ACTION_SCRIPTS_BASE).withSuffix(script.baseName());
            const Id menuId = base.withSuffix(".Menu");
            if (!ActionManager::actionContainer(menuId)) {
                ActionContainer *container = ActionManager::createMenu(menuId);
                scriptContainer->addMenu(container);
                auto menu = container->menu();
                menu->setTitle(script.baseName());
                ActionBuilder(this, base)
                    .setText(script.baseName())
                    .setToolTip(Tr::tr("Run script \"%1\"").arg(script.toUserOutput()))
                    .addOnTriggered([script]() { runScript(script); });
                connect(menu->addAction(Tr::tr("Run")), &QAction::triggered, this, [script]() {
                    runScript(script);
                });
                connect(menu->addAction(Tr::tr("Edit")), &QAction::triggered, this, [script]() {
                    Core::EditorManager::openEditor(script);
                });
            }
        }
    }

    void onEditorOpened(Core::IEditor *editor)
    {
        const FilePath path = editor->document()->filePath();
        if (path.isChildOf(Core::ICore::userResourcePath("scripts"))
            || path.isChildOf(Core::ICore::resourcePath("lua/scripts"))) {
            // The button goes on a text editor widget's tool bar, and a script
            // open in a view that has no widget has nowhere to put it. Every
            // text file used to open in one, so the cast could not fail and
            // was not checked; now it can, and an unchecked one is a crash on
            // opening a script.
            auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
            if (!textEditor || !textEditor->editorWidget())
                return;
            TextEditor::TextEditorWidget *editorWidget = textEditor->editorWidget();
            editorWidget->toolBar()
                ->addAction(Utils::Icons::RUN_SMALL_TOOLBAR.icon(), Tr::tr("Run"), [path]() {
                    runScript(path);
                });
        }
    }

    static void runScript(const FilePath &script)
    {
        static std::map<FilePath, std::unique_ptr<Utils::LuaState>> scriptStates;
        scriptStates.erase(script);

        Result<QByteArray> content = script.fileContents();
        if (content) {
            auto state = Lua::runScript(QString::fromUtf8(*content), script.fileName());
            scriptStates[script] = std::move(state);
            return;
        }

        MessageManager::writeFlashing(
            Tr::tr("Failed to read script \"%1\": %2")
                .arg(script.toUserOutput())
                .arg(content.error()));
    }
};

#ifdef WITH_TESTS

// The bindings had no test of any kind, so "TextEditor.currentEditor() is nil
// on a C++ file" went unnoticed until it was looked for. A script is the only
// thing that can say whether a binding works, so the harness runs one.
class LuaTextEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAScriptSeesTheEditorInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testAScriptSeesTheEditorInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("lua-editor-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("script.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint beta = 2;\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
        editor->gotoLine(2, 4);

        // What a script actually sees, reported back through a function the
        // fixture puts in the state.
        bool ran = false;
        bool hasEditor = false;
        int blockNumber = -1;
        const std::unique_ptr<Utils::LuaState> state = runScript(
            R"(
                local te = require("TextEditor")
                local editor = te.currentEditor()
                if editor == nil then
                    report(false, -1)
                    return
                end
                report(true, editor:cursor():mainCursor():blockNumber())
            )",
            "lua-editor-any-view-test",
            [&](sol::state &lua) {
                lua["report"] = [&](bool found, int line) {
                    ran = true;
                    hasEditor = found;
                    blockNumber = line;
                };
            });

        QVERIFY2(ran, "the script did not run to the point of reporting anything");
        QVERIFY2(hasEditor, "a script asking for the current editor was handed nil");
        // And it is this editor, at the caret the fixture put there.
        QCOMPARE(blockNumber, 1);
    }
};

QObject *createLuaTextEditorTest()
{
    return new LuaTextEditorTest;
}

#endif // WITH_TESTS

} // namespace Lua::Internal

#include "luaplugin.moc"
