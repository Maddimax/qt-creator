// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fossilsettings.h"

#include "constants.h"
#include "fossiltr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/pathvalidation.h>

#include <vcsbase/vcsbaseconstants.h>

using namespace Utils;

namespace Fossil::Internal {

FossilSettings &settings()
{
    static FossilSettings theSettings;
    return theSettings;
}

FossilSettings::FossilSettings()
{
    setAutoApply(false);
    setSettingsGroup(Constants::FOSSIL);

    binaryPath.setExpectedKind(PathChooserKind::ExistingCommand);
    binaryPath.setDefaultValue(Constants::FOSSILDEFAULT);
    binaryPath.setDisplayName(Tr::tr("Fossil Command"));
    binaryPath.setHistoryCompleter("Fossil.Command.History");
    binaryPath.setLabelText(Tr::tr("Command:"));

    defaultRepoPath.setSettingsKey("defaultRepoPath");
    defaultRepoPath.setExpectedKind(PathChooserKind::Directory);
    defaultRepoPath.setDisplayName(Tr::tr("Fossil Repositories"));
    defaultRepoPath.setLabelText(Tr::tr("Default path:"));
    defaultRepoPath.setToolTip(Tr::tr("Directory to store local repositories by default."));

    userName.setDisplayStyle(StringAspect::LineEditDisplay);
    userName.setLabelText(Tr::tr("Default user:"));
    userName.setToolTip(Tr::tr("Existing user to become an author of changes made to the repository."));

    sslIdentityFile.setSettingsKey("sslIdentityFile");
    sslIdentityFile.setExpectedKind(PathChooserKind::File);
    sslIdentityFile.setDisplayName(Tr::tr("SSL/TLS Identity Key"));
    sslIdentityFile.setLabelText(Tr::tr("SSL/TLS identity:"));
    sslIdentityFile.setToolTip(Tr::tr("SSL/TLS client identity key to use if requested by the server."));

    diffIgnoreAllWhiteSpace.setSettingsKey("diffIgnoreAllWhiteSpace");

    diffStripTrailingCR.setSettingsKey("diffStripTrailingCR");

    annotateShowCommitters.setSettingsKey("annotateShowCommitters");

    annotateListVersions.setSettingsKey("annotateListVersions");

    timelineWidth.setSettingsKey("timelineWidth");
    timelineWidth.setLabelText(Tr::tr("Log width:"));
    timelineWidth.setToolTip(Tr::tr("The width of log entry line (>20). "
        "Choose 0 to see a single line per entry."));

    timelineLineageFilter.setSettingsKey("timelineLineageFilter");

    timelineVerbose.setSettingsKey("timelineVerbose");

    timelineItemType.setDefaultValue("all");
    timelineItemType.setSettingsKey("timelineItemType");

    disableAutosync.setSettingsKey("disableAutosync");
    disableAutosync.setDefaultValue(true);
    disableAutosync.setLabelText(Tr::tr("Disable auto-sync"));
    disableAutosync.setToolTip(Tr::tr("Disable automatic pull prior to commit or update and "
        "automatic push after commit or tag or branch creation."));

    timeout.setLabelText(Tr::tr("Timeout:"));
    timeout.setSuffix(Tr::tr("s"));

    logCount.setLabelText(Tr::tr("Log count:"));
    logCount.setToolTip(Tr::tr("The number of recent commit log entries to show. "
        "Choose 0 to see all entries."));

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Fossil/FossilSettingsPage.qml"));

    readSettings();
}

// FossilSettingsPage

class FossilSettingsPage final : public Core::IOptionsPage
{
public:
    FossilSettingsPage()
    {
        setId(Constants::VCS_ID_FOSSIL);
        setDisplayName(Tr::tr("Fossil"));
        setCategory(VcsBase::Constants::VCS_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

const FossilSettingsPage settingsPage;

} // Fossil::Internal
