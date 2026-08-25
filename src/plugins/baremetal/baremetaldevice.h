// Copyright (C) 2016 Tim Sander <tim@krieglstein.org>
// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevice.h>

namespace BareMetal::Internal {

class BareMetalDevice final : public ProjectExplorer::IDevice
{
public:
    using Ptr = std::shared_ptr<BareMetalDevice>;
    using ConstPtr = std::shared_ptr<const BareMetalDevice>;

    static Ptr create() { return Ptr(new BareMetalDevice); }
    ~BareMetalDevice() final;

    static QString defaultDisplayName();

    ProjectExplorer::IDeviceWidget *createWidget() final;

    QString debugServerProviderId() const;
    void setDebugServerProviderId(const QString &id);
    void unregisterDebugServerProvider(const QString &providerId) const;

private:
    friend class BareMetalDeviceWidget;
    friend class BareMetalDeviceTest;

    void fromMap(const Utils::Store &map) final;

    BareMetalDevice();
    // The provider, as the id it is stored by. A choice rather than a string:
    // which providers there are is what the manager says, and it changes while
    // a page is open.
    mutable Utils::StringSelectionAspect m_debugServerProviderId{this};
    Utils::ActionAspect m_manageProviders{this};
};

void setupBareMetalDevice();

#ifdef WITH_TESTS
QObject *createBareMetalDeviceTest();
#endif

} // BareMetal::Internal
