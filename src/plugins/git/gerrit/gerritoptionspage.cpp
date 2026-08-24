// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritoptionspage.h"
#include "gerritparameters.h"
#include "gerritserver.h"
#include "../gittr.h"

#include <utils/aspects.h>
#include <utils/pathchooser.h>

#include <vcsbase/vcsbaseconstants.h>

using namespace Utils;

namespace Gerrit::Internal {

// What the Gerrit settings page edits. The settings themselves live in
// GerritParameters, which the rest of the plugin reads and which keeps its own
// settings keys, so these aspects have none: they are read from it when the
// page is built and written back on apply.
class GerritSettingsAspects final : public AspectContainer
{
public:
    explicit GerritSettingsAspects(const std::function<void()> &onChanged)
        : m_onChanged(onChanged)
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/gerrit/GerritSettingsPage.qml"));

        m_host.setQmlName("Host");
        m_host.setLabelText(Git::Tr::tr("&Host:"));
        m_host.setDisplayStyle(StringAspect::LineEditDisplay);

        m_user.setQmlName("User");
        m_user.setLabelText(Git::Tr::tr("&User:"));
        m_user.setDisplayStyle(StringAspect::LineEditDisplay);

        m_ssh.setQmlName("Ssh");
        m_ssh.setLabelText(Git::Tr::tr("&ssh:"));
        m_ssh.setExpectedKind(PathChooserKind::ExistingCommand);
        m_ssh.setCommandVersionArguments({"-V"});
        m_ssh.setHistoryCompleter("Git.SshCommand.History");

        m_curl.setQmlName("Curl");
        m_curl.setLabelText(Git::Tr::tr("cur&l:"));
        m_curl.setExpectedKind(PathChooserKind::ExistingCommand);
        m_curl.setCommandVersionArguments({"-V"});

        m_port.setQmlName("Port");
        m_port.setLabelText(Git::Tr::tr("SSH &Port:"));
        m_port.setRange(1, 65535);

        m_https.setQmlName("Https");
        m_https.setLabelText(Git::Tr::tr("P&rotocol:"));
        m_https.setLabel(Git::Tr::tr("HTTPS"), BoolAspect::LabelPlacement::AtCheckBox);
        m_https.setToolTip(Git::Tr::tr(
            "Determines the protocol used to form a URL in case\n"
            "\"canonicalWebUrl\" is not configured in the file\n"
            "\"gerrit.config\"."));

        readFromParameters();
    }

    void apply() override
    {
        AspectContainer::apply();

        GerritParameters &s = gerritSettings();
        const GerritServer server(m_host().trimmed(),
                                  static_cast<unsigned short>(m_port()),
                                  m_user().trimmed(),
                                  GerritServer::Ssh);
        const FilePath ssh = m_ssh();
        const FilePath curl = m_curl();
        const bool https = m_https();

        if (server == s.server && ssh == s.ssh && curl == s.curl && https == s.https)
            return;

        // Before the assignment: which flag the port takes depends on what the
        // ssh binary is, so it is a change of binary that has to ask again.
        const bool sshChanged = s.ssh != ssh;

        s.server = server;
        s.ssh = ssh;
        s.curl = curl;
        s.https = https;
        if (sshChanged)
            s.setPortFlagBySshType();
        s.toSettings();
        if (m_onChanged)
            m_onChanged();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        readFromParameters();
    }

private:
    void readFromParameters()
    {
        const GerritParameters &s = gerritSettings();
        m_host.setValue(s.server.host);
        m_user.setValue(s.server.user.userName);
        m_ssh.setValue(s.ssh);
        m_curl.setValue(s.curl);
        m_port.setValue(s.server.port);
        m_https.setValue(s.https);
    }

    StringAspect m_host{this};
    StringAspect m_user{this};
    FilePathAspect m_ssh{this};
    FilePathAspect m_curl{this};
    IntegerAspect m_port{this};
    BoolAspect m_https{this};
    std::function<void()> m_onChanged;
};

// GerritOptionsPage

GerritOptionsPage::GerritOptionsPage(const std::function<void()> &onChanged)
{
    setId("Gerrit");
    setDisplayName(Git::Tr::tr("Gerrit"));
    setCategory(VcsBase::Constants::VCS_SETTINGS_CATEGORY);
    setSettingsProvider([onChanged] {
        static GerritSettingsAspects theSettings(onChanged);
        return &theSettings;
    });
}

} // Gerrit::Internal
