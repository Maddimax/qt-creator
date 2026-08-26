// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gitlaboptionspage.h"

#include "gitlabparameters.h"
#include "gitlabtr.h"

#include <coreplugin/icore.h>

#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/shutdownguard.h>

#include <vcsbase/vcsbaseconstants.h>

#include <QRegularExpression>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace GitLab {

static bool hostValid(const QString &host)
{
    static const QRegularExpression ip(R"(^(\d+).(\d+).(\d+).(\d+)$)");
    static const QRegularExpression dn(R"(^([a-zA-Z0-9][a-zA-Z0-9-]+\.)+[a-zA-Z0-9][a-zA-Z0-9-]+$)");
    const QRegularExpressionMatch match = ip.match(host);
    if (match.hasMatch()) {
        for (int i = 1; i < 5; ++i) {
            int val = match.captured(i).toInt();
            if (val < 0 || val > 255)
                return false;
        }
        return true;
    }
    return (host == "localhost") || dn.match(host).hasMatch();
}

// What the GitLab page edits. The servers themselves live in
// GitLabParameters, which the rest of the plugin reads and which keeps its own
// settings format, so these aspects have none: they are read from it when the
// page is built and written back on apply.

class GitLabServerAspects final : public AspectContainer
{
public:
    GitLabServerAspects()
    {
        host.setQmlName("Host");
        host.setLabelText(Tr::tr("Host:"));
        host.setDisplayStyle(StringAspect::LineEditDisplay);
        host.setValidationFunction([](const QString &text) -> Result<> {
            if (hostValid(text))
                return ResultOk;
            return ResultError(Tr::tr("Not a host name or an IP address."));
        });

        description.setQmlName("Description");
        description.setLabelText(Tr::tr("Description:"));
        description.setDisplayStyle(StringAspect::LineEditDisplay);

        token.setQmlName("Token");
        token.setLabelText(Tr::tr("Access token:"));
        token.setDisplayStyle(StringAspect::LineEditDisplay);

        port.setQmlName("Port");
        port.setLabelText(Tr::tr("Port:"));
        port.setRange(1, 65535);
        port.setValue(GitLabServer::defaultPort);

        secure.setQmlName("Secure");
        secure.setLabelText(Tr::tr("HTTPS:"));
        secure.setLabelPlacement(BoolAspect::LabelPlacement::InExtraLabel);
        secure.setDefaultValue(true);

    }

    GitLabServer server() const
    {
        GitLabServer result;
        // Kept, not generated: the default server is named by id, and a server
        // that got a new one every time it was read would stop being it.
        result.id = m_id;
        result.host = host.volatileValue();
        result.description = description.volatileValue();
        result.token = token.volatileValue();
        result.port = port.volatileValue();
        result.secure = secure.volatileValue();
        return result;
    }

    void setServer(const GitLabServer &server)
    {
        m_id = server.id;
        host.setValue(server.host);
        description.setValue(server.description);
        token.setValue(server.token);
        port.setValue(server.port);
        secure.setValue(server.secure);
    }

    StringAspect host{this};
    StringAspect description{this};
    StringAspect token{this};
    IntegerAspect port{this};
    BoolAspect secure{this};

private:
    Id m_id = Id::generate();
};

class GitLabSettingsAspects final : public AspectContainer
{
public:
    GitLabSettingsAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/GitLab/GitLabSettingsPage.qml"));

        servers.setQmlName("Servers");
        servers.setLabelText(Tr::tr("Servers"));
        servers.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
        servers.setCreateItemFunction([] { return std::make_shared<GitLabServerAspects>(); });
        servers.listViewDataCallback = [](GitLabServerAspects *item, int role) -> QVariant {
            if (role == Qt::DisplayRole)
                return item->server().displayString();
            return {};
        };

        defaultServer.setQmlName("DefaultServer");
        defaultServer.setLabelText(Tr::tr("Default:"));
        defaultServer.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        curl.setQmlName("Curl");
        curl.setLabelText(Tr::tr("curl:"));
        curl.setExpectedKind(PathChooserKind::ExistingCommand);

        // Which servers there are to be the default one is the list's answer,
        // and it changes as the list is edited - a server is named in the combo
        // by the host that was just typed into it.
        connect(&servers, &AspectList::volatileItemListChanged,
                this, &GitLabSettingsAspects::refreshServerChoices);
        connect(&servers, &BaseAspect::volatileValueChanged,
                this, &GitLabSettingsAspects::refreshServerChoices);

        readFromParameters();
    }

    void apply() override
    {
        AspectContainer::apply();

        GitLabParameters result;
        for (const std::shared_ptr<BaseAspect> &item : servers.items())
            result.gitLabServers.append(static_cast<GitLabServerAspects *>(item.get())->server());
        result.defaultGitLabServer = Id::fromSetting(defaultServer.itemValue());
        result.curl = curl();

        if (result == gitLabParameters())
            return;

        gitLabParameters().assign(result);
        gitLabParameters().toSettings(Core::ICore::settings());
        emit gitLabParameters().changed();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        readFromParameters();
    }

private:
    void readFromParameters()
    {
        const GitLabParameters &p = gitLabParameters();
        curl.setValue(p.curl);
        servers.clear();
        for (const GitLabServer &server : p.gitLabServers) {
            auto item = std::make_shared<GitLabServerAspects>();
            item->setServer(server);
            servers.addItem(item);
        }
        // What is stored is where the page starts from, not an edit of it.
        servers.apply();
        refreshServerChoices();
        defaultServer.setValue(defaultServer.indexForItemValue(p.defaultGitLabServer.toSetting()));
    }

    void refreshServerChoices()
    {
        // Which server is the default is remembered by id across the rebuild.
        // A selection aspect holds a position, and adding or removing a server
        // moves the rest of them.
        const QVariant wanted = defaultServer.itemValue();
        defaultServer.clearOptions();
        for (const std::shared_ptr<BaseAspect> &item : servers.volatileItems()) {
            const GitLabServer server = static_cast<GitLabServerAspects *>(item.get())->server();
            defaultServer.addOption({server.displayString(), {}, server.id.toSetting()});
        }
        const int index = defaultServer.indexForItemValue(wanted);
        defaultServer.setValue(index < 0 ? 0 : index);
    }

    AspectList servers{this};
    SelectionAspect defaultServer{this};
    FilePathAspect curl{this};
};

// GitLabOptionsPage

GitLabOptionsPage::GitLabOptionsPage()
{
    setId(Constants::GITLAB_SETTINGS);
    setDisplayName(Tr::tr("GitLab"));
    setCategory(VcsBase::Constants::VCS_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static GuardedObject<GitLabSettingsAspects> theAspects;
        return theAspects.get();
    });
}

#ifdef WITH_TESTS

// The page's servers used to live in a QComboBox's item data, edited in a
// modal dialog, so what it held could only be read back out of widgets.

class GitLabSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testThePageShowsTheServersThatAreStored();
    void testTheDefaultIsPickedByIdAndNotByPosition();
    void testRenamingAServerRenamesItEverywhere();
    void testAddingAServerGivesItAnIdAndApplyingStoresIt();
    void testApplyingWritesTheParametersBack();
    void testAHostThatIsNeitherANameNorAnAddressIsRefused();

private:
    static GitLabServerAspects *itemAt(const AspectList &list, int index)
    {
        return static_cast<GitLabServerAspects *>(list.volatileItems().at(index).get());
    }
    static const AspectList &serversOf(const GitLabSettingsAspects &page)
    {
        return static_cast<const AspectList &>(*page.aspects().first());
    }
    static SelectionAspect &defaultOf(const GitLabSettingsAspects &page)
    {
        return static_cast<SelectionAspect &>(*page.aspects().at(1));
    }

    GitLabParameters m_original;
    Id m_secondId;
    Id m_defaultId;
};

void GitLabSettingsTest::init()
{
    m_original.assign(gitLabParameters());

    GitLabParameters stored;
    stored.gitLabServers.append(
        GitLabServer(Id::generate(), "gitlab.com", "The public one", "abc", 443, true));
    m_secondId = Id::generate();
    stored.gitLabServers.append(
        GitLabServer(m_secondId, "10.0.0.1", {}, "def", 8443, false));
    m_defaultId = Id::generate();
    stored.gitLabServers.append(
        GitLabServer(m_defaultId, "gitlab.example.org", "Ours", "ghi", 443, true));
    // The last one, so that a default remembered by position would land on the
    // wrong server as soon as one in front of it goes.
    stored.defaultGitLabServer = m_defaultId;
    stored.curl = FilePath::fromString("/usr/bin/curl");
    gitLabParameters().assign(stored);
}

void GitLabSettingsTest::cleanup()
{
    gitLabParameters().assign(m_original);
}

void GitLabSettingsTest::testThePageShowsTheServersThatAreStored()
{
    GitLabSettingsAspects page;
    const AspectList &servers = serversOf(page);

    QCOMPARE(servers.volatileItems().size(), 3);
    QCOMPARE(itemAt(servers, 0)->host.volatileValue(), QString("gitlab.com"));
    QCOMPARE(itemAt(servers, 1)->port.volatileValue(), 8443);
    QCOMPARE(itemAt(servers, 1)->secure.volatileValue(), false);
    QCOMPARE(itemAt(servers, 0)->token.volatileValue(), QString("abc"));

    // Stored is where the page starts from, so there is nothing to apply.
    QVERIFY(!static_cast<const BaseAspect &>(page).isDirty());
}

void GitLabSettingsTest::testTheDefaultIsPickedByIdAndNotByPosition()
{
    GitLabSettingsAspects page;
    SelectionAspect &fallback = defaultOf(page);

    QCOMPARE(fallback.optionCount(), 3);
    QCOMPARE(fallback.itemValue(), m_defaultId.toSetting());

    // Removing one in front of it leaves it the default, which storing a
    // position would not: it is at 2 before and at 1 after.
    const_cast<AspectList &>(serversOf(page)).removeItem(serversOf(page).volatileItems().first());
    QCOMPARE(fallback.optionCount(), 2);
    QCOMPARE(fallback.itemValue(), m_defaultId.toSetting());
    QCOMPARE(fallback.value(), 1);
}

void GitLabSettingsTest::testRenamingAServerRenamesItEverywhere()
{
    GitLabSettingsAspects page;
    const AspectList &servers = serversOf(page);
    QCOMPARE(servers.listViewDataCallback(itemAt(servers, 0), Qt::DisplayRole).toString(),
             QString("gitlab.com (The public one)"));

    itemAt(servers, 0)->host.setVolatileValue(QString("gitlab.example.com"));

    QCOMPARE(servers.listViewDataCallback(itemAt(servers, 0), Qt::DisplayRole).toString(),
             QString("gitlab.example.com (The public one)"));
    // And in the combo, which names the servers the list holds.
    QCOMPARE(defaultOf(page).displayForIndex(0), QString("gitlab.example.com (The public one)"));
}

void GitLabSettingsTest::testAddingAServerGivesItAnIdAndApplyingStoresIt()
{
    GitLabSettingsAspects page;
    const AspectList &servers = serversOf(page);
    auto added = static_cast<GitLabServerAspects *>(
        const_cast<AspectList &>(servers).createAndAddItem().get());
    added->host.setVolatileValue(QString("gitlab.internal"));

    const Id addedId = added->server().id;
    QVERIFY(addedId.isValid());
    QVERIFY(addedId != m_defaultId);

    static_cast<BaseAspect &>(page).apply();

    QCOMPARE(gitLabParameters().gitLabServers.size(), 4);
    QCOMPARE(gitLabParameters().serverForId(addedId).host, QString("gitlab.internal"));
    // Reading a server back must not give it a new id, or it would stop being
    // the one the default names.
    QCOMPARE(gitLabParameters().defaultGitLabServer, m_defaultId);
}

void GitLabSettingsTest::testApplyingWritesTheParametersBack()
{
    GitLabSettingsAspects page;
    itemAt(serversOf(page), 0)->description.setVolatileValue(QString("Renamed"));
    defaultOf(page).setValue(0);

    static_cast<BaseAspect &>(page).apply();

    QCOMPARE(gitLabParameters().gitLabServers.at(0).description, QString("Renamed"));
    QCOMPARE(gitLabParameters().defaultGitLabServer,
             gitLabParameters().gitLabServers.at(0).id);
    QCOMPARE(gitLabParameters().curl, FilePath::fromString("/usr/bin/curl"));
}

void GitLabSettingsTest::testAHostThatIsNeitherANameNorAnAddressIsRefused()
{
    GitLabSettingsAspects page;
    const BaseAspect &host = itemAt(serversOf(page), 0)->host;

    QCOMPARE(host.validationMessage("gitlab.com"), QString());
    QCOMPARE(host.validationMessage("localhost"), QString());
    QCOMPARE(host.validationMessage("10.0.0.1"), QString());
    // The dialog used to drop what was typed without saying anything.
    QVERIFY(!host.validationMessage("not a host").isEmpty());
    QVERIFY(!host.validationMessage("999.1.1.1").isEmpty());
    QVERIFY(!host.validationMessage("").isEmpty());
}

QObject *createGitLabSettingsTest()
{
    return new GitLabSettingsTest;
}

#endif // WITH_TESTS

} // namespace GitLab

#include "gitlaboptionspage.moc"
