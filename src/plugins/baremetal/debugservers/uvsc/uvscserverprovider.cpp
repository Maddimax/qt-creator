// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uvscserverprovider.h"

#include <coreplugin/icore.h>

#include "uvproject.h"
#include "uvprojectwriter.h"
#include "uvtargetdeviceviewer.h"
#include "uvtargetdriverviewer.h"

#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <debugger/debuggerengine.h>
#include <debugger/debuggerkitaspect.h>

#include <projectexplorer/project.h>
#include <projectexplorer/runcontrol.h>

#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/result.h>

#include <QFileInfo>
#include <QFormLayout>
#include <QRegularExpressionValidator>

#include <fstream> // for std::ofstream

using namespace Debugger;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace BareMetal::Internal {

using namespace Uv;

// Whole software package selection keys.
constexpr char toolsIniKeyC[] = "ToolsIni";
constexpr char deviceSelectionKeyC[] = "DeviceSelection";
constexpr char driverSelectionKeyC[] = "DriverSelection";

constexpr int defaultPortNumber = 5101;

// UvscServerProvider

QString UvscServerProvider::buildDllRegistryKey(const DriverSelection &driver)
{
    const QFileInfo fi(driver.dll);
    return fi.baseName();
}

QString UvscServerProvider::adjustFlashAlgorithmProperty(const QString &property)
{
    return property.startsWith("0x") ? property.mid(2) : property;
}

// UvSelectionAspect

AspectPresentation UvSelectionAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::TextWithAction;
    p.actionText = m_actionText;
    return p;
}

void UvSelectionAspect::triggerAction()
{
    if (m_onTrigger)
        m_onTrigger();
}

void UvSelectionAspect::setSummary(const QString &summary)
{
    if (m_summary == summary)
        return;
    m_summary = summary;
    emit displayTextChanged();
}

// UvTableAspect

AspectPresentation UvTableAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Table;
    return p;
}

// UvscServerProvider

UvscServerProvider::UvscServerProvider(const QString &id)
    : IDebugServerProvider(id)
{
    setEngineType(UvscEngineType);
    setChannel("localhost", defaultPortNumber);
    setToolsetNumber(ArmAdsToolsetNumber);

    toolsIniFile.setSettingsKey(toolsIniKeyC);
    toolsIniFile.setLabelText(Tr::tr("Tools file path:"));
    toolsIniFile.setExpectedKind(PathChooserKind::File);
    toolsIniFile.setPromptDialogFilter("tools.ini");

    // A device can only be picked once uVision has been found.
    const auto updateToolsAvailable = [this] {
        const bool available = FilePath::fromUserInput(toolsIniFile.volatileValue()).exists();
        deviceGroup.setEnabled(available);
        driverGroup.setEnabled(available);
    };

    deviceGroup.setLabelText(Tr::tr("Target device"));
    deviceSelector.setLabelText(Tr::tr("Device:"));
    deviceSelector.setActionText(Tr::tr("Select..."));
    deviceSelector.setOnTrigger([this] {
        Uv::DeviceSelectionDialog dialog(FilePath::fromUserInput(toolsIniFile.volatileValue()),
                                         Core::ICore::dialogParent());
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_deviceSelection = dialog.selection();
        refreshDeviceDetails();
    });
    deviceVendor.setLabelText(Tr::tr("Vendor:"));
    devicePackage.setLabelText(Tr::tr("Package:"));
    deviceDesc.setLabelText(Tr::tr("Description:"));
    deviceMemory.setLabelText(Tr::tr("Memory:"));
    deviceMemory.setModel(new Uv::DeviceSelectionMemoryModel(m_deviceSelection, this));
    deviceAlgorithm.setLabelText(Tr::tr("Flash algorithm:"));
    deviceAlgorithm.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    connect(&deviceAlgorithm, &BaseAspect::volatileValueChanged, this, [this] {
        m_deviceSelection.algorithmIndex = deviceAlgorithm.volatileValue();
    });
    devicePeripheralDescriptionFile.setLabelText(Tr::tr("Peripheral description file:"));
    devicePeripheralDescriptionFile.setExpectedKind(PathChooserKind::File);
    devicePeripheralDescriptionFile.setPromptDialogFilter(
        Tr::tr("Peripheral description files (*.svd)"));
    devicePeripheralDescriptionFile.setPromptDialogTitle(
        Tr::tr("Select Peripheral Description File"));
    connect(&devicePeripheralDescriptionFile, &BaseAspect::volatileValueChanged, this, [this] {
        m_deviceSelection.svd = devicePeripheralDescriptionFile.volatileValue();
    });

    driverGroup.setLabelText(Tr::tr("Target driver"));
    driverSelector.setLabelText(Tr::tr("Driver:"));
    driverSelector.setActionText(Tr::tr("Select..."));
    driverSelector.setOnTrigger([this] {
        Uv::DriverSelectionDialog dialog(FilePath::fromUserInput(toolsIniFile.volatileValue()),
                                         m_supportedDrivers, Core::ICore::dialogParent());
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_driverSelection = dialog.selection();
        refreshDriverDetails();
    });
    driverDll.setLabelText(Tr::tr("Driver library:"));
    driverCpuDll.setLabelText(Tr::tr("CPU library:"));
    driverCpuDll.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    connect(&driverCpuDll, &BaseAspect::volatileValueChanged, this, [this] {
        m_driverSelection.cpuDllIndex = driverCpuDll.volatileValue();
    });

    updateToolsAvailable();
    connect(&toolsIniFile, &BaseAspect::volatileValueChanged, this, updateToolsAvailable);
}

void UvscServerProvider::addSettingsRows(AspectContainer &rows)
{
    IDebugServerProvider::addSettingsRows(rows);
    refreshDeviceDetails();
    refreshDriverDetails();
    rows.registerAspect(&address);
    rows.registerAspect(&toolsIniFile);
    rows.registerAspect(&deviceGroup);
    rows.registerAspect(&driverGroup);
}

static QString trimVendor(const QString &vendor)
{
    const int colonIndex = vendor.lastIndexOf(':');
    return vendor.mid(0, colonIndex);
}

void UvscServerProvider::refreshDeviceDetails()
{
    deviceSelector.setSummary(m_deviceSelection.name.isEmpty()
                                  ? Tr::tr("Target device not selected.")
                                  : m_deviceSelection.name);
    deviceVendor.setText(trimVendor(m_deviceSelection.vendorName));
    devicePackage.setText(m_deviceSelection.package.name);
    deviceDesc.setText(m_deviceSelection.desc);
    if (auto model = static_cast<Uv::DeviceSelectionMemoryModel *>(deviceMemory.tableModel()))
        model->refresh();
    deviceAlgorithm.clearOptions();
    for (const Uv::DeviceSelection::Algorithm &algorithm : m_deviceSelection.algorithms)
        deviceAlgorithm.addOption(algorithm.path);
    // A device that offers no algorithm has no index to select either.
    if (deviceAlgorithm.optionCount() > 0)
        deviceAlgorithm.setValue(qBound(0, m_deviceSelection.algorithmIndex,
                                        deviceAlgorithm.optionCount() - 1));
    devicePeripheralDescriptionFile.setValue(m_deviceSelection.svd);
}

void UvscServerProvider::refreshDriverDetails()
{
    driverSelector.setSummary(m_driverSelection.name.isEmpty()
                                  ? Tr::tr("Target driver not selected.")
                                  : m_driverSelection.name);
    driverDll.setText(m_driverSelection.dll);
    driverCpuDll.clearOptions();
    for (const QString &dll : m_driverSelection.cpuDlls)
        driverCpuDll.addOption(dll);
    if (driverCpuDll.optionCount() > 0)
        driverCpuDll.setValue(qBound(0, m_driverSelection.cpuDllIndex,
                                     driverCpuDll.optionCount() - 1));
}

void UvscServerProvider::setDeviceSelection(const DeviceSelection &deviceSelection)
{
    m_deviceSelection = deviceSelection;
}

DeviceSelection UvscServerProvider::deviceSelection() const
{
    return m_deviceSelection;
}

void UvscServerProvider::setDriverSelection(const DriverSelection &driverSelection)
{
    m_driverSelection = driverSelection;
}

DriverSelection UvscServerProvider::driverSelection() const
{
    return m_driverSelection;
}

void UvscServerProvider::setToolsetNumber(ToolsetNumber toolsetNumber)
{
    m_toolsetNumber = toolsetNumber;
}

UvscServerProvider::ToolsetNumber UvscServerProvider::toolsetNumber() const
{
    return m_toolsetNumber;
}

void UvscServerProvider::setSupportedDrivers(const QStringList &supportedDrivers)
{
    m_supportedDrivers = supportedDrivers;
}

QStringList UvscServerProvider::supportedDrivers() const
{
    return m_supportedDrivers;
}

bool UvscServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!IDebugServerProvider::operator==(other))
        return false;
    const auto p = static_cast<const UvscServerProvider *>(&other);
    return toolsIniFile() == p->toolsIniFile()
            && m_deviceSelection == p->m_deviceSelection
            && m_driverSelection == p->m_driverSelection
            && m_toolsetNumber == p->m_toolsetNumber;
}

FilePath UvscServerProvider::buildProjectFilePath(RunControl *runControl) const
{
    const QString projectName = runControl->project()->displayName() + ".uvprojx";
    const FilePath path = runControl->buildDirectory().pathAppended(projectName);
    return path;
}

FilePath UvscServerProvider::buildOptionsFilePath(RunControl *runControl) const
{
    const QString projectName = runControl->project()->displayName() + ".uvoptx";
    const FilePath path = runControl->buildDirectory().pathAppended(projectName);
    return path;
}

void UvscServerProvider::toMap(Store &data) const
{
    IDebugServerProvider::toMap(data);
    data.insert(deviceSelectionKeyC, variantFromStore(m_deviceSelection.toMap()));
    data.insert(driverSelectionKeyC, variantFromStore(m_driverSelection.toMap()));
}

bool UvscServerProvider::isValid() const
{
    return channel().isValid();
}

Result<> UvscServerProvider::setupDebuggerRunParameters(DebuggerRunParameters &rp,
                                                        RunControl *runControl) const
{
    const FilePath bin = rp.inferior().command.executable();
    if (bin.isEmpty()) {
        return ResultError(Tr::tr("Cannot debug: Local executable is not set."));
    } else if (!bin.exists()) {
        return ResultError(Tr::tr("Cannot debug: Could not find executable for \"%1\".")
                                 .arg(bin.toUserOutput()));
    }

    QString errorMessage;
    const FilePath projFilePath = projectFilePath(runControl, errorMessage);
    if (!projFilePath.exists())
        return ResultError(errorMessage);

    const FilePath optFilePath = optionsFilePath(runControl, errorMessage);
    if (!optFilePath.exists())
        return ResultError(errorMessage);

    const FilePath peripheralDescriptionFile = FilePath::fromString(m_deviceSelection.svd);

    ProcessRunData inferior;
    inferior.command.setExecutable(bin);
    rp.setPeripheralDescriptionFile(peripheralDescriptionFile);
    rp.setUVisionProjectFilePath(projFilePath);
    rp.setUVisionOptionsFilePath(optFilePath);
    rp.setUVisionSimulator(isSimulator());
    rp.setInferior(inferior);
    rp.setSymbolFile(bin);
    rp.setStartMode(AttachToRemoteServer);
    rp.setRemoteChannel(channelPipe());
    rp.setUseContinueInsteadOfRun(true);
    return ResultOk;
}

std::optional<BarrierKickerGetter> UvscServerProvider::serverRunner(RunControl *runControl) const
{
    return [this, runControl](const QStoredBarrier &ready) {
        return runControl->processTaskWithModifier([this, runControl, ready](Process &process) {
            process.setCommand({DebuggerKitAspect::runnable(runControl->kit()).command.executable(),
                                {"-j0", QStringLiteral("-s%1").arg(channel().port())}});
            connectReadyBarrier(runControl, process, ready.activeStorage());
        });
    };
}

void UvscServerProvider::fromMap(const Store &data)
{
    IDebugServerProvider::fromMap(data);
    m_deviceSelection.fromMap(storeFromVariant(data.value(deviceSelectionKeyC)));
    m_driverSelection.fromMap(storeFromVariant(data.value(driverSelectionKeyC)));
}

FilePath UvscServerProvider::projectFilePath(RunControl *runControl, QString &errorMessage) const
{
    const FilePath projectPath = buildProjectFilePath(runControl);
    std::ofstream ofs(projectPath.path().toStdString(), std::ofstream::out);
    Uv::ProjectWriter writer(&ofs);
    const Uv::Project project(this, runControl->project());
    if (!writer.write(&project)) {
        errorMessage = Tr::tr("Unable to create a uVision project template.");
        return {};
    }
    return projectPath;
}

// HexValueValidator

class HexValueValidator final : public QRegularExpressionValidator
{
public:
    explicit HexValueValidator(QObject *parent = nullptr)
        : QRegularExpressionValidator(parent)
    {
        static const QRegularExpression re("^0x[0-9a-fA-F]{1,8}");
        setRegularExpression(re);
    }
};

} // BareMetal::Internal
