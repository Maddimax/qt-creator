// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "buildaspects.h"

#include "buildconfiguration.h"
#include "buildpropertiessettings.h"
#include "devicesupport/devicekitaspects.h"
#include "devicesupport/idevice.h"
#include "projectexplorerconstants.h"
#include "projectexplorer.h"
#include "projectexplorersettings.h"
#include "projectexplorertr.h"
#include "target.h"

#include <coreplugin/fileutils.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/pathchooser.h>

#include <QDir>

using namespace Utils;

namespace ProjectExplorer {

class BuildDirectoryAspect::Private
{
public:
    explicit Private(BuildConfiguration *bc)
        : genericProblem(bc)
        , specialProblemDisplay(bc)
    {}

    FilePath sourceDir;
    FilePath savedShadowBuildDir;
    QString specialProblem;
    // Two rows under the setting rather than two labels this aspect owns. The
    // generic one is about the character that is wrong and where to stop being
    // told about it; the special one is whatever the build system said.
    TextDisplay genericProblem;
    TextDisplay specialProblemDisplay;
};

BuildDirectoryAspect::BuildDirectoryAspect(BuildConfiguration *bc)
    : FilePathAspect(bc),
      d(new Private(bc))
{
    setSettingsKey("ProjectExplorer.BuildConfiguration.BuildDirectory");
    setLabelText(Tr::tr("Build directory:"));
    setExpectedKind(Utils::PathChooserKind::Directory);

    for (TextDisplay *problem : {&d->genericProblem, &d->specialProblemDisplay}) {
        problem->setIconType(InfoType::Warning);
        problem->setWordWrap(true);
        // No label of its own, so it takes the label's column as well: a
        // warning reads across the form rather than in the narrow half.
        problem->setSpan(2);
        problem->setVisible(false);
    }
    // The generic warning says where to turn itself off.
    d->genericProblem.setTextFormat(AspectControls::TextFormat::RichText);
    connect(&d->genericProblem, &TextDisplay::linkActivated, this, [] {
        Core::ICore::showSettings(Constants::BUILD_AND_RUN_SETTINGS_PAGE_ID);
    });

    // Behaviour, not layout: where the build may be put follows the build
    // device, which is the kit's and can change while the page is open.
    const auto followBuildDevice = [this] {
        const auto buildDevice = BuildDeviceKitAspect::device(buildConfiguration()->kit());
        setAllowPathFromDevice(buildDevice
                               && buildDevice->type()
                                      != ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE);
    };
    connect(bc, &BuildConfiguration::kitChanged, this, followBuildDevice);
    followBuildDevice();

    setValidationFunction([this](QString text) -> FancyLineEdit::AsyncValidationFuture {
        const FilePath fixedDir = fixupDir(FilePath::fromUserInput(text));
        if (!fixedDir.isEmpty())
            text = fixedDir.toUserOutput();

        const FilePath newPath = FilePath::fromUserInput(text);
        const auto buildDevice = BuildDeviceKitAspect::device(buildConfiguration()->kit());

        const FilePath expandedPath = absoluteBuildDir(newPath);
        const QString problem = updateProblemLabelsHelper(expandedPath.toFSPathString());
        if (!problem.isEmpty())
            return QtFuture::makeReadyFuture(Result<QString>(ResultError(problem)));

        if (buildDevice && buildDevice->type() != ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE
            && !buildDevice->rootPath().ensureReachable(expandedPath)) {
            return QtFuture::makeReadyFuture((Utils::Result<QString>(ResultError(
                Tr::tr("The build directory is not reachable from the build device.")))));
        }

        return defaultValidationFunction()(text);
    });

    setOpenTerminalHandler(
        [bc] { Core::FileUtils::openTerminal(bc->buildDirectory(), bc->environment()); });

    ProjectExplorerSettings::registerCallback(
        this, &ProjectExplorerSettings::warnAgainstNonAsciiBuildDir, [this] { validateInput(); });
}

BuildDirectoryAspect::~BuildDirectoryAspect()
{
    delete d;
}

void BuildDirectoryAspect::allowInSourceBuilds(const FilePath &sourceDir)
{
    d->sourceDir = sourceDir;
    makeCheckable(CheckBoxPlacement::Top, Tr::tr("Shadow build:"), Key());
    setChecked(d->sourceDir != expandedValue());

    // Turning the shadow build off means building in the source directory, and
    // turning it back on means going back to where it was. This used to be
    // wired up when the page was drawn, so the check box did nothing until
    // then.
    connect(this, &StringAspect::checkedChanged, this, [this] {
        if (isChecked()) {
            setValue(d->savedShadowBuildDir.isEmpty() ? d->sourceDir : d->savedShadowBuildDir);
        } else {
            d->savedShadowBuildDir = expandedValue(); // FIXME: Check.
            setValue(d->sourceDir);
        }
    });
}

bool BuildDirectoryAspect::isShadowBuild() const
{
    return !d->sourceDir.isEmpty() && d->sourceDir != expandedValue();
}

void BuildDirectoryAspect::setProblem(const QString &description)
{
    d->specialProblem = description;
    validateInput();
}

void BuildDirectoryAspect::toMap(Store &map) const
{
    FilePathAspect::toMap(map);
    if (!d->sourceDir.isEmpty()) {
        const FilePath shadowDir = isChecked() ? expandedValue() : d->savedShadowBuildDir;
        saveToMap(map, shadowDir.toSettings(), QString(), settingsKey() + ".shadowDir");
    }
}

void BuildDirectoryAspect::fromMap(const Store &map)
{
    FilePathAspect::fromMap(map);
    if (!d->sourceDir.isEmpty()) {
        d->savedShadowBuildDir = FilePath::fromSettings(map.value(settingsKey() + ".shadowDir"));
        if (d->savedShadowBuildDir.isEmpty())
            setValue(d->sourceDir);
        setChecked(d->sourceDir != expandedValue()); // FIXME: Check.
    }
}

FilePath BuildDirectoryAspect::absoluteBuildDir(const FilePath &rawPath) const
{
    const BuildConfiguration * const bc = buildConfiguration();
    return BuildConfiguration::expandedBuildDirectory(
        bc->kit(), rawPath, bc->project()->projectDirectory(), *bc->macroExpander());
}

void BuildDirectoryAspect::announceChanges(Changes changes, Announcement howToAnnounce)
{
    if (changes.volatileValueFromValue && isCheckable())
        setChecked(d->sourceDir != expandedValue());
    FilePathAspect::announceChanges(changes, howToAnnounce);
}

FilePath BuildDirectoryAspect::fixupDir(const FilePath &dir)
{
    if (!dir.isLocal())
        return {};
    if (!HostOsInfo::isWindowsHost() || !dir.startsWithDriveLetter())
        return {};
    const QString dirString = dir.toUrlishString().toLower();
    const QStringList drives = Utils::transform(QDir::drives(), [](const QFileInfo &fi) {
        return fi.absoluteFilePath().toLower().chopped(1);
    });
    if (!Utils::contains(drives, [&dirString](const QString &drive) {
            return dirString.startsWith(drive);
        }) && !drives.isEmpty()) {
        QString newDir = dir.path();
        newDir.replace(0, 2, drives.first());
        return dir.withNewPath(newDir);
    }
    return {};
}

QString BuildDirectoryAspect::updateProblemLabelsHelper(const QString &value)
{
    QString genericProblem;
    QString genericProblemLabelString;
    if (ProjectExplorerSettings::get(this).warnAgainstNonAsciiBuildDir()) {
        const auto isInvalid = [](QChar c) { return c.isSpace() || !isascii(c.toLatin1()); };
        if (const auto invalidChar = Utils::findOr(value, std::nullopt, isInvalid)) {
            genericProblem = Tr::tr(
                                 "Build directory contains potentially problematic character \"%1\".")
                                 .arg(*invalidChar);
            genericProblemLabelString
                = genericProblem + " "
                  + Tr::tr("This warning can be suppressed <a href=\"dummy\">here</a>.");
        }
    }

    const auto updateRow = [](const QString &text, TextDisplay *row) {
        row->setText(text);
        row->setVisible(!text.isEmpty());
    };

    updateRow(genericProblemLabelString, &d->genericProblem);
    updateRow(d->specialProblem, &d->specialProblemDisplay);

    if (genericProblem.isEmpty() && d->specialProblem.isEmpty())
        return {};
    if (genericProblem.isEmpty())
        return d->specialProblem;
    if (d->specialProblem.isEmpty())
        return genericProblem;
    return genericProblem + '\n' + d->specialProblem;
}

BuildConfiguration *BuildDirectoryAspect::buildConfiguration() const
{
    return qobject_cast<BuildConfiguration *>(container());
}

SeparateDebugInfoAspect::SeparateDebugInfoAspect(AspectContainer *container)
    : TriStateAspect(container)
{
    setLabelText(Tr::tr("Separate debug info:"));
    setSettingsKey("SeparateDebugInfo");
    setValue(buildPropertiesSettings().separateDebugInfo());
}

} // namespace ProjectExplorer
