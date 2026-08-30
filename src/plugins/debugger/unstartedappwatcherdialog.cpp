// Copyright (C) 2016 Petar Perisin <petar.perisin@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "unstartedappwatcherdialog.h"

#include "debuggeritem.h"
#include "debuggerkitaspect.h"
#include "debuggertr.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/completionhistory.h>
#include <utils/fileutils.h>
#include <utils/pathvalidation.h>
#include <utils/processinterface.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

using namespace ProjectExplorer;
using namespace Utils;

namespace Debugger::Internal {

// What each state of the watcher looks like. Pulled out of setWaitingState()
// so that the four can be compared without a dialog: they differ in what the
// reader may touch, which is easy to get wrong and invisible in a screenshot.
struct WatcherLook
{
    QString message;
    bool canWatch = false;
    bool watching = false;
    // Whether the executable and the kit may still be changed. Watching pins
    // both: the list of processes to ignore was taken for that executable.
    bool executableEnabled = true;
    bool kitEnabled = true;
};

static WatcherLook lookFor(UnstartedAppWatcherState state)
{
    switch (state) {
    case InvalidWatcherState:
        return {Tr::tr("Select valid executable."), false, false, true, true};
    case NotWatchingState:
        return {Tr::tr("Not watching."), true, false, true, true};
    case WatchingState:
        return {Tr::tr("Waiting for process to start..."), true, true, false, false};
    case FoundState:
        return {Tr::tr("Attach"), false, true, false, true};
    }
    return {};
}

class UnstartedAppWatcherSettings final : public AspectContainer
{
public:
    UnstartedAppWatcherSettings()
    {
        setAutoApply(true);
        setQmlSource(
            QUrl("qrc:/qt/qml/QtCreator/Debugger/UnstartedAppWatcherDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("Kit:"));
        kitChooser.setShowIcons(true);
        kitChooser.setKitPredicate([](const Kit *k) {
            return ToolchainKitAspect::targetAbi(k).os() == Abi::hostAbi().os();
        });

        executable.setQmlName("Executable");
        executable.setLabelText(Tr::tr("Executable:"));
        executable.setExpectedKind(PathChooserKind::ExistingCommand);
        executable.setHistoryCompleter("LocalExecutable");

        reset.setQmlName("Reset");
        reset.setActionText(Tr::tr("Reset"));
        reset.setEnabled(false);

        hideOnAttach.setQmlName("HideOnAttach");
        hideOnAttach.setLabel(Tr::tr("Reopen dialog when application finishes"),
                              BoolAspect::LabelPlacement::AtCheckBox);
        hideOnAttach.setToolTip(Tr::tr("Reopens this dialog when application finishes."));
        hideOnAttach.setValue(false);

        continueOnAttach.setQmlName("ContinueOnAttach");
        continueOnAttach.setLabel(Tr::tr("Continue on attach"),
                                  BoolAspect::LabelPlacement::AtCheckBox);
        continueOnAttach.setToolTip(Tr::tr("Debugger does not stop the"
                                           " application after attach."));
        continueOnAttach.setValue(true);

        waiting.setQmlName("Waiting");
    }

    KitChooserAspect kitChooser{this};
    FilePathAspect executable{this};
    ActionAspect reset{this};
    BoolAspect hideOnAttach{this};
    BoolAspect continueOnAttach{this};
    TextDisplay waiting{this};
};

static bool isLocal(RunConfiguration *runConfiguration)
{
    Kit *kit = runConfiguration ? runConfiguration->kit() : nullptr;
    return RunDeviceTypeKitAspect::deviceTypeId(kit) == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
}

// Where browsing for the executable starts: beside what the active run
// configuration runs, or failing that in the project's build directory.
static FilePath browseStartFor()
{
    FilePath path;
    Project *project = ProjectTree::currentProject();
    if (RunConfiguration *runConfig = activeRunConfig(project)) {
        const ProcessRunData runnable = runConfig->runnable();
        if (isLocal(runConfig))
            path = runnable.command.executable().parentDir();
    }

    if (path.isEmpty()) {
        if (const BuildConfiguration *const bc = activeBuildConfig(project))
            path = bc->buildDirectory();
        else if (project)
            path = project->projectDirectory();
    }
    return path;
}

/*!
    \class Debugger::Internal::UnstartedAppWatcherDialog

    \brief The UnstartedAppWatcherDialog class provides ability to wait for a certain application
           to be started, after what it will attach to it.

    This dialog can be useful in cases where automated scripts are used in order to execute some
    tests on application. In those cases application will be started from a script. This dialog
    allows user to attach to application in those cases in very short time after they are started.

    In order to attach, user needs to provide appropriate kit (for local debugging) and
    application path.

    After selecting start, dialog will check if selected application is started every
    10 miliseconds. As soon as application is started, QtCreator will attach to it.

    After user attaches, it is possible to keep dialog active and as soon as debugging
    session ends, it will start watching again. This is because sometimes automated test
    scripts can restart application several times during tests.
*/

UnstartedAppWatcherDialog::UnstartedAppWatcherDialog(std::optional<QPoint> pos, QWidget *parent)
    : QDialog(parent)
    , m_settings(new UnstartedAppWatcherSettings)
    , m_lastPosition(pos)
{
    if (pos)
        move(*pos);
    setWindowTitle(Tr::tr("Attach to Process Not Yet Started"));

    m_settings->executable.setBaseDirectory(Lazy<FilePath>([] { return browseStartFor(); }));

    // The widget path chooser was asked to put the last entry back; the aspect
    // keeps the same history but leaves the field alone.
    const QStringList history = CompletionHistory::entries("LocalExecutable");
    if (!history.isEmpty())
        m_settings->executable.setValue(FilePath::fromUserInput(history.first()));

    m_settings->kitChooser.populate();
    if (Kit *const kit = activeKitForCurrentProject())
        m_settings->kitChooser.setCurrentKitId(kit->id());
    else if (KitManager::waitForLoaded() && KitManager::defaultKit())
        m_settings->kitChooser.setCurrentKitId(KitManager::defaultKit()->id());

    // Only where there is a local run configuration to take an executable
    // from; otherwise the button has nothing to put back.
    if (RunConfiguration *const runConfig = activeRunConfigForCurrentProject()) {
        const ProcessRunData runnable = runConfig->runnable();
        if (isLocal(runConfig)) {
            m_settings->reset.setEnabled(true);
            m_settings->reset.setAction([this, runnable] {
                m_settings->executable.setValue(runnable.command.executable());
            });
        }
    }

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_watchingPushButton = buttonBox->addButton(Tr::tr("Start Watching"),
                                                QDialogButtonBox::ActionRole);
    m_watchingPushButton->setCheckable(true);
    m_watchingPushButton->setChecked(false);
    m_watchingPushButton->setEnabled(false);
    m_watchingPushButton->setDefault(true);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(Core::createAspectForm(m_settings.get()));
    mainLayout->addWidget(buttonBox);

    connect(m_watchingPushButton, &QAbstractButton::toggled,
            this, &UnstartedAppWatcherDialog::startStopWatching);
    connect(&m_settings->executable, &BaseAspect::changed,
            this, &UnstartedAppWatcherDialog::stopAndCheckExecutable);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&m_timer, &QTimer::timeout,
            this, &UnstartedAppWatcherDialog::findProcess);
    connect(&m_settings->kitChooser.kit, &BaseAspect::changed,
            this, &UnstartedAppWatcherDialog::kitChanged);
    kitChanged();
    m_settings->executable.setFocusToInputField();

    setWaitingState(checkExecutableString() ? NotWatchingState : InvalidWatcherState);
}

UnstartedAppWatcherDialog::~UnstartedAppWatcherDialog() = default;

bool UnstartedAppWatcherDialog::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Escape && !ke->modifiers()) {
            ke->accept();
            return true;
        }
    }
    return QDialog::event(e);
}

void UnstartedAppWatcherDialog::startWatching()
{
    if (m_lastPosition)
        move(*m_lastPosition);
    show();
    if (checkExecutableString()) {
        setWaitingState(WatchingState);
        startStopTimer(true);
    } else {
        setWaitingState(InvalidWatcherState);
    }
}

void UnstartedAppWatcherDialog::pidFound(const ProcessInfo &p)
{
    setWaitingState(FoundState);
    startStopTimer(false);
    m_process = p;

    if (hideOnAttach()) {
        m_lastPosition = pos();
        hide();
    } else {
        accept();
    }

    emit processFound();
}

void UnstartedAppWatcherDialog::startStopWatching(bool start)
{
    setWaitingState(start ? WatchingState : NotWatchingState);
    m_watchingPushButton->setText(start ? Tr::tr("Stop Watching") : Tr::tr("Start Watching"));
    startStopTimer(start);
}

void UnstartedAppWatcherDialog::startStopTimer(bool start)
{
    if (start)
        m_timer.start(10);
    else
        m_timer.stop();
}

void UnstartedAppWatcherDialog::findProcess()
{
    const QString appName = m_settings->executable().normalizedPathName().path();
    ProcessInfo fallback;
    const QList<ProcessInfo> processInfoList = ProcessInfo::processInfoList().value_or(
        QList<ProcessInfo>());
    for (const ProcessInfo &processInfo : processInfoList) {
        if (m_excluded.contains(processInfo.processId))
            continue;
        if (Utils::FileUtils::normalizedPathName(processInfo.executable) == appName) {
            pidFound(processInfo);
            return;
        }
        if (processInfo.commandLine.startsWith(appName))
            fallback = processInfo;
    }
    if (fallback.processId != 0)
        pidFound(fallback);
}

void UnstartedAppWatcherDialog::stopAndCheckExecutable()
{
    startStopTimer(false);
    setWaitingState(checkExecutableString() ? NotWatchingState : InvalidWatcherState);
}

void UnstartedAppWatcherDialog::kitChanged()
{
    const DebuggerItem debugger = DebuggerKitAspect::debugger(m_settings->kitChooser.currentKit());
    if (!debugger)
        return;
    if (debugger.engineType() == Debugger::CdbEngineType) {
        m_settings->continueOnAttach.setEnabled(false);
        m_settings->continueOnAttach.setValue(true);
    } else {
        m_settings->continueOnAttach.setEnabled(true);
    }
}

bool UnstartedAppWatcherDialog::checkExecutableString() const
{
    return m_settings->executable().isFile();
}

Kit *UnstartedAppWatcherDialog::currentKit() const
{
    return m_settings->kitChooser.currentKit();
}

ProcessInfo UnstartedAppWatcherDialog::currentProcess() const
{
    return m_process;
}

bool UnstartedAppWatcherDialog::hideOnAttach() const
{
    return m_settings->hideOnAttach();
}

// The box shows checked for a CDB kit and this still answers false: CDB
// continues the application itself, so asking the caller to continue it again
// would be asking twice. The widget dialog answered the same way, by folding
// the box's enabled state into its value.
bool UnstartedAppWatcherDialog::continueOnAttach() const
{
    return m_settings->continueOnAttach.isEnabled() && m_settings->continueOnAttach();
}

void UnstartedAppWatcherDialog::setWaitingState(UnstartedAppWatcherState state)
{
    const WatcherLook look = lookFor(state);
    m_settings->waiting.setText(look.message);
    m_watchingPushButton->setEnabled(look.canWatch);
    m_watchingPushButton->setChecked(look.watching);
    m_settings->executable.setEnabled(look.executableEnabled);
    m_settings->kitChooser.setEnabled(look.kitEnabled);

    if (state == WatchingState) {
        // Whatever is already running is not what is being waited for.
        m_excluded.clear();
        const QList<ProcessInfo> processInfoList
            = ProcessInfo::processInfoList().value_or(QList<ProcessInfo>());
        for (const ProcessInfo &processInfo : processInfoList)
            m_excluded.insert(processInfo.processId);
    }
}

#ifdef WITH_TESTS

class UnstartedAppWatcherDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        UnstartedAppWatcherSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "UnstartedAppWatcherDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatEachStateLetsTheReaderTouch()
    {
        // Watching pins both fields: the processes to ignore were taken for
        // that executable on that kit, so changing either mid-watch would be
        // waiting for one thing while ignoring another.
        const WatcherLook watching = lookFor(WatchingState);
        QVERIFY2(!watching.executableEnabled, "the executable could be changed while watching");
        QVERIFY2(!watching.kitEnabled, "the kit could be changed while watching");
        QVERIFY(watching.watching);
        QVERIFY(watching.canWatch);

        // Nothing to watch for yet, so there is nothing to start.
        const WatcherLook invalid = lookFor(InvalidWatcherState);
        QVERIFY2(!invalid.canWatch, "watching could be started with no executable");
        QVERIFY(invalid.executableEnabled);

        const WatcherLook notWatching = lookFor(NotWatchingState);
        QVERIFY(notWatching.canWatch);
        QVERIFY2(!notWatching.watching, "the dialog said it was watching when it was not");
        QVERIFY(notWatching.executableEnabled);

        // Found: the process is there, so there is nothing left to wait for,
        // but the kit is still the reader's to change before attaching.
        const WatcherLook found = lookFor(FoundState);
        QVERIFY2(!found.canWatch, "watching could be restarted after the process was found");
        QVERIFY2(!found.executableEnabled, "the executable could be changed after the find");
        QVERIFY2(found.kitEnabled, "the kit could not be changed before attaching");

        // Each state says something different, so the reader can tell them
        // apart.
        const QStringList messages{invalid.message, notWatching.message,
                                   watching.message, found.message};
        QCOMPARE(QSet<QString>(messages.begin(), messages.end()).size(), 4);
    }

    void testTheStateReachesTheAspects()
    {
        UnstartedAppWatcherDialog dlg(std::nullopt);

        dlg.setWaitingState(WatchingState);
        QCOMPARE(dlg.m_settings->waiting.text(), lookFor(WatchingState).message);
        QVERIFY2(!dlg.m_settings->executable.isEnabled(),
                 "the executable stayed editable while watching");
        QVERIFY2(!dlg.m_settings->kitChooser.isEnabled(),
                 "the kit stayed choosable while watching");

        dlg.setWaitingState(NotWatchingState);
        QCOMPARE(dlg.m_settings->waiting.text(), lookFor(NotWatchingState).message);
        QVERIFY(dlg.m_settings->executable.isEnabled());
        QVERIFY(dlg.m_settings->kitChooser.isEnabled());
    }

    void testWhatTheBoxesStartAt()
    {
        UnstartedAppWatcherSettings settings;

        // Reopening is off and continuing is on, as the widget dialog opened.
        QVERIFY2(!settings.hideOnAttach(), "the dialog offered to reopen itself by default");
        QVERIFY2(settings.continueOnAttach(), "the application would be left stopped");

        // Both carry their own text rather than a label beside them.
        QCOMPARE(settings.hideOnAttach.presentation().labelPlacement,
                 AspectControls::LabelPlacement::AtControl);
        QCOMPARE(settings.continueOnAttach.presentation().labelPlacement,
                 AspectControls::LabelPlacement::AtControl);
    }

    void testADebuggerThatContinuesByItselfIsNotAskedTo()
    {
        // CDB continues the application on its own, so the dialog shows the
        // box checked and still answers false - asking again would be asking
        // twice. The widget dialog did this by folding the box's enabled state
        // into the answer.
        UnstartedAppWatcherDialog dlg(std::nullopt);

        dlg.m_settings->continueOnAttach.setEnabled(false);
        dlg.m_settings->continueOnAttach.setValue(true);
        QVERIFY2(!dlg.continueOnAttach(),
                 "a debugger that continues by itself was asked to continue");

        dlg.m_settings->continueOnAttach.setEnabled(true);
        QVERIFY(dlg.continueOnAttach());

        dlg.m_settings->continueOnAttach.setValue(false);
        QVERIFY(!dlg.continueOnAttach());
    }

    void testTheResetButtonOnlyWhereThereIsSomethingToPutBack()
    {
        // Without a local run configuration the button has no executable to
        // restore, so it is not offered.
        UnstartedAppWatcherSettings settings;
        QVERIFY2(!settings.reset.isEnabled(),
                 "Reset was offered with nothing to put back");
    }
};

QObject *createUnstartedAppWatcherDialogTest()
{
    return new UnstartedAppWatcherDialogTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

#ifdef WITH_TESTS
#include "unstartedappwatcherdialog.moc"
#endif
