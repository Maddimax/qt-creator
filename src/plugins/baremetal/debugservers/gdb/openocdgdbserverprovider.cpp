// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "openocdgdbserverprovider.h"

#include "gdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <utils/guiutils.h>
#include <utils/pathchooser.h>
#include <utils/variablechooser.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>

using namespace Utils;

namespace BareMetal::Internal {

const char rootScriptsDirKeyC[] = "RootScriptsDir";
const char configurationFileKeyC[] = "ConfigurationPath";

// OpenOcdGdbServerProvider

class OpenOcdGdbServerProvider final : public GdbServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;

    QString channelPipe() const final;
    Utils::CommandLine command() const final;

    QSet<StartupMode> supportedStartupModes() const final;
    bool isValid() const final;

private:
    explicit OpenOcdGdbServerProvider();

    QString readyMessage() const final { return {"for gdb connections"}; }

    static QString defaultInitCommands();
    static QString defaultResetCommands();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    Utils::FilePathAspect rootScriptsDir{this};
    Utils::FilePathAspect configurationFile{this};

    friend class OpenOcdGdbServerProviderFactory;
};


OpenOcdGdbServerProvider::OpenOcdGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_OPENOCD_PROVIDER_ID)
{
    fillStartupModes();
    executableFile.setValue(FilePath("openocd"));
    executableFile.setCommandVersionArguments({"--version"});
    initCommands.setValue(defaultInitCommands());
    resetCommands.setValue(defaultResetCommands());
    setChannel("localhost", 3333);
    setTypeDisplayName(Tr::tr("OpenOCD"));

    rootScriptsDir.setSettingsKey(rootScriptsDirKeyC);
    rootScriptsDir.setLabelText(Tr::tr("Root scripts directory:"));
    rootScriptsDir.setExpectedKind(PathChooserKind::Directory);

    configurationFile.setSettingsKey(configurationFileKeyC);
    configurationFile.setLabelText(Tr::tr("Configuration file:"));
    configurationFile.setExpectedKind(PathChooserKind::File);
    configurationFile.setPromptDialogFilter("*.cfg");

    // Behaviour, not layout: in pipe mode there is no address to connect to.
    const auto updateAddressVisible = [this] {
        address.setVisible(startupMode.volatileValue() != StartupOnPipe);
    };
    updateAddressVisible();
    connect(&startupMode, &BaseAspect::volatileValueChanged, this, updateAddressVisible);
}

void OpenOcdGdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    GdbServerProvider::addSettingsRows(rows);
    rows.registerAspect(&address);
    rows.registerAspect(&executableFile);
    rows.registerAspect(&rootScriptsDir);
    rows.registerAspect(&configurationFile);
    rows.registerAspect(&additionalArguments);
    rows.registerAspect(&initCommands);
    rows.registerAspect(&resetCommands);
}

QString OpenOcdGdbServerProvider::defaultInitCommands()
{
    return {"set remote hardware-breakpoint-limit 6\n"
                         "set remote hardware-watchpoint-limit 4\n"
                         "monitor reset halt\n"
                         "load\n"
                         "monitor reset halt\n"};
}

QString OpenOcdGdbServerProvider::defaultResetCommands()
{
    return {"monitor reset halt\n"};
}

QString OpenOcdGdbServerProvider::channelPipe() const
{
    CommandLine cmd = command();
    QStringList args = {"|", cmd.executable().path()};
    for (const QString &a : ProcessArgs::splitArgs(cmd.arguments(), HostOsInfo::hostOs())) {
        if (a.startsWith('\"') && a.endsWith('\"'))
            args << a;
        else
            args << ('\"' + a + '\"');
    }
    return args.join(' ');
}

CommandLine OpenOcdGdbServerProvider::command() const
{
    CommandLine cmd{executableFile()};

    cmd.addArg("-c");
    if (startupMode() == StartupOnPipe)
        cmd.addArg("gdb_port pipe");
    else
        cmd.addArg("gdb_port " + QString::number(channel().port()));

    if (!rootScriptsDir().isEmpty())
        cmd.addArgs({"-s", rootScriptsDir().path()});

    if (!configurationFile().isEmpty())
        cmd.addArgs({"-f", configurationFile().path()});

    if (!additionalArguments().isEmpty())
        cmd.addArgs(additionalArguments(), CommandLine::Raw);

    return cmd;
}

QSet<GdbServerProvider::StartupMode>
OpenOcdGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork, StartupOnPipe};
}

bool OpenOcdGdbServerProvider::isValid() const
{
    if (!GdbServerProvider::isValid())
        return false;

    const StartupMode m = startupMode();

    if (m == StartupOnNetwork) {
        if (channel().host().isEmpty())
            return false;
    }

    if (m == StartupOnNetwork || m == StartupOnPipe) {
        if (executableFile().isEmpty())
            return false;
    }

    return true;
}

void OpenOcdGdbServerProvider::toMap(Store &data) const
{
    GdbServerProvider::toMap(data);
}

void OpenOcdGdbServerProvider::fromMap(const Store &data)
{
    GdbServerProvider::fromMap(data);
}

bool OpenOcdGdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!GdbServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const OpenOcdGdbServerProvider *>(&other);
    return executableFile() == p->executableFile()
            && rootScriptsDir() == p->rootScriptsDir()
            && configurationFile() == p->configurationFile()
            && additionalArguments() == p->additionalArguments();
}

// OpenOcdGdbServerProviderFactory

class OpenOcdGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    OpenOcdGdbServerProviderFactory()
    {
        setId(Constants::GDBSERVER_OPENOCD_PROVIDER_ID);
        setDisplayName(Tr::tr("OpenOCD"));
        setCreator([] { return new OpenOcdGdbServerProvider; });
    }
};

void setupOpenOcdGdbServerProvider()
{
    static OpenOcdGdbServerProviderFactory theOpenOcdGdbServerProviderFactory;
}

} // BareMetal::Internal
