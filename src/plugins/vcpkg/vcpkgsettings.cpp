// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcpkgsettings.h"

#include "vcpkgconstants.h"
#include "vcpkgtr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectimporter.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectsettings.h>
#include <projectexplorer/useglobalaspect.h>

#include <utils/environment.h>
#include <utils/layoutbuilder.h>
#include <utils/pathvalidation.h>
#include <utils/utilsicons.h>

#include <QDesktopServices>
#include <QToolButton>
#include <QVariant>

using namespace ProjectExplorer;
using namespace Utils;

namespace Vcpkg::Internal {

// --- VcpkgSettings -----------------------------------------------------------

VcpkgSettings::VcpkgSettings()
{
    setSettingsGroup(Constants::Settings::GROUP_ID);
    setAutoApply(false);

    vcpkgRoot.setSettingsKey("VcpkgRoot");
    vcpkgRoot.setExpectedKind(PathChooserKind::ExistingDirectory);
    FilePath defaultPath = FilePath::fromUserInput(
        qtcEnvironmentVariable(Constants::ENVVAR_VCPKG_ROOT));

    if (!defaultPath.isDir())
        defaultPath = Environment::systemEnvironment().searchInPath(Constants::VCPKG_COMMAND).parentDir();

    if (defaultPath.isDir())
        vcpkgRoot.setDefaultPathValue(defaultPath);

    connect(this, &AspectContainer::applied, this, &VcpkgSettings::setVcpkgRootEnvironmentVariable);

    openWebsite.setActionText(Tr::tr("Website"));
    openWebsite.setToolTip(Constants::WEBSITE_URL);
    openWebsite.setQmlName("OpenWebsite");
    openWebsite.setAction(
        [] { QDesktopServices::openUrl(QUrl::fromUserInput(Constants::WEBSITE_URL)); });

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Vcpkg/VcpkgSettingsPage.qml"));

    readSettings();
    setVcpkgRootEnvironmentVariable();
}

void VcpkgSettings::setVcpkgRootEnvironmentVariable()
{
    Environment::modifySystemEnvironment({{Constants::ENVVAR_VCPKG_ROOT,
        vcpkgRoot.expandedValue().nativePath()}});
}

// --- VcpkgProjectSettings ----------------------------------------------------

class VcpkgProjectSettings : public VcpkgSettings
{
public:
    explicit VcpkgProjectSettings(Project *project)
        : m_project(project)
    {
        setAutoApply(true);

        // Load project data (after base constructor loaded global data)
        Store data = storeFromVariant(project->namedSettings(Constants::Settings::GENERAL_ID));
        fromMap(data);
        useGlobalSettings.setValue(data.value(Constants::Settings::USE_GLOBAL_SETTINGS, true).toBool());

        vcpkgRoot.setEnabled(!useGlobalSettings());
        setVcpkgRootEnvironmentVariable();

        // Set up save connections after loading to avoid spurious saves during init
        useGlobalSettings.addOnChanged(this, [this] {
            vcpkgRoot.setEnabled(!useGlobalSettings());
            save();
        });
        vcpkgRoot.addOnChanged(this, [this] {
            if (!useGlobalSettings())
                save();
        });

        // Re-read when project is fully loaded (reading in constructor is too early)
        connect(m_project, &Project::settingsLoaded, this, [this] {
            Store data = storeFromVariant(m_project->namedSettings(Constants::Settings::GENERAL_ID));
            fromMap(data);
            useGlobalSettings.setValue(data.value(Constants::Settings::USE_GLOBAL_SETTINGS, true).toBool());
            setVcpkgRootEnvironmentVariable();
        });
    }

    void save()
    {
        Store data;
        toMap(data);
        data.insert(Constants::Settings::USE_GLOBAL_SETTINGS, useGlobalSettings());
        m_project->setNamedSettings(Constants::Settings::GENERAL_ID, variantFromStore(data));
        setVcpkgRootEnvironmentVariable();
    }

    static Key extraDataKey() { return "VcpkgProjectSettings"; }

    UseGlobalAspect useGlobalSettings{Constants::Settings::GENERAL_ID};

private:
    Project * const m_project;
};

// --- Helpers -----------------------------------------------------------------

static VcpkgProjectSettings *vcpkgProjectSettings(Project *project)
{
    return ProjectExplorer::projectSettings<VcpkgProjectSettings>(project);
}

VcpkgSettings *vcpkgSettingsForProject(Project *project)
{
    static VcpkgSettings theSettings;
    if (!project)
        return &theSettings;
    VcpkgProjectSettings *ps = vcpkgProjectSettings(project);
    if (ps->useGlobalSettings())
        return &theSettings;
    return ps;
}

// --- Settings page -----------------------------------------------------------

class VcpkgSettingsPage : public Core::IOptionsPage
{
public:
    VcpkgSettingsPage()
    {
        setId(Constants::Settings::GENERAL_ID);
        setDisplayName("Vcpkg");
        setCategory(Constants::Settings::CATEGORY);
        setSettingsProvider([] { return vcpkgSettingsForProject(nullptr); });
    }
};

static const VcpkgSettingsPage settingsPage;

// --- Project panel -----------------------------------------------------------

// What the panel shows. The flag is kept out of the settings container for the
// same reason it is everywhere else: a container that turns itself off would
// turn the flag off with it. See ProjectCommentsPanel.
class VcpkgProjectPanel final : public AspectContainer
{
public:
    explicit VcpkgProjectPanel(VcpkgProjectSettings *settings)
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Vcpkg/VcpkgProjectPanel.qml"));

        settings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&settings->useGlobalSettings);

        settings->setQmlName("Settings");
        registerAspect(settings);
    }

    static Utils::Key extraDataKey() { return "VcpkgProjectPanel"; }
};

static VcpkgProjectPanel *vcpkgProjectPanel(Project *project)
{
    const Utils::Key key = VcpkgProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new VcpkgProjectPanel(vcpkgProjectSettings(project)));
        project->setExtraData(key, v);
    }
    return v.value<VcpkgProjectPanel *>();
}

class VcpkgSettingsPanelFactory final : public ProjectPanelFactory
{
public:
    VcpkgSettingsPanelFactory()
    {
        setPriority(120);
        setDisplayName("Vcpkg");
        setSettingsProvider([](Project *project) {
            return vcpkgProjectPanel(project);
        });
    }
};

const VcpkgSettingsPanelFactory projectSettingsPane;

} // Vcpkg::Internal
