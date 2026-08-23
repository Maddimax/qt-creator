// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

#include <projectexplorer/devicesupport/idevicefwd.h>

#include <utils/aspects.h>

#include <QVersionNumber>

namespace ProjectExplorer { class Kit; }
namespace Utils { class Environment; }

namespace QbsProjectManager::Internal {

class QbsSettings : public Utils::AspectContainer
{
    QbsSettings();

public:
    static QbsSettings &instance();

    static Utils::FilePath qbsExecutableFilePathForKit(const ProjectExplorer::Kit &kit);
    static Utils::FilePath qbsExecutableFilePathForDevice(const ProjectExplorer::IDeviceConstPtr &device);
    static Utils::FilePath defaultQbsExecutableFilePath();
    static Utils::FilePath qbsConfigFilePath(const ProjectExplorer::IDeviceConstPtr &device);
    static Utils::Environment qbsProcessEnvironment(const ProjectExplorer::IDeviceConstPtr &device);

    static bool useCreatorSettingsDirForQbs(const ProjectExplorer::IDeviceConstPtr &device);
    static Utils::FilePath qbsSettingsBaseDir(const ProjectExplorer::IDeviceConstPtr &device);
    static QVersionNumber qbsVersion(const ProjectExplorer::IDeviceConstPtr &device);

    Utils::FilePathAspect qbsExecutableFilePath{this};
    Utils::StringAspect defaultInstallDirTemplate{this};
    Utils::BoolAspect useCreatorSettings{this};
    Utils::ActionAspect resetExecutablePath{this};
    // Registered so that it is one of the container's aspects: a page's QML
    // reaches only those.
    Utils::TextDisplay versionLabel{this};
};

class QbsSettingsPage : public Core::IOptionsPage
{
public:
    QbsSettingsPage();
};

} // QbsProjectManager::Internal
