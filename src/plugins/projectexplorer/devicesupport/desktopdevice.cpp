// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "desktopdevice.h"

#include "idevicefactory.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <QGroupBox>
#include <QLabel>

#include "../projectexplorerconstants.h"
#include "../projectexplorertr.h"
#include "devicemanager.h"
#include "idevice.h"
#include "idevicewidget.h"

#include <coreplugin/fileutils.h>

#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/async.h>
#include <utils/devicefileaccess.h>
#include <utils/environment.h>
#include <utils/globaltasktree.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/portlist.h>
#include <utils/processinfo.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/terminalcommand.h>
#include <utils/terminalhooks.h>
#include <utils/url.h>
#include <utils/winutils.h>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>

#ifdef Q_OS_WIN
#include <cstring>
#include <stdlib.h>
#include <windows.h>
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME 0x0800
#endif // PROCESS_SUSPEND_RESUME
#else // Q_OS_WIN
#include <errno.h>
#include <signal.h>
#endif // else Q_OS_WIN

#ifdef WITH_TESTS
#include <QTemporaryFile>
#include <QTest>
#endif

using namespace ProjectExplorer::Constants;
using namespace QtTaskTree;
using namespace Utils;

namespace ProjectExplorer {

static Result<> cannotKillError(qint64 pid, const QString &why)
{
    return ResultError(Tr::tr("Cannot kill process with pid %1: %2").arg(pid).arg(why));
}

static Result<> appendCannotInterruptError(qint64 pid, const QString &why,
                                           const Result<> &previousResult = ResultOk)
{
    QString error = Tr::tr("Cannot interrupt process with pid %1: %2").arg(pid).arg(why);
    if (previousResult.has_value())
        error.append('\n' + previousResult.error());
    return ResultError(error);
}

static Result<> killProcessSilently(qint64 pid)
{
#ifdef Q_OS_WIN
    const DWORD rights = PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION
                         |PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ
                         |PROCESS_DUP_HANDLE|PROCESS_TERMINATE|PROCESS_CREATE_THREAD|PROCESS_SUSPEND_RESUME;
    if (const HANDLE handle = OpenProcess(rights, FALSE, DWORD(pid))) {
        const Result<> result = TerminateProcess(handle, UINT(-1))
        ? ResultOk : cannotKillError(pid, winErrorMessage(GetLastError()));
        CloseHandle(handle);
        return result;
    } else {
        return cannotKillError(pid, Tr::tr("Cannot open process."));
    }
#else
    if (pid <= 0)
        return cannotKillError(pid, Tr::tr("Invalid process id."));
    else if (kill(pid, SIGKILL))
        return cannotKillError(pid, QString::fromLocal8Bit(strerror(errno)));
    return ResultOk;
#endif // Q_OS_WIN
}

static Result<> interruptProcessSilently(qint64 pid, const Utils::FilePath &debuggerCommand)
{
#ifdef Q_OS_WIN
    Result<> result = ResultOk;
    enum SpecialInterrupt { NoSpecialInterrupt, Win32Interrupt, Win64Interrupt };

    bool is64BitSystem = is64BitWindowsSystem();
    SpecialInterrupt si = NoSpecialInterrupt;
    if (is64BitSystem)
        si = is64BitWindowsBinary(debuggerCommand) ? Win64Interrupt : Win32Interrupt;
    /*
    Windows 64 bit has a 32 bit subsystem (WOW64) which makes it possible to run a
    32 bit application inside a 64 bit environment.
    When GDB is used DebugBreakProcess must be called from the same system (32/64 bit) running
    the inferior. If CDB is used we could in theory break wow64 processes,
    but the break is actually a wow64 breakpoint. CDB is configured to ignore these
    breakpoints, because they also appear on module loading.
    Therefore we need helper executables (win(32/64)interrupt.exe) on Windows 64 bit calling
    DebugBreakProcess from the correct system.

    DebugBreak matrix for windows

    Api = UseDebugBreakApi
    Win64 = UseWin64InterruptHelper
    Win32 = UseWin32InterruptHelper
    N/A = This configuration is not possible

          | Windows 32bit   | Windows 64bit
          | QtCreator 32bit | QtCreator 32bit                   | QtCreator 64bit
          | Inferior 32bit  | Inferior 32bit  | Inferior 64bit  | Inferior 32bit  | Inferior 64bit
----------|-----------------|-----------------|-----------------|-----------------|----------------
CDB 32bit | Api             | Api             | N/A             | Win32           | N/A
    64bit | N/A             | Win64           | Win64           | Api             | Api
----------|-----------------|-----------------|-----------------|-----------------|----------------
GDB 32bit | Api             | Api             | N/A             | Win32           | N/A
    64bit | N/A             | N/A             | Win64           | N/A             | Api
----------|-----------------|-----------------|-----------------|-----------------|----------------

    */
    HANDLE inferior = NULL;
    do {
        const DWORD rights = PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION
                             |PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ
                             |PROCESS_DUP_HANDLE|PROCESS_TERMINATE|PROCESS_CREATE_THREAD|PROCESS_SUSPEND_RESUME;
        inferior = OpenProcess(rights, FALSE, pid);
        if (inferior == NULL) {
            return appendCannotInterruptError(pid, Tr::tr("Cannot open process: %1")
                                              + winErrorMessage(GetLastError()), result);
        }
        bool creatorIs64Bit = is64BitWindowsBinary(
            FilePath::fromUserInput(QCoreApplication::applicationFilePath()));
        if (!is64BitSystem
            || si == NoSpecialInterrupt
            || (si == Win64Interrupt && creatorIs64Bit)
            || (si == Win32Interrupt && !creatorIs64Bit)) {
            if (!DebugBreakProcess(inferior)) {
                result = appendCannotInterruptError(pid, Tr::tr("DebugBreakProcess failed:")
                                                    + QLatin1Char(' ') + winErrorMessage(GetLastError()), result);
            }
        } else if (si == Win32Interrupt || si == Win64Interrupt) {
            QString executable = QCoreApplication::applicationDirPath();
            executable += si == Win32Interrupt
                              ? QLatin1String("/win32interrupt.exe")
                              : QLatin1String("/win64interrupt.exe");
            if (!QFileInfo::exists(executable)) {
                result = appendCannotInterruptError(
                    pid,
                    Tr::tr("%1 does not exist. Your %2 installation seems to be corrupt.")
                        .arg(
                            QDir::toNativeSeparators(executable),
                            QGuiApplication::applicationDisplayName()),
                    result);
            }
            switch (QProcess::execute(executable, QStringList(QString::number(pid)))) {
            case -2:
                return appendCannotInterruptError(pid, Tr::tr(
                                                           "Cannot start %1. Check src\\tools\\win64interrupt\\win64interrupt.c "
                                                           "for more information.").arg(QDir::toNativeSeparators(executable)), result);
            case 0:
                break;
            default:
                return appendCannotInterruptError(pid, QDir::toNativeSeparators(executable)
                                                           + QLatin1Char(' ') + Tr::tr("could not break the process."), result);
                break;
            }
        }
    } while (false);
    if (inferior != NULL)
        CloseHandle(inferior);
    return result;
#else
    Q_UNUSED(debuggerCommand)
    if (pid <= 0)
        return appendCannotInterruptError(pid, Tr::tr("Invalid process id."));
    else if (kill(pid, SIGINT))
        return appendCannotInterruptError(pid, QString::fromLocal8Bit(strerror(errno)));
    return ResultOk;
#endif // Q_OS_WIN
}

static Result<> doSignalOperation(const SignalOperationData &data)
{
    Result<> result = ResultOk;
    switch (data.mode) {
    case SignalOperationMode::KillByPath: {
        const QList<ProcessInfo> processInfoList
            = ProcessInfo::processInfoList().value_or(QList<ProcessInfo>());
        for (const ProcessInfo &processInfo : processInfoList) {
            if (processInfo.commandLine == data.filePath.path())
                result = killProcessSilently(processInfo.processId);
        }
        break;
    }
    case SignalOperationMode::KillByPid:
        result = killProcessSilently(data.pid);
        break;
    case SignalOperationMode::InterruptByPid:
        result = interruptProcessSilently(data.pid, data.debuggerPath);
        break;
    };
    return result;
}

class DesktopDeviceConfigurationWidget final : public IDeviceWidget
{
public:
    explicit DesktopDeviceConfigurationWidget(const IDevicePtr &device)
        : IDeviceWidget(device)
    {
        QTC_CHECK(device->machineType() == IDevice::Hardware);

        using namespace Layouting;
        Form {
            Tr::tr("Machine type:"), Tr::tr("Physical Device"), br,
            device->freePortsAspect, br,
            empty, device->freePortsWarning, br,
            noMargin,
            device->runToolsGroup, br,
            device->sourceAndBuildToolsGroup, br,
            device->autoDetectionGroup, br,
        }.attachTo(this);

        installMarkSettingsDirtyTriggerRecursively(this);
    }

    void updateDeviceFromUi() final {}
};

class DesktopDevicePrivate
{
public:
    QSingleTaskTreeRunner taskTreeRunner;
};

DesktopDevice::DesktopDevice()
    : d(new DesktopDevicePrivate)
{
    setFileAccess(DesktopDeviceFileAccess::instance());

    setupId(IDevice::AutoDetected, DESKTOP_DEVICE_ID);
    setType(DESKTOP_DEVICE_TYPE);
    setDefaultDisplayName(Tr::tr("Local PC"));
    setDisplayType(Tr::tr("Desktop"));

    DeviceManager::setDeviceState(id(), IDevice::DeviceReadyToUse, false);
    setMachineType(IDevice::Hardware);
    setOsType(HostOsInfo::hostOs());

    const QString portRange
        = QString::fromLatin1("%1-%2").arg(DESKTOP_PORT_START).arg(DESKTOP_PORT_END);
    setFreePorts(Utils::PortList::fromString(portRange));

    setOpenTerminal([](const Environment &env, const FilePath &path, const Continuation<> &cont) {
        const Environment realEnv = env.hasChanges() ? env : Environment::systemEnvironment();

        const Result<FilePath> shell = Terminal::defaultShellForDevice(path);
        if (!shell)
            return cont(ResultError(shell.error()));

        Process process;
        process.setTerminalMode(TerminalMode::Detached);
        process.setEnvironment(realEnv);
        process.setCommand(CommandLine{*shell});
        FilePath workingDir = path;
        if (!workingDir.isDir())
            workingDir = workingDir.parentDir();
        if (QTC_GUARD(workingDir.exists()))
            process.setWorkingDirectory(workingDir);
        process.start();

        cont(ResultOk);
    });

    struct DeployToolsAvailability
    {
        bool rsync;
        bool sftp;
    };

    static auto hostDeployTools = []() -> DeployToolsAvailability
    {
        static auto check = [](const QString &tool) {
            return FilePath::fromPathPart(tool).searchInPath().isExecutableFile();
        };
        return {check("rsync"), check("sftp")};
    };

    auto updateExtraData = [this](const DeployToolsAvailability &tools) {
        setExtraData(Constants::SUPPORTS_RSYNC, tools.rsync);
        setExtraData(Constants::SUPPORTS_SFTP, tools.sftp);
    };

    if (HostOsInfo::isWindowsHost()) {
        const auto onSetup = [](Async<DeployToolsAvailability> &task) {
            task.setConcurrentCallData(hostDeployTools);
        };
        const auto onDone = [updateExtraData](const Async<DeployToolsAvailability> &task) {
            updateExtraData(task.result());
        };
        d->taskTreeRunner.start({AsyncTask<DeployToolsAvailability>(onSetup, onDone)});
    } else {
        updateExtraData(hostDeployTools());
    }
}

DesktopDevice::~DesktopDevice() = default;

IDevice::DeviceInfo DesktopDevice::deviceInformation() const
{
    return {};
}

IDeviceWidget *DesktopDevice::createWidget()
{
    return new DesktopDeviceConfigurationWidget(shared_from_this());
}

bool DesktopDevice::canCreateProcessModel() const
{
    return true;
}

ExecutableItem DesktopDevice::signalOperationRecipeImpl(
    const SignalOperationData &data, const Storage<Result<>> &resultStorage) const
{
    const auto onSetup = [data, resultStorage] {
        *resultStorage = doSignalOperation(data);
        return toDoneResult(*resultStorage == ResultOk);
    };

    return QSyncTask(onSetup);
}

QUrl DesktopDevice::toolControlChannel(const ControlChannelHint &) const
{
    QUrl url;
    url.setScheme(Utils::urlTcpScheme());
    url.setHost("localhost");
    return url;
}

Result<Environment> DesktopDevice::sourcedEnvironment(const FilePath &script) const
{
    if (HostOsInfo::isAnyUnixHost())
        return getUnixEnvironment(script);
    if (HostOsInfo::isWindowsHost())
        return getEnvironmentFromBatFile(script);
    return IDevice::sourcedEnvironment(script);
}

Result<> DesktopDevice::handlesFile(const FilePath &filePath) const
{
    if (!filePath.isLocal())
        return ResultError(Tr::tr("\"%1\" can only handle local files.").arg(displayName()));

    if (deviceState() != DeviceReadyToUse)
        return ResultError(Tr::tr("Device \"%1\" is not ready to use.").arg(displayName()));

    return ResultOk;
}

FilePath DesktopDevice::filePath(const QString &pathOnDevice) const
{
    return FilePath::fromParts({}, {}, pathOnDevice);
}

Result<Environment> DesktopDevice::systemEnvironmentWithError() const
{
    return Environment::systemEnvironment();
}

Result<Environment> DesktopDevice::systemEnvironmentIfKnown() const
{
    return systemEnvironmentWithError();
}

FilePath DesktopDevice::rootPath() const
{
    // FIXME: This is ugly as  .filePath(xxx) and .rootPath().withNewPath(xxx) diverge here.
    if (id() == DESKTOP_DEVICE_ID)
        return HostOsInfo::root();
    return IDevice::rootPath();
}

void DesktopDevice::initDeviceToolAspects()
{
    IDevice::initDeviceToolAspects();
    // Restoring the stored aspect values happens only after construction.
    QMetaObject::invokeMethod(this, [this] {
        GlobalTaskTree::start(autoDetectDeviceToolsRecipe());
    }, Qt::QueuedConnection);
}

#ifdef WITH_TESTS
namespace Internal {
class DesktopDeviceTest : public QObject
{
    Q_OBJECT
private:
    // DesktopDevice reports nothing of its own, so the rows are built from a
    // list handed in - the same list deviceInformation() would return.
    static void fillInfoAspects(const IDevice::Ptr &device, const IDevice::DeviceInfo &info)
    {
        device->deviceInfoAspects().clear();
        for (const IDevice::DeviceInfoItem &item : info) {
            const auto row = new TextDisplay;
            row->setLabelText(item.key);
            row->setText(item.value);
            device->deviceInfoAspects().registerAspect(row, /*takeOwnership=*/true);
        }
    }

private slots:
    // A device that answers without talking to anything must do so through
    // systemEnvironmentIfKnown() too, which callers in the middle of a device or
    // kit announcement use instead of the blocking getter.
    void testSystemEnvironmentIsKnownRightAway()
    {
        const IDevice::ConstPtr device = DeviceManager::defaultDesktopDevice();
        QVERIFY(device);
        const Result<Environment> known = device->systemEnvironmentIfKnown();
        if (!known)
            QFAIL(qPrintable(known.error()));
        QCOMPARE(known->toStringList(), Environment::systemEnvironment().toStringList());
    }

    void testADeviceSaysWhenItHasNoPortsToHandOut()
    {
        // Which ports a device has is the device's business, and so is saying
        // that it has none. Two widgets used to work that out and draw the
        // label themselves, so neither renderer could see it and the two
        // disagreed on what it said.
        IDeviceFactory * const factory = IDeviceFactory::find(Constants::DESKTOP_DEVICE_TYPE);
        QVERIFY(factory);
        const IDevice::Ptr device = factory->construct();
        QVERIFY(device);

        device->freePortsAspect.setValue(QString("30000-30010"));
        QVERIFY(!device->freePortsWarning.isVisible());

        device->freePortsAspect.setValue(QString());
        QVERIFY(device->freePortsWarning.isVisible());
        QVERIFY(!device->freePortsWarning.text().isEmpty());
        QCOMPARE(device->freePortsWarning.presentation().infoType, InfoType::Warning);

        device->freePortsAspect.setValue(QString("30000"));
        QVERIFY(!device->freePortsWarning.isVisible());

        // And the field says what a port list looks like, which was the one
        // widget's placeholder rather than the aspect's.
        QVERIFY(!device->freePortsAspect.presentation().placeholderText.isEmpty());
    }

    void testWhatADeviceReportsBecomesRowsAnyRendererCanDraw()
    {
        // A device with nothing to set still has something to say. Two of them
        // drew that themselves - a Form of QLabels in one, a hand-rolled
        // QFormLayout in the other - so nothing but those widgets could show
        // it, and deviceInformation() said something different again.
        IDeviceFactory * const factory = IDeviceFactory::find(Constants::DESKTOP_DEVICE_TYPE);
        QVERIFY(factory);
        const IDevice::Ptr device = factory->construct();
        QVERIFY(device);

        // The real path first: a device is asked what it reports, and the
        // container comes back drawable even when the answer is nothing.
        device->refreshDeviceInfoAspects();
        QCOMPARE(device->deviceInfoAspects().aspects().size(),
                 device->deviceInformation().size());
        const std::unique_ptr<QWidget> emptyForm(
            Core::createAspectForm(&device->deviceInfoAspects()));
        QVERIFY2(emptyForm, "the info container says nothing about how to draw it");

        const IDevice::DeviceInfo info{{"Serial number:", "unknown"},
                                       {"OS version:", "14 (SDK 34)"}};
        fillInfoAspects(device, info);
        QCOMPARE(device->deviceInfoAspects().aspects().size(), info.size());
        for (int i = 0; i < info.size(); ++i) {
            BaseAspect * const row = device->deviceInfoAspects().aspects().at(i);
            QCOMPARE(row->labelText(), info.at(i).key);
            QCOMPARE(row->displayText(), info.at(i).value);
            // Reported, not set: nothing here is a control to type into.
            QCOMPARE(row->presentation().control, AspectControls::Label);
        }

        // And what it says changes: a device that has come up reports more
        // than one that has not, so the rows are replaced rather than added to.
        const IDevice::DeviceInfo later{{"Serial number:", "R58M12345"}};
        fillInfoAspects(device, later);
        QCOMPARE(device->deviceInfoAspects().aspects().size(), later.size());
        QCOMPARE(device->deviceInfoAspects().aspects().first()->displayText(),
                 QString("R58M12345"));

        // Drawn as a name and a value, not just a value: a row that says only
        // "R58M12345" says nothing.
        const std::unique_ptr<QWidget> form(
            Core::createAspectForm(&device->deviceInfoAspects()));
        QVERIFY(form);
        QStringList shown;
        for (const QLabel * const label : form->findChildren<QLabel *>())
            shown << label->text();
        QVERIFY2(shown.contains("Serial number:"), qPrintable(shown.join(" | ")));
        QVERIFY2(shown.contains("R58M12345"), qPrintable(shown.join(" | ")));

    }

    void testTheGroupsBelowADeviceAreContainersNotAClosure()
    {
        // What a device can run tools with, what it builds from, and how it
        // finds those out were three groups built by a
        // std::function<void(Layouting::Layout *)> on IDevice - widgets, so
        // only the widget renderer could ever draw them.
        IDeviceFactory * const factory = IDeviceFactory::find(Constants::DESKTOP_DEVICE_TYPE);
        QVERIFY(factory);
        const IDevice::Ptr device = factory->construct();
        QVERIFY(device);

        for (Utils::AspectContainer * const group : {&device->runToolsGroup,
                                                     &device->sourceAndBuildToolsGroup,
                                                     &device->autoDetectionGroup}) {
            const Utils::AspectPresentation p = group->presentation();
            QCOMPARE(p.control, Utils::AspectControls::Container);
            QVERIFY2(!p.labelText.isEmpty(), "a group with no title is not a group");
        }

        // The tool aspects are the device's, listed by the groups rather than
        // moved into them: their settings keys must not change.
        QVERIFY(!device->runToolsGroup.aspects().isEmpty());
        for (Utils::BaseAspect * const tool : device->runToolsGroup.aspects())
            QVERIFY(device->aspects().contains(tool));

        // Auto-detection is a button and what the run said, not a QPushButton
        // and a QPlainTextEdit the closure made.
        const Utils::AspectPresentation button = device->runAutoDetection.presentation();
        QCOMPARE(button.control, Utils::AspectControls::Button);
        QVERIFY(!button.actionText.isEmpty());
        const Utils::AspectPresentation log = device->autoDetectionLog.presentation();
        QCOMPARE(log.control, Utils::AspectControls::TextEdit);
        QVERIFY(log.readOnly);
        QVERIFY(!log.placeholderText.isEmpty());

        // And placed in a form - which is how every device widget draws it -
        // a titled group comes with its title on it. Only the Quick delegate
        // used to do that; the widget renderer drew a bare column.
        Layouting::Form form{device->runToolsGroup, Layouting::br, Layouting::noMargin};
        const std::unique_ptr<QWidget> widget(form.emerge());
        QVERIFY(widget);
        const QList<QGroupBox *> boxes = widget->findChildren<QGroupBox *>();
        QVERIFY2(!boxes.isEmpty(), "a titled group drew no group box");
        QCOMPARE(boxes.first()->title(), device->runToolsGroup.labelText());
    }

    void testADeviceSaysWhatKindOfMachineItIs()
    {
        // "Physical Device" or "Emulator" was worked out from machineType()
        // and drawn by two widgets, which disagreed: one of them only ever
        // said "Physical Device" because that was all it expected to be.
        IDeviceFactory * const factory = IDeviceFactory::find(Constants::DESKTOP_DEVICE_TYPE);
        QVERIFY(factory);
        const IDevice::Ptr device = factory->construct();
        QVERIFY(device);

        QVERIFY(!device->machineTypeDisplay.labelText().isEmpty());
        device->setMachineType(IDevice::Hardware);
        const QString hardware = device->machineTypeDisplay.text();
        QVERIFY(!hardware.isEmpty());

        device->setMachineType(IDevice::Emulator);
        QVERIFY2(device->machineTypeDisplay.text() != hardware,
                 "an emulator says the same as a physical device");
    }

    void testScriptSourcing()
    {
        if (!HostOsInfo::isAnyUnixHost())
            QSKIP("Only applies on Unix hosts");

        QTemporaryFile f(QDir::tempPath() + "/XXXXXXenv.dummy");
        QVERIFY(f.open());
        f.write(R"(
PREFIX=base
export VAL1=${PREFIX}_foo
export VAL2=${PREFIX}_bar
export LD_LIBRARY_PATH=/opt/lib
)");
        f.close();
        EnvironmentChanges changes;
        changes.setFile(FilePath::fromString(f.fileName()), true);
        Environment baseEnv;
        baseEnv.set("LD_LIBRARY_PATH", "/usr/local/lib");
        changes.modifyEnvironment(baseEnv, nullptr);
        QCOMPARE(
            baseEnv.toStringList(),
            (QStringList{
                "LD_LIBRARY_PATH=/opt/lib:/usr/local/lib",
                "VAL1=base_foo",
                "VAL2=base_bar"}));
    }

    void testBatFile()
    {
        if (!HostOsInfo::isWindowsHost())
            QSKIP("Only applies on Windows hosts");

        QTemporaryFile f(QDir::tempPath() + "/XXXXXXenv.bat");
        QVERIFY(f.open());
        f.write(R"(
set AAA=aaa
set ZZZ=zzz
set PATH=C:\Program Files\MyTool;%PATH%
)");
        f.close();
        EnvironmentChanges changes;
        changes.setFile(FilePath::fromString(f.fileName()), true);
        Environment baseEnv;
        baseEnv.set("PATH", "C:\\Program\\ Files\\LLVM\\bin");
        changes.modifyEnvironment(baseEnv, nullptr);
        QCOMPARE(
            baseEnv.toStringList(),
            (QStringList{
                "AAA=aaa",
                "PATH=C:\\Program Files\\MyTool;C:\\Program\\ Files\\LLVM\\bin",
                "ZZZ=zzz"}));
    }
};

QObject *createDesktopDeviceTest()
{
    return new DesktopDeviceTest;
}
} // namespace Internal
#endif // WITH_TESTS

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include <desktopdevice.moc>
#endif
