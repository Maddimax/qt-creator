// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicesettingspage.h"

#include "devicefactoryselectiondialog.h"
#include "devicemanager.h"
#include "devicemanagermodel.h"
#include "deviceprocessesdialog.h"
#include "devicetestdialog.h"
#include "idevice.h"
#include "idevicefactory.h"

#include "../projectexplorerconstants.h"
#include "../projectexplorertr.h"

#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/async.h>
#include <utils/guiutils.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>

#include <QStandardItem>

#ifdef WITH_TESTS
#include <QScopeGuard>
#include <QTest>
#endif

using namespace Core;
using namespace Utils;

namespace ProjectExplorer::Internal {

const char LastDeviceIndexKey[] = "LastDisplayedMaemoDeviceConfig";

class DeviceSettingsWidget final : public AspectContainer
{
public:
    DeviceSettingsWidget();

    void apply() override;
    void cancel() override;
    bool isDirty() const override;

private:
    void saveSettings();
    void handleDeviceUpdated(Id id);
    void currentDeviceChanged();
    void addDevice();
    void addDeviceOfKind(IDeviceFactory *factory);
    void removeDevice();
    void setDefaultDevice();
    void testDevice();
    void showProcessList();
    void updateButtons();
    void fillDeviceChoices();
    void showDevice(const IDevicePtr &device);
    IDevicePtr currentDevice() const;
    Id idAfterRemoval(Id id) const;

    DeviceManagerModel m_deviceManagerModel;
    // Neither is a change to a device: what is on the list, and what will be
    // once the page is applied, are two different lists.
    QSet<Id> m_markedForDeletion;
    QSet<Id> m_newDevices;
    IDevicePtr m_shownDevice;

    StringSelectionAspect m_device{this};
    ActionAspect m_addButton{this};
    ActionAspect m_removeButton{this};
    ActionAspect m_defaultDeviceButton{this};
    // One button per action the current device offers. Which ones there are
    // changes with the device, so the container is refilled rather than fixed.
    AspectContainer m_deviceActions{this};

    AspectContainer m_general{this};
    // What the device itself asks, or what it reports where it asks nothing.
    ContainerAspect m_typeSpecific{this};
};

DeviceSettingsWidget::DeviceSettingsWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/devicesupport/DevicesPage.qml"));

    m_device.setQmlName("Device");
    m_device.setLabelText(Tr::tr("Device:"));
    m_device.setSizeAdjustPolicy(
        AspectControls::SizeAdjustPolicy::ToMinimumContentsLengthWithIcon);
    fillDeviceChoices();

    // Adding a device is a wizard, and the kinds that need no wizard are
    // offered beside it rather than instead of it.
    m_addButton.setQmlName("Add");
    m_addButton.setActionText(Tr::tr("Add..."));
    m_addButton.setActionIsDefault(true);
    m_addButton.setAction([this] { addDevice(); });
    QList<AspectPresentation::Choice> kinds;
    kinds.append({Tr::tr("Start Wizard to Add Device..."), {}, true, QString()});
    for (IDeviceFactory * const factory : IDeviceFactory::allDeviceFactories()) {
        if (!factory->canCreate() || !factory->quickCreationAllowed())
            continue;
        //: Add <Device Type Name>
        kinds.append({Tr::tr("Add %1").arg(factory->displayName()), {}, true,
                      factory->deviceType().toSetting()});
    }
    m_addButton.setChoices(kinds);
    m_addButton.setOnChoice([this](const QVariant &id) {
        const Id type = Id::fromSetting(id);
        if (!type.isValid()) {
            addDevice();
            return;
        }
        for (IDeviceFactory * const factory : IDeviceFactory::allDeviceFactories()) {
            if (factory->deviceType() == type) {
                addDeviceOfKind(factory);
                return;
            }
        }
    });
    m_addButton.setEnabled(
        Utils::anyOf(IDeviceFactory::allDeviceFactories(), &IDeviceFactory::canCreate));

    m_removeButton.setQmlName("Remove");
    m_removeButton.setActionText(Tr::tr("Remove"));
    m_removeButton.setAction([this] { removeDevice(); });

    m_defaultDeviceButton.setQmlName("SetAsDefault");
    m_defaultDeviceButton.setActionText(Tr::tr("Set As Default"));
    m_defaultDeviceButton.setAction([this] { setDefaultDevice(); });

    m_deviceActions.setQmlName("DeviceActions");

    m_general.setQmlName("General");
    m_general.setLabelText(Tr::tr("General"));

    m_typeSpecific.setQmlName("TypeSpecific");
    m_typeSpecific.setLabelText(Tr::tr("Type Specific"));

    // Behaviour, not layout.
    connect(&m_device, &StringSelectionAspect::volatileValueChanged,
            this, &DeviceSettingsWidget::currentDeviceChanged);
    connect(DeviceManager::instance(), &DeviceManager::deviceUpdated,
            this, &DeviceSettingsWidget::handleDeviceUpdated);
    connect(this, &AspectContainer::shown, this, [this] {
        m_device.refill();
        currentDeviceChanged();
    });

    Id toSelect;
    if (const Id preselected = preselectedOptionsPageItem(Constants::DEVICE_SETTINGS_PAGE_ID);
        preselected.isValid()) {
        toSelect = preselected;
    } else {
        const int lastIndex = ICore::settings()->value(LastDeviceIndexKey, 0).toInt();
        toSelect = m_deviceManagerModel.deviceId(qMax(lastIndex, 0));
    }
    if (!toSelect.isValid())
        toSelect = m_deviceManagerModel.deviceId(0);
    m_device.setValue(toSelect.toString());
    currentDeviceChanged();
}

void DeviceSettingsWidget::fillDeviceChoices()
{
    m_device.setFillCallback([this](const StringSelectionAspect::ResultCallback &cb) {
        QList<QStandardItem *> items;
        for (int i = 0, n = m_deviceManagerModel.rowCount(); i < n; ++i) {
            const IDevicePtr device = m_deviceManagerModel.device(i);
            if (!device)
                continue;
            // What will happen to a device on apply is said in the entry
            // rather than in its font: the two states a device can be in that
            // are not yet true have to survive being read out.
            QString name = device->displayName();
            if (m_markedForDeletion.contains(device->id()))
                name = Tr::tr("%1 (to be removed)").arg(name);
            else if (m_newDevices.contains(device->id()))
                name = Tr::tr("%1 (new)").arg(name);
            auto item = new QStandardItem(name);
            item->setData(device->id().toString());
            items.append(item);
        }
        cb(items);
    });
}

IDevicePtr DeviceSettingsWidget::currentDevice() const
{
    const Id id = Id::fromString(m_device.volatileValue());
    return id.isValid() ? DeviceManager::mutableDevice(id) : IDevicePtr();
}

void DeviceSettingsWidget::showDevice(const IDevicePtr &device)
{
    m_shownDevice = device;

    m_general.clear();
    m_typeSpecific.setContainer(nullptr);
    if (!device) {
        m_deviceActions.clear();
        return;
    }

    BaseAspect * const name = device->displayNameAspect();
    name->setLabelText(Tr::tr("Name:"));
    m_general.registerAspect(name);

    const auto addRow = [this](const QString &label, const QString &value) {
        const auto row = new TextDisplay;
        row->setLabelText(label);
        row->setText(value);
        m_general.registerAspect(row, /*takeOwnership=*/true);
        return row;
    };
    addRow(Tr::tr("Type:"), device->displayType());
    addRow(Tr::tr("Auto-detected:"),
           device->isAutoDetected() ? Tr::tr("Yes (id is \"%1\")").arg(device->id().toString())
                                    : Tr::tr("No"));
    // The state used to be a pixmap the device handed over. What it means -
    // reachable, not reachable, not asked yet - is what a renderer needs.
    TextDisplay * const state = addRow(Tr::tr("Current state:"), device->deviceStateToString());
    switch (device->deviceState()) {
    case IDevice::DeviceReadyToUse:
    case IDevice::DeviceConnected:
        state->setIconType(InfoType::Ok);
        break;
    case IDevice::DeviceDisconnected:
        state->setIconType(InfoType::Error);
        break;
    default:
        state->setIconType(InfoType::None);
        break;
    }

    // What a device asks, in the order it says. A kind with nothing to ask
    // still has something to say, which is what it reports.
    device->fillSettingsAspects();
    if (device->settingsAspects().aspects().isEmpty()) {
        device->refreshDeviceInfoAspects();
        m_typeSpecific.setContainer(&device->deviceInfoAspects());
    } else {
        m_typeSpecific.setContainer(&device->settingsAspects());
    }

    m_deviceActions.clear();
    QList<IDevice::DeviceAction> deviceActions;
    if (device->canCreateProcessModel()) {
        deviceActions << IDevice::DeviceAction{
            Tr::tr("Show Running Processes..."),
            [this](const IDevice::ConstPtr &) { showProcessList(); },
            [](const IDevice::ConstPtr &) { return true; }};
    }
    deviceActions << device->deviceActions();
    if (device->hasDeviceTester()) {
        deviceActions << IDevice::DeviceAction{
            Tr::tr("Test"),
            [this](const IDevice::ConstPtr &) { testDevice(); },
            [](const IDevice::ConstPtr &) { return true; }};
    }
    int index = 0;
    for (const IDevice::DeviceAction &deviceAction : std::as_const(deviceActions)) {
        const auto button = new ActionAspect;
        button->setQmlName(QString("Action%1").arg(index++));
        button->setActionText(deviceAction.display);
        if (deviceAction.activeChecker)
            button->setEnabled(deviceAction.activeChecker(device));
        button->setAction([this, deviceAction] {
            const IDevicePtr device = currentDevice();
            QTC_ASSERT(device, return);
            ICore::askToApplySettings([deviceAction, device] { deviceAction.execute(device); });
        });
        m_deviceActions.registerAspect(button, /*takeOwnership=*/true);
    }
}

void DeviceSettingsWidget::currentDeviceChanged()
{
    const IDevicePtr device = currentDevice();
    if (device != m_shownDevice)
        showDevice(device);
    updateButtons();
}

void DeviceSettingsWidget::updateButtons()
{
    const IDevicePtr device = currentDevice();
    m_general.setEnabled(bool(device));
    m_typeSpecific.setEnabled(bool(device));
    if (!device) {
        m_removeButton.setEnabled(false);
        m_defaultDeviceButton.setEnabled(false);
        return;
    }

    const bool isMarkedForDeletion = m_markedForDeletion.contains(device->id());
    m_removeButton.setEnabled(
        (!device->isAutoDetected() || device->deviceState() == IDevice::DeviceDisconnected)
        && !m_newDevices.contains(device->id()));
    m_removeButton.setActionText(isMarkedForDeletion ? Tr::tr("Restore") : Tr::tr("Remove"));
    m_defaultDeviceButton.setEnabled(DeviceManager::defaultDevice(device->type()) != device);
}

void DeviceSettingsWidget::addDevice()
{
    DeviceFactorySelectionDialog d;
    if (d.exec() != QDialog::Accepted)
        return;

    const Id toCreate = d.selectedId();
    if (!toCreate.isValid())
        return;
    IDeviceFactory * const factory = IDeviceFactory::find(toCreate);
    if (!factory)
        return;
    const IDevice::Ptr device = factory->create();
    if (!device)
        return;

    Utils::asyncRun([device] { device->checkOsType(); });

    DeviceManager::addDevice(device);
    m_newDevices.insert(device->id());
    m_device.refill();
    m_device.setValue(device->id().toString());
    currentDeviceChanged();
    saveSettings();
    if (device->hasDeviceTester())
        testDevice();
}

void DeviceSettingsWidget::addDeviceOfKind(IDeviceFactory *factory)
{
    const IDevice::Ptr device = factory->construct();
    QTC_ASSERT(device, return);
    DeviceManager::addDevice(device);
    m_newDevices.insert(device->id());
    m_device.refill();
    m_device.setValue(device->id().toString());
    currentDeviceChanged();
    saveSettings();
}

Id DeviceSettingsWidget::idAfterRemoval(Id id) const
{
    const auto isKept = [this](int i) {
        const IDevicePtr device = m_deviceManagerModel.device(i);
        return device && !m_markedForDeletion.contains(device->id());
    };

    const int index = m_deviceManagerModel.indexForId(id);
    // Prefer the next device in the list, fall back to the previous one.
    for (int i = index + 1, n = m_deviceManagerModel.rowCount(); i < n; ++i) {
        if (isKept(i))
            return m_deviceManagerModel.deviceId(i);
    }
    for (int i = index - 1; i >= 0; --i) {
        if (isKept(i))
            return m_deviceManagerModel.deviceId(i);
    }
    return {};
}

void DeviceSettingsWidget::removeDevice()
{
    const IDevicePtr device = currentDevice();
    QTC_ASSERT(device, return);
    const Id id = device->id();

    // Removal keeps the device in the list, so move on to make repeated
    // removal work.
    if (m_markedForDeletion.contains(id)) {
        m_markedForDeletion.remove(id);
        m_device.refill();
    } else {
        m_markedForDeletion.insert(id);
        const Id next = idAfterRemoval(id);
        m_device.refill();
        if (next.isValid())
            m_device.setValue(next.toString());
    }
    currentDeviceChanged();
}

void DeviceSettingsWidget::setDefaultDevice()
{
    const IDevicePtr device = currentDevice();
    QTC_ASSERT(device, return);
    DeviceManager::setDefaultDevice(device->id());
    m_defaultDeviceButton.setEnabled(false);
}

void DeviceSettingsWidget::testDevice()
{
    const IDevicePtr device = currentDevice();
    QTC_ASSERT(device && device->hasDeviceTester(), return);
    auto dlg = new DeviceTestDialog(device, ICore::dialogParent());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(true);
    dlg->show();
    connect(dlg, &QObject::destroyed, this, [this, id = device->id()] {
        handleDeviceUpdated(id);
    });
}

void DeviceSettingsWidget::showProcessList()
{
    const IDevicePtr device = currentDevice();
    QTC_ASSERT(device && device->canCreateProcessModel(), return);
    DeviceProcessesDialog dlg;
    dlg.addCloseButton();
    dlg.setDevice(device);
    dlg.exec();
}

void DeviceSettingsWidget::handleDeviceUpdated(Id id)
{
    const IDevicePtr device = currentDevice();
    if (!device || device->id() != id)
        return;
    // The device is the same one, but what it says about itself is not, so
    // the general group and the buttons are rebuilt rather than left alone.
    showDevice(device);
    updateButtons();
}

void DeviceSettingsWidget::saveSettings()
{
    const IDevicePtr device = currentDevice();
    // Everything a device shows is one of its own aspects now, so applying
    // the device is all there is to do; there is no widget holding a value
    // back.
    if (device)
        device->doApply();
    ICore::settings()->setValueWithDefault(
        LastDeviceIndexKey, m_deviceManagerModel.indexForId(device ? device->id() : Id()), 0);
}

void DeviceSettingsWidget::apply()
{
    for (const Id &id : std::as_const(m_markedForDeletion))
        DeviceManager::removeDevice(id);
    m_markedForDeletion.clear();
    m_newDevices.clear();

    saveSettings();
    m_device.refill();
    currentDeviceChanged();
}

void DeviceSettingsWidget::cancel()
{
    for (const Id &id : std::as_const(m_newDevices))
        DeviceManager::removeDevice(id);
    m_markedForDeletion.clear();
    m_newDevices.clear();

    for (int i = 0, n = m_deviceManagerModel.rowCount(); i < n; ++i) {
        if (const IDevicePtr device = m_deviceManagerModel.device(i))
            device->cancel();
    }
    m_device.refill();
    currentDeviceChanged();
}

bool DeviceSettingsWidget::isDirty() const
{
    // What the page itself holds - which device is being looked at - is not a
    // setting, so only the list and the devices count.
    if (!m_markedForDeletion.isEmpty() || !m_newDevices.isEmpty())
        return true;

    for (int i = 0, n = m_deviceManagerModel.rowCount(); i < n; ++i) {
        if (const IDevicePtr device = m_deviceManagerModel.device(i); device && device->isDirty())
            return true;
    }
    return false;
}

// DeviceSettingsPage

DeviceSettingsPage::DeviceSettingsPage()
{
    setId(Constants::DEVICE_SETTINGS_PAGE_ID);
    setDisplayName(Tr::tr("Devices"));
    setCategory(Constants::DEVICE_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static GuardedObject<DeviceSettingsWidget> theAspects;
        return theAspects.get();
    });
}

#ifdef WITH_TESTS
// By name rather than by type: a page has several buttons and several
// containers, and aspect<T>() hands back whichever comes first - which once
// meant a test asking for Remove and triggering Add.
template<typename T>
static T *aspectNamed(AspectContainer &page, const QString &qmlName)
{
    for (BaseAspect * const aspect : page.aspects()) {
        if (auto typed = qobject_cast<T *>(aspect); typed && typed->qmlName() == qmlName)
            return typed;
    }
    return nullptr;
}

class DeviceSettingsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhatThePageSaysAboutADeviceIsTheDevicesOwn()
    {
        // The page draws whatever the current device asks for and knows about
        // no kind in particular. What it shows about the device itself - name,
        // type, whether it was detected, whether it answers - comes from the
        // device too, so it is rebuilt rather than written by the page.
        DeviceSettingsWidget page;
        AspectContainer * const general = aspectNamed<AspectContainer>(page, "General");
        QVERIFY(general);

        auto selection = aspectNamed<StringSelectionAspect>(page, "Device");
        QVERIFY(selection);
        const IDevicePtr device = DeviceManager::find(Id::fromString(selection->volatileValue()));
        if (!device)
            QSKIP("No devices are configured here");

        // The name is the device's own aspect, not a copy of its text: typing
        // into the page is what makes the device dirty.
        QVERIFY(general->aspects().contains(device->displayNameAspect()));

        QStringList rows;
        for (BaseAspect * const aspect : general->aspects()) {
            if (auto row = qobject_cast<TextDisplay *>(aspect))
                rows << row->labelText() + ' ' + row->displayText();
        }
        QCOMPARE(rows.size(), 3);
        QVERIFY2(rows.at(0).endsWith(device->displayType()), qPrintable(rows.at(0)));
        QVERIFY2(rows.at(2).endsWith(device->deviceStateToString()), qPrintable(rows.at(2)));

        // What the device asks about itself, which every kind answers even
        // when the answer is "nothing to set, here is what I know".
        auto typeSpecific = aspectNamed<ContainerAspect>(page, "TypeSpecific");
        QVERIFY(typeSpecific);
        QVERIFY(typeSpecific->container());
        QVERIFY(!typeSpecific->container()->aspects().isEmpty());
    }

    void testRemovingADeviceSaysSoBeforeItHappens()
    {
        // Remove does not remove: it marks, so that Cancel can put the device
        // back. Both the button and the entry in the list have to say which of
        // the two states the device is in, or the page lies about what Apply
        // will do.
        //
        // On its own device rather than on whatever is configured here: the
        // only device on a plain checkout is the auto-detected desktop one,
        // which cannot be removed, and the test skipped every time.
        IDeviceFactory * const factory
            = Utils::findOr(IDeviceFactory::allDeviceFactories(), nullptr,
                            [](IDeviceFactory *f) {
                                return f->canCreate() && f->quickCreationAllowed();
                            });
        if (!factory)
            QSKIP("No device kind here can be created without a wizard");
        const IDevice::Ptr device = factory->construct();
        QVERIFY(device);
        device->setDisplayName("Device made by the settings page test");
        DeviceManager::addDevice(device);
        // Taken away again however this ends: a device left behind by a failed
        // assertion is written to the user's own settings on shutdown.
        const QScopeGuard cleanup([id = device->id()] { DeviceManager::removeDevice(id); });
        const int before = DeviceManager::deviceCount();

        DeviceSettingsWidget page;
        auto selection = aspectNamed<StringSelectionAspect>(page, "Device");
        QVERIFY(selection);
        selection->setValue(device->id().toString());

        auto remove = aspectNamed<ActionAspect>(page, "Remove");
        QVERIFY(remove);
        QVERIFY(remove->isEnabled());
        QCOMPARE(remove->presentation().actionText, QString("Remove"));

        remove->triggerAction();

        // Still there, and still the same count: nothing happens until Apply.
        QCOMPARE(DeviceManager::deviceCount(), before);
        QVERIFY(page.isDirty());
        // The selection moves off the marked device, so that removing several
        // in a row works without clicking elsewhere in between.
        if (before > 1)
            QVERIFY(selection->volatileValue() != device->id().toString());

        const auto entryFor = [selection](const IDevicePtr &device) {
            const AspectPresentation p = selection->presentation();
            const int row = Utils::indexOf(p.choices, [&device](const auto &choice) {
                return choice.id.toString() == device->id().toString();
            });
            return row < 0 ? QString() : p.choices.at(row).display;
        };
        const QString marked = entryFor(device);
        QVERIFY2(marked.contains(device->displayName()), qPrintable(marked));
        QVERIFY2(marked != device->displayName(), qPrintable(marked));

        // Going back to it offers to put it back rather than to remove it
        // again, and doing so leaves no trace in the list.
        selection->setValue(device->id().toString());
        QCOMPARE(remove->presentation().actionText, QString("Restore"));
        remove->triggerAction();
        QCOMPARE(remove->presentation().actionText, QString("Remove"));
        QCOMPARE(entryFor(device), device->displayName());
        QVERIFY(!page.isDirty());
    }
};

QObject *createDeviceSettingsPageTest()
{
    return new DeviceSettingsPageTest;
}
#endif // WITH_TESTS

} // ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "devicesettingspage.moc"
#endif
