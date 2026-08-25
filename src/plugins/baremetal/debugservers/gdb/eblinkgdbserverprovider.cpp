// Copyright (C) 2019 Andrey Sobol <andrey.sobol.nn@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "eblinkgdbserverprovider.h"

#include "gdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

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
const char deviceScriptC[] = "DeviceScript";
const char interfaceTypeC[] = "InterfaceType";
const char interfaceResetOnConnectC[] = "interfaceResetOnConnect";
const char interfaceSpeedC[] = "InterfaceSpeed";
const char interfaceExplicidDeviceC[] = "InterfaceExplicidDevice";
const char targetNameC[] = "TargetName";
const char targetDisableStackC[] = "TargetDisableStack";
const char gdbShutDownAfterDisconnectC[] = "GdbShutDownAfterDisconnect";
const char gdbNotUseCacheC[] = "GdbNotUseCache";

enum InterfaceType { SWD, JTAG };

// EBlinkGdbServerProvider

class EBlinkGdbServerProvider final : public GdbServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;

    Utils::CommandLine command() const final;

    QSet<StartupMode> supportedStartupModes() const final;
    bool isValid() const final;

private:
    EBlinkGdbServerProvider();

    static QString defaultInitCommands();
    static QString defaultResetCommands();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    Utils::IntegerAspect verboseLevel{this};             // verbose <0..7>
    Utils::TypedSelectionAspect<InterfaceType> interfaceType{this}; // -I stlink ;swd jtag
    Utils::FilePathAspect deviceScript{this};            // -D <script>
    Utils::BoolAspect interfaceResetOnConnect{this};     // (inversed) -I stlink,dr
    Utils::IntegerAspect interfaceSpeed{this};           // -I stlink,speed=4000
    Utils::BoolAspect gdbShutDownAfterDisconnect{this};  // -G S
    Utils::BoolAspect gdbNotUseCache{this};              // -G nc
    // Stored but never asked for: the widget had no row for either.
    Utils::StringAspect interfaceExplicidDevice{this};   // device=<usb_bus>:<usb_addr>
    Utils::StringAspect targetName{this};                // -T cortex-m
    Utils::BoolAspect targetDisableStack{this};          // -T cortex-m,nu

    QString scriptFileWoExt() const;

    friend class EBlinkGdbServerProviderFactory;
};

EBlinkGdbServerProvider::EBlinkGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_EBLINK_PROVIDER_ID)
{
    fillStartupModes();
    initCommands.setValue(defaultInitCommands());
    resetCommands.setValue(defaultResetCommands());
    setChannel("127.0.0.1", 2331);
    setTypeDisplayName(Tr::tr("EBlink"));

    executableFile.setSettingsKey(executableFileKeyC);
    executableFile.setValue(FilePath("eblink")); // server execute filename

    deviceScript.setSettingsKey(deviceScriptC);
    deviceScript.setLabelText(Tr::tr("Script file:"));
    deviceScript.setExpectedKind(PathChooserKind::File);
    deviceScript.setPromptDialogFilter("*.script");
    deviceScript.setValue(FilePath("stm32-auto.script"));

    verboseLevel.setSettingsKey(verboseLevelKeyC);
    verboseLevel.setLabelText(Tr::tr("Verbosity level:"));
    verboseLevel.setRange(0, 7);
    verboseLevel.setToolTip(Tr::tr("Specify the verbosity level (0 to 7)."));

    interfaceResetOnConnect.setSettingsKey(interfaceResetOnConnectC);
    interfaceResetOnConnect.setDefaultValue(true);
    interfaceResetOnConnect.setLabelText(Tr::tr("Connect under reset:"));
    interfaceResetOnConnect.setToolTip(Tr::tr("Connect under reset (hotplug)."));
    interfaceResetOnConnect.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    interfaceType.setSettingsKey(interfaceTypeC);
    interfaceType.setLabelText(Tr::tr("Type:"));
    interfaceType.setToolTip(Tr::tr("Interface type."));
    interfaceType.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    interfaceType.addOption({Tr::tr("SWD"), {}, SWD});
    interfaceType.addOption({Tr::tr("JTAG"), {}, JTAG});
    interfaceType.setDefaultValue(SWD);

    interfaceSpeed.setSettingsKey(interfaceSpeedC);
    interfaceSpeed.setLabelText(Tr::tr("Speed:"));
    interfaceSpeed.setRange(120, 8000);
    interfaceSpeed.setDefaultValue(4000);
    interfaceSpeed.setToolTip(Tr::tr("Specify the speed of the interface "
                                     "(120 to 8000) in kilohertz (kHz)."));

    gdbNotUseCache.setSettingsKey(gdbNotUseCacheC);
    gdbNotUseCache.setLabelText(Tr::tr("Disable cache:"));
    gdbNotUseCache.setToolTip(Tr::tr("Do not use EBlink flash cache."));
    gdbNotUseCache.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    gdbShutDownAfterDisconnect.setSettingsKey(gdbShutDownAfterDisconnectC);
    gdbShutDownAfterDisconnect.setDefaultValue(true);
    gdbShutDownAfterDisconnect.setLabelText(Tr::tr("Auto shutdown:"));
    gdbShutDownAfterDisconnect.setToolTip(
        Tr::tr("Shut down EBlink server after disconnect."));
    gdbShutDownAfterDisconnect.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    interfaceExplicidDevice.setSettingsKey(interfaceExplicidDeviceC);
    targetName.setSettingsKey(targetNameC);
    targetName.setDefaultValue("cortex-m");
    targetDisableStack.setSettingsKey(targetDisableStackC);
}

void EBlinkGdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    GdbServerProvider::addSettingsRows(rows);
    rows.registerAspect(&address);
    rows.registerAspect(&executableFile);
    rows.registerAspect(&deviceScript);
    rows.registerAspect(&verboseLevel);
    rows.registerAspect(&interfaceResetOnConnect);
    rows.registerAspect(&interfaceType);
    rows.registerAspect(&interfaceSpeed);
    rows.registerAspect(&gdbNotUseCache);
    rows.registerAspect(&gdbShutDownAfterDisconnect);
    rows.registerAspect(&initCommands);
    rows.registerAspect(&resetCommands);
}

QString EBlinkGdbServerProvider::defaultInitCommands()
{
    return {"monitor reset halt\n"
        "load\n"
        "monitor reset halt\n"
        "break main\n"};
}

QString EBlinkGdbServerProvider::defaultResetCommands()
{
    return {"monitor reset halt\n"};
}

QString EBlinkGdbServerProvider::scriptFileWoExt() const
{
    // Server starts only without extension in scriptname
    return deviceScript().absolutePath().pathAppended(deviceScript().baseName()).path();
}

CommandLine EBlinkGdbServerProvider::command() const
{
    CommandLine cmd{executableFile()};
    QStringList interFaceTypeStrings = {"swd", "jtag"};

    // Obligatorily -I
    cmd.addArg("-I");
    QString interfaceArgs("stlink,%1,speed=%2");
    interfaceArgs = interfaceArgs.arg(interFaceTypeStrings.at(interfaceType()))
                                .arg(QString::number(interfaceSpeed()));
    if (!interfaceResetOnConnect())
        interfaceArgs.append(",dr");
    if (!interfaceExplicidDevice().trimmed().isEmpty())
        interfaceArgs.append(",device=" + interfaceExplicidDevice().trimmed());
    cmd.addArg(interfaceArgs);

    // Obligatorily -D
    cmd.addArg("-D");
    cmd.addArg(scriptFileWoExt());

    // Obligatorily -G
    cmd.addArg("-G");
    QString gdbArgs("port=%1,address=%2");
    gdbArgs = gdbArgs.arg(QString::number(channel().port()))
                    .arg(channel().host());
    if (gdbNotUseCache())
        gdbArgs.append(",nc");
    if (gdbShutDownAfterDisconnect())
        gdbArgs.append(",S");
    cmd.addArg(gdbArgs);

    cmd.addArg("-T");
    QString targetArgs(targetName().trimmed());
    if (targetDisableStack())
        targetArgs.append(",nu");
    cmd.addArg(targetArgs);

    cmd.addArg("-v");
    cmd.addArg(QString::number(verboseLevel()));

    if (HostOsInfo::isWindowsHost())
        cmd.addArg("-g"); // no gui

    return cmd;
}

QSet<GdbServerProvider::StartupMode>
EBlinkGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork};
}

bool EBlinkGdbServerProvider::isValid() const
{
    if (!GdbServerProvider::isValid())
        return false;

    switch (startupMode()) {
    case StartupOnNetwork:
        return !channel().host().isEmpty() && !executableFile().isEmpty()
                                           && !deviceScript().isEmpty();
    default:
        return false;
    }
}

void EBlinkGdbServerProvider::toMap(Store &data) const
{
    GdbServerProvider::toMap(data);
}

void EBlinkGdbServerProvider::fromMap(const Store &data)
{
    GdbServerProvider::fromMap(data);
}

bool EBlinkGdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!GdbServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const EBlinkGdbServerProvider *>(&other);
    return executableFile() == p->executableFile()
            && verboseLevel() == p->verboseLevel()
            && interfaceType() == p->interfaceType()
            && deviceScript() == p->deviceScript()
            && interfaceResetOnConnect() == p->interfaceResetOnConnect()
            && interfaceSpeed() == p->interfaceSpeed()
            && interfaceExplicidDevice() == p->interfaceExplicidDevice()
            && targetName() == p->targetName()
            && targetDisableStack() == p->targetDisableStack()
            && gdbShutDownAfterDisconnect() == p->gdbShutDownAfterDisconnect()
            && gdbNotUseCache() == p->gdbNotUseCache();
}

// EBlinkGdbServerProviderFactory

class EBlinkGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    EBlinkGdbServerProviderFactory()
    {
        setId(Constants::GDBSERVER_EBLINK_PROVIDER_ID);
        setDisplayName(Tr::tr("EBlink"));
        setCreator([] { return new EBlinkGdbServerProvider; });
    }
};

void setupEBlinkGdbServerProvider()
{
    static EBlinkGdbServerProviderFactory theEBlinkGdbServerProviderFactory;
}

} // BareMetal::Internal
