// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "stlinkutilgdbserverprovider.h"

#include "gdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <utils/filepath.h>
#include <utils/pathchooser.h>
#include <utils/variablechooser.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QSpinBox>

using namespace Utils;

namespace BareMetal::Internal {

const char executableFileKeyC[] = "ExecutableFile";
const char verboseLevelKeyC[] = "VerboseLevel";
const char extendedModeKeyC[] = "ExtendedMode";
const char resetBoardKeyC[] = "ResetBoard";
const char transportLayerKeyC[] = "TransportLayer";
const char connectUnderResetKeyC[] = "ConnectUnderReset";

class StLinkUtilGdbServerProvider;

enum TransportLayer { ScsiOverUsb = 1, RawUsb = 2, UnspecifiedTransport };

// StLinkUtilGdbServerProvider

class StLinkUtilGdbServerProvider final : public GdbServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;

    Utils::CommandLine command() const final;

    QSet<StartupMode> supportedStartupModes() const final;
    bool isValid() const final;

private:
    StLinkUtilGdbServerProvider();

    static QString defaultInitCommands();
    static QString defaultResetCommands();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    Utils::IntegerAspect verboseLevel{this};
    Utils::BoolAspect extendedMode{this};
    Utils::BoolAspect resetBoard{this};
    Utils::BoolAspect connectUnderReset{this};
    Utils::TypedSelectionAspect<TransportLayer> transport{this};

    friend class StLinkUtilGdbServerProviderFactory;
};

StLinkUtilGdbServerProvider::StLinkUtilGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_STLINK_UTIL_PROVIDER_ID)
{
    fillStartupModes();
    initCommands.setValue(defaultInitCommands());
    resetCommands.setValue(defaultResetCommands());
    setChannel("localhost", 4242);
    setTypeDisplayName(Tr::tr("ST-LINK Utility"));

    executableFile.setSettingsKey(executableFileKeyC);
    executableFile.setValue(FilePath("st-util"));
    executableFile.setCommandVersionArguments({"--version"});

    verboseLevel.setSettingsKey(verboseLevelKeyC);
    verboseLevel.setLabelText(Tr::tr("Verbosity level:"));
    verboseLevel.setRange(0, 99);
    verboseLevel.setToolTip(Tr::tr("Specify the verbosity level (0..99)."));

    extendedMode.setSettingsKey(extendedModeKeyC);
    extendedMode.setLabelText(Tr::tr("Extended mode:"));
    extendedMode.setToolTip(Tr::tr("Continue listening for connections "
                                   "after disconnect."));
    extendedMode.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    resetBoard.setSettingsKey(resetBoardKeyC);
    resetBoard.setDefaultValue(true);
    resetBoard.setLabelText(Tr::tr("Reset on connection:"));
    resetBoard.setToolTip(Tr::tr("Reset board on connection."));
    resetBoard.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    connectUnderReset.setSettingsKey(connectUnderResetKeyC);
    connectUnderReset.setLabelText(Tr::tr("Connect under reset:"));
    connectUnderReset.setToolTip(Tr::tr("Connects to the board before "
                                        "executing any instructions."));
    connectUnderReset.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    transport.setSettingsKey(transportLayerKeyC);
    transport.setLabelText(Tr::tr("Version:"));
    transport.setToolTip(Tr::tr("Transport layer type."));
    transport.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    transport.addOption({Tr::tr("ST-LINK/V1"), {}, ScsiOverUsb});
    transport.addOption({Tr::tr("ST-LINK/V2"), {}, RawUsb});
    transport.addOption({Tr::tr("Keep unspecified"), {}, UnspecifiedTransport});
    transport.setDefaultValue(RawUsb);
}

void StLinkUtilGdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    GdbServerProvider::addSettingsRows(rows);
    rows.registerAspect(&address);
    rows.registerAspect(&executableFile);
    rows.registerAspect(&verboseLevel);
    rows.registerAspect(&extendedMode);
    rows.registerAspect(&resetBoard);
    rows.registerAspect(&connectUnderReset);
    rows.registerAspect(&transport);
    rows.registerAspect(&initCommands);
    rows.registerAspect(&resetCommands);
}

QString StLinkUtilGdbServerProvider::defaultInitCommands()
{
    return {"load\n"};
}

QString StLinkUtilGdbServerProvider::defaultResetCommands()
{
    return {};
}

CommandLine StLinkUtilGdbServerProvider::command() const
{
    CommandLine cmd{executableFile()};

    if (extendedMode())
        cmd.addArg("--multi");

    if (!resetBoard())
        cmd.addArg("--no-reset");

    if (transport() != UnspecifiedTransport)
        cmd.addArg("--stlink_version=" + QString::number(transport()));

    if (connectUnderReset())
        cmd.addArg("--connect-under-reset");

    cmd.addArg("--listen_port=" + QString::number(channel().port()));
    cmd.addArg("--verbose=" + QString::number(verboseLevel()));

    return cmd;
}

QSet<GdbServerProvider::StartupMode>
StLinkUtilGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork};
}

bool StLinkUtilGdbServerProvider::isValid() const
{
    if (!GdbServerProvider::isValid())
        return false;

    const StartupMode m = startupMode();

    if (m == StartupOnNetwork) {
        if (channel().host().isEmpty())
            return false;
    }

    if (m == StartupOnNetwork) {
        if (executableFile().isEmpty())
            return false;
    }

    return true;
}

void StLinkUtilGdbServerProvider::toMap(Store &data) const
{
    GdbServerProvider::toMap(data);
}

void StLinkUtilGdbServerProvider::fromMap(const Store &data)
{
    GdbServerProvider::fromMap(data);
}

bool StLinkUtilGdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!GdbServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const StLinkUtilGdbServerProvider *>(&other);
    return executableFile() == p->executableFile()
            && verboseLevel() == p->verboseLevel()
            && extendedMode() == p->extendedMode()
            && resetBoard() == p->resetBoard()
            && transport() == p->transport()
            && connectUnderReset() == p->connectUnderReset();
}

// StLinkUtilGdbServerProviderFactory

class StLinkUtilGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    StLinkUtilGdbServerProviderFactory()
    {
        setId(Constants::GDBSERVER_STLINK_UTIL_PROVIDER_ID);
        setDisplayName(Tr::tr("ST-LINK Utility"));
        setCreator([] { return new StLinkUtilGdbServerProvider; });
    }
};

void setupStLinkUtilGdbServerProvider()
{
    static StLinkUtilGdbServerProviderFactory theStLinkUtilGdbServerProviderFactory;
}

} // ProjectExplorer::Internal
