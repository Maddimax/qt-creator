// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../projectexplorer_export.h"

#include <projectexplorer/devicesupport/idevicefwd.h>

#include <QDialog>

#include <memory>

namespace Utils { class ProcessInfo; }

namespace ProjectExplorer {

class KitChooserAspect;

namespace Internal {
class DeviceProcessesDialogPrivate;
#ifdef WITH_TESTS
class DeviceProcessesDialogTest;
QObject *createDeviceProcessesDialogTest();
#endif
} // namespace Internal

class PROJECTEXPLORER_EXPORT DeviceProcessesDialog : public QDialog
{
public:
    DeviceProcessesDialog();
    ~DeviceProcessesDialog() override;

    void addAcceptButton(const QString &label);
    void addCloseButton();

    void setDevice(const IDeviceConstPtr &device);
    void showAllDevices();
    Utils::ProcessInfo currentProcess() const;
    KitChooserAspect &kitChooser() const;
    void logMessage(const QString &line);

private:
    void setKitVisible(bool);
    void setDeviceToList(const IDeviceConstPtr &device);
    void updateDevice();
    void updateProcessList();
    void killProcess();
    void handleRemoteError(const QString &errorMsg);
    void handleProcessListUpdated();
    void updateButtons();

    const std::unique_ptr<Internal::DeviceProcessesDialogPrivate> d;

#ifdef WITH_TESTS
    friend class Internal::DeviceProcessesDialogTest;
#endif
};

} // namespace ProjectExplorer
