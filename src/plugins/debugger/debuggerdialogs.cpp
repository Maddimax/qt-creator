// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debuggerdialogs.h"

#include "cdb/cdbengine.h"
#include "debuggerruncontrol.h"
#include "debuggertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/sshparameters.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#ifdef WITH_TESTS
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Debugger::Internal {

///////////////////////////////////////////////////////////////////////
//
// StartApplicationParameters
//
///////////////////////////////////////////////////////////////////////

class StartApplicationParameters
{
public:
    QString displayName() const;
    bool equals(const StartApplicationParameters &rhs) const;
    void toSettings(QtcSettings *) const;
    void fromSettings(const QtcSettings *settings);

    bool operator==(const StartApplicationParameters &p) const { return equals(p); }

    Id kitId;
    uint serverPort;
    QString serverAddress;
    ProcessRunData runnable;
    bool breakAtMain = false;
    bool runInTerminal = false;
    bool useTargetExtendedRemote = false;
    FilePath sysRoot;
    QString serverInitCommands;
    QString serverResetCommands;
    FilePath debugInfoLocation;
};

bool StartApplicationParameters::equals(const StartApplicationParameters &rhs) const
{
    return runnable.command == rhs.runnable.command
        && serverPort == rhs.serverPort
        && runnable.workingDirectory == rhs.runnable.workingDirectory
        && breakAtMain == rhs.breakAtMain
        && runInTerminal == rhs.runInTerminal
        && sysRoot == rhs.sysRoot
        && serverInitCommands == rhs.serverInitCommands
        && serverResetCommands == rhs.serverResetCommands
        && kitId == rhs.kitId
        && debugInfoLocation == rhs.debugInfoLocation
        && serverAddress == rhs.serverAddress;
}

QString StartApplicationParameters::displayName() const
{
    const int maxLength = 60;

    QString name = runnable.command.executable().fileName()
            + ' ' + runnable.command.arguments();
    if (name.size() > 60) {
        int index = name.lastIndexOf(' ', maxLength);
        if (index == -1)
            index = maxLength;
        name.truncate(index);
        name += "...";
    }

    if (Kit *kit = KitManager::kit(kitId))
        name += QString::fromLatin1(" (%1)").arg(kit->displayName());

    return name;
}

void StartApplicationParameters::toSettings(QtcSettings *settings) const
{
    settings->setValue("LastKitId", kitId.toSetting());
    settings->setValue("LastServerPort", serverPort);
    settings->setValue("LastServerAddress", serverAddress);
    settings->setValue("LastExternalExecutable", runnable.command.executable().toSettings());
    settings->setValue("LastExternalExecutableArguments", runnable.command.arguments());
    settings->setValue("LastExternalWorkingDirectory", runnable.workingDirectory.toSettings());
    settings->setValue("LastExternalBreakAtMain", breakAtMain);
    settings->setValue("LastExternalRunInTerminal", runInTerminal);
    settings->setValue("LastExternalUseTargetExtended", useTargetExtendedRemote);
    settings->setValue("LastServerInitCommands", serverInitCommands);
    settings->setValue("LastServerResetCommands", serverResetCommands);
    settings->setValue("LastDebugInfoLocation", debugInfoLocation.toSettings());
    settings->setValue("LastSysRoot", sysRoot.toSettings());
}

void StartApplicationParameters::fromSettings(const QtcSettings *settings)
{
    kitId = Id::fromSetting(settings->value("LastKitId"));
    serverPort = settings->value("LastServerPort").toUInt();
    serverAddress = settings->value("LastServerAddress").toString();
    runnable.command.setExecutable(FilePath::fromSettings(settings->value("LastExternalExecutable")));
    runnable.command.setArguments(settings->value("LastExternalExecutableArguments").toString());
    runnable.workingDirectory = FilePath::fromSettings(settings->value("LastExternalWorkingDirectory"));
    breakAtMain = settings->value("LastExternalBreakAtMain").toBool();
    runInTerminal = settings->value("LastExternalRunInTerminal").toBool();
    useTargetExtendedRemote = settings->value("LastExternalUseTargetExtended").toBool();
    serverInitCommands = settings->value("LastServerInitCommands").toString();
    serverResetCommands = settings->value("LastServerResetCommands").toString();
    debugInfoLocation = FilePath::fromSettings(settings->value("LastDebugInfoLocation"));
    sysRoot = FilePath::fromSettings(settings->value("LastSysRoot"));
}

///////////////////////////////////////////////////////////////////////
//
// StartApplicationDialog
//
///////////////////////////////////////////////////////////////////////

class StartApplicationSettings final : public AspectContainer
{
public:
    StartApplicationSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/StartApplicationDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("&Kit:"));
        kitChooser.setShowIcons(true);

        serverPort.setQmlName("ServerPort");
        serverPort.setLabelText(Tr::tr("Server port:"));
        serverPort.setRange(1, 65535);

        localExecutable.setQmlName("LocalExecutable");
        localExecutable.setLabelText(Tr::tr("Local &executable:"));
        localExecutable.setExpectedKind(PathChooserKind::File);
        localExecutable.setPromptDialogTitle(Tr::tr("Select Executable"));
        localExecutable.setHistoryCompleter("LocalExecutable");

        arguments.setQmlName("Arguments");
        arguments.setLabelText(Tr::tr("Command line &arguments:"));
        arguments.setDisplayStyle(StringAspect::LineEditDisplay);
        arguments.setHistoryCompleter("CommandlineArguments");

        workingDirectory.setQmlName("WorkingDirectory");
        workingDirectory.setLabelText(Tr::tr("&Working directory:"));
        workingDirectory.setExpectedKind(PathChooserKind::ExistingDirectory);
        workingDirectory.setPromptDialogTitle(Tr::tr("Select Working Directory"));
        workingDirectory.setHistoryCompleter("WorkingDirectory");

        // The widget check boxes carried no text of their own; the form label
        // beside them is what names them.
        runInTerminal.setQmlName("RunInTerminal");
        runInTerminal.setLabel(Tr::tr("Run in &terminal:"),
                               BoolAspect::LabelPlacement::InExtraLabel);

        breakAtMain.setQmlName("BreakAtMain");
        breakAtMain.setLabel(Tr::tr("Break at \"&main\":"),
                             BoolAspect::LabelPlacement::InExtraLabel);

        useTargetExtendedRemote.setQmlName("UseTargetExtendedRemote");
        useTargetExtendedRemote.setLabel(Tr::tr("Use target extended-remote to connect:"),
                                         BoolAspect::LabelPlacement::InExtraLabel);

        sysRoot.setQmlName("SysRoot");
        sysRoot.setLabelText(Tr::tr("Override S&ysRoot:"));
        sysRoot.setExpectedKind(PathChooserKind::Directory);
        sysRoot.setHistoryCompleter("Debugger.SysRoot.History");
        sysRoot.setPromptDialogTitle(Tr::tr("Select SysRoot Directory"));
        sysRoot.setToolTip(Tr::tr("This option can be used to override the kit's SysRoot setting."));

        serverInitCommands.setQmlName("InitCommands");
        serverInitCommands.setLabelText(Tr::tr("&Init commands:"));
        serverInitCommands.setDisplayStyle(StringAspect::TextEditDisplay);
        serverInitCommands.setToolTip(
            Tr::tr("This option can be used to send the target init commands."));

        serverResetCommands.setQmlName("ResetCommands");
        serverResetCommands.setLabelText(Tr::tr("&Reset commands:"));
        serverResetCommands.setDisplayStyle(StringAspect::TextEditDisplay);
        serverResetCommands.setToolTip(
            Tr::tr("This option can be used to send the target reset commands."));

        debugInfoLocation.setQmlName("DebugInfo");
        debugInfoLocation.setLabelText(Tr::tr("Debug &information:"));
        debugInfoLocation.setPromptDialogTitle(
            Tr::tr("Select Location of Debugging Information"));
        debugInfoLocation.setToolTip(
            Tr::tr("Base path for external debug information and debug sources. "
                   "If empty, $SYSROOT/usr/lib/debug will be chosen."));
        debugInfoLocation.setHistoryCompleter("Debugger.DebugLocation.History");

        channelOverrideHint.setQmlName("ChannelHint");
        channelOverrideHint.setText(
            Tr::tr("Normally, the running server is identified by the IP of the "
                   "device in the kit and the server port selected above.\n"
                   "You can choose another communication channel here, such as "
                   "a serial line or custom ip:port."));

        channelOverride.setQmlName("ChannelOverride");
        channelOverride.setLabelText(Tr::tr("Override server channel:"));
        channelOverride.setDisplayStyle(StringAspect::LineEditDisplay);
        //: "For example, /dev/ttyS0, COM1, 127.0.0.1:1234"
        channelOverride.setPlaceHolderText(
            Tr::tr("For example, %1").arg("/dev/ttyS0, COM1, 127.0.0.1:1234"));

        history.setQmlName("Recent");
        history.setLabelText(Tr::tr("&Recent:"));
        history.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    }

    // The fields that only mean something when attaching to a server that is
    // already running. Named once, because the dialog hides them together and
    // a field left behind is one the reader cannot use but can still fill in.
    QList<BaseAspect *> remoteOnly()
    {
        return {&serverPort, &serverInitCommands, &serverResetCommands,
                &channelOverrideHint, &channelOverride};
    }

    KitChooserAspect kitChooser{this};
    IntegerAspect serverPort{this};
    FilePathAspect localExecutable{this};
    StringAspect arguments{this};
    FilePathAspect workingDirectory{this};
    BoolAspect runInTerminal{this};
    BoolAspect breakAtMain{this};
    BoolAspect useTargetExtendedRemote{this};
    FilePathAspect sysRoot{this};
    StringAspect serverInitCommands{this};
    StringAspect serverResetCommands{this};
    FilePathAspect debugInfoLocation{this};
    TextDisplay channelOverrideHint{this};
    StringAspect channelOverride{this};
    SelectionAspect history{this};
};

class StartApplicationDialog final : public QDialog
{
public:
    StartApplicationDialog();

    static void run(bool);

private:
    void historyIndexChanged(int);
    void updateState();
    StartApplicationParameters parameters() const;
    void setParameters(const StartApplicationParameters &p);
    void setHistory(const QList<StartApplicationParameters> &l);
    void onChannelOverrideChanged(const QString &channel);

    StartApplicationSettings m_settings;
    QDialogButtonBox *buttonBox;

#ifdef WITH_TESTS
    friend class StartApplicationDialogTest;
#endif
};

StartApplicationDialog::StartApplicationDialog()
  : QDialog(ICore::dialogParent())
{
    setWindowTitle(Tr::tr("Start Debugger"));

    m_settings.kitChooser.populate();

    buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    auto verticalLayout = new QVBoxLayout(this);
    verticalLayout->addWidget(Core::createAspectForm(&m_settings));
    verticalLayout->addWidget(buttonBox);

    connect(&m_settings.localExecutable, &FilePathAspect::validChanged,
            this, &StartApplicationDialog::updateState);
    connect(&m_settings.history, &BaseAspect::changed, this, [this] {
        historyIndexChanged(m_settings.history.value());
    });
    connect(&m_settings.channelOverride, &BaseAspect::changed, this, [this] {
        onChannelOverrideChanged(m_settings.channelOverride());
    });

    updateState();

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void StartApplicationDialog::setHistory(const QList<StartApplicationParameters> &l)
{
    m_settings.history.clearOptions();
    // Most recent first, as the combo box listed them.
    for (int i = l.size(); --i >= 0; ) {
        const StartApplicationParameters &p = l.at(i);
        if (!p.runnable.command.isEmpty())
            m_settings.history.addOption({p.displayName(), {}, QVariant::fromValue(p)});
    }
}

// A channel of its own replaces the device address and the port, so there is
// nothing to choose a port for.
void StartApplicationDialog::onChannelOverrideChanged(const QString &channel)
{
    m_settings.serverPort.setEnabled(channel.isEmpty());
}

void StartApplicationDialog::historyIndexChanged(int index)
{
    if (index < 0)
        return;
    const QVariant v = m_settings.history.itemValueForIndex(index);
    QTC_ASSERT(v.canConvert<StartApplicationParameters>(), return);
    setParameters(v.value<StartApplicationParameters>());
}

void StartApplicationDialog::updateState()
{
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_settings.localExecutable.isValid());
}

void StartApplicationDialog::run(bool attachRemote)
{
    const Key settingsGroup = "DebugMode";
    const QString arrayName = "StartApplication";

    QList<StartApplicationParameters> history;
    QtcSettings *settings = ICore::settings();
    settings->beginGroup(settingsGroup);
    if (const int arraySize = settings->beginReadArray(arrayName)) {
        for (int i = 0; i < arraySize; ++i) {
            settings->setArrayIndex(i);
            StartApplicationParameters p;
            p.fromSettings(settings);
            history.append(p);
        }
    } else {
        history.append(StartApplicationParameters());
    }
    settings->endArray();
    settings->endGroup();

    StartApplicationDialog dialog;
    dialog.setHistory(history);
    dialog.setParameters(history.back());
    if (!attachRemote) {
        for (BaseAspect *const aspect : dialog.m_settings.remoteOnly())
            aspect->setVisible(false);
    }
    if (dialog.exec() != QDialog::Accepted)
        return;

    Kit *k = dialog.m_settings.kitChooser.currentKit();

    const StartApplicationParameters newParameters = dialog.parameters();
    if (newParameters != history.back()) {
        history.append(newParameters);
        while (history.size() > 10)
            history.takeFirst();
        settings->beginGroup(settingsGroup);
        settings->beginWriteArray(arrayName);
        for (int i = 0; i < history.size(); ++i) {
            settings->setArrayIndex(i);
            history.at(i).toSettings(settings);
        }
        settings->endArray();
        settings->endGroup();
    }

    IDevice::ConstPtr dev = RunDeviceKitAspect::device(k);
    if (!dev) {
        QMessageBox::critical(
            &dialog, Tr::tr("Cannot Debug"), Tr::tr("Cannot debug application: Kit has no device."));
        return;
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(k);

    DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
    const QString inputAddress = dialog.m_settings.channelOverride();
    if (!inputAddress.isEmpty())
        rp.setRemoteChannel(inputAddress);
    else
        rp.setRemoteChannel(dev->sshParameters().host() + ':' + QString::number(newParameters.serverPort));
    rp.setDisplayName(newParameters.displayName());
    rp.setBreakOnMain(newParameters.breakAtMain);
    rp.setDebugInfoLocation(newParameters.debugInfoLocation);
    rp.setInferior(newParameters.runnable);
    rp.setCommandsAfterConnect(newParameters.serverInitCommands);
    rp.setCommandsForReset(newParameters.serverResetCommands);
    rp.setUseTerminal(newParameters.runInTerminal);
    rp.setUseExtendedRemote(newParameters.useTargetExtendedRemote);
    if (!newParameters.sysRoot.isEmpty())
        rp.setSysRoot(newParameters.sysRoot);

    bool isLocal = dev->type() == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
    if (isLocal) // FIXME: Restriction needed?
        rp.setInferiorEnvironment(k->runEnvironment());

    if (!attachRemote)
        rp.setStartMode(isLocal ? StartExternal : StartRemoteProcess);

    if (attachRemote) {
        rp.setStartMode(AttachToRemoteServer);
        rp.setCloseMode(KillAtClose);
        rp.setUseContinueInsteadOfRun(true);
        rp.setDisplayName(Tr::tr("Attach to %1").arg(rp.remoteChannel()));
    }

    runControl->setRunRecipe(debuggerRecipe(runControl, rp));
    runControl->start();
}

void runAttachToRemoteServerDialog()
{
    StartApplicationDialog::run(true);
}

void runStartAndDebugApplicationDialog()
{
    StartApplicationDialog::run(false);
}

StartApplicationParameters StartApplicationDialog::parameters() const
{
    StartApplicationParameters result;
    result.serverPort = m_settings.serverPort();
    result.serverAddress = m_settings.channelOverride();
    result.runnable.command.setExecutable(m_settings.localExecutable());
    result.sysRoot = m_settings.sysRoot();
    result.serverInitCommands = m_settings.serverInitCommands();
    result.serverResetCommands = m_settings.serverResetCommands();
    result.kitId = m_settings.kitChooser.currentKitId();
    result.debugInfoLocation = m_settings.debugInfoLocation();
    result.runnable.command.setArguments(m_settings.arguments());
    result.runnable.workingDirectory = m_settings.workingDirectory();
    result.breakAtMain = m_settings.breakAtMain();
    result.runInTerminal = m_settings.runInTerminal();
    result.useTargetExtendedRemote = m_settings.useTargetExtendedRemote();
    return result;
}

void StartApplicationDialog::setParameters(const StartApplicationParameters &p)
{
    m_settings.kitChooser.setCurrentKitId(p.kitId);
    m_settings.serverPort.setValue(p.serverPort);
    m_settings.channelOverride.setValue(p.serverAddress);
    m_settings.localExecutable.setValue(p.runnable.command.executable());
    m_settings.sysRoot.setValue(p.sysRoot);
    m_settings.serverInitCommands.setValue(p.serverInitCommands);
    m_settings.serverResetCommands.setValue(p.serverResetCommands);
    m_settings.debugInfoLocation.setValue(p.debugInfoLocation);
    m_settings.arguments.setValue(p.runnable.command.arguments());
    m_settings.workingDirectory.setValue(p.runnable.workingDirectory);
    m_settings.breakAtMain.setValue(p.breakAtMain);
    m_settings.runInTerminal.setValue(p.runInTerminal);
    m_settings.useTargetExtendedRemote.setValue(p.useTargetExtendedRemote);
    updateState();
}

///////////////////////////////////////////////////////////////////////
//
// AttachToQmlPortDialog
//
///////////////////////////////////////////////////////////////////////

class AttachToQmlPortSettings final : public AspectContainer
{
public:
    AttachToQmlPortSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/AttachToQmlPortDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("Kit:"));
        kitChooser.setShowIcons(true);

        port.setQmlName("Port");
        port.setLabelText(Tr::tr("&Port:"));
        port.setRange(0, 65535);
        // The port qmljsdebugger is usually told to listen on.
        port.setDefaultValue(3768);
        port.setValue(3768);
    }

    KitChooserAspect kitChooser{this};
    IntegerAspect port{this};
};

class AttachToQmlPortDialog final : public QDialog
{
public:
    AttachToQmlPortDialog();

    int port() const { return m_settings.port(); }
    void setPort(const int port) { m_settings.port.setValue(port); }

    Kit *kit() const { return m_settings.kitChooser.currentKit(); }
    void setKitId(Utils::Id id) { m_settings.kitChooser.setCurrentKitId(id); }

private:
    AttachToQmlPortSettings m_settings;
};

AttachToQmlPortDialog::AttachToQmlPortDialog()
  : QDialog(ICore::dialogParent())
{
    setWindowTitle(Tr::tr("Attach to QML Port"));

    m_settings.kitChooser.populate();

    auto buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    auto verticalLayout = new QVBoxLayout(this);
    verticalLayout->addWidget(Core::createAspectForm(&m_settings));
    verticalLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void runAttachToQmlPortDialog()
{
    AttachToQmlPortDialog dlg;
    QtcSettings *settings = ICore::settings();

    const Key lastQmlServerPortKey = "DebugMode/LastQmlServerPort";
    const QVariant qmlServerPort = settings->value(lastQmlServerPortKey);

    if (qmlServerPort.isValid())
        dlg.setPort(qmlServerPort.toInt());
    else
        dlg.setPort(-1);

    const Key lastProfileKey = "DebugMode/LastProfile";
    const Id kitId = Id::fromSetting(settings->value(lastProfileKey));
    if (kitId.isValid())
        dlg.setKitId(kitId);

    if (dlg.exec() != QDialog::Accepted)
        return;

    Kit *kit = dlg.kit();
    QTC_ASSERT(kit, return);
    settings->setValue(lastQmlServerPortKey, dlg.port());
    settings->setValue(lastProfileKey, kit->id().toSetting());

    IDevice::ConstPtr device = RunDeviceKitAspect::device(kit);
    QTC_ASSERT(device, return);

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);
    DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);

    QUrl qmlServer = device->toolControlChannel(IDevice::QmlControlChannel);
    qmlServer.setPort(dlg.port());
    rp.setQmlServer(qmlServer);

    const SshParameters sshParameters = device->sshParameters();
    rp.setRemoteChannel(sshParameters.host() + ':' + QString::number(sshParameters.port()));
    rp.setStartMode(AttachToQmlServer);

    runControl->setRunRecipe(debuggerRecipe(runControl, rp));
    runControl->start();
}

// StartRemoteCdbDialog

static QString cdbRemoteHelp()
{
    const char cdbConnectionSyntax[] =
            "Server:Port<br>"
            "tcp:server=Server,port=Port[,password=Password][,ipversion=6]\n"
            "tcp:clicon=Server,port=Port[,password=Password][,ipversion=6]\n"
            "npipe:server=Server,pipe=PipeName[,password=Password]\n"
            "com:port=COMPort,baud=BaudRate,channel=COMChannel[,password=Password]\n"
            "spipe:proto=Protocol,{certuser=Cert|machuser=Cert},server=Server,pipe=PipeName[,password=Password]\n"
            "ssl:proto=Protocol,{certuser=Cert|machuser=Cert},server=Server,port=Socket[,password=Password]\n"
            "ssl:proto=Protocol,{certuser=Cert|machuser=Cert},clicon=Server,port=Socket[,password=Password]";

    const QString ext32 = QDir::toNativeSeparators(CdbEngine::extensionLibraryName(false));
    const QString ext64 = QDir::toNativeSeparators(CdbEngine::extensionLibraryName(true));
    return Tr::
        tr("<html><body><p>The remote CDB needs to load the matching %1 CDB extension "
           "(<code>%2</code> or <code>%3</code>, respectively).</p><p>Copy it onto the remote "
           "machine and set the "
           "environment variable <code>%4</code> to point to its folder.</p><p>"
           "Launch the remote CDB as <code>%5 &lt;executable&gt;</code> "
           "to use TCP/IP as communication protocol.</p><p>Enter the connection parameters as:</p>"
           "<pre>%6</pre></body></html>")
            .arg(QGuiApplication::applicationDisplayName(),
                 ext32,
                 ext64,
                 QString("_NT_DEBUGGER_EXTENSION_PATH"),
                 QString("cdb.exe -server tcp:port=1234"),
                 QString(cdbConnectionSyntax));
}

class StartRemoteCdbDialog final : public QDialog
{
public:
    StartRemoteCdbDialog();

    QString connection() const;
    void setConnection(const QString &);

private:
    void textChanged(const QString &);
    void accept() override;

    QPushButton *m_okButton = nullptr;
    QLineEdit *m_lineEdit;
};

StartRemoteCdbDialog::StartRemoteCdbDialog()
    : QDialog(ICore::dialogParent()), m_lineEdit(new QLineEdit)
{
    setWindowTitle(Tr::tr("Start a CDB Remote Session"));

    auto groupBox = new QGroupBox;

    auto helpLabel = new QLabel(cdbRemoteHelp());
    helpLabel->setWordWrap(true);
    helpLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto label = new QLabel(Tr::tr("&Connection:"));
    label->setBuddy(m_lineEdit);
    m_lineEdit->setMinimumWidth(400);

    auto box = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);

    auto formLayout = new QFormLayout;
    formLayout->addRow(helpLabel);
    formLayout->addRow(label, m_lineEdit);
    groupBox->setLayout(formLayout);

    auto vLayout = new QVBoxLayout(this);
    vLayout->addWidget(groupBox);
    vLayout->addWidget(box);

    m_okButton = box->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &StartRemoteCdbDialog::textChanged);
    connect(m_lineEdit, &QLineEdit::returnPressed, m_okButton, &QAbstractButton::animateClick);
    connect(box, &QDialogButtonBox::accepted, this, &StartRemoteCdbDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void StartRemoteCdbDialog::accept()
{
    if (!m_lineEdit->text().isEmpty())
        QDialog::accept();
}

void StartRemoteCdbDialog::textChanged(const QString &t)
{
    m_okButton->setEnabled(!t.isEmpty());
}

QString StartRemoteCdbDialog::connection() const
{
    const QString rc = m_lineEdit->text();
    // Transform an IP:POrt ('localhost:1234') specification into full spec
    QRegularExpression ipRegexp("([\\w\\.\\-_]+):([0-9]{1,4})");
    QTC_ASSERT(ipRegexp.isValid(), return QString());
    const QRegularExpressionMatch match = ipRegexp.match(rc);
    if (match.hasMatch())
        return QString::fromLatin1("tcp:server=%1,port=%2").arg(match.captured(1), match.captured(2));
    return rc;
}

void StartRemoteCdbDialog::setConnection(const QString &c)
{
    m_lineEdit->setText(c);
    m_okButton->setEnabled(!c.isEmpty());
}

void runStartRemoteCdbSessionDialog(Kit *kit)
{
    QTC_ASSERT(kit, return);
    const Key connectionKey = "DebugMode/CdbRemoteConnection";

    StartRemoteCdbDialog dlg;
    QString previousConnection = ICore::settings()->value(connectionKey).toString();
    if (previousConnection.isEmpty())
        previousConnection = "localhost:1234";
    dlg.setConnection(previousConnection);
    if (dlg.exec() != QDialog::Accepted)
        return;

    ICore::settings()->setValue(connectionKey, dlg.connection());

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);

    DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
    rp.setStartMode(AttachToRemoteServer);
    rp.setCloseMode(KillAtClose);
    rp.setRemoteChannel(dlg.connection());

    runControl->setRunRecipe(debuggerRecipe(runControl, rp));
    runControl->start();
}

//
// AddressDialog
//

class AddressDialog final : public QDialog
{
public:
     AddressDialog();

     void setAddress(quint64 a) { m_lineEdit->setText("0x" + QString::number(a, 16)); }
     quint64 address() const { return m_lineEdit->text().toULongLong(nullptr, 16); }

     void setOkButtonEnabled(bool v) { m_box->button(QDialogButtonBox::Ok)->setEnabled(v); }
     bool isOkButtonEnabled() const { return m_box->button(QDialogButtonBox::Ok)->isEnabled(); }

private:
     void accept() override;

     QLineEdit *m_lineEdit;
     QDialogButtonBox *m_box;
};

AddressDialog::AddressDialog()
    : QDialog(ICore::dialogParent()),
      m_lineEdit(new QLineEdit),
      m_box(new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel))
{
    setWindowTitle(Tr::tr("Select Start Address"));

    auto hLayout = new QHBoxLayout;
    hLayout->addWidget(new QLabel(Tr::tr("Enter an address:") + ' '));
    hLayout->addWidget(m_lineEdit);

    auto vLayout = new QVBoxLayout;
    vLayout->addLayout(hLayout);
    vLayout->addWidget(m_box);
    setLayout(vLayout);

    connect(m_box, &QDialogButtonBox::accepted, this, &AddressDialog::accept);
    connect(m_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_lineEdit, &QLineEdit::returnPressed, this, &AddressDialog::accept);

    connect(m_lineEdit, &QLineEdit::textChanged, this, [this] {
        const QString text = m_lineEdit->text();
        bool ok = false;
        text.toULongLong(&ok, 16);
        setOkButtonEnabled(ok);
    });

    setOkButtonEnabled(false);
}

void AddressDialog::accept()
{
    if (isOkButtonEnabled())
        QDialog::accept();
}

std::optional<quint64> runAddressDialog(quint64 initialAddress)
{
    AddressDialog dialog;
    dialog.setAddress(initialAddress);

    if (dialog.exec() != QDialog::Accepted)
        return {};

    return dialog.address();
}

#ifdef WITH_TESTS

class StartApplicationDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        StartApplicationSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "StartApplicationDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatARecentRunIsCalled()
    {
        // The name is the command, cut at a word boundary so that a long
        // command line does not become a combo box as wide as the screen.
        StartApplicationParameters p;
        p.runnable.command.setExecutable(FilePath::fromString("/usr/bin/tool"));
        p.runnable.command.setArguments("--one --two");
        QCOMPARE(p.displayName(), QString("tool --one --two"));

        StartApplicationParameters long_;
        long_.runnable.command.setExecutable(FilePath::fromString("/usr/bin/tool"));
        long_.runnable.command.setArguments(QString("--argument").repeated(12));
        const QString name = long_.displayName();
        QVERIFY2(name.endsWith("..."), "a long command line was not cut short");
        QVERIFY2(name.size() <= 64, qPrintable(QString("name is %1 long").arg(name.size())));
    }

    void testARecentRunFillsTheFieldsIn()
    {
        StartApplicationDialog dlg;

        StartApplicationParameters p;
        p.runnable.command.setExecutable(FilePath::fromString("/usr/bin/tool"));
        p.runnable.command.setArguments("--one");
        p.runnable.workingDirectory = FilePath::fromString("/tmp");
        p.serverPort = 2345;
        p.serverAddress = "127.0.0.1:1234";
        p.breakAtMain = true;
        p.runInTerminal = true;
        p.useTargetExtendedRemote = true;
        p.sysRoot = FilePath::fromString("/opt/sysroot");
        p.serverInitCommands = "init";
        p.serverResetCommands = "reset";
        p.debugInfoLocation = FilePath::fromString("/opt/debug");

        dlg.setParameters(p);
        const StartApplicationParameters back = dlg.parameters();

        // Every field the dialog carries comes back, including the one the
        // comparison below leaves out.
        QCOMPARE(back.runnable.command.executable(), p.runnable.command.executable());
        QCOMPARE(back.runnable.command.arguments(), p.runnable.command.arguments());
        QCOMPARE(back.runnable.workingDirectory, p.runnable.workingDirectory);
        QCOMPARE(back.serverPort, p.serverPort);
        QCOMPARE(back.serverAddress, p.serverAddress);
        QCOMPARE(back.breakAtMain, p.breakAtMain);
        QCOMPARE(back.runInTerminal, p.runInTerminal);
        QCOMPARE(back.useTargetExtendedRemote, p.useTargetExtendedRemote);
        QCOMPARE(back.sysRoot, p.sysRoot);
        QCOMPARE(back.serverInitCommands, p.serverInitCommands);
        QCOMPARE(back.serverResetCommands, p.serverResetCommands);
        QCOMPARE(back.debugInfoLocation, p.debugInfoLocation);
    }

    void testTheKeysItRemembers()
    {
        // The settings are an array of past runs, so the keys are the
        // compatibility surface for every entry a reader already has.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QtcSettings settings(dir.filePath("test.ini"), QSettings::IniFormat);

        StartApplicationParameters written;
        written.runnable.command.setExecutable(FilePath::fromString("/usr/bin/tool"));
        written.runnable.command.setArguments("--one");
        written.serverPort = 2345;
        written.useTargetExtendedRemote = true;
        written.serverInitCommands = "init";
        written.toSettings(&settings);

        QCOMPARE(settings.value("LastExternalExecutableArguments").toString(), QString("--one"));
        QCOMPARE(settings.value("LastServerPort").toUInt(), 2345u);
        QVERIFY(settings.value("LastExternalUseTargetExtended").toBool());

        StartApplicationParameters read;
        read.fromSettings(&settings);
        QCOMPARE(read.runnable.command.executable(), written.runnable.command.executable());
        QCOMPARE(read.serverPort, written.serverPort);
        QCOMPARE(read.serverInitCommands, written.serverInitCommands);
        QVERIFY2(read.useTargetExtendedRemote,
                 "extended-remote was written but did not come back");
    }

    void testAChannelOfItsOwnReplacesThePort()
    {
        // The port is part of the address the dialog builds; a channel given
        // in full leaves nothing to build.
        StartApplicationDialog dlg;
        QVERIFY(dlg.m_settings.serverPort.isEnabled());

        dlg.m_settings.channelOverride.setValue(QString("/dev/ttyS0"));
        QVERIFY2(!dlg.m_settings.serverPort.isEnabled(),
                 "a port could still be chosen beside a channel of its own");

        dlg.m_settings.channelOverride.setValue(QString());
        QVERIFY(dlg.m_settings.serverPort.isEnabled());
    }

    void testWhichFieldsAreForARunningServerOnly()
    {
        // Starting an application locally has no server to reach, so these
        // five go away together. A field left behind is one the reader can
        // fill in and that is then ignored.
        StartApplicationSettings settings;
        const QList<BaseAspect *> remote = settings.remoteOnly();
        QCOMPARE(remote.size(), 5);
        QVERIFY(remote.contains(&settings.serverPort));
        QVERIFY(remote.contains(&settings.channelOverride));
        QVERIFY(remote.contains(&settings.serverInitCommands));
        QVERIFY(remote.contains(&settings.serverResetCommands));

        // And the ones that mean something either way stay.
        QVERIFY2(!remote.contains(&settings.localExecutable), "the executable was hidden");
        QVERIFY2(!remote.contains(&settings.breakAtMain), "breaking at main was hidden");

        for (BaseAspect *const aspect : remote)
            QVERIFY(aspect->isVisible());
        for (BaseAspect *const aspect : remote)
            aspect->setVisible(false);
        QVERIFY2(!settings.serverPort.isVisible(), "the port stayed on a local run");
        QVERIFY2(settings.localExecutable.isVisible(), "the executable went away too");
    }
};

QObject *createStartApplicationDialogTest()
{
    return new StartApplicationDialogTest;
}

class AttachToQmlPortSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        AttachToQmlPortSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "AttachToQmlPortDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testThePortItOpensOn()
    {
        AttachToQmlPortSettings settings;

        // qmljsdebugger's usual port, so the reader usually has nothing to
        // change.
        QCOMPARE(settings.port(), 3768);
        // The whole range, as the spin box allowed.
        QCOMPARE(settings.port.presentation().maximum.toInt(), 65535);

        settings.port.setValue(1234);
        QCOMPARE(settings.port(), 1234);
    }

    void testTheKitsAreToldApartByMoreThanTheirNames()
    {
        // The widget chooser showed each kit's own icon here; two kits with
        // the same name are otherwise the same entry twice.
        AttachToQmlPortSettings settings;
        settings.kitChooser.populate();
        if (settings.kitChooser.kit.optionCount() == 0)
            QSKIP("no kits are configured on this machine");

        bool anyIcon = false;
        for (int i = 0; i < settings.kitChooser.kit.optionCount(); ++i) {
            const std::optional<SelectionAspect::Option> option
                = settings.kitChooser.kit.optionForIndex(i);
            QVERIFY(option);
            anyIcon = anyIcon || !option->icon.isNull();
        }
        QVERIFY2(anyIcon, "no kit carried an icon of its own");
    }

    void testTheDialogRoundTripsWhatItIsGiven()
    {
        AttachToQmlPortDialog dlg;
        dlg.setPort(4321);
        QCOMPARE(dlg.port(), 4321);
    }
};

QObject *createAttachToQmlPortSettingsTest()
{
    return new AttachToQmlPortSettingsTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

Q_DECLARE_METATYPE(Debugger::Internal::StartApplicationParameters)

#ifdef WITH_TESTS
#include "debuggerdialogs.moc"
#endif
