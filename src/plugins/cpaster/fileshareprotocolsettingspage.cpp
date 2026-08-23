// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fileshareprotocolsettingspage.h"

#include "cpastertr.h"
#include "cpasterconstants.h"

#include <utils/pathvalidation.h>
#include <utils/temporarydirectory.h>

#include <QLabel>

using namespace Utils;

namespace CodePaster {

FileShareProtocolSettings &fileShareSettings()
{
    static FileShareProtocolSettings theSettings;
    return theSettings;
}

FileShareProtocolSettings::FileShareProtocolSettings()
{
    setAutoApply(false);
    setSettingsGroup("FileSharePasterSettings");

    path.setSettingsKey("Path");
    path.setExpectedKind(PathChooserKind::ExistingDirectory);
    path.setDefaultValue(TemporaryDirectory::masterDirectoryPath());
    path.setLabelText(Tr::tr("&Path:"));

    displayCount.setSettingsKey("DisplayCount");
    displayCount.setDefaultValue(10);
    displayCount.setSuffix(' ' + Tr::tr("entries"));
    displayCount.setLabelText(Tr::tr("&Display:"));

    note.setText(Tr::tr(
        "The fileshare-based paster protocol allows for sharing code snippets using "
        "simple files on a shared network drive. Files are never deleted."));
    note.setWordWrap(true);
    note.setQmlName("Note");
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CodePaster/FileShareSettingsPage.qml"));

    readSettings();
}

class FileShareProtocolSettingsPage final : public Core::IOptionsPage
{
public:
    FileShareProtocolSettingsPage()
    {
        setId("X.CodePaster.FileSharePaster");
        setDisplayName(Tr::tr("Fileshare"));
        setCategory(Constants::CPASTER_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &fileShareSettings(); });
    }
};

Core::IOptionsPage &fileShareSettingsPage()
{
    static FileShareProtocolSettingsPage theSettings;
    return theSettings;
}

} // namespace CodePaster
