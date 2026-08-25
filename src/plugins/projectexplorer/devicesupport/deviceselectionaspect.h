// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../projectexplorer_export.h"
#include "idevicefwd.h"

#include <utils/aspects.h>

namespace ProjectExplorer {

// Which device a settings page is looking at, with an entry for all of them.
// The counterpart of DeviceComboBox, which is the same list drawn as a widget:
// the tools pages narrow what they show to one device, and re-detect on it.
//
// The options follow the device list while the page is open, and the value is
// a device id rather than a position, so adding or removing a device does not
// change what is selected.
class PROJECTEXPLORER_EXPORT DeviceSelectionAspect : public Utils::SelectionAspect
{
    Q_OBJECT

public:
    explicit DeviceSelectionAspect(Utils::AspectContainer *container = nullptr);
    ~DeviceSelectionAspect() override;

    // Null where the entry for all devices is selected.
    IDeviceConstPtr currentDevice() const;

    // The one that is selected, or every device where that is the entry for
    // all of them - what a re-detect runs over.
    QList<IDeviceConstPtr> selectedDevices() const;

    // Only devices of this type are offered. Unset offers all of them.
    void setTypeFilter(Utils::Id type);

signals:
    void currentDeviceChanged();

private:
    void refreshOptions();

    Utils::Id m_typeFilter;
};

} // namespace ProjectExplorer
