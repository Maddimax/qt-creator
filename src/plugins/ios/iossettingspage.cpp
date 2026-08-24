// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "iossettingspage.h"

#include "iosconfigurations.h"
#include "iosconstants.h"
#include "iostr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/aspects.h>

#include <QDesktopServices>
#include <QUrl>

using namespace Utils;

namespace Ios::Internal {

// What the iOS page edits. The setting lives in IosConfigurations, which the
// rest of the plugin reads, so the aspect is a view of it: read when the page
// is built, written back on apply.
class IosSettings final : public AspectContainer
{
public:
    IosSettings()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Ios/IosSettingsPage.qml"));

        m_askAboutDevices.setQmlName("AskAboutDevices");
        m_askAboutDevices.setLabel(Tr::tr("Ask about devices not in developer mode"),
                                   BoolAspect::LabelPlacement::AtCheckBox);

        m_xcodeNote.setQmlName("XcodeNote");
        m_xcodeNote.setTextFormat(AspectControls::TextFormat::RichText);
        m_xcodeNote.setWordWrap(true);
        m_xcodeNote.setText(
            Tr::tr("Configure available simulator devices in <a href=\"%1\">Xcode</a>.")
                .arg("https://developer.apple.com/documentation/xcode/"
                     "running-your-app-in-simulator-or-on-a-device/"
                     "#Configure-the-list-of-simulated-devices"));
        connect(&m_xcodeNote, &TextDisplay::linkActivated,
                this, [](const QString &link) { QDesktopServices::openUrl(QUrl(link)); });

        readFromConfigurations();
    }

    void apply() override
    {
        AspectContainer::apply();
        IosConfigurations::setIgnoreAllDevices(!m_askAboutDevices());
        IosConfigurations::updateAutomaticKitList();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        readFromConfigurations();
    }

private:
    void readFromConfigurations()
    {
        m_askAboutDevices.setValue(!IosConfigurations::ignoreAllDevices());
    }

    BoolAspect m_askAboutDevices{this};
    TextDisplay m_xcodeNote{this};
};

// IosSettingsPage

class IosSettingsPage final : public Core::IOptionsPage
{
public:
    IosSettingsPage()
    {
        setId(Constants::IOS_SETTINGS_ID);
        setDisplayName(Tr::tr("iOS"));
        setCategory(ProjectExplorer::Constants::DEVICE_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static IosSettings theSettings;
            return &theSettings;
        });
    }
};

void setupIosSettingsPage()
{
    static IosSettingsPage theIosSettingsPage;
}

} // Ios::Internal
