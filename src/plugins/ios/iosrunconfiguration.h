// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "iossimulator.h"

#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runconfigurationaspects.h>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPushButton;
QT_END_NAMESPACE

namespace Ios::Internal {

class IosRunConfiguration;

// Which simulator to run on, and a button to go and look again. One row: the
// list and the button read as one setting, so they are a container rather than
// an aspect that draws two controls of its own.
class IosDeviceTypeAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit IosDeviceTypeAspect(Utils::AspectContainer *container,
                                 IosRunConfiguration *runConfiguration);

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    IosDeviceType deviceType() const;
    void setDeviceType(const IosDeviceType &deviceType);

    void deviceChanges();
    void updateDeviceType();

    // The simulators to choose between, and the button that asks the system
    // for them again.
    Utils::StringSelectionAspect simulator{this};
    Utils::ActionAspect refresh{this};

    class Data : public Utils::BaseAspect::Data
    {
    public:
        Utils::FilePath bundleDirectory;
        IosDeviceType deviceType;
        QString applicationName;
        Utils::FilePath localExecutable;
    };

private:
    Utils::FilePath bundleDirectory() const;
    QString applicationName() const;
    Utils::FilePath localExecutable() const;

    // Whether the row is shown at all, and what the list holds. Both follow
    // the kit rather than the page being opened.
    void updateVisibility();

    IosDeviceType m_deviceType;
    IosRunConfiguration *m_runConfiguration = nullptr;
};

class IosRunConfiguration : public ProjectExplorer::RunConfiguration
{
    Q_OBJECT // FIXME: Used in  IosDsymBuildStep

public:
    IosRunConfiguration(ProjectExplorer::BuildConfiguration *bc, Utils::Id id);

    QString applicationName() const;
    Utils::FilePath bundleDirectory() const;
    Utils::FilePath localExecutable() const;
    QString disabledReason(Utils::Id runMode) const override;
    IosDeviceType deviceType() const;

private:
    bool isEnabled(Utils::Id runMode) const final;

    ProjectExplorer::ExecutableAspect executable{this};
    ProjectExplorer::ArgumentsAspect arguments{this};
    IosDeviceTypeAspect iosDeviceType;
};

void setupIosRunConfiguration();

#ifdef WITH_TESTS
QObject *createIosDeviceTypeTest();
#endif

} // Ios::Internal
