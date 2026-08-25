// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "genericgdbserverprovider.h"

#include "gdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>



using namespace Utils;

namespace BareMetal::Internal {

// GenericGdbServerProvider

class GenericGdbServerProvider final : public GdbServerProvider
{
private:
    GenericGdbServerProvider();
    QSet<StartupMode> supportedStartupModes() const final;
    void addSettingsRows(Utils::AspectContainer &rows) final;
    friend class GenericGdbServerProviderFactory;
};

GenericGdbServerProvider::GenericGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_GENERIC_PROVIDER_ID)
{
    fillStartupModes();
    setChannel("localhost", 1234);
    setTypeDisplayName(Tr::tr("Generic"));

    executableFile.setCommandVersionArguments({"--version"});
    useExtendedRemote.setToolTip(Tr::tr("Use GDB target extended-remote"));
}

QSet<GdbServerProvider::StartupMode> GenericGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork};
}

void GenericGdbServerProvider::addSettingsRows(AspectContainer &rows)
{
    GdbServerProvider::addSettingsRows(rows);
    rows.registerAspect(&address);
    rows.registerAspect(&executableFile);
    rows.registerAspect(&additionalArguments);
    rows.registerAspect(&useExtendedRemote);
    rows.registerAspect(&initCommands);
    rows.registerAspect(&resetCommands);
}

// GenericGdbServerProviderFactory

class GenericGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    GenericGdbServerProviderFactory()
    {
        setId(Constants::GDBSERVER_GENERIC_PROVIDER_ID);
        setDisplayName(Tr::tr("Generic"));
        setCreator([] { return new GenericGdbServerProvider; });
    }
};

void setupGenericGdbServerProvider()
{
    static GenericGdbServerProviderFactory theGenericGdbServerProviderFactory;
}

} // ProjectExplorer::Internal
