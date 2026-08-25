// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "uvtargetdeviceselection.h"
#include "uvtargetdriverselection.h"

#include <baremetal/idebugserverprovider.h>

#include <utils/qtcprocess.h>

namespace Utils { class PathChooser; }

namespace BareMetal::Internal {

namespace Uv {
class DeviceSelection;
class DeviceSelector;
class DriverSelection;
class DriverSelector;
}

// What has been picked, and the button that opens the dialog which picks it.
// The dialog is the same one the old expander's tool panel opened.
class UvSelectionAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    Utils::AspectPresentation presentation() const override;
    QString displayText() const override { return m_summary; }
    void triggerAction() override;

    void setSummary(const QString &summary);
    void setActionText(const QString &text) { m_actionText = text; }
    void setOnTrigger(const std::function<void()> &onTrigger) { m_onTrigger = onTrigger; }

private:
    QString m_summary;
    QString m_actionText;
    std::function<void()> m_onTrigger;
};

// Rows that belong to somebody else's model - what a device says about its own
// memory. The aspect only says how to draw them.
class UvTableAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override { return m_model; }
    void setModel(QAbstractItemModel *model) { m_model = model; }

private:
    QAbstractItemModel *m_model = nullptr;
};

// UvscServerProvider

class UvscServerProvider : public IDebugServerProvider
{
public:
    enum ToolsetNumber {
        UnknownToolsetNumber = -1,
        ArmAdsToolsetNumber = 4 // ARM-ADS toolset
    };

    void setDeviceSelection(const Uv::DeviceSelection &deviceSelection);
    Uv::DeviceSelection deviceSelection() const;

    void setDriverSelection(const Uv::DriverSelection &driverSelection);
    Uv::DriverSelection driverSelection() const;

    ToolsetNumber toolsetNumber() const;
    QStringList supportedDrivers() const;

    bool operator==(const IDebugServerProvider &other) const override;

    void toMap(Utils::Store &map) const override;

    Utils::Result<> setupDebuggerRunParameters(Debugger::DebuggerRunParameters &rp,
        ProjectExplorer::RunControl *runControl) const final;
    std::optional<QtTaskTree::BarrierKickerGetter> serverRunner(
        ProjectExplorer::RunControl *runControl) const final;

    bool isValid() const override;

    static QString buildDllRegistryKey(const Uv::DriverSelection &driver);
    static QString adjustFlashAlgorithmProperty(const QString &property);

protected:
    explicit UvscServerProvider(const QString &id);

    void setToolsetNumber(ToolsetNumber toolsetNumber);
    void setSupportedDrivers(const QStringList &supportedDrivers);

    Utils::FilePath buildProjectFilePath(ProjectExplorer::RunControl *runControl) const;
    Utils::FilePath buildOptionsFilePath(ProjectExplorer::RunControl *runControl) const;

    void fromMap(const Utils::Store &data) override;

    // uVision specific stuff.
    Utils::FilePath projectFilePath(ProjectExplorer::RunControl *runControl, QString &errorMessage) const;
    virtual Utils::FilePath optionsFilePath(ProjectExplorer::RunControl *runControl,
                                            QString &errorMessage) const = 0;

    void addSettingsRows(Utils::AspectContainer &rows) override;

    // What the details under the picker say about the device that was picked.
    void refreshDeviceDetails();
    void refreshDriverDetails();

    Utils::FilePathAspect toolsIniFile{this};

    // The target device: which one, and what it is made of. The picker is a
    // dialog; everything under it is what the chosen device reports.
    Utils::AspectContainer deviceGroup{this};
    UvSelectionAspect deviceSelector{&deviceGroup};
    Utils::TextDisplay deviceVendor{&deviceGroup};
    Utils::TextDisplay devicePackage{&deviceGroup};
    Utils::TextDisplay deviceDesc{&deviceGroup};
    UvTableAspect deviceMemory{&deviceGroup};
    Utils::SelectionAspect deviceAlgorithm{&deviceGroup};
    Utils::FilePathAspect devicePeripheralDescriptionFile{&deviceGroup};

    // The driver that talks to it, and which CPU DLL that driver uses.
    Utils::AspectContainer driverGroup{this};
    UvSelectionAspect driverSelector{&driverGroup};
    Utils::TextDisplay driverDll{&driverGroup};
    Utils::SelectionAspect driverCpuDll{&driverGroup};

    Uv::DeviceSelection m_deviceSelection;
    Uv::DriverSelection m_driverSelection;

    // Note: Don't store it to the map!
    ToolsetNumber m_toolsetNumber = UnknownToolsetNumber;
    QStringList m_supportedDrivers;
};

} // namespace BareMetal::Internal
