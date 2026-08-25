// Copyright (C) 2019 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "idebugserverprovider.h"

#include "baremetaldevice.h"
#include "baremetaltr.h"
#include "debugserverprovidermanager.h"

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/runcontrol.h>

#include <utils/qtcassert.h>

#include <QUuid>

using namespace Debugger;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace BareMetal::Internal {

const char idKeyC[] = "Id";
const char displayNameKeyC[] = "DisplayName";
const char engineTypeKeyC[] = "EngineType";

const char hostKeyC[] = "Host";
const char portKeyC[] = "Port";

const std::chrono::seconds readyTimeout(10);

static QString createId(const QString &id)
{
    QString newId = id.left(id.indexOf(':'));
    newId.append(':' + QUuid::createUuid().toString());
    return newId;
}

// IDebugServerProvider

IDebugServerProvider::IDebugServerProvider(const QString &id)
    : m_id(createId(id))
{
    // A settings page applies on Apply, so nothing here writes itself
    // through as it is typed.
    setAutoApply(false);

    providerName.setSettingsKey(displayNameKeyC);
    providerName.setLabelText(Tr::tr("Name:"));
    providerName.setToolTip(Tr::tr("Enter the name of the debugger server provider."));

    address.setInlineRow(true);
    address.setLabelText(Tr::tr("Host:"));
    host.setSettingsKey(hostKeyC);
    host.setToolTip(Tr::tr("Enter TCP/IP hostname of the debug server, "
                           "like \"localhost\" or \"192.0.2.1\"."));
    port.setSettingsKey(portKeyC);
    port.setRange(0, 65535);
    port.setToolTip(Tr::tr("Enter TCP/IP port which will be listened by "
                           "the debug server."));
}

void IDebugServerProvider::addSettingsRows(AspectContainer &rows)
{
    rows.registerAspect(&providerName);
}

IDebugServerProvider::~IDebugServerProvider()
{
    DeviceManager::forEachDevice([this](const IDeviceConstPtr &dev) {
        if (auto device = std::dynamic_pointer_cast<const BareMetalDevice>(dev))
            device->unregisterDebugServerProvider(id());
    });
}

QString IDebugServerProvider::displayName() const
{
    if (providerName().isEmpty())
        return typeDisplayName();
    return providerName();
}

void IDebugServerProvider::setDisplayName(const QString &name)
{
    if (providerName() == name)
        return;

    providerName.setValue(name);
    providerUpdated();
}

void IDebugServerProvider::setChannel(const QUrl &channel)
{
    host.setValue(channel.host());
    port.setValue(channel.port());
}

void IDebugServerProvider::setChannel(const QString &hostName, int portNumber)
{
    host.setValue(hostName);
    port.setValue(portNumber);
}

QUrl IDebugServerProvider::channel() const
{
    QUrl url;
    url.setScheme("tcp");
    url.setHost(host());
    url.setPort(port());
    return url;
}

QString IDebugServerProvider::channelPipe() const
{
    return {};
}

void IDebugServerProvider::connectReadyBarrier(RunControl *runControl, Process &process,
                                               QBarrier *barrier) const
{
    QTC_ASSERT(barrier, return);
    const QString message = readyMessage();
    if (message.isEmpty()) {
        QObject::connect(&process, &Process::started, barrier, &QBarrier::advance);
        return;
    }

    process.setTextChannelMode(Channel::Output, TextChannelMode::MultiLine);
    process.setTextChannelMode(Channel::Error, TextChannelMode::MultiLine);
    const auto onText = [barrier, message](const QString &text) {
        if (text.contains(message))
            barrier->advance();
    };
    QObject::connect(&process, &Process::textOnStandardOutput, barrier, onText);
    QObject::connect(&process, &Process::textOnStandardError, barrier, onText);
    QObject::connect(&process, &Process::started, barrier, [runControl, barrier] {
        QTimer::singleShot(readyTimeout, barrier, [runControl, barrier] {
            if (barrier->isRunning()) {
                runControl->postMessage(Tr::tr("The debug server has not announced that it is "
                                               "ready. Starting the debugger anyway."),
                                        ErrorMessageFormat);
                barrier->advance();
            }
        });
    });
}

QString IDebugServerProvider::id() const
{
    return m_id;
}

QString IDebugServerProvider::typeDisplayName() const
{
    return m_typeDisplayName;
}

void IDebugServerProvider::setTypeDisplayName(const QString &typeDisplayName)
{
    m_typeDisplayName = typeDisplayName;
}

DebuggerEngineType IDebugServerProvider::engineType() const
{
    return m_engineType;
}

void IDebugServerProvider::setEngineType(DebuggerEngineType engineType)
{
    if (m_engineType == engineType)
        return;
    m_engineType = engineType;
    providerUpdated();
}

bool IDebugServerProvider::operator==(const IDebugServerProvider &other) const
{
    // Providers are identified by their unique id. Comparing only the type
    // prefix (and ignoring the id and the display name) made a cloned
    // provider compare equal to its origin, so it was rejected as a
    // duplicate on registration and could not be added.
    return id() == other.id();
}

void IDebugServerProvider::toMap(Store &data) const
{
    AspectContainer::toMap(data);
    // Neither is the user's to change, so neither is an aspect.
    data.insert(idKeyC, m_id);
    data.insert(engineTypeKeyC, m_engineType);
}

void IDebugServerProvider::providerUpdated()
{
    DebugServerProviderManager::notifyAboutUpdate(this);
}

void IDebugServerProvider::resetId()
{
    m_id = createId(m_id);
}

void IDebugServerProvider::fromMap(const Store &data)
{
    AspectContainer::fromMap(data);
    m_id = data.value(idKeyC).toString();
    m_engineType = static_cast<DebuggerEngineType>(
                data.value(engineTypeKeyC, NoEngineType).toInt());
}

// IDebugServerProviderFactory

static QList<IDebugServerProviderFactory *> theDebugServerProviderFactories;

IDebugServerProviderFactory::IDebugServerProviderFactory()
{
    theDebugServerProviderFactories.append(this);
}

IDebugServerProviderFactory::~IDebugServerProviderFactory()
{
    theDebugServerProviderFactories.removeOne(this);
}

const QList<IDebugServerProviderFactory *> IDebugServerProviderFactory::factories()
{
    return theDebugServerProviderFactories;
}

QString IDebugServerProviderFactory::id() const
{
    return m_id;
}

void IDebugServerProviderFactory::setId(const QString &id)
{
    m_id = id;
}

QString IDebugServerProviderFactory::displayName() const
{
    return m_displayName;
}

IDebugServerProvider *IDebugServerProviderFactory::create() const
{
    return m_creator();
}

IDebugServerProvider *IDebugServerProviderFactory::restore(const Store &data) const
{
    IDebugServerProvider *p = m_creator();
    p->fromMap(data);
    return p;
}

bool IDebugServerProviderFactory::canRestore(const Store &data) const
{
    const QString id = idFromMap(data);
    return id.startsWith(m_id + ':');
}

void IDebugServerProviderFactory::setDisplayName(const QString &name)
{
    m_displayName = name;
}

void IDebugServerProviderFactory::setCreator(const std::function<IDebugServerProvider *()> &creator)
{
    m_creator = creator;
}

QString IDebugServerProviderFactory::idFromMap(const Store &data)
{
    return data.value(idKeyC).toString();
}

void IDebugServerProviderFactory::idToMap(Store &data, const QString &id)
{
    data.insert(idKeyC, id);
}

} // BareMetal::Internal

#ifdef WITH_TESTS

#include "baremetalconstants.h"

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/algorithm.h>

#include <QTest>

namespace BareMetal::Internal {

class ReadyMessageProvider final : public IDebugServerProvider
{
public:
    explicit ReadyMessageProvider(const QString &message)
        : IDebugServerProvider("Test"), m_message(message)
    {}

    Result<> setupDebuggerRunParameters(DebuggerRunParameters &, RunControl *) const final
    { return ResultOk; }
    std::optional<BarrierKickerGetter> serverRunner(RunControl *) const final { return {}; }
    bool isValid() const final { return true; }
    QString readyMessage() const final { return m_message; }

    using IDebugServerProvider::connectReadyBarrier;

private:
    const QString m_message;
};

class DebugServerReadyTest final : public QObject
{
    Q_OBJECT

private slots:
    void testReadyBarrier_data();
    void testReadyBarrier();
    void testOpenOcdReadyMessage();
};

void DebugServerReadyTest::testReadyBarrier_data()
{
    QTest::addColumn<QString>("readyMessage");
    QTest::addColumn<QString>("standardOutput");
    QTest::addColumn<QString>("standardError");
    QTest::addColumn<bool>("readyOnStart");
    QTest::addColumn<bool>("readyOnOutput");

    const QString openOcdLine = "Info : Listening on port 3333 for gdb connections\n";

    QTest::newRow("no ready message")
        << QString() << QString() << QString() << true << true;
    QTest::newRow("ready line on stderr")
        << "for gdb connections" << QString() << openOcdLine << false << true;
    QTest::newRow("ready line on stdout")
        << "for gdb connections" << openOcdLine << QString() << false << true;
    QTest::newRow("other output only")
        << "for gdb connections" << "Info : clock speed 950 kHz\n"
        << "Warn : target not examined yet\n" << false << false;
}

void DebugServerReadyTest::testReadyBarrier()
{
    QFETCH(QString, readyMessage);
    QFETCH(QString, standardOutput);
    QFETCH(QString, standardError);
    QFETCH(bool, readyOnStart);
    QFETCH(bool, readyOnOutput);

    RunControl runControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    const ReadyMessageProvider provider(readyMessage);
    Process process;
    QStartedBarrier barrier;

    provider.connectReadyBarrier(&runControl, process, &barrier);
    QVERIFY(barrier.isRunning());

    // What the process reports is under test here, not the process itself.
    emit process.started();
    QCOMPARE(!barrier.isRunning(), readyOnStart);

    if (!standardOutput.isEmpty())
        emit process.textOnStandardOutput(standardOutput);
    if (!standardError.isEmpty())
        emit process.textOnStandardError(standardError);
    QCOMPARE(!barrier.isRunning(), readyOnOutput);
}

void DebugServerReadyTest::testOpenOcdReadyMessage()
{
    // Recorded from OpenOCD 0.12.0, which writes this to its standard error.
    const QStringList lines = {
        "Open On-Chip Debugger 0.12.0",
        "Info : Listening on port 6666 for tcl connections",
        "Info : Listening on port 4444 for telnet connections",
        "Info : clock speed 950 kHz",
        "Info : stm32f1x.cpu: hardware has 6 breakpoints, 4 watchpoints",
        "Info : starting gdb server for stm32f1x.cpu on 3333",
        "Info : Listening on port 3333 for gdb connections"
    };

    IDebugServerProviderFactory *factory
        = Utils::findOrDefault(IDebugServerProviderFactory::factories(),
                               [](IDebugServerProviderFactory *factory) {
        return factory->id() == Constants::GDBSERVER_OPENOCD_PROVIDER_ID;
    });
    QVERIFY(factory);
    const std::unique_ptr<IDebugServerProvider> provider(factory->create());

    const QString message = provider->readyMessage();
    QVERIFY(!message.isEmpty());
    QVERIFY(lines.last().contains(message));
    // Nothing the server writes before the gdb port is open may match.
    for (qsizetype i = 0; i < lines.size() - 1; ++i)
        QVERIFY2(!lines.at(i).contains(message), qPrintable(lines.at(i)));
}

QObject *createDebugServerReadyTest()
{
    return new DebugServerReadyTest;
}

} // BareMetal::Internal

#endif // WITH_TESTS

#include "idebugserverprovider.moc"
