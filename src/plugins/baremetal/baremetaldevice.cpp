// Copyright (C) 2016 Tim Sander <tim@krieglstein.org>
// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "baremetaldevice.h"

#include "baremetalconstants.h"
#include "baremetaldevice.h"
#include "baremetaltr.h"
#include "debugserverproviderchooser.h"
#include "debugserverprovidermanager.h"
#include "idebugserverprovider.h"

#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/wizard.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QFormLayout>
#include <QStandardItem>
#include <QLineEdit>
#include <QWizardPage>

using namespace ProjectExplorer;
using namespace Utils;

namespace BareMetal::Internal {


// BareMetalDevice

BareMetalDevice::BareMetalDevice()
{
    setDisplayType(Tr::tr("Bare Metal"));
    setOsType(Utils::OsTypeOther);

    m_debugServerProviderId.setSettingsKey("IDebugServerProviderId");
    m_debugServerProviderId.setLabelText(Tr::tr("Debug server provider:"));
    m_debugServerProviderId.setFillCallback(
        [](const StringSelectionAspect::ResultCallback &cb) {
            QList<QStandardItem *> items;
            const auto none = new QStandardItem(Tr::tr("None", "No debug server provider"));
            none->setData(QString());
            items.append(none);
            for (const IDebugServerProvider * const provider :
                 DebugServerProviderManager::providers()) {
                if (!provider->isValid())
                    continue;
                const auto item = new QStandardItem(provider->displayName());
                item->setData(provider->id());
                items.append(item);
            }
            cb(items);
        });
    // Which providers there are changes while the page is open.
    QObject::connect(DebugServerProviderManager::instance(),
                     &DebugServerProviderManager::providersChanged,
                     &m_debugServerProviderId,
                     [this] { m_debugServerProviderId.refill(); });

    m_manageProviders.setActionText(Tr::tr("Manage..."));
    m_manageProviders.setAction(
        [] { Core::ICore::showSettings(Utils::Id(Constants::DEBUG_SERVER_PROVIDERS_SETTINGS_ID)); });
}

BareMetalDevice::~BareMetalDevice() = default;

QString BareMetalDevice::defaultDisplayName()
{
    return Tr::tr("Bare Metal Device");
}

QString BareMetalDevice::debugServerProviderId() const
{
    return m_debugServerProviderId();
}

void BareMetalDevice::setDebugServerProviderId(const QString &id)
{
    if (id == debugServerProviderId())
        return;
    m_debugServerProviderId.setValue(id);
}

void BareMetalDevice::unregisterDebugServerProvider(const QString &providerId) const
{
    if (providerId == debugServerProviderId())
        m_debugServerProviderId.setValue(QString());
}

void BareMetalDevice::addSettingsRows(AspectContainer &rows)
{
    // One row: which debug server provider, and the page that manages them.
    addRow(rows, {&m_debugServerProviderId, &m_manageProviders});
}

void BareMetalDevice::fromMap(const Store &map)
{
    IDevice::fromMap(map);

    // Override wrong state from Creator 19.0.0, see QTCREATORBUG-34221
    setDeviceState(DeviceStateUnknown);

    if (debugServerProviderId().isEmpty()) {
        const QString name = displayName();
        if (IDebugServerProvider *provider =
                DebugServerProviderManager::findByDisplayName(name)) {
            setDebugServerProviderId(provider->id());
        }
    }
}

//  BareMetalDeviceConfigurationWizardSetupPage

class BareMetalDeviceConfigurationWizardSetupPage final : public QWizardPage
{
public:
    explicit BareMetalDeviceConfigurationWizardSetupPage(QWidget *parent)
        : QWizardPage(parent)
    {
        setTitle(Tr::tr("Set up Debug Server or Hardware Debugger"));

        m_nameLineEdit = new QLineEdit(this);

        m_providerChooser = new DebugServerProviderChooser(false, this);
        m_providerChooser->populate();

        const auto formLayout = new QFormLayout(this);
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        formLayout->addRow(Tr::tr("Name:"), m_nameLineEdit);
        formLayout->addRow(Tr::tr("Debug server provider:"), m_providerChooser);

        connect(m_nameLineEdit, &QLineEdit::textChanged,
                this, &BareMetalDeviceConfigurationWizardSetupPage::completeChanged);
        connect(m_providerChooser, &DebugServerProviderChooser::providerChanged,
                this, &QWizardPage::completeChanged);
    }

    void initializePage() final
    {
        m_nameLineEdit->setText(BareMetalDevice::defaultDisplayName());
    }

    bool isComplete() const final
    {
        return !configurationName().isEmpty();
    }

    QString configurationName() const { return m_nameLineEdit->text().trimmed(); }
    QString debugServerProviderId() const { return m_providerChooser->currentProviderId(); }

private:
    QLineEdit *m_nameLineEdit = nullptr;
    DebugServerProviderChooser *m_providerChooser = nullptr;
};

//  BareMetalDeviceConfigurationWizardSetupPage

class BareMetalDeviceConfigurationWizard final : public Wizard
{
public:
    BareMetalDeviceConfigurationWizard()
        : m_setupPage(new BareMetalDeviceConfigurationWizardSetupPage(this))
    {
        enum PageId { SetupPageId };

        setWindowTitle(Tr::tr("New Bare Metal Device Configuration Setup"));
        setPage(SetupPageId, m_setupPage);
        m_setupPage->setCommitPage(true);
    }

    IDevicePtr device() const
    {
        const auto dev = BareMetalDevice::create();
        dev->setupId(IDevice::ManuallyAdded, Utils::Id());
        dev->setDefaultDisplayName(m_setupPage->configurationName());
        dev->setType(Constants::BareMetalOsType);
        dev->setMachineType(IDevice::Hardware);
        dev->setDebugServerProviderId(m_setupPage->debugServerProviderId());
        dev->setDeviceState(IDevice::DeviceStateUnknown);
        return dev;
    }

private:
    BareMetalDeviceConfigurationWizardSetupPage *m_setupPage = nullptr;
};


// Factory

class BareMetalDeviceFactory final : public IDeviceFactory
{
public:
    BareMetalDeviceFactory()
        : IDeviceFactory(Constants::BareMetalOsType)
    {
        setDisplayName(Tr::tr("Bare Metal Device"));
        setCombinedIcon(":/baremetal/images/baremetaldevicesmall.png",
                        ":/baremetal/images/baremetaldevice.png");
        setConstructionFunction(&BareMetalDevice::create);
        setCreator([] {
            BareMetalDeviceConfigurationWizard wizard;
            if (wizard.exec() != QDialog::Accepted)
                return IDevice::Ptr();
            return wizard.device();
        });
    }
};

#ifdef WITH_TESTS
class BareMetalDeviceTest final : public QObject
{
    Q_OBJECT

private:
    // A provider of any kind the plugin can make, so that the list has
    // something in it on a machine with none configured.
    static IDebugServerProvider *registerAProvider()
    {
        for (IDebugServerProviderFactory * const factory :
             IDebugServerProviderFactory::factories()) {
            IDebugServerProvider * const provider = factory->create();
            if (!provider)
                continue;
            if (provider->isValid() && DebugServerProviderManager::registerProvider(provider))
                return provider;
            delete provider;
        }
        return nullptr;
    }

private slots:
    void testTheProviderIsAChoiceTheDeviceOffers()
    {
        // The provider used to be picked with DebugServerProviderChooser, a
        // combo box and a Manage button that only a widget page could show,
        // syncing itself to the device by hand. The device offers the choice
        // itself now.
        const IDevice::Ptr device = BareMetalDevice::create();
        QVERIFY(device);
        const auto dev = std::static_pointer_cast<BareMetalDevice>(device);

        const AspectPresentation p = dev->m_debugServerProviderId.presentation();
        QCOMPARE(p.control, AspectControls::ComboBox);
        QVERIFY(!p.labelText.isEmpty());

        // "None" is always offered, and stands for no provider rather than for
        // a provider called "None".
        QVERIFY(!p.choices.isEmpty());
        QCOMPARE(p.choices.first().id.toString(), QString());

        // One entry per valid provider, by id: what is stored is the id, so
        // refilling the list does not change which provider is picked. A
        // provider of this test's own, since a machine with none configured
        // would check nothing.
        IDebugServerProvider * const mine = registerAProvider();
        if (!mine)
            QSKIP("No kind of debug server provider can be created here");

        dev->m_debugServerProviderId.refill();
        const AspectPresentation filled = dev->m_debugServerProviderId.presentation();
        const QList<IDebugServerProvider *> valid = Utils::filtered(
            DebugServerProviderManager::providers(),
            [](const IDebugServerProvider *p) { return p->isValid(); });
        QVERIFY(valid.contains(mine));
        QCOMPARE(filled.choices.size(), valid.size() + 1);
        for (const IDebugServerProvider * const provider : valid) {
            QVERIFY2(Utils::anyOf(filled.choices,
                                  [provider](const AspectPresentation::Choice &c) {
                                      return c.id.toString() == provider->id();
                                  }),
                     qPrintable(provider->displayName() + " is not offered by its id"));
        }
        DebugServerProviderManager::deregisterProvider(mine);

        // And the value is the id the device stores, not a position in a list
        // that is rebuilt whenever a provider is added.
        dev->setDebugServerProviderId("some.provider.id");
        QCOMPARE(dev->debugServerProviderId(), QString("some.provider.id"));
        dev->unregisterDebugServerProvider("some.provider.id");
        QCOMPARE(dev->debugServerProviderId(), QString());
    }

    void testManagingProvidersIsSomethingTheDeviceOffers()
    {
        const IDevice::Ptr device = BareMetalDevice::create();
        const auto dev = std::static_pointer_cast<BareMetalDevice>(device);
        const AspectPresentation p = dev->m_manageProviders.presentation();
        QCOMPARE(p.control, AspectControls::Button);
        QVERIFY(!p.actionText.isEmpty());
    }
};

QObject *createBareMetalDeviceTest()
{
    return new BareMetalDeviceTest;
}
#endif // WITH_TESTS

void setupBareMetalDevice()
{
    static BareMetalDeviceFactory theBareMetalDeviceFactory;
}

} // BareMetal::Internal

#ifdef WITH_TESTS
#include "baremetaldevice.moc"
#endif
