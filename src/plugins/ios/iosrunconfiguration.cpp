// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "iosrunconfiguration.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include "iosconstants.h"
#include "iosdevice.h"
#include "iostr.h"
#include "simulatorcontrol.h"

#include <projectexplorer/abi.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

#include <utils/algorithm.h>
#include <utils/guardedcallback.h>
#include <utils/filepath.h>
#include <utils/qtcprocess.h>

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QLineEdit>
#include <QList>
#include <QStandardItem>
#include <QVariant>
#include <QWidget>

using namespace ProjectExplorer;
using namespace Utils;

namespace Ios::Internal {

const char deviceTypeKey[] = "Ios.device_type";

static QString displayName(const SimulatorInfo &device)
{
    return QString("%1 (iOS %2, %3)")
        .arg(
            device.name,
            device.runtime.version,
            Utils::transform(device.runtime.architectures, [](Abi::Architecture arch) {
                return Abi::toString(arch);
            }).join(", "));
}

// The entries a device-type chooser offers: one simulator each, shown by name
// and runtime and stored by the identifier the run configuration keeps. Free
// so that it can be checked against a list of simulators rather than whatever
// this machine happens to have installed.
QList<QStandardItem *> simulatorItems(const QList<SimulatorInfo> &simulators)
{
    QList<QStandardItem *> items;
    for (const SimulatorInfo &device : simulators) {
        auto item = new QStandardItem(displayName(device));
        item->setData(device.identifier);
        items.append(item);
    }
    return items;
}

static IosDeviceType toIosDeviceType(const SimulatorInfo &device)
{
    IosDeviceType iosDeviceType(IosDeviceType::SimulatedDevice,
                                device.identifier,
                                displayName(device));
    return iosDeviceType;
}

IosRunConfiguration::IosRunConfiguration(BuildConfiguration *bc, Id id)
    : RunConfiguration(bc, id), iosDeviceType(this, this)
{
    executable.setDeviceSelector(kit(), ExecutableAspect::RunDevice);

    setUpdater([this] {
        IDevice::ConstPtr dev = RunDeviceKitAspect::device(kit());
        const QString devName = dev ? dev->displayName() : IosDevice::name();
        setDefaultDisplayName(Tr::tr("Run on %1").arg(devName));
        setDisplayName(Tr::tr("Run %1 on %2").arg(applicationName()).arg(devName));

        executable.setExecutable(localExecutable());
        iosDeviceType.updateDeviceType();
    });
}

void IosDeviceTypeAspect::deviceChanges()
{
    updateVisibility();
    m_runConfiguration->update();
}

void IosDeviceTypeAspect::updateDeviceType()
{
    if (RunDeviceTypeKitAspect::deviceTypeId(m_runConfiguration->kit())
            == Constants::IOS_DEVICE_TYPE)
        m_deviceType = IosDeviceType(IosDeviceType::IosDevice);
    else if (m_deviceType.type == IosDeviceType::IosDevice)
        m_deviceType = IosDeviceType(IosDeviceType::SimulatedDevice);
}

bool IosRunConfiguration::isEnabled(Id runMode) const
{
    Utils::Id devType = RunDeviceTypeKitAspect::deviceTypeId(kit());
    if (devType != Constants::IOS_DEVICE_TYPE && devType != Constants::IOS_SIMULATOR_TYPE)
        return false;
    if (devType == Constants::IOS_SIMULATOR_TYPE)
        return true;

    IDevice::ConstPtr dev = RunDeviceKitAspect::device(kit());
    if (!dev || dev->deviceState() != IDevice::DeviceReadyToUse)
        return false;

    IosDevice::ConstPtr iosdevice = std::dynamic_pointer_cast<const IosDevice>(dev);
    if (iosdevice && iosdevice->handler() == IosDevice::Handler::DeviceCtl
        && runMode != ProjectExplorer::Constants::NORMAL_RUN_MODE
        && !IosDeviceManager::isDeviceCtlDebugSupported()) {
        return false;
    }

    return true;
}

QString IosRunConfiguration::applicationName() const
{
    return buildSystem()->extraData(buildKey(), Constants::IosTarget).toString();
}

FilePath IosRunConfiguration::bundleDirectory() const
{
    Utils::Id devType = RunDeviceTypeKitAspect::deviceTypeId(kit());
    bool isDevice = (devType == Constants::IOS_DEVICE_TYPE);
    if (!isDevice && devType != Constants::IOS_SIMULATOR_TYPE) {
        qCWarning(iosLog) << "unexpected device type in bundleDirForTarget: " << devType.toString();
        return {};
    }
    FilePath res;
    bool shouldAppendBuildTypeAndPlatform = true;
    QString pathStr = buildSystem()->extraData(buildKey(), Constants::IosBuildDir).toString();
    const QString cmakeGenerator
        = buildSystem()->extraData(buildKey(), Constants::IosCmakeGenerator).toString();

    if (cmakeGenerator.isEmpty()) {
        // qmake gives absolute IosBuildDir
        res = FilePath::fromString(pathStr);
    } else if (!pathStr.isEmpty()) {
        // CMake gives IosBuildDir relative to root build directory
        if (cmakeGenerator == "Xcode") {
            // When generating Xcode project, CMake may put a "${EFFECTIVE_PLATFORM_NAME}" macro,
            // which is expanded by Xcode at build time.
            // To get an actual executable path at configure time, replace this macro here
            // depending on the device type.
            pathStr.replace(
                "${EFFECTIVE_PLATFORM_NAME}",
                QLatin1String(isDevice ? "-iphoneos" : "-iphonesimulator"));
        }

        // With Ninja generator IosBuildDir may be just "." when executable is in the root directory,
        // so use canonical path to ensure that redundand dot is removed.
        res = buildConfiguration()->buildDirectory().pathAppended(pathStr).canonicalPath();
        // All done with path provided by CMake
        shouldAppendBuildTypeAndPlatform = false;
    }

    if (res.isEmpty()) {
        // Fallback
        res = buildConfiguration()->buildDirectory();
        shouldAppendBuildTypeAndPlatform = true;
    }

    if (shouldAppendBuildTypeAndPlatform) {
        switch (buildConfiguration()->buildType()) {
        case BuildConfiguration::Debug :
        case BuildConfiguration::Unknown :
            if (isDevice)
                res = res / "Debug-iphoneos";
            else
                res = res.pathAppended("Debug-iphonesimulator");
            break;
        case BuildConfiguration::Profile :
        case BuildConfiguration::Release :
            if (isDevice)
                res = res.pathAppended("Release-iphoneos");
            else
                res = res.pathAppended("Release-iphonesimulator");
            break;
        default:
            qCWarning(iosLog) << "IosBuildStep had an unknown buildType "
                              << buildConfiguration()->buildType();
        }
    }
    return res.pathAppended(applicationName() + ".app");
}

FilePath IosRunConfiguration::localExecutable() const
{
    return bundleDirectory().pathAppended(applicationName());
}

void IosDeviceTypeAspect::fromMap(const Store &map)
{
    bool deviceTypeIsInt;
    map.value(deviceTypeKey).toInt(&deviceTypeIsInt);
    if (deviceTypeIsInt || !m_deviceType.fromMap(storeFromVariant(map.value(deviceTypeKey))))
        updateDeviceType();

    updateVisibility();
    m_runConfiguration->update();
}

void IosDeviceTypeAspect::toMap(Store &map) const
{
    map.insert(deviceTypeKey, QVariant::fromValue(deviceType().toMap()));
}

QString IosRunConfiguration::disabledReason(Id runMode) const
{
    Utils::Id devType = RunDeviceTypeKitAspect::deviceTypeId(kit());
    if (devType != Constants::IOS_DEVICE_TYPE && devType != Constants::IOS_SIMULATOR_TYPE)
        return Tr::tr("Kit has incorrect device type for running on iOS devices.");
    IDevice::ConstPtr dev = RunDeviceKitAspect::device(kit());
    QString validDevName;
    bool hasConncetedDev = false;
    if (devType == Constants::IOS_DEVICE_TYPE) {
        for (int idev = 0; idev < DeviceManager::deviceCount(); ++idev) {
            IDevice::ConstPtr availDev = DeviceManager::deviceAt(idev);
            if (availDev && availDev->type() == Constants::IOS_DEVICE_TYPE) {
                if (availDev->deviceState() == IDevice::DeviceReadyToUse) {
                    validDevName += QLatin1Char(' ');
                    validDevName += availDev->displayName();
                } else if (availDev->deviceState() == IDevice::DeviceConnected) {
                    hasConncetedDev = true;
                }
            }
        }
    }

    if (!dev) {
        if (!validDevName.isEmpty())
            return Tr::tr("No device chosen. Select %1.").arg(validDevName); // should not happen
        else if (hasConncetedDev)
            return Tr::tr("No device chosen. Enable developer mode on a device."); // should not happen
        else
            return Tr::tr("No device available.");
    } else if (devType == Constants::IOS_DEVICE_TYPE) {
        switch (dev->deviceState()) {
        case IDevice::DeviceReadyToUse:
            break;
        case IDevice::DeviceConnected:
            return Tr::tr("To use this device you need to enable developer mode on it.");
        case IDevice::DeviceDisconnected:
        case IDevice::DeviceStateUnknown:
            if (!validDevName.isEmpty())
                return Tr::tr("%1 is not connected. Select %2?")
                        .arg(dev->displayName(), validDevName);
            else if (hasConncetedDev)
                return Tr::tr("%1 is not connected. Enable developer mode on a device?")
                        .arg(dev->displayName());
            else
                return Tr::tr("%1 is not connected.").arg(dev->displayName());
        }
        IosDevice::ConstPtr iosdevice = std::dynamic_pointer_cast<const IosDevice>(dev);
        if (iosdevice && iosdevice->handler() == IosDevice::Handler::DeviceCtl
            && runMode != ProjectExplorer::Constants::NORMAL_RUN_MODE
            && !IosDeviceManager::isDeviceCtlDebugSupported()) {
            return Tr::tr("Debugging on devices with iOS 17 and later requires Xcode 16 or later.");
        }
    }
    return RunConfiguration::disabledReason(runMode);
}

IosDeviceType IosRunConfiguration::deviceType() const
{
    return iosDeviceType.deviceType();
}

IosDeviceType IosDeviceTypeAspect::deviceType() const
{
    if (m_deviceType.type == IosDeviceType::SimulatedDevice) {
        QList<SimulatorInfo> availableSimulators = SimulatorControl::availableSimulators();
        if (availableSimulators.isEmpty())
            return m_deviceType;
        if (Utils::contains(availableSimulators,
                            Utils::equal(&SimulatorInfo::identifier, m_deviceType.identifier))) {
                 return m_deviceType;
        }
        // Simulator device has vanished, choose one
        return toIosDeviceType(availableSimulators.first());
    }
    return m_deviceType;
}

void IosDeviceTypeAspect::setDeviceType(const IosDeviceType &deviceType)
{
    m_deviceType = deviceType;
}

IosDeviceTypeAspect::IosDeviceTypeAspect(AspectContainer *container, IosRunConfiguration *rc)
    : AspectContainer(container), m_runConfiguration(rc)
{
    addDataExtractor(this, &IosDeviceTypeAspect::deviceType, &Data::deviceType);
    addDataExtractor(this, &IosDeviceTypeAspect::bundleDirectory, &Data::bundleDirectory);
    addDataExtractor(this, &IosDeviceTypeAspect::applicationName, &Data::applicationName);
    addDataExtractor(this, &IosDeviceTypeAspect::localExecutable, &Data::localExecutable);

    connect(DeviceManager::instance(), &DeviceManager::updated,
            this, &IosDeviceTypeAspect::deviceChanges);
    connect(KitManager::instance(), &KitManager::kitsChanged,
            this, &IosDeviceTypeAspect::deviceChanges);

    // One row: the list and the button are one setting.
    setInlineRow(true);
    setLabelText(Tr::tr("Device type:"));

    simulator.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        cb(simulatorItems(SimulatorControl::availableSimulators()));
    });
    simulator.addOnVolatileValueChanged(this, [this] {
        const QString identifier = simulator.volatileValue();
        if (identifier.isEmpty() || identifier == m_deviceType.identifier)
            return;
        const SimulatorInfo chosen = Utils::findOrDefault(
            SimulatorControl::availableSimulators(),
            Utils::equal(&SimulatorInfo::identifier, identifier));
        if (!chosen.identifier.isEmpty())
            setDeviceType(toIosDeviceType(chosen));
    });

    refresh.setActionText(Tr::tr("Update"));
    refresh.setAction([this] {
        refresh.setEnabled(false);
        SimulatorControl::updateAvailableSimulators(guardedCallback(this, [this] {
            simulator.refill();
            refresh.setEnabled(true);
        }));
    });

    // Whether there is anything to choose is the kit's answer, not the page's.
    updateVisibility();
}

void IosDeviceTypeAspect::updateVisibility()
{
    // A real device runs what it runs; there is nothing to pick.
    setVisible(deviceType().type != IosDeviceType::IosDevice);
    simulator.setValue(deviceType().identifier);
}


FilePath IosDeviceTypeAspect::bundleDirectory() const
{
    return m_runConfiguration->bundleDirectory();
}

QString IosDeviceTypeAspect::applicationName() const
{
    return m_runConfiguration->applicationName();
}

FilePath IosDeviceTypeAspect::localExecutable() const
{
    return m_runConfiguration->localExecutable();
}

// IosRunConfigurationFactory

class IosRunConfigurationFactory final : public RunConfigurationFactory
{
public:
    IosRunConfigurationFactory()
    {
        registerRunConfiguration<IosRunConfiguration>(Constants::IOS_RUNCONFIG_ID);
        addSupportedTargetDeviceType(Constants::IOS_DEVICE_TYPE);
        addSupportedTargetDeviceType(Constants::IOS_SIMULATOR_TYPE);
        setExecutionTypeId(Constants::IOS_EXECUTION_TYPE_ID);
    }
};

void setupIosRunConfiguration()
{
    static IosRunConfigurationFactory theIosRunConfigurationFactory;
}

#ifdef WITH_TESTS
class IosDeviceTypeTest : public QObject
{
    Q_OBJECT

    static SimulatorInfo simulator(const QString &name, const QString &id,
                                   const QString &version)
    {
        SimulatorInfo info;
        info.name = name;
        info.identifier = id;
        info.available = true;
        info.state = "Shutdown";
        info.runtime.version = version;
        info.runtime.architectures = {Abi::ArmArchitecture};
        return info;
    }

private slots:
    void testTheEntriesAreNamedForPeopleAndStoredForTheRunConfiguration()
    {
        const QList<SimulatorInfo> simulators{simulator("iPhone 16", "AAAA-1111", "18.0"),
                                              simulator("iPad Pro", "BBBB-2222", "17.4")};
        const QList<QStandardItem *> items = simulatorItems(simulators);
        QCOMPARE(items.size(), 2);

        // What is shown says which simulator and which iOS, because "iPhone 16"
        // on its own is several of them.
        QVERIFY2(items.at(0)->text().contains("iPhone 16"), qPrintable(items.at(0)->text()));
        QVERIFY2(items.at(0)->text().contains("18.0"), qPrintable(items.at(0)->text()));

        // What is stored is the identifier: the names are not unique and they
        // change between Xcode versions, and a run configuration keeps this in
        // its .user file.
        QCOMPARE(items.at(0)->data().toString(), QString("AAAA-1111"));
        QCOMPARE(items.at(1)->data().toString(), QString("BBBB-2222"));
        QVERIFY(items.at(1)->text() != items.at(0)->text());

        qDeleteAll(items);
    }

    void testNoSimulatorsIsNoEntries()
    {
        QVERIFY(simulatorItems({}).isEmpty());
    }
};

QObject *createIosDeviceTypeTest()
{
    return new IosDeviceTypeTest;
}
#endif // WITH_TESTS

} // Ios::Internal

#ifdef WITH_TESTS
#include "iosrunconfiguration.moc"
#endif
