// Copyright (C) 2019 Kovalev Dmitry <kovalevda1991@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jlinkgdbserverprovider.h"

#include "gdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>

#include <QStandardItem>
#include <baremetal/debugserverprovidermanager.h>

#include <utils/guiutils.h>
#include <utils/pathchooser.h>
#include <utils/variablechooser.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QLabel>

using namespace Utils;

namespace BareMetal::Internal {

// An entry in one of the lists below: what it says, and the argument it
// stands for. An entry with no argument is the server's own default.
static QStandardItem *named(const QString &text, const QString &id)
{
    const auto item = new QStandardItem(text);
    item->setData(id);
    return item;
}

const char jlinkDeviceKeyC[] = "JLinkDevice";
const char jlinkHostInterfaceKeyC[] = "JLinkHostInterface";
const char jlinkHostInterfaceIPAddressKeyC[] = "JLinkHostInterfaceIPAddress";
const char jlinkTargetInterfaceKeyC[] = "JLinkTargetInterface";
const char jlinkTargetInterfaceSpeedKeyC[] = "JLinkTargetInterfaceSpeed";

// JLinkGdbServerProvider

class JLinkGdbServerProvider final : public GdbServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;

    CommandLine command() const final;

    QSet<StartupMode> supportedStartupModes() const final;
    bool isValid() const final;

private:
    JLinkGdbServerProvider();

    // The J-Link GDB server only honors an interrupt request when GDB drives
    // it in target-async mode; otherwise the running target cannot be stopped.
    bool useTargetAsync() const final { return true; }

    static QString defaultInitCommands();
    static QString defaultResetCommands();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    Utils::StringAspect jlinkDevice{this};
    // Which way the probe is reached, and the address when it is over IP.
    Utils::AspectContainer hostInterfaceRow{this};
    Utils::StringSelectionAspect jlinkHost{&hostInterfaceRow};
    Utils::StringAspect jlinkHostAddr{&hostInterfaceRow};
    // Which wire protocol the probe speaks, and how fast.
    Utils::AspectContainer targetInterfaceRow{this};
    Utils::StringSelectionAspect jlinkTargetIface{&targetInterfaceRow};
    Utils::StringSelectionAspect jlinkTargetIfaceSpeed{&targetInterfaceRow};

    friend class JLinkGdbServerProviderFactory;
};

JLinkGdbServerProvider::JLinkGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_JLINK_PROVIDER_ID)
{
    fillStartupModes();
    initCommands.setValue(defaultInitCommands());
    resetCommands.setValue(defaultResetCommands());
    setChannel("localhost", 2331);
    setTypeDisplayName(Tr::tr("JLink"));

    executableFile.setCommandVersionArguments({"--version"});

    jlinkDevice.setSettingsKey(jlinkDeviceKeyC);
    jlinkDevice.setLabelText(Tr::tr("Device:"));
    jlinkDevice.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);

    additionalArguments.setDisplayStyle(StringAspect::DisplayStyle::TextEditDisplay);

    hostInterfaceRow.setInlineRow(true);
    hostInterfaceRow.setLabelText(Tr::tr("Host interface:"));
    jlinkHost.setSettingsKey(jlinkHostInterfaceKeyC);
    jlinkHost.setDefaultValue("USB");
    jlinkHost.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        cb({named(Tr::tr("Default"), {}), named(Tr::tr("USB"), "USB"),
            named(Tr::tr("TCP/IP"), "IP")});
    });
    jlinkHostAddr.setSettingsKey(jlinkHostInterfaceIPAddressKeyC);
    jlinkHostAddr.setLabelText(Tr::tr("IP address:"));
    jlinkHostAddr.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);

    targetInterfaceRow.setInlineRow(true);
    targetInterfaceRow.setLabelText(Tr::tr("Target interface:"));
    jlinkTargetIface.setSettingsKey(jlinkTargetInterfaceKeyC);
    jlinkTargetIface.setDefaultValue("SWD");
    jlinkTargetIface.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        cb({named(Tr::tr("Default"), {}), named(Tr::tr("JTAG"), "JTAG"),
            named(Tr::tr("Compact JTAG"), "cJTAG"), named(Tr::tr("SWD"), "SWD"),
            named(Tr::tr("Renesas RX FINE"), "FINE"), named(Tr::tr("ICSP"), "ICSP")});
    });
    jlinkTargetIfaceSpeed.setSettingsKey(jlinkTargetInterfaceSpeedKeyC);
    jlinkTargetIfaceSpeed.setDefaultValue("12000");
    jlinkTargetIfaceSpeed.setLabelText(Tr::tr("Speed:"));
    jlinkTargetIfaceSpeed.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        QList<QStandardItem *> items{named(Tr::tr("Default"), {}),
                                     named(Tr::tr("Auto"), "auto"),
                                     named(Tr::tr("Adaptive"), "adaptive")};
        const QStringList fixedSpeeds = {"1", "5", "10", "20", "30", "50", "100", "200", "300",
                                         "400", "500", "600", "750", "800", "900", "1000", "1334",
                                         "1600", "2000",  "2667" ,"3200", "4000", "4800", "5334",
                                         "6000", "8000", "9600", "12000", "15000", "20000", "25000",
                                         "30000", "40000", "50000"};
        for (const QString &speed : fixedSpeeds)
            items << named(Tr::tr("%1 kHz").arg(speed), speed);
        cb(items);
    });

    // Behaviour, not layout: an address is only asked for over IP, and a
    // speed only once an interface has been picked.
    const auto updateAllowedControls = [this] {
        jlinkHostAddr.setVisible(jlinkHost.volatileValue() == "IP");
        jlinkTargetIfaceSpeed.setVisible(!jlinkTargetIface.volatileValue().isEmpty());
    };
    updateAllowedControls();
    connect(&jlinkHost, &BaseAspect::volatileValueChanged, this, updateAllowedControls);
    connect(&jlinkTargetIface, &BaseAspect::volatileValueChanged, this, updateAllowedControls);
}

void JLinkGdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    GdbServerProvider::addSettingsRows(rows);
    rows.registerAspect(&address);
    rows.registerAspect(&executableFile);
    rows.registerAspect(&hostInterfaceRow);
    rows.registerAspect(&targetInterfaceRow);
    rows.registerAspect(&jlinkDevice);
    rows.registerAspect(&additionalArguments);
    rows.registerAspect(&initCommands);
    rows.registerAspect(&resetCommands);
}

QString JLinkGdbServerProvider::defaultInitCommands()
{
    return {"set remote hardware-breakpoint-limit 6\n"
        "set remote hardware-watchpoint-limit 4\n"
        "monitor reset halt\n"
        "load\n"
        "monitor reset halt\n"};
}

QString JLinkGdbServerProvider::defaultResetCommands()
{
    return {"monitor reset halt\n"};
}

CommandLine JLinkGdbServerProvider::command() const
{
    CommandLine cmd{executableFile()};

    if (startupMode() == StartupOnNetwork)
        cmd.addArgs("-port " + QString::number(channel().port()), CommandLine::Raw);

    if (jlinkHost() == "USB") {
        cmd.addArgs("-select usb", CommandLine::Raw);
    } else if (jlinkHost() == "IP") {
        cmd.addArgs("-select ip=" + jlinkHostAddr(), CommandLine::Raw);
    }

    if (!jlinkTargetIface().isEmpty()) {
        cmd.addArgs("-if " + jlinkTargetIface(), CommandLine::Raw);
        if (!jlinkTargetIfaceSpeed().isEmpty())
            cmd.addArgs("-speed " + jlinkTargetIfaceSpeed(), CommandLine::Raw);
    }

    if (!jlinkDevice().isEmpty())
        cmd.addArgs("-device " + jlinkDevice(), CommandLine::Raw);

    if (!additionalArguments().isEmpty())
        cmd.addArgs(additionalArguments(), CommandLine::Raw);

    return cmd;
}

QSet<GdbServerProvider::StartupMode>
JLinkGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork};
}

bool JLinkGdbServerProvider::isValid() const
{
    if (!GdbServerProvider::isValid())
        return false;

    const StartupMode m = startupMode();

    if (m == StartupOnNetwork) {
        if (channel().host().isEmpty())
            return false;
    }

    return true;
}

void JLinkGdbServerProvider::toMap(Store &data) const
{
    GdbServerProvider::toMap(data);
}

void JLinkGdbServerProvider::fromMap(const Store &data)
{
    GdbServerProvider::fromMap(data);
}

bool JLinkGdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!GdbServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const JLinkGdbServerProvider *>(&other);
    return executableFile() == p->executableFile()
            && jlinkDevice() == p->jlinkDevice()
            && jlinkHost() == p->jlinkHost()
            && jlinkHostAddr() == p->jlinkHostAddr()
            && jlinkTargetIface() == p->jlinkTargetIface()
            && jlinkTargetIfaceSpeed() == p->jlinkTargetIfaceSpeed()
            && additionalArguments() == p->additionalArguments();
}

// JLinkGdbServerProviderFactory

class JLinkGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    JLinkGdbServerProviderFactory()
    {
        setId(Constants::GDBSERVER_JLINK_PROVIDER_ID);
        setDisplayName(Tr::tr("JLink"));
        setCreator([] { return new JLinkGdbServerProvider; });
    }
};

void setupJLinkGdbServerProvider()
{
    static JLinkGdbServerProviderFactory theJLinkGdbServerProviderFactory;
}

} // BareMetal::Internal
