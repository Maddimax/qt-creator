// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>
#include <utils/summaryaspect.h>

#include <memory>

namespace ProjectExplorer::Internal {

class WindowsAppSdkSettingsPrivate;

// Which of the three checks the summary reports on.
enum WindowsAppSdkValidation {
    DownloadPathExistsRow,
    NugetPathExistsRow,
    WindowsAppSdkPathExists
};

class WindowsAppSdkSettings : public Utils::AspectContainer
{
    WindowsAppSdkSettings();
    friend WindowsAppSdkSettings &windowsAppSdkSettings();

public:
    ~WindowsAppSdkSettings() override;

    Utils::FilePathAspect downloadLocation{this};
    Utils::FilePathAspect nugetLocation{this};
    Utils::FilePathAspect windowsAppSdkLocation{this};
    Utils::ActionAspect downloadNuget{this};
    Utils::ActionAspect downloadWindowsAppSdk{this};
    // Constructed with the container: its rows are what has to be true, and
    // its label is whether they are.
    Utils::SummaryAspect summary;

    // Re-runs the three checks the summary reports. The paths write through
    // as they are edited, so this is what keeps the summary in step.
    void validate();

private:
    std::unique_ptr<WindowsAppSdkSettingsPrivate> d;
};

// A directory holds the Windows App SDK when a package is in it. The name of
// the directory says nothing: what the SDK is is the .nupkg.
bool hasWindowsAppSdkPackage(const Utils::FilePath &directory);

// Where NuGet unpacked the SDK under \a downloadPath - the first
// "Microsoft.WindowsAppSDK.*" entry - or an empty path where it did not.
Utils::FilePath windowsAppSdkPackageDir(const Utils::FilePath &downloadPath);

WindowsAppSdkSettings &windowsAppSdkSettings();

void setupWindowsAppSdkSettings();

} // namespace ProjectExplorer::Internal
