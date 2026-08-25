// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gdbserverprovider.h"

#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <debugger/debuggerengine.h>

#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/runcontrol.h>

#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/result.h>


using namespace Debugger;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace BareMetal::Internal {

const char startupModeKeyC[] = "Mode";
const char peripheralDescriptionFileKeyC[] = "PeripheralDescriptionFile";
const char initCommandsKeyC[] = "InitCommands";
const char resetCommandsKeyC[] = "ResetCommands";
const char useExtendedRemoteKeyC[] = "UseExtendedRemote";
const char executableFileKeyC[] = "ExecutableFile";
const char additionalArgumentsKeyC[] = "AdditionalArguments";

// GdbServerProvider

GdbServerProvider::GdbServerProvider(const QString &id)
    : IDebugServerProvider(id)
{
    setEngineType(Debugger::GdbEngineType);

    startupMode.setSettingsKey(startupModeKeyC);
    startupMode.setLabelText(Tr::tr("Startup mode:"));
    startupMode.setToolTip(Tr::tr("Choose the desired startup mode "
                                  "of the GDB server provider."));
    startupMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    peripheralDescriptionFile.setSettingsKey(peripheralDescriptionFileKeyC);
    peripheralDescriptionFile.setLabelText(Tr::tr("Peripheral description file:"));
    peripheralDescriptionFile.setExpectedKind(PathChooserKind::File);
    peripheralDescriptionFile.setPromptDialogFilter(
        Tr::tr("Peripheral description files (*.svd)"));
    peripheralDescriptionFile.setPromptDialogTitle(
        Tr::tr("Select Peripheral Description File"));

    initCommands.setSettingsKey(initCommandsKeyC);
    initCommands.setLabelText(Tr::tr("Init commands:"));
    initCommands.setDisplayStyle(StringAspect::DisplayStyle::TextEditDisplay);
    initCommands.setToolTip(
        Tr::tr("Enter GDB commands to reset the board "
               "and to write the nonvolatile memory.\n\n"
               "GDB runs them after it has connected to the debug server, which is what "
               "lets them use \"monitor\" and \"load\". To set up the debug server itself, "
               "use its own configuration file or command line, as it starts before GDB."));

    resetCommands.setSettingsKey(resetCommandsKeyC);
    resetCommands.setLabelText(Tr::tr("Reset commands:"));
    resetCommands.setDisplayStyle(StringAspect::DisplayStyle::TextEditDisplay);
    resetCommands.setToolTip(Tr::tr("Enter GDB commands to reset the hardware. "
                                    "The MCU should be halted after these commands."));

    useExtendedRemote.setSettingsKey(useExtendedRemoteKeyC);
    useExtendedRemote.setLabelText(Tr::tr("Extended mode:"));
    useExtendedRemote.setLabel(Tr::tr("Use extended mode to run the debugger server."));
    useExtendedRemote.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    executableFile.setSettingsKey(executableFileKeyC);
    executableFile.setLabelText(Tr::tr("Executable file:"));
    executableFile.setExpectedKind(PathChooserKind::ExistingCommand);

    additionalArguments.setSettingsKey(additionalArgumentsKeyC);
    additionalArguments.setLabelText(Tr::tr("Additional arguments:"));
    additionalArguments.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
}

void GdbServerProvider::fillStartupModes()
{
    startupMode.clearOptions();
    for (const StartupMode mode : supportedStartupModes()) {
        startupMode.addOption(
            {mode == StartupOnNetwork ? Tr::tr("Startup in TCP/IP Mode")
                                      : Tr::tr("Startup in Pipe Mode"), {}, mode});
    }
}

void GdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    IDebugServerProvider::addSettingsRows(rows);
    rows.registerAspect(&startupMode);
}

Utils::CommandLine GdbServerProvider::command() const
{
    if (executableFile().isEmpty())
        return {};
    return CommandLine{executableFile(), additionalArguments(), CommandLine::Raw};
}

bool GdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!IDebugServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const GdbServerProvider *>(&other);
    return startupMode() == p->startupMode()
            && peripheralDescriptionFile() == p->peripheralDescriptionFile()
            && initCommands() == p->initCommands()
            && resetCommands() == p->resetCommands()
            && useExtendedRemote() == p->useExtendedRemote();
}

void GdbServerProvider::toMap(Store &data) const
{
    IDebugServerProvider::toMap(data);
}

bool GdbServerProvider::isValid() const
{
    return (startupMode() == GdbServerProvider::StartupOnNetwork && channel().isValid()) ||
           (startupMode() == GdbServerProvider::StartupOnPipe && !channelPipe().isEmpty());
}

Result<> GdbServerProvider::setupDebuggerRunParameters(DebuggerRunParameters &rp,
                                                       RunControl *runControl) const
{
    Q_UNUSED(runControl)
    const CommandLine cmd = rp.inferior().command;
    const FilePath bin = FilePath::fromString(cmd.executable().path());
    if (bin.isEmpty()) {
        return ResultError(Tr::tr("Cannot debug: Local executable is not set."));
    }
    if (!bin.exists()) {
        return ResultError(Tr::tr("Cannot debug: Could not find executable for \"%1\".")
                                 .arg(bin.toUserOutput()));
    }

    ProcessRunData inferior;
    inferior.command.setExecutable(bin);
    inferior.command.setArguments(cmd.arguments());
    rp.setInferior(inferior);
    rp.setSymbolFile(bin);
    rp.setStartMode(AttachToRemoteServer);
    rp.setCommandsAfterConnect(initCommands());
    rp.setCommandsForReset(resetCommands());
    if (startupMode() == GdbServerProvider::StartupOnNetwork)
        rp.setRemoteChannel(channel().toString());
    else
        rp.setRemoteChannel(channelPipe());
    rp.setUseContinueInsteadOfRun(true);
    rp.setUseTargetAsync(useTargetAsync());
    rp.setUseExtendedRemote(useExtendedRemote());
    rp.setPeripheralDescriptionFile(peripheralDescriptionFile());
    return ResultOk;
}

std::optional<BarrierKickerGetter> GdbServerProvider::serverRunner(RunControl *runControl) const
{
    const CommandLine cmd = command();
    if (startupMode() != GdbServerProvider::StartupOnNetwork || cmd.isEmpty())
        return {};

    // Command arguments are in host OS style as the bare metal's GDB servers are launched
    // on the host, not on that target.
    return [this, runControl, cmd](const QStoredBarrier &ready) {
        return runControl->processTaskWithModifier([this, runControl, cmd, ready](Process &process) {
            // Baremetal's GDB servers are launched on the host, not on the target.
            process.setCommand(cmd.toLocal());
            connectReadyBarrier(runControl, process, ready.activeStorage());
        });
    };
}

void GdbServerProvider::fromMap(const Store &data)
{
    IDebugServerProvider::fromMap(data);
}

} // BareMetal::Internal
