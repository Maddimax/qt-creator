// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gitlabprojectsettings.h"

#include "gitlaboptionspage.h"
#include "gitlabparameters.h"
#include "gitlabplugin.h"
#include "gitlabquery.h"
#include "gitlabtr.h"
#include "resultparser.h"

#include <git/gitclient.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>

#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QUrl>

using namespace Utils;

namespace GitLab {

const char PSK_LINKED_ID[]  = "GitLab.LinkedId";
const char PSK_SERVER[]     = "GitLab.Server";
const char PSK_PROJECT[]    = "GitLab.Project";
const char PSK_LAST_REQ[]   = "GitLab.LastRequest";

static QString accessLevelString(int accessLevel)
{
    switch (accessLevel) {
    case 10: return Tr::tr("Guest");
    case 20: return Tr::tr("Reporter");
    case 30: return Tr::tr("Developer");
    case 40: return Tr::tr("Maintainer");
    case 50: return Tr::tr("Owner");
    }
    return {};
}

std::tuple<QString, QString, int>
GitLabProjectSettings::remotePartsFromRemote(const QString &remote)
{
    QString host;
    QString path;
    int port = -1;
    if (remote.startsWith("git@")) {
            int colon = remote.indexOf(':');
            host = remote.mid(4, colon - 4);
            path = remote.mid(colon + 1);
    } else {
        const QUrl url(remote);
        host = url.host();
        path = url.path().mid(1); // ignore leading slash
        port = url.port();
    }
    if (path.endsWith(".git"))
        path.chop(4);

    return std::make_tuple(host, path, port);
}

GitLabProjectSettings::GitLabProjectSettings(ProjectExplorer::Project *project)
    : m_project(project)
{
    load();
    connect(project, &ProjectExplorer::Project::settingsLoaded,
            this, &GitLabProjectSettings::load);
    connect(project, &ProjectExplorer::Project::aboutToSaveSettings,
            this, &GitLabProjectSettings::save);
}

void GitLabProjectSettings::setLinked(bool linked)
{
    m_linked = linked;
    save();
}

void GitLabProjectSettings::load()
{
    m_id = Id::fromSetting(m_project->namedSettings(PSK_LINKED_ID));
    m_host = m_project->namedSettings(PSK_SERVER).toString();
    m_currentProject = m_project->namedSettings(PSK_PROJECT).toString();
    m_lastRequest = m_project->namedSettings(PSK_LAST_REQ).toDateTime();

    // may still be wrong, but we avoid an additional request by just doing sanity check here
    if (!m_id.isValid() || m_host.isEmpty())
        m_linked = false;
    else
        m_linked = gitLabParameters().serverForId(m_id).id.isValid();
}

void GitLabProjectSettings::save()
{
    if (m_linked) {
        m_project->setNamedSettings(PSK_LINKED_ID, m_id.toSetting());
        m_project->setNamedSettings(PSK_SERVER, m_host);
    } else {
        m_project->setNamedSettings(PSK_LINKED_ID, Id().toSetting());
        m_project->setNamedSettings(PSK_SERVER, QString());
    }
    m_project->setNamedSettings(PSK_PROJECT, m_currentProject);
    m_project->setNamedSettings(PSK_LAST_REQ, m_lastRequest);
}

// What the panel shows. GitLabProjectSettings is not a container - it keeps
// whether the project is linked and to what, and nothing else - so the panel
// holds the aspects and drives it.
class GitLabProjectPanel final : public Utils::AspectContainer
{
public:
    explicit GitLabProjectPanel(ProjectExplorer::Project *project)
        : m_projectSettings(projectSettings(project))
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/GitLab/GitLabProjectPanel.qml"));

        m_globalLink.setQmlName("GlobalLink");
        m_globalLink.setTextFormat(AspectControls::TextFormat::RichText);
        m_globalLink.setText("<a href=\"page\">" + Tr::tr("Global settings") + "</a>");
        connect(&m_globalLink, &TextDisplay::linkActivated, this, [] {
            Core::ICore::showSettings(Constants::GITLAB_SETTINGS);
        });
        registerAspect(&m_globalLink);

        m_host.setQmlName("Host");
        m_host.setLabelText(Tr::tr("Host:"));
        m_host.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        registerAspect(&m_host);

        m_linkedServer.setQmlName("LinkedServer");
        m_linkedServer.setLabelText(Tr::tr("Linked GitLab Configuration"));
        m_linkedServer.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        registerAspect(&m_linkedServer);

        m_info.setQmlName("Info");
        m_info.setVisible(false);
        registerAspect(&m_info);

        m_link.setQmlName("LinkWithGitLab");
        m_link.setActionText(Tr::tr("Link with GitLab"));
        m_link.setAction([this] { checkConnection(Link); });
        registerAspect(&m_link);

        m_unlink.setQmlName("Unlink");
        m_unlink.setActionText(Tr::tr("Unlink from GitLab"));
        m_unlink.setEnabled(false);
        m_unlink.setAction([this] { unlink(); });
        registerAspect(&m_unlink);

        m_checkConnection.setQmlName("CheckConnection");
        m_checkConnection.setActionText(Tr::tr("Test Connection"));
        m_checkConnection.setEnabled(false);
        m_checkConnection.setAction([this] { checkConnection(Connection); });
        registerAspect(&m_checkConnection);

        m_note.setQmlName("Note");
        m_note.setWordWrap(true);
        m_note.setText(Tr::tr("Projects linked with GitLab receive event notifications in the "
                              "Version Control output pane."));
        registerAspect(&m_note);

        // Behaviour, not layout: what was checked stops being true the moment
        // something else is chosen.
        m_host.addOnChanged(this, [this] { m_info.setVisible(false); });
        m_linkedServer.addOnChanged(this, [this] { m_info.setVisible(false); });
        connect(&gitLabParameters(), &GitLabParameters::changed,
                this, [this] { updateUi(); });
        updateUi();
    }

    static Utils::Key extraDataKey() { return "GitLabProjectPanel"; }

private:
    enum CheckMode { Connection, Link };

    void unlink();
    void checkConnection(CheckMode mode);
    void onConnectionChecked(const Project &project, const Id &serverId,
                             const QString &remote, const QString &projName);
    void updateUi();
    void updateEnabledStates();

    GitLabProjectSettings *m_projectSettings = nullptr;
    TextDisplay m_globalLink;
    SelectionAspect m_host;
    SelectionAspect m_linkedServer;
    TextDisplay m_info;
    ActionAspect m_link;
    ActionAspect m_unlink;
    ActionAspect m_checkConnection;
    TextDisplay m_note;
    CheckMode m_checkMode = Connection;
    QtTaskTree::QSingleTaskTreeRunner m_taskTreeRunner;
};

void GitLabProjectPanel::unlink()
{
    QTC_ASSERT(m_projectSettings->isLinked(), return);
    m_projectSettings->setLinked(false);
    m_projectSettings->setCurrentProject({});
    updateEnabledStates();
    linkedStateChanged(false);
}

void GitLabProjectPanel::checkConnection(CheckMode mode)
{
    const GitLabServer server = m_linkedServer.itemValue().value<GitLabServer>();
    const QString remote = m_host.itemValue().toString();

    const auto [remoteHost, projectName, port] = GitLabProjectSettings::remotePartsFromRemote(remote);
    if (remoteHost != server.host) { // port check as well
        m_info.setIconType(InfoType::NotOk);
        m_info.setText(Tr::tr("Remote host does not match chosen GitLab configuration."));
        m_info.setVisible(true);
        return;
    }

    // temporarily disable ui
    m_linkedServer.setEnabled(false);
    m_host.setEnabled(false);
    m_checkConnection.setEnabled(false);

    m_checkMode = mode;
    m_taskTreeRunner.start(gitLabQuery(
        [serverId = server.id, projectName = projectName](GitLabQuery &query) {
            query.setServerId(serverId);
            query.setQuery(Query(Query::Project, {projectName}));
        },
        [this, serverId = server.id, remote, projectName = projectName](const GitLabQuery &query) {
            onConnectionChecked(ResultParser::parseProject(query.result()), serverId, remote,
                                projectName);
        }));
}

void GitLabProjectPanel::onConnectionChecked(const Project &project,
                                             const Id &serverId,
                                             const QString &remote,
                                             const QString &projectName)
{
    bool linkable = false;
    if (!project.error.message.isEmpty()) {
        m_info.setIconType(InfoType::Error);
        m_info.setText(Tr::tr("Check settings for misconfiguration.")
                       + " (" + project.error.message + ')');
    } else {
        if (project.accessLevel != -1) {
            m_info.setIconType(InfoType::Ok);
            m_info.setText(Tr::tr("Accessible (%1).").arg(accessLevelString(project.accessLevel)));
            linkable = true;
        } else {
            m_info.setIconType(InfoType::Warning);
            m_info.setText(Tr::tr("Read only access."));
        }
    }
    m_info.setVisible(true);

    if (m_checkMode == Link && linkable) {
        m_projectSettings->setCurrentServer(serverId);
        m_projectSettings->setCurrentServerHost(remote);
        m_projectSettings->setLinked(true);
        m_projectSettings->setCurrentProject(projectName);
        linkedStateChanged(true);
    }
    updateEnabledStates();
}

void GitLabProjectPanel::updateUi()
{
    m_linkedServer.clearOptions();
    const QList<GitLabServer> allServers = gitLabParameters().gitLabServers;
    for (const GitLabServer &server : allServers) {
        const QString display = server.host + " (" + server.description + ')';
        m_linkedServer.addOption({display, {}, QVariant::fromValue(server)});
    }

    const FilePath projectDirectory = m_projectSettings->project()->projectDirectory();
    const FilePath repository =
        Git::Internal::gitClient().findRepositoryForDirectory(projectDirectory);

    m_host.clearOptions();
    if (!repository.isEmpty()) {
        const QMap<QString, QString> remotes =
            Git::Internal::gitClient().synchronousRemotesList(repository);
        for (auto it = remotes.begin(), end = remotes.end(); it != end; ++it) {
            const QString display = it.key() + " (" + it.value() + ')';
            m_host.addOption({display, {}, QVariant::fromValue(it.value())});
        }
    }

    const Id id = m_projectSettings->currentServer();
    const QString serverHost = m_projectSettings->currentServerHost();
    if (id.isValid()) {
        const GitLabServer server = gitLabParameters().serverForId(id);
        auto [remoteHost, projName, port] = GitLabProjectSettings::remotePartsFromRemote(serverHost);
        if (server.id.isValid() && server.host == remoteHost) { // found config
            m_projectSettings->setLinked(true);
            m_host.setValue(m_host.indexForItemValue(QVariant::fromValue(serverHost)));
            m_linkedServer.setValue(m_linkedServer.indexForItemValue(QVariant::fromValue(server)));
            linkedStateChanged(true);
        } else {
            m_projectSettings->setLinked(false);
            linkedStateChanged(false);
        }
    }
    updateEnabledStates();
}

void GitLabProjectPanel::updateEnabledStates()
{
    const bool isGitRepository = m_host.optionCount() > 0;
    const bool hasGitLabServers = m_linkedServer.optionCount() > 0;
    const bool linked = m_projectSettings->isLinked();

    m_linkedServer.setEnabled(isGitRepository && !linked);
    m_host.setEnabled(isGitRepository && !linked);
    m_link.setEnabled(isGitRepository && !linked && hasGitLabServers);
    m_unlink.setEnabled(isGitRepository && linked);
    m_checkConnection.setEnabled(isGitRepository && hasGitLabServers);
    if (!isGitRepository) {
        const FilePath projectDirectory = m_projectSettings->project()->projectDirectory();
        const FilePath repository =
            Git::Internal::gitClient().findRepositoryForDirectory(projectDirectory);
        if (repository.isEmpty())
            m_info.setText(Tr::tr("Not a git repository."));
        else
            m_info.setText(Tr::tr("Local git repository without remotes."));
        m_info.setIconType(InfoType::None);
        m_info.setVisible(true);
    }
}

static GitLabProjectPanel *gitLabProjectPanel(ProjectExplorer::Project *project)
{
    const Utils::Key key = GitLabProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new GitLabProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<GitLabProjectPanel *>();
}

class GitlabProjectPanelFactory final : public ProjectExplorer::ProjectPanelFactory
{
public:
    GitlabProjectPanelFactory()
    {
        setPriority(999);
        setDisplayName(Tr::tr("GitLab"));
        setSettingsProvider([](ProjectExplorer::Project *project) {
            return gitLabProjectPanel(project);
        });
    }
};

void setupGitlabProjectPanel()
{
    static GitlabProjectPanelFactory theGitlabProjectPanelFactory;
}

} // namespace GitLab
