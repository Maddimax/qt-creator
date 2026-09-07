// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "autotestplugin.h"

#include "autotestconstants.h"
#include "autotesticons.h"
#include "autotesttr.h"
#include "mcptools.h"
#include "projectsettingswidget.h"
#include "testcodeparser.h"
#include "testconfiguration.h"
#include "testframeworkmanager.h"
#include "testnavigationwidget.h"
#include "testprojectsettings.h"
#include "testresultspane.h"
#include "testrunner.h"
#include "testtreeitem.h"
#include "testtreemodel.h"

#include "boost/boosttestframework.h"
#include "catch/catchtestframework.h"
#include "ctest/ctesttool.h"
#include "gtest/gtestframework.h"
#include "qtest/datataglocatorfilter.h"
#include "qtest/qttestframework.h"
#include "quick/quicktestframework.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <cplusplus/CppDocument.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>

#include <cppeditor/cppeditorconstants.h>
#include <cppeditor/cppmodelmanager.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/buildmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectsettings.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>

#include <texteditor/textdocument.h>
#ifdef WITH_TESTS
#include <utils/temporarydirectory.h>
#include <QtTest>
#endif
#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/processinterface.h>
#include <utils/textutils.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QTextCursor>

#ifdef WITH_TESTS
#include "autotestunittests.h"
#include "testresultmodel.h"
#include "testsettings_test.h"
#endif

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Autotest::Internal {

class AutotestPluginPrivate final : public QObject
{
    Q_OBJECT
public:
    AutotestPluginPrivate();
    ~AutotestPluginPrivate() final;

    TestResultsPane *m_resultsPane = nullptr;
    QMap<QString, ChoicePair> m_runconfigCache;

    void initializeMenuEntries();
    void onRunAllTriggered(TestRunMode mode);
    void onRunSelectedTriggered(TestRunMode mode);
    void onRunFailedTriggered();
    void onRunFileTriggered();
    void onRunUnderCursorTriggered(TestRunMode mode);
    void onDisableTemporarily(bool disable);

    TestCodeParser m_testCodeParser;
    TestTreeModel m_testTreeModel{&m_testCodeParser};
    TestRunner m_testRunner;
    DataTagLocatorFilter m_dataTagLocatorFilter;
    QMetaObject::Connection m_testTreeModelConnection;
};

static AutotestPluginPrivate *dd = nullptr;

AutotestPluginPrivate::AutotestPluginPrivate()
{
    dd = this; // Needed as the code below access it via the static plugin interface
    initializeMenuEntries();
    TestFrameworkManager::registerTestFramework(&theQtTestFramework());
    TestFrameworkManager::registerTestFramework(&theQuickTestFramework());
    TestFrameworkManager::registerTestFramework(&theGTestFramework());
    TestFrameworkManager::registerTestFramework(&theBoostTestFramework());
    TestFrameworkManager::registerTestFramework(&theCatchFramework());

    TestFrameworkManager::registerTestTool(&theCTestTool());

    m_resultsPane = TestResultsPane::instance();

    setupAutotestProjectPanel();

    TestFrameworkManager::activateFrameworksAndToolsFromSettings();
    m_testTreeModel.synchronizeTestFrameworks();
    m_testTreeModel.synchronizeTestTools();

    auto projectManager = ProjectManager::instance();
    connect(projectManager, &ProjectManager::startupProjectChanged,
            this, [this] { m_runconfigCache.clear(); });
}

AutotestPluginPrivate::~AutotestPluginPrivate()
{
    delete m_resultsPane;
}

TestProjectSettings *testProjectSettings(Project *project)
{
    return ProjectExplorer::projectSettings<TestProjectSettings>(project);
}

void AutotestPluginPrivate::initializeMenuEntries()
{
    const Id menuId = Constants::MENU_ID;

    MenuBuilder(menuId)
        .setTitle(Tr::tr("&Tests"))
        .setOnAllDisabledBehavior(ActionContainer::Show)
        .addToContainer(Core::Constants::M_TOOLS);

    ActionBuilder(this, Constants::ACTION_RUN_ALL_ID)
        .setText(Tr::tr("Run &All Tests"))
        .setIcon(Utils::Icons::RUN_SMALL.icon())
        .setToolTip(Tr::tr("Run All Tests"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+A"), Tr::tr("Alt+Shift+T,Alt+A"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunAllTriggered(TestRunMode::Run); });

    ActionBuilder(this, Constants::ACTION_RUN_ALL_NODEPLOY_ID)
        .setText(Tr::tr("Run All Tests Without Deployment"))
        .setIcon(Utils::Icons::RUN_SMALL.icon())
        .setToolTip(Tr::tr("Run All Tests Without Deployment"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+E"), Tr::tr("Alt+Shift+T,Alt+E"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunAllTriggered(TestRunMode::RunWithoutDeploy); });

    ActionBuilder(this, Constants::ACTION_RUN_SELECTED_ID)
        .setText(Tr::tr("&Run Selected Tests"))
        .setIcon(Utils::Icons::RUN_SELECTED.icon())
        .setToolTip(Tr::tr("Run Selected Tests"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+R"), Tr::tr("Alt+Shift+T,Alt+R"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunSelectedTriggered(TestRunMode::Run); });

    ActionBuilder(this, Constants::ACTION_RUN_SELECTED_NODEPLOY_ID)
        .setText(Tr::tr("&Run Selected Tests Without Deployment"))
        .setIcon(Utils::Icons::RUN_SELECTED.icon())
        .setToolTip(Tr::tr("Run Selected Tests Without Deployment"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+W"), Tr::tr("Alt+Shift+T,Alt+W"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunSelectedTriggered(TestRunMode::RunWithoutDeploy); });

    ActionBuilder(this, Constants::ACTION_RUN_ALL_DBG_ID)
        .setText(Tr::tr("&Debug All Tests"))
        .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
        .setToolTip(Tr::tr("Run all tests in debug mode"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunAllTriggered(TestRunMode::Debug); });

    ActionBuilder(this, Constants::ACTION_RUN_ALL_DBG_NDEP_ID)
        .setText(Tr::tr("De&bug All Tests Without Deployment"))
        .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
        .setToolTip(Tr::tr("Run all tests in debug mode without deployment"))
        // .setDefaultKeySequence(Tr::tr(""), Tr::tr(""))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunAllTriggered(TestRunMode::DebugWithoutDeploy); });

    ActionBuilder(this, Constants::ACTION_RUN_SELECTED_DBG_ID)
        .setText(Tr::tr("De&bug Selected Tests"))
        .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
        .setToolTip(Tr::tr("Run selected tests in debug mode"))
        // .setDefaultKeySequence(Tr::tr(""), Tr::tr(""))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunSelectedTriggered(TestRunMode::Debug); });

    ActionBuilder(this, Constants::ACTION_RUN_SELECTED_DBG_NDEP_ID)
        .setText(Tr::tr("De&bug Selected Tests Without Deployment"))
        .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
        .setToolTip(Tr::tr("Run selected tests in debug mode without deployment"))
        // .setDefaultKeySequence(Tr::tr(""), Tr::tr(""))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunSelectedTriggered(TestRunMode::DebugWithoutDeploy); });

    ActionBuilder(this, Constants::ACTION_RUN_FAILED_ID)
        .setText(Tr::tr("Run &Failed Tests"))
        .setIcon(Icons::RUN_FAILED.icon())
        .setToolTip(Tr::tr("Run Failed Tests"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+F"), Tr::tr("Alt+Shift+T,Alt+F"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunFailedTriggered(); });

    ActionBuilder(this, Constants::ACTION_RUN_FILE_ID)
        .setText(Tr::tr("Run Tests for &Current File"))
        .setIcon(Utils::Icons::RUN_FILE.icon())
        .setToolTip(Tr::tr("Run Tests for Current File"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+C"), Tr::tr("Alt+Shift+T,Alt+C"))
        .addToContainer(menuId)
        .setEnabled(false)
        .addOnTriggered(this, [this] { onRunFileTriggered(); });

    ActionBuilder(this, Constants::ACTION_DISABLE_TMP)
        .setText(Tr::tr("Disable Temporarily"))
        .setToolTip(Tr::tr("Disable scanning and other actions until explicitly rescanning, "
                           "re-enabling, or restarting Qt Creator."))
        .setCheckable(true)
        .addToContainer(menuId)
        .addOnTriggered(this, [this](bool on) { onDisableTemporarily(on); });

    ActionBuilder(this, Constants::ACTION_SCAN_ID)
        .setText(Tr::tr("Re&scan Tests"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Meta+T, Ctrl+Meta+S"), Tr::tr("Alt+Shift+T,Alt+S"))
        .addToContainer(menuId)
        .addOnTriggered(this, [] {
            if (dd->m_testCodeParser.state() == TestCodeParser::DisabledTemporarily)
                dd->onDisableTemporarily(false);  // Rescan Test should explicitly re-enable
            else
                dd->m_testCodeParser.updateTestTree();
        });

    connect(BuildManager::instance(), &BuildManager::buildStateChanged,
            this, &updateMenuItemsEnabledState);
    connect(BuildManager::instance(), &BuildManager::buildQueueFinished,
            this, &updateMenuItemsEnabledState);
    connect(ProjectExplorerPlugin::instance(), &ProjectExplorerPlugin::runActionsUpdated,
            this, &updateMenuItemsEnabledState);
    m_testTreeModelConnection = connect(&dd->m_testTreeModel, &TestTreeModel::testTreeModelChanged,
                                        this, &updateMenuItemsEnabledState);
}

void AutotestPluginPrivate::onRunAllTriggered(TestRunMode mode)
{
    m_testRunner.runTests(mode, m_testTreeModel.getAllTestCases(mode));
}

void AutotestPluginPrivate::onRunSelectedTriggered(TestRunMode mode)
{
    m_testRunner.runTests(mode, m_testTreeModel.getSelectedTests(mode));
}

void AutotestPluginPrivate::onRunFailedTriggered()
{
    const QList<ITestConfiguration *> failed = m_testTreeModel.getFailedTests();
    if (failed.isEmpty()) // the framework might not be able to provide them
        return;
    m_testRunner.runTests(TestRunMode::Run, failed);
}

void AutotestPluginPrivate::onRunFileTriggered()
{
    const IDocument *document = EditorManager::currentDocument();
    if (!document)
        return;

    const FilePath &fileName = document->filePath();
    if (fileName.isEmpty())
        return;

    const QList<ITestConfiguration *> tests = m_testTreeModel.getTestsForFile(fileName);
    if (tests.isEmpty())
        return;

    m_testRunner.runTests(TestRunMode::Run, tests);
}

static QList<ITestConfiguration *> testItemsToTestConfigurations(const QList<ITestTreeItem *> &items,
                                                                TestRunMode mode)
{
    QList<ITestConfiguration *> configs;
    for (const ITestTreeItem * item : items) {
        if (ITestConfiguration *currentConfig = item->asConfiguration(mode))
            configs << currentConfig;
    }
    return configs;
}

// What Run Test Under Cursor needs to know about where the reader is.
struct CursorContext
{
    Utils::FilePath filePath;
    int line = 0;
    int column = 0;
    QString word;
};

// Where the caret is and what word it is on, asked of the editor rather than
// of a TextEditorWidget. This used to start with currentTextEditor(), which is
// a qobject_cast to BaseTextEditor and comes back null for the Qt Quick
// editor - so on a C++ file, which opens in that one, Run Test Under Cursor
// tripped a QTC_ASSERT and did nothing at all.
std::optional<CursorContext> cursorContextOf(Core::IEditor *editor)
{
    if (!editor)
        return {};
    auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    if (!document)
        return {};

    QTextCursor cursor = TextEditor::textCursorOf(editor);
    if (cursor.isNull())
        return {};
    cursor.select(QTextCursor::WordUnderCursor);

    return CursorContext{document->filePath(), editor->currentLine(), editor->currentColumn(),
                         cursor.selectedText()};
}

void AutotestPluginPrivate::onRunUnderCursorTriggered(TestRunMode mode)
{
    const std::optional<CursorContext> where
        = cursorContextOf(Core::EditorManager::currentEditor());
    QTC_ASSERT(where, return);
    const int line = where->line;
    const FilePath filePath = where->filePath;

    const CPlusPlus::Snapshot snapshot = CppEditor::CppModelManager::snapshot();
    const CPlusPlus::Document::Ptr doc = snapshot.document(filePath);
    if (doc.isNull()) // not part of C++ snapshot
        return;

    CPlusPlus::Scope *scope = doc->scopeAt(line, where->column);
    const QString text = where->word;

    while (scope && scope->asBlock())
        scope = scope->enclosingScope();
    if (scope) {
        QList<const CPlusPlus::Name *> fullName;
        if (scope->asFunction()) {
            fullName = CPlusPlus::LookupContext::fullyQualifiedName(scope);
        } else if (scope->asNamespace()) {
            for (int count = scope->memberCount(), i = 0; i < count; ++i) {
                CPlusPlus::Symbol *member = scope->memberAt(i);
                if (member->line() != line)
                    continue;
                fullName = CPlusPlus::LookupContext::fullyQualifiedName(member);
                if (!fullName.isEmpty()) {
                    const QString funcName = CPlusPlus::Overview().prettyName(fullName.last());
                    if (funcName == text)
                        break;
                    else
                        fullName.clear();
                }
            }
        }
        if (!fullName.isEmpty()) {
            const QString funcName = CPlusPlus::Overview().prettyName(fullName);
            const TestFrameworks active = activeTestFrameworks();
            for (auto framework : active) {
                const QStringList testName = framework->testNameForSymbolName(funcName);
                if (testName.isEmpty())
                    continue;
                TestTreeItem *it = framework->rootNode()->findTestByNameAndFile(testName, filePath);
                if (it) {
                    const QList<ITestConfiguration *> testsToRun
                            = testItemsToTestConfigurations({ it }, mode);
                    if (!testsToRun.isEmpty()) {
                        m_testRunner.runTests(mode, testsToRun);
                        return;
                    }
                }
            }
        }
    }

    // general approach
    if (text.isEmpty())
        return; // Do not trigger when no name under cursor

    const QList<ITestTreeItem *> testsItems = m_testTreeModel.testItemsByName(text);
    if (testsItems.isEmpty())
        return; // Wrong location triggered

    // check whether we have been triggered on a test function definition
    QList<ITestTreeItem *> filteredItems = Utils::filtered(testsItems, [&](ITestTreeItem *it){
        return it->line() == line && it->filePath() == filePath;
    });

    if (filteredItems.isEmpty() && testsItems.size() > 1) {
        CPlusPlus::Scope *scope = doc->scopeAt(line, where->column);
        if (scope->asClass()) {
            const QList<const CPlusPlus::Name *> fullName
                    = CPlusPlus::LookupContext::fullyQualifiedName(scope);
            const QString className = CPlusPlus::Overview().prettyName(fullName);

            filteredItems = Utils::filtered(testsItems,
                                            [&text, &className](ITestTreeItem *it){
                return it->name() == text
                        && static_cast<ITestTreeItem *>(it->parent())->name() == className;
            });
        }
    }
    if ((filteredItems.size() != 1 && testsItems.size() > 1)
            && (mode == TestRunMode::Debug || mode == TestRunMode::DebugWithoutDeploy)) {
        MessageManager::writeFlashing(Tr::tr("Cannot debug multiple tests at once."));
        return;
    }
    const QList<ITestConfiguration *> testsToRun = testItemsToTestConfigurations(
                filteredItems.size() == 1 ? filteredItems : testsItems, mode);

    if (testsToRun.isEmpty()) {
        MessageManager::writeFlashing(Tr::tr("Selected test was not found (%1).").arg(text));
        return;
    }

    m_testRunner.runTests(mode, testsToRun);
}

void AutotestPluginPrivate::onDisableTemporarily(bool disable)
{
    if (disable) {
        // cancel running parse
        m_testCodeParser.aboutToShutdown(false);
        // clear model
        m_testTreeModel.removeAllTestItems();
        m_testTreeModel.removeAllTestToolItems();
        updateMenuItemsEnabledState();
    } else {
        // re-enable
        m_testCodeParser.setState(TestCodeParser::Idle);
        // trigger scan
        m_testCodeParser.updateTestTree();
    }
}

TestFrameworks activeTestFrameworks()
{
    Project *project = ProjectManager::startupProject();
    TestFrameworks sorted;
    if (!project || testProjectSettings(project)->useGlobalSettings()) {
        sorted = Utils::filtered(TestFrameworkManager::registeredFrameworks(),
                                 &ITestFramework::active);
    } else { // we've got custom project settings
        const TestProjectSettings *settings = testProjectSettings(project);
        const QHash<ITestFramework *, bool> active = settings->activeTestFrameworks();
        sorted = Utils::filtered(TestFrameworkManager::registeredFrameworks(),
                                 [active](ITestFramework *framework) {
            return active.value(framework, false);
        });
    }
    return sorted;
}

void updateMenuItemsEnabledState()
{
    const Project *project = ProjectManager::startupProject();
    const bool disabled = dd->m_testCodeParser.state() == TestCodeParser::DisabledTemporarily;
    const bool canScan = disabled || (!dd->m_testRunner.isTestRunning()
                                      && dd->m_testCodeParser.state() == TestCodeParser::Idle);
    const bool hasTests = dd->m_testTreeModel.hasTests(false);
    // avoid expensive call to PE::canRunStartupProject() - limit to minimum necessary checks
    const bool canRun = !disabled && hasTests && canScan
            && project && !project->needsConfiguration() && project->activeRunConfiguration()
            && !BuildManager::isBuilding();
    const bool canRunFailed = canRun && dd->m_testTreeModel.hasFailedTests();
    const bool canDbg = canRun && dd->m_testTreeModel.hasTests(true)
            && !TestConfiguration::runsOnAndroid(project);

    ActionManager::command(Constants::ACTION_RUN_ALL_ID)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_SELECTED_ID)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_ALL_NODEPLOY_ID)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_ALL_DBG_ID)->action()->setEnabled(canDbg);
    ActionManager::command(Constants::ACTION_RUN_SELECTED_DBG_ID)->action()->setEnabled(canDbg);
    ActionManager::command(Constants::ACTION_RUN_ALL_DBG_NDEP_ID)->action()->setEnabled(canDbg);
    ActionManager::command(Constants::ACTION_RUN_SELECTED_DBG_NDEP_ID)->action()->setEnabled(canDbg);
    ActionManager::command(Constants::ACTION_RUN_SELECTED_NODEPLOY_ID)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_FAILED_ID)->action()->setEnabled(canRunFailed);
    ActionManager::command(Constants::ACTION_RUN_FILE_ID)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_SCAN_ID)->action()->setEnabled(canScan);

    ActionContainer *contextMenu = ActionManager::actionContainer(CppEditor::Constants::M_CONTEXT);
    if (!contextMenu)
        return; // When no context menu, actions do not exists

    ActionManager::command(Constants::ACTION_RUN_UCURSOR)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_UCURSOR_NODEPLOY)->action()->setEnabled(canRun);
    ActionManager::command(Constants::ACTION_RUN_DBG_UCURSOR)->action()->setEnabled(canDbg);
    ActionManager::command(Constants::ACTION_RUN_DBG_UCURSOR_NODEPLOY)->action()->setEnabled(canDbg);
}

void cacheRunConfigChoice(const QString &buildTargetKey, const ChoicePair &choice)
{
    if (dd)
        dd->m_runconfigCache.insert(buildTargetKey, choice);
}

ChoicePair cachedChoiceFor(const QString &buildTargetKey)
{
    return dd ? dd->m_runconfigCache.value(buildTargetKey) : ChoicePair();
}

void clearChoiceCache()
{
    if (dd)
        dd->m_runconfigCache.clear();
}

void popupResultsPane()
{
    if (dd)
        dd->m_resultsPane->popup(Core::IOutputPane::NoModeSwitch);
}

QString wildcardPatternFromString(const QString &original)
{
    QString pattern = original;
    pattern.replace('\\', "\\\\");
    pattern.replace('.', "\\.");
    pattern.replace('^', "\\^").replace('$', "\\$");
    pattern.replace('(', "\\(").replace(')', "\\)");
    pattern.replace('[', "\\[").replace(']', "\\]");
    pattern.replace('{', "\\{").replace('}', "\\}");
    pattern.replace('+', "\\+");
    pattern.replace('*', ".*");
    pattern.replace('?', '.');
    return pattern;
}

bool ChoicePair::matches(const RunConfiguration *rc) const
{
    return rc && rc->displayName() == displayName && rc->runnable().command.executable() == executable;
}

// AutotestPlugin

#ifdef WITH_TESTS
QObject *createRunUnderCursorTest();
#endif

class AutotestPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "AutoTest.json")

public:
    AutotestPlugin()
    {
        // needed to be used in QueuedConnection connects
        qRegisterMetaType<TestResult>();
        qRegisterMetaType<TestTreeItem *>();
        qRegisterMetaType<TestCodeLocationAndType>();
        // warm up meta type system to be able to read Qt::CheckState with persistent settings
        qRegisterMetaType<Qt::CheckState>();

        setupTestNavigationWidgetFactory();
    }

    ~AutotestPlugin() final
    {
        delete dd;
        dd = nullptr;
    }

    void initialize() final
    {
        IOptionsPage::registerCategory(
            Constants::AUTOTEST_SETTINGS_CATEGORY,
            Tr::tr("Testing"),
            ":/autotest/images/settingscategory_autotest.png");

        setupTestSettings();

        dd = new AutotestPluginPrivate;
#ifdef WITH_TESTS
        addTestCreator(createAutotestUnitTests);
        addTestCreator(createTestSettingsTest);
        addTestCreator(createTestResultModelTest);
        addTestCreator(Internal::createRunConfigurationSelectionTest);
        addTestCreator(createTestResultsTreeStateTest);
        addTestCreator(Internal::createRunUnderCursorTest);
#endif
    }

    void extensionsInitialized() final
    {
        registerMcpTools();

        ActionContainer *contextMenu = ActionManager::actionContainer(CppEditor::Constants::M_CONTEXT);
        if (!contextMenu) // if QC is started without CppEditor plugin
            return;

        const Id menuId = "Autotest.TestUnderCursor";
        ActionContainer * const runTestMenu = ActionManager::createMenu(menuId);
        runTestMenu->menu()->setTitle(Tr::tr("Run Test Under Cursor"));
        contextMenu->addSeparator();
        contextMenu->addMenu(runTestMenu);
        contextMenu->addSeparator();

        ActionBuilder(this, Constants::ACTION_RUN_UCURSOR)
            .setText(Tr::tr("&Run Test"))
            .setEnabled(false)
            .setIcon(Utils::Icons::RUN_SMALL.icon())
            .addToContainer(menuId)
            .addOnTriggered([] { dd->onRunUnderCursorTriggered(TestRunMode::Run); });

        ActionBuilder(this, Constants::ACTION_RUN_UCURSOR_NODEPLOY)
            .setText(Tr::tr("Run Test Without Deployment"))
            .setIcon(Utils::Icons::RUN_SMALL.icon())
            .setEnabled(false)
            .addToContainer(menuId)
            .addOnTriggered([] { dd->onRunUnderCursorTriggered(TestRunMode::RunWithoutDeploy); });

        ActionBuilder(this, Constants::ACTION_RUN_DBG_UCURSOR)
            .setText(Tr::tr("&Debug Test"))
            .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
            .setEnabled(false)
            .addToContainer(menuId)
            .addOnTriggered([] { dd->onRunUnderCursorTriggered(TestRunMode::Debug); });

        ActionBuilder(this, Constants::ACTION_RUN_DBG_UCURSOR_NODEPLOY)
            .setText(Tr::tr("Debug Test Without Deployment"))
            .setIcon(ProjectExplorer::Icons::DEBUG_START_SMALL.icon())
            .setEnabled(false)
            .addToContainer(menuId)
            .addOnTriggered([] { dd->onRunUnderCursorTriggered(TestRunMode::DebugWithoutDeploy); });
    }

    ShutdownFlag aboutToShutdown() final
    {
        dd->m_testCodeParser.aboutToShutdown(true);
        dd->m_resultsPane->aboutToShutdown();
        disconnect(dd->m_testTreeModelConnection);
        return SynchronousShutdown;
    }
};

#ifdef WITH_TESTS

// Run Test Under Cursor asked for a BaseTextEditor, which is a qobject_cast
// that comes back null for the Qt Quick editor - so on a C++ file, which opens
// in that one, the command tripped a QTC_ASSERT and did nothing. What it
// actually needs is where the caret is and what word it is on, and both views
// answer that.
class RunUnderCursorTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheCursorIsFoundInEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheCursorIsFoundInEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("autotest-under-cursor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("cursor.cpp");
        QVERIFY(file.writeFileContents("void alpha() {}\nvoid beta() {}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);

        // On "beta", the second line's function name. gotoLine() takes a
        // one-based line and a zero-based column.
        editor->gotoLine(2, 6);
        QCOMPARE(TextEditor::textCursorOf(editor).positionInBlock(), 6);

        const std::optional<CursorContext> where = cursorContextOf(editor);
        QVERIFY2(where, "the editor was not asked at all");
        QCOMPARE(where->filePath, file);
        QCOMPARE(where->line, 2);
        QCOMPARE(where->word, QString("beta"));
        // The column is one-based here, the way currentColumn() answers it.
        QCOMPARE(where->column, 7);

        // And nothing at all when there is no editor, which is the case the
        // assertion in the caller is there for.
        QVERIFY2(!cursorContextOf(nullptr), "a null editor produced a context");
    }
};

QObject *createRunUnderCursorTest()
{
    return new RunUnderCursorTest;
}

#endif // WITH_TESTS

} // Autotest::Internal

#include "autotestplugin.moc"
