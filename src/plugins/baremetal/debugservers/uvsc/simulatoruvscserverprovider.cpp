// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "simulatoruvscserverprovider.h"

#include "uvproject.h"
#include "uvprojectwriter.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <QCheckBox>
#include <QFormLayout>

#include <fstream> // for std::ofstream

using namespace Debugger;
using namespace ProjectExplorer;
using namespace Utils;

namespace BareMetal::Internal {

using namespace Uv;

const char limitSpeedKeyC[] = "LimitSpeed";

static DriverSelection defaultSimulatorDriverSelection()
{
    DriverSelection selection;
    // We don't use any driver DLL for a simulator,
    // we just use only one CPU DLL (yes?).
    selection.name = "None";
    selection.dll = "None";
    selection.index = 0;
    selection.cpuDlls = QStringList{"SARMCM3.DLL"};
    selection.cpuDllIndex = 0;
    return selection;
}

// SimulatorUvProjectOptionsWriter

class SimulatorUvProjectOptions final : public Uv::ProjectOptions
{
public:
    explicit SimulatorUvProjectOptions(const SimulatorUvscServerProvider *provider)
        : Uv::ProjectOptions(provider)
    {
        m_debugOpt->appendProperty("sLrtime", int(provider->limitSpeed()));
    }
};

// SimulatorUvscServerProvider

SimulatorUvscServerProvider::SimulatorUvscServerProvider()
    : UvscServerProvider(Constants::UVSC_SIMULATOR_PROVIDER_ID)
{
    setTypeDisplayName(Tr::tr("uVision Simulator"));

    limitSpeed.setSettingsKey(limitSpeedKeyC);
    limitSpeed.setLabelText(Tr::tr("Limit speed to real-time:"));
    limitSpeed.setToolTip(Tr::tr("Limit speed to real-time."));
    limitSpeed.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
}

void SimulatorUvscServerProvider::addSettingsRows(AspectContainer &rows)
{
    UvscServerProvider::addSettingsRows(rows);
    rows.registerAspect(&limitSpeed);
    setDriverSelection(defaultSimulatorDriverSelection());
}

void SimulatorUvscServerProvider::toMap(Store &data) const
{
    UvscServerProvider::toMap(data);
}

void SimulatorUvscServerProvider::fromMap(const Store &data)
{
    UvscServerProvider::fromMap(data);
}

bool SimulatorUvscServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!UvscServerProvider::operator==(other))
        return false;
    const auto p = static_cast<const SimulatorUvscServerProvider *>(&other);
    return limitSpeed() == p->limitSpeed();
}

FilePath SimulatorUvscServerProvider::optionsFilePath(RunControl *runControl,
                                                      QString &errorMessage) const
{
    const FilePath optionsPath = buildOptionsFilePath(runControl);
    std::ofstream ofs(optionsPath.path().toStdString(), std::ofstream::out);
    Uv::ProjectOptionsWriter writer(&ofs);
    const SimulatorUvProjectOptions projectOptions(this);
    if (!writer.write(&projectOptions)) {
        errorMessage = Tr::tr("Unable to create a uVision project options template.");
        return {};
    }
    return optionsPath;
}

// SimulatorUvscServerProviderFactory

class SimulatorUvscServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    SimulatorUvscServerProviderFactory()
    {
        setId(Constants::UVSC_SIMULATOR_PROVIDER_ID);
        setDisplayName(Tr::tr("uVision Simulator"));
        setCreator([] { return new SimulatorUvscServerProvider; });
    }
};

void setupSimulatorUvscServerProvider()
{
    static SimulatorUvscServerProviderFactory theSimulatorUvscServerProviderFactory;
}

} // BareMetal::Internal
