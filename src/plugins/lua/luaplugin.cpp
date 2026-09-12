// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaengine.h"
#include "luapluginspec.h"
#include "luatr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/inavigationwidgetfactory.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/jsexpander.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <texteditor/texteditor.h>

#include <utils/environment.h>
#include <utils/historycompleter.h>
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
#include <QStyledItemDelegate>
#include <coreplugin/editormanager/ieditor.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

using namespace Core;
using namespace Utils;
using namespace ExtensionSystem;

namespace Lua::Internal {

#ifdef WITH_TESTS
QObject *createLuaTextEditorTest();
QObject *createLuaReplTest();
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

// What the REPL has said so far. Whether a line is an error is a role, not a
// marker inside the text, so that a line of output is never mistaken for one.
class LuaReplModel : public QAbstractListModel
{
public:
    enum Roles { IsErrorRole = Qt::UserRole };

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_lines.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_lines.size())
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return m_lines.at(index.row()).text;
        case IsErrorRole:
            return m_lines.at(index.row()).isError;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        QHash<int, QByteArray> names = QAbstractListModel::roleNames();
        names[IsErrorRole] = "isError";
        return names;
    }

    void append(const QString &text, bool isError)
    {
        beginInsertRows({}, m_lines.size(), m_lines.size());
        m_lines.append({text, isError});
        endInsertRows();
    }

    void clear()
    {
        beginResetModel();
        m_lines.clear();
        endResetModel();
    }

private:
    struct Line
    {
        QString text;
        bool isError = false;
    };

    QList<Line> m_lines;
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
        label->setText(text);
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

        const bool isError = index.data(LuaReplModel::IsErrorRole).toBool();

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

// The REPL itself: the Lua state, what it has said, and whether it is waiting
// to be told something. No view, because whether the terminal is waiting and
// what it is waiting with are what a view draws rather than what it owns.
class LuaRepl : public QObject
{
    Q_OBJECT

public:
    LuaReplModel *model() { return &m_model; }

    bool isWaitingForInput() const { return m_readCallback.valid(); }
    QString prompt() const { return m_prompt; }
    bool isStarted() const { return m_luaState != nullptr; }

    void submit(const QString &text)
    {
        QTC_ASSERT(isWaitingForInput(), return);
        const sol::function callback = m_readCallback;
        m_readCallback = {};
        setPrompt({});
        callback(text);
    }

    void reset()
    {
        m_model.clear();
        m_readCallback = {};
        setPrompt({});

        QFile f(":/lua/scripts/ilua.lua");
        QTC_CHECK(f.open(QIODevice::ReadOnly));
        const auto ilua = QString::fromUtf8(f.readAll());
        m_luaState = runScript(ilua, "ilua.lua", [this](sol::state &lua) {
            lua["print"] = [this](sol::variadic_args va) { say(va, false); };
            lua["printError"] = [this](sol::variadic_args va) { say(va, true); };
            lua["LuaCopyright"] = LUA_COPYRIGHT;

            sol::table async = lua.script("return require('async')", "_ilua_").get<sol::table>();
            sol::function wrap = async["wrap"];

            lua["readline_cb"] = [this](const QString &prompt, sol::function callback) {
                m_readCallback = callback;
                setPrompt(prompt);
            };

            lua["readline"] = wrap(lua["readline_cb"]);
        });
    }

signals:
    void promptChanged(const QString &prompt);
    // A line arrived, which is a view's cue to follow it down.
    void linePrinted();

private:
    void say(sol::variadic_args va, bool isError)
    {
        m_model.append(variadicToStringList(va).join("\t").replace("\r\n", "\n"), isError);
        emit linePrinted();
    }

    void setPrompt(const QString &prompt)
    {
        if (m_prompt == prompt)
            return;
        m_prompt = prompt;
        emit promptChanged(m_prompt);
    }

    std::unique_ptr<LuaState> m_luaState;
    sol::function m_readCallback;
    QString m_prompt;
    LuaReplModel m_model;
};

// What the Qt Quick view needs of the REPL above. One per view and short
// lived: Core::createQmlView() parents the controller to the widget it hands
// back, while the REPL belongs to the pane and outlives every view of it.
class LuaReplController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QAbstractItemModel *history READ history CONSTANT)
    Q_PROPERTY(QString prompt READ prompt NOTIFY promptChanged)

public:
    LuaReplController(LuaRepl *repl, QObject *parent = nullptr)
        : QObject(parent)
        , m_repl(repl)
        , m_history(new HistoryCompleter(Key("LuaREPL.InputHistory"), 200, this))
    {
        connect(m_repl, &LuaRepl::promptChanged, this, &LuaReplController::promptChanged);
        connect(m_repl, &LuaRepl::linePrinted, this, &LuaReplController::linePrinted);
    }

    QAbstractItemModel *model() const { return m_repl->model(); }
    QAbstractItemModel *history() const { return m_history->model(); }
    QString prompt() const { return m_repl->prompt(); }

    Q_INVOKABLE void submit(const QString &text)
    {
        if (!m_repl->isWaitingForInput())
            return;
        if (!text.isEmpty())
            m_history->addEntry(text);
        m_repl->submit(text);
    }

    // Started here rather than on first paint: a scene is built before it is
    // shown, and a REPL that has not run has nothing for the view to draw.
    Q_INVOKABLE void start()
    {
        if (!m_repl->isStarted())
            m_repl->reset();
    }

signals:
    void promptChanged(const QString &prompt);
    void linePrinted();

private:
    LuaRepl *m_repl;
    HistoryCompleter *m_history;
};

class LuaReplView : public QListView
{
    Q_OBJECT

public:
    LuaReplView(LuaRepl *repl, QWidget *parent = nullptr)
        : QListView(parent)
        , m_repl(repl)
    {
        setModel(m_repl->model());
        setItemDelegate(new ItemDelegate(this));
        connect(m_repl, &LuaRepl::linePrinted, this, &QListView::scrollToBottom);
    }

    void showEvent(QShowEvent *) override
    {
        if (!m_repl->isStarted())
            m_repl->reset();
    }

private:
    LuaRepl *m_repl;
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
    LuaRepl m_repl;

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
            // The Qt Quick pane is what a reader gets. QTC_WIDGET_LUA_PANE
            // asks for the QListView below, and a build with no front end to
            // host QML gets it without asking.
            if (!Utils::qtcEnvironmentVariableIsSet("QTC_WIDGET_LUA_PANE")
                && Core::hasQmlViewFactory()) {
                auto * const controller = new LuaReplController(&m_repl);
                m_ui = Core::createQmlView(QUrl("qrc:/qt/qml/QtCreator/Lua/LuaReplPane.qml"),
                                           controller);
                if (m_ui)
                    return m_ui;
                delete controller;
            }

            auto * const terminal = new LuaReplView(&m_repl);
            LineEdit *inputEdit = new LineEdit;
            QLabel *prompt = new QLabel;

            inputEdit->setReadOnly(true);
            inputEdit->setHistoryCompleter(Utils::Key("LuaREPL.InputHistory"), false, 200);

            // Queued, so that it does not interfere with the history
            // completer: otherwise selecting an item and copying it into the
            // field get out of sync.
            connect(
                inputEdit,
                &QLineEdit::returnPressed,
                this,
                [this, inputEdit] {
                    inputEdit->setReadOnly(true);
                    m_repl.submit(inputEdit->text());
                    inputEdit->clear();
                },
                Qt::QueuedConnection);

            connect(&m_repl, &LuaRepl::promptChanged, this, [prompt, inputEdit](const QString &p) {
                prompt->setText(p);
                inputEdit->setReadOnly(p.isEmpty());
            });

            // clang-format off
            m_ui = Column {
                noMargin,
                spacing(0),
                terminal,
                Row { prompt, inputEdit },
            }.emerge();
            // clang-format on
        }

        return m_ui;
    }

    void visibilityChanged(bool) override {};

    void clearContents() override { m_repl.reset(); }
    void setFocus() override { outputWidget(nullptr)->setFocus(); }
    bool hasFocus() const override { return true; }
    bool canFocus() const override { return true; }

    bool canNavigate() const override { return false; }
    bool canNext() const override { return false; }
    bool canPrevious() const override { return false; }
    void goToNext() override {}
    void goToPrev() override {}

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
        addTestCreator(createLuaReplTest);
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

class LuaReplTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheReplSaysWhichOfItsLinesAreErrors()
    {
        // No view anywhere in this test: the REPL is the Lua state and what
        // it has said, and neither of those needs drawing.
        LuaRepl repl;
        repl.reset();
        const LuaReplModel * const model = repl.model();

        QVERIFY2(model->rowCount() > 0, "the REPL said nothing at all on being started");
        const int banner = model->rowCount() - 1;
        QVERIFY2(!model->index(banner).data(LuaReplModel::IsErrorRole).toBool(),
                 "the REPL's own greeting is reported as an error");

        repl.submit("return 6 * 7");
        QCOMPARE(model->rowCount(), banner + 2);
        const QModelIndex answer = model->index(model->rowCount() - 1);
        QVERIFY2(answer.data().toString().contains("42"),
                 qPrintable("the REPL answered \"" + answer.data().toString() + "\""));
        QVERIFY(!answer.data(LuaReplModel::IsErrorRole).toBool());

        repl.submit("this is not lua(");
        const QModelIndex failed = model->index(model->rowCount() - 1);
        QVERIFY2(failed.data(LuaReplModel::IsErrorRole).toBool(),
                 qPrintable("broken code was reported as ordinary output: \""
                            + failed.data().toString() + "\""));
        QVERIFY2(!failed.data().toString().contains("__ERROR__"),
                 "the marker that used to say this is still in the text a reader sees");
    }

    void testTheReplSaysWhenItIsWaitingAndWhatWith()
    {
        // Whether the terminal wants typing, and the prompt to show beside the
        // field, used to exist only as the argument of a signal whichever view
        // happened to be listening caught. A view that opens later - or a
        // second one on the same REPL - has to be able to ask.
        LuaRepl repl;
        QVERIFY2(!repl.isWaitingForInput(), "a REPL that has not started is asking for input");
        QVERIFY(repl.prompt().isEmpty());

        QSignalSpy prompts(&repl, &LuaRepl::promptChanged);
        repl.reset();

        QVERIFY2(repl.isWaitingForInput(), "the REPL started and never asked for a line");
        QVERIFY2(!repl.prompt().isEmpty(), "the REPL is waiting with nothing to show beside it");
        QVERIFY2(!prompts.isEmpty(), "nothing was told that the REPL started waiting");

        const QString waitingWith = repl.prompt();
        repl.submit("return 1");
        QVERIFY2(repl.isWaitingForInput(), "the REPL answered and never asked again");
        QCOMPARE(repl.prompt(), waitingWith);
        // Empty in between, which is what tells a field to stop taking typing
        // while the answer is being worked out.
        QVERIFY2(prompts.size() >= 3,
                 qPrintable(QString("the prompt changed %1 times, so it never went empty")
                                .arg(prompts.size())));
    }

    void testAPrintedLineDoesNotThrowAwayTheOnesBeforeIt()
    {
        // Every print used to rebuild the whole list, so a reader who had
        // selected a line lost the selection each time the REPL spoke.
        LuaRepl repl;
        repl.reset();

        QSignalSpy resets(repl.model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserts(repl.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy printed(&repl, &LuaRepl::linePrinted);

        repl.submit("return 1");

        QVERIFY2(resets.isEmpty(), "printing a line reset the whole list");
        QVERIFY2(!inserts.isEmpty(), "a line arrived without the view being told where");
        QVERIFY2(!printed.isEmpty(), "no view would know to follow the new line down");
    }

    void testAModelRowNamesItsErrorFlagForAViewThatAsksByName()
    {
        LuaReplModel model;
        QVERIFY2(model.roleNames().values().contains("isError"),
                 "a view that reads roles by name cannot tell the errors apart");
    }

    void testBuildingTheQuickPaneStartsTheReplAndBindsToIt()
    {
        if (!Core::hasQmlViewFactory())
            QSKIP("no QML view factory is installed, so there is nothing to host the pane with");

        // A scene that loads but warns is a scene with a broken binding in it,
        // and the suite stays green either way. Collected here because the Lua
        // plugin cannot reach the QML engine to ask it directly.
        static QStringList complaints;
        static QtMessageHandler previous = nullptr;
        complaints.clear();
        previous = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext &context, const QString &message) {
                if (type == QtWarningMsg && message.contains("LuaReplPane.qml"))
                    complaints << message;
                // Forwarded, not swallowed: a handler that eats every message
                // takes the log away from whatever fails next.
                if (previous)
                    previous(type, context, message);
            });
        const QScopeGuard restoreHandler([] { qInstallMessageHandler(previous); });

        // No Qt Quick headers here on purpose: the seam hands back a QWidget,
        // and a plugin that had to know it was a QQuickWidget would be a seam
        // that leaks. What this can see is that the scene loaded and ran -
        // Component.onCompleted calls start(), so a REPL that is running is
        // proof the QML parsed, instantiated and reached the controller.
        LuaRepl repl;
        QVERIFY(!repl.isStarted());
        auto * const controller = new LuaReplController(&repl);
        const std::unique_ptr<QWidget> owned(
            Core::createQmlView(QUrl("qrc:/qt/qml/QtCreator/Lua/LuaReplPane.qml"), controller));
        QVERIFY2(owned, "the pane was not built at all");
        QVERIFY2(repl.isStarted(),
                 "the scene never reached the controller, so the QML did not load");

        // Shown, so that the list builds delegates: a binding that is only
        // wrong inside one is not reported until one exists.
        owned->resize(400, 300);
        owned->show();
        QVERIFY(QTest::qWaitForWindowExposed(owned.get()));
        QTRY_VERIFY2(repl.model()->rowCount() > 0, "the REPL drew no lines to make delegates of");
        QTRY_VERIFY2(repl.isWaitingForInput(), "the REPL started and never asked for a line");
        QVERIFY2(complaints.isEmpty(), qPrintable(complaints.join("; ")));

        // What the view binds to is the REPL's own model, not a copy.
        QCOMPARE(controller->model(), repl.model());
        QCOMPARE(controller->prompt(), repl.prompt());

        const int before = repl.model()->rowCount();
        controller->submit("return 6 * 7");
        QCOMPARE(repl.model()->rowCount(), before + 1);
        QVERIFY(repl.model()->index(before).data().toString().contains("42"));
    }

    void testTheQuickPaneRemembersWhatWasTypedBeforeIt()
    {
        if (!Core::hasQmlViewFactory())
            QSKIP("no QML view factory is installed, so there is nothing to host the pane with");

        // The history the widget line edit completes from is the history the
        // Qt Quick field walks: same key, so a reader who switches gets their
        // own lines back.
        LuaRepl repl;
        auto * const controller = new LuaReplController(&repl, this);
        QAbstractItemModel * const history = controller->history();
        QVERIFY(history);
        const int before = history->rowCount();

        repl.reset();
        QTRY_VERIFY(repl.isWaitingForInput());
        controller->submit("return 1");
        QCOMPARE(history->rowCount(), before + 1);
        QCOMPARE(history->index(0, 0).data().toString(), QString("return 1"));

        // An empty line is not worth recalling.
        QTRY_VERIFY(repl.isWaitingForInput());
        controller->submit("");
        QCOMPARE(history->rowCount(), before + 1);
    }

    void testTheInputHistoryIsAModelAViewCanRead()
    {
        // The input line is a FancyLineEdit with a history completer, which
        // was the one part of this pane with no obvious QML equivalent. It
        // needs none: the completer keeps its history in a model, so a view
        // that is not a QLineEdit can offer the same recall from the same
        // entries.
        auto * const completer
            = new Utils::HistoryCompleter(Utils::Key("LuaREPL.InputHistoryTest"), 200, this);
        QAbstractItemModel * const history = completer->model();
        QVERIFY2(history, "the history is not readable as a model at all");
        const int before = history->rowCount();
        completer->addEntry("return 6 * 7");
        QCOMPARE(history->rowCount(), before + 1);
        QCOMPARE(history->index(0, 0).data().toString(), QString("return 6 * 7"));
    }
};

QObject *createLuaReplTest()
{
    return new LuaReplTest;
}

QObject *createLuaTextEditorTest()
{
    return new LuaTextEditorTest;
}

#endif // WITH_TESTS

} // namespace Lua::Internal

#include "luaplugin.moc"
