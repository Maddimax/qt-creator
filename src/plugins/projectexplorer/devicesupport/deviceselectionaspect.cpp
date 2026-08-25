// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "deviceselectionaspect.h"

#include "devicemanager.h"
#include "idevice.h"
#include "../projectexplorertr.h"

using namespace Utils;

namespace ProjectExplorer {

DeviceSelectionAspect::DeviceSelectionAspect(AspectContainer *container)
    : SelectionAspect(container)
{
    setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    setLabelText(Tr::tr("Device:"));

    refreshOptions();
    const auto refresh = [this](Id) { refreshOptions(); };
    connect(DeviceManager::instance(), &DeviceManager::deviceAdded, this, refresh);
    connect(DeviceManager::instance(), &DeviceManager::deviceRemoved, this, refresh);
    connect(DeviceManager::instance(), &DeviceManager::deviceUpdated, this, refresh);

    connect(this, &BaseAspect::volatileValueChanged,
            this, &DeviceSelectionAspect::currentDeviceChanged);
}

DeviceSelectionAspect::~DeviceSelectionAspect() = default;

void DeviceSelectionAspect::setTypeFilter(Id type)
{
    if (m_typeFilter == type)
        return;
    m_typeFilter = type;
    refreshOptions();
}

IDeviceConstPtr DeviceSelectionAspect::currentDevice() const
{
    const QVariant id = itemValue();
    if (!id.isValid())
        return {};
    return DeviceManager::find(Id::fromSetting(id));
}

QList<IDeviceConstPtr> DeviceSelectionAspect::selectedDevices() const
{
    if (const IDeviceConstPtr dev = currentDevice())
        return {dev};
    QList<IDeviceConstPtr> devices;
    for (int i = 0; i < DeviceManager::deviceCount(); ++i)
        devices << DeviceManager::deviceAt(i);
    return devices;
}

// Mirrors DeviceManagerModel::data(): the entry for all devices first, then
// each device, with the default one for its type saying so.
void DeviceSelectionAspect::refreshOptions()
{
    const QVariant wanted = itemValue();

    clearOptions();
    addOption(Option(Tr::tr("All", "All devices"), {}, {}));
    for (int i = 0; i < DeviceManager::deviceCount(); ++i) {
        const IDevice::Ptr dev = DeviceManager::deviceAt(i);
        if (m_typeFilter.isValid() && dev->type() != m_typeFilter)
            continue;
        const QString name = DeviceManager::defaultDevice(dev->type()) == dev
                                 ? Tr::tr("%1 (default for %2)")
                                       .arg(dev->displayName(), dev->displayType())
                                 : dev->displayName();
        addOption(Option(name, {}, dev->id().toSetting()));
    }

    // By id, not by position: a device list that changed under the page must
    // not silently move the selection to another device.
    const int index = indexForItemValue(wanted);
    setValue(index < 0 ? 0 : index);
}

} // namespace ProjectExplorer
