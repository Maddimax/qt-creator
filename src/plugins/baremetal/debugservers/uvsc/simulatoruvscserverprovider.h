// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "uvscserverprovider.h"

namespace BareMetal::Internal {

// SimulatorUvscServerProvider

class SimulatorUvscServerProvider final : public UvscServerProvider
{
public:
    void toMap(Utils::Store &data) const final;
    void fromMap(const Utils::Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;
    bool isSimulator() const final { return true; }

    Utils::FilePath optionsFilePath(ProjectExplorer::RunControl *runControl,
                                    QString &errorMessage) const final;

private:
    explicit SimulatorUvscServerProvider();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    Utils::BoolAspect limitSpeed{this};

    friend class SimulatorUvscServerProviderFactory;
    friend class SimulatorUvProjectOptions;
};

void setupSimulatorUvscServerProvider();

} // BareMetal::Internal
