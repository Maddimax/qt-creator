// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <baremetal/idebugserverprovider.h>

#include <utils/commandline.h>

namespace BareMetal::Internal {

// GdbServerProvider

class GdbServerProvider : public IDebugServerProvider
{
public:
    enum StartupMode {
        StartupOnNetwork,
        StartupOnPipe
    };

    bool operator==(const IDebugServerProvider &other) const override;

    void addSettingsRows(Utils::AspectContainer &rows) override;

    void toMap(Utils::Store &data) const override;

    virtual Utils::CommandLine command() const;

    Utils::Result<> setupDebuggerRunParameters(Debugger::DebuggerRunParameters &rp,
        ProjectExplorer::RunControl *runControl) const final;
    std::optional<QtTaskTree::BarrierKickerGetter> serverRunner(
        ProjectExplorer::RunControl *runControl) const final;

    bool isValid() const override;
    virtual QSet<StartupMode> supportedStartupModes() const = 0;

protected:
    // Whether the GDB "target-async" mode is needed to interrupt the running
    // inferior. Off by default; enabled per provider where the GDB server
    // requires it (e.g. J-Link).
    virtual bool useTargetAsync() const { return false; }

    explicit GdbServerProvider(const QString &id);

    // Which modes this kind supports is its own answer, and a virtual cannot
    // be asked from the base's constructor - so every kind fills the list at
    // the top of its own. Reading the aspect before it has options asserts.
    void fillStartupModes();

    void fromMap(const Utils::Store &data) override;

    // The rows every GDB-compatible provider has. Which of them a kind shows
    // is its own business; see addSettingsRows().
    Utils::TypedSelectionAspect<StartupMode> startupMode{this};
    Utils::FilePathAspect peripheralDescriptionFile{this};
    Utils::StringAspect initCommands{this};
    Utils::StringAspect resetCommands{this};
    Utils::BoolAspect useExtendedRemote{this};
    Utils::FilePathAspect executableFile{this};
    Utils::StringAspect additionalArguments{this};
};

} // BareMetal::Internal
