// Copyright (C) 2017 Orgad Shaneh <orgads@gmail.com>.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritremotechooser.h"
#include "gerritparameters.h"
#include "gerritserver.h"
#include "../gitclient.h"
#include "../gittr.h"

#include <utils/algorithm.h>
#include <utils/filepath.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif


using namespace Utils;

namespace Gerrit::Internal {

QList<RemoteChoice> remoteChoices(const QList<QPair<QString, GerritServer>> &remotes,
                                  bool allowDups)
{
    QList<RemoteChoice> choices;
    for (const QPair<QString, GerritServer> &remote : remotes) {
        const QString &name = remote.first;
        const GerritServer &server = remote.second;

        // Two remotes may point at the same server - a fetch and a push URL,
        // or the fallback repeating one of them. Only the first is offered.
        bool alreadyListed = false;
        for (const RemoteChoice &choice : choices)
            alreadyListed = alreadyListed || choice.server == server;
        if (!allowDups && alreadyListed)
            continue;

        choices.append({name, server, server.host + QString(" (%1)").arg(name)});
    }
    return choices;
}

int defaultRemoteIndex(const QList<RemoteChoice> &choices)
{
    if (choices.isEmpty())
        return -1;
    // The remote actually called "gerrit" is the one to review against; the
    // widget combo box picked the last such entry, because each one set the
    // current index as it was added.
    for (int i = choices.size(); --i >= 0; ) {
        if (choices.at(i).name == "gerrit")
            return i;
    }
    return 0;
}

GerritRemoteChooserAspect::GerritRemoteChooserAspect(AspectContainer *container)
    : AspectContainer(container)
{
    // The chooser and the way to ask again are one line, as the widget's
    // QHBoxLayout was.
    setInlineRow(true);

    remote.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    refresh.setActionText(Git::Tr::tr("Refresh Remote Servers"));
    refresh.setActionIcon(Utils::Icons::RESET.icon());
    refresh.setAction([this] { updateRemotes(true); });

    connect(&remote, &BaseAspect::changed, this, [this] {
        if (m_updatingRemotes || m_choices.isEmpty())
            return;
        emit remoteChanged();
    });
}

void GerritRemoteChooserAspect::setRepository(const FilePath &repository)
{
    m_repository = repository;
}

void GerritRemoteChooserAspect::setFallbackEnabled(bool value)
{
    m_enableFallback = value;
}

void GerritRemoteChooserAspect::setAllowDups(bool value)
{
    m_allowDups = value;
}

bool GerritRemoteChooserAspect::setCurrentRemote(const QString &remoteName)
{
    for (int i = 0; i < m_choices.size(); ++i) {
        if (m_choices.at(i).name == remoteName) {
            remote.setValue(i);
            return true;
        }
    }
    return false;
}

void GerritRemoteChooserAspect::setChoices(const QList<RemoteChoice> &choices)
{
    m_updatingRemotes = true;
    m_choices = choices;

    remote.clearOptions();
    for (const RemoteChoice &choice : choices)
        remote.addOption(choice.displayName);
    remote.setValue(qMax(0, defaultRemoteIndex(choices)));

    // Nothing to choose between is nothing to choose.
    remote.setEnabled(choices.size() > 1);
    m_updatingRemotes = false;

    if (!m_choices.isEmpty())
        emit remoteChanged();
}

void GerritRemoteChooserAspect::updateRemotes(bool forceReload)
{
    QTC_ASSERT(!m_repository.isEmpty(), return);

    QString errorMessage; // Mute errors. We'll just fallback to the defaults
    const QMap<QString, QString> remotesList
        = Git::Internal::gitClient().synchronousRemotesList(m_repository, &errorMessage);

    QList<QPair<QString, GerritServer>> resolved;
    for (auto it = remotesList.cbegin(), end = remotesList.cend(); it != end; ++it) {
        GerritServer server;
        if (!server.fillFromRemote(it.value(), forceReload))
            continue;
        resolved.append({it.key(), server});
    }
    if (m_enableFallback)
        resolved.append({Git::Tr::tr("Fallback"), gerritSettings().server});

    setChoices(remoteChoices(resolved, m_allowDups));
}

GerritServer GerritRemoteChooserAspect::currentServer() const
{
    const int index = remote.value();
    QTC_ASSERT(index >= 0 && index < m_choices.size(), return GerritServer());
    return m_choices.at(index).server;
}

QString GerritRemoteChooserAspect::currentRemoteName() const
{
    const int index = remote.value();
    QTC_ASSERT(index >= 0 && index < m_choices.size(), return QString());
    return m_choices.at(index).name;
}












#ifdef WITH_TESTS

// A server as fillFromRemote() leaves one: with a user. Two servers with *no*
// user never compare equal - GerritUser::isSameAs() answers false when it has
// nothing to compare - so a fixture without one cannot exercise the dedup at
// all.
static GerritServer serverOn(const QString &host, const QString &user = "someone")
{
    GerritServer server;
    server.host = host;
    server.user.userName = user;
    return server;
}

class GerritRemoteChooserTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhatEachRemoteIsCalled()
    {
        // The reader sees the host and which remote it came from, because two
        // remotes can point at the same host.
        const QList<RemoteChoice> choices
            = remoteChoices({{"origin", serverOn("codereview.example.com")}}, false);
        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices.first().displayName, QString("codereview.example.com (origin)"));
        QCOMPARE(choices.first().name, QString("origin"));
    }

    void testTwoServersAreTheSameOnlyWhenTheyKnowWho()
    {
        // The rule the listing below depends on, and it is not host equality:
        // a server with no user is not the same as anything, including another
        // server with no user.
        GerritServer anonymous;
        anonymous.host = "codereview.example.com";
        GerritServer alsoAnonymous;
        alsoAnonymous.host = "codereview.example.com";
        QVERIFY2(!(anonymous == alsoAnonymous),
                 "two servers with no user compared equal");

        QVERIFY(serverOn("a") == serverOn("a"));
        QVERIFY(!(serverOn("a") == serverOn("b")));
        QVERIFY2(!(serverOn("a", "one") == serverOn("a", "two")),
                 "the same host under two accounts was taken to be one server");
    }

    void testTwoRemotesOnTheSameServerAreListedOnce()
    {
        // A fetch and a push remote resolve to the same server, and the
        // fallback usually repeats one of them.
        const GerritServer same = serverOn("codereview.example.com");
        const QList<QPair<QString, GerritServer>> remotes
            = {{"origin", same}, {"gerrit", same}, {"other", serverOn("elsewhere.example.com")}};

        const QList<RemoteChoice> collapsed = remoteChoices(remotes, false);
        QCOMPARE(collapsed.size(), 2);
        QCOMPARE(collapsed.at(0).name, QString("origin"));
        QCOMPARE(collapsed.at(1).name, QString("other"));

        // Unless the caller wants every remote listed, which the push dialog
        // does.
        QCOMPARE(remoteChoices(remotes, true).size(), 3);
    }

    void testWhichRemoteTheChooserOpensOn()
    {
        // Nothing to choose between.
        QCOMPARE(defaultRemoteIndex({}), -1);

        // The remote actually called "gerrit" is the one to review against,
        // wherever it is in the list.
        const QList<RemoteChoice> choices
            = remoteChoices({{"origin", serverOn("a")}, {"gerrit", serverOn("b")},
                             {"upstream", serverOn("c")}}, true);
        QCOMPARE(defaultRemoteIndex(choices), 1);

        // With no such remote, the first one.
        const QList<RemoteChoice> none
            = remoteChoices({{"origin", serverOn("a")}, {"upstream", serverOn("c")}}, true);
        QCOMPARE(defaultRemoteIndex(none), 0);
    }

    void testTheChooserOffersWhatItWasGiven()
    {
        GerritRemoteChooserAspect chooser;
        QVERIFY(chooser.isEmpty());

        chooser.setChoices(remoteChoices({{"origin", serverOn("a")},
                                          {"gerrit", serverOn("b")}}, true));
        QVERIFY(!chooser.isEmpty());
        QCOMPARE(chooser.remote.optionCount(), 2);

        // It opens on the gerrit remote, and answers with that server.
        QCOMPARE(chooser.currentRemoteName(), QString("gerrit"));
        QCOMPARE(chooser.currentServer().host, QString("b"));

        // And a caller can ask for one by name.
        QVERIFY(chooser.setCurrentRemote("origin"));
        QCOMPARE(chooser.currentRemoteName(), QString("origin"));
        QVERIFY2(!chooser.setCurrentRemote("nosuchremote"),
                 "a remote that is not there was chosen anyway");
        QCOMPARE(chooser.currentRemoteName(), QString("origin"));
    }

    void testNothingToChooseBetweenIsNothingToChoose()
    {
        GerritRemoteChooserAspect chooser;
        chooser.setChoices(remoteChoices({{"origin", serverOn("a")}}, true));
        QVERIFY2(!chooser.remote.isEnabled(),
                 "a chooser with one entry offered a choice");

        chooser.setChoices(remoteChoices({{"origin", serverOn("a")},
                                          {"gerrit", serverOn("b")}}, true));
        QVERIFY(chooser.remote.isEnabled());
    }

    void testTheChooserSaysWhenTheRemoteChanged()
    {
        GerritRemoteChooserAspect chooser;
        QSignalSpy changed(&chooser, &GerritRemoteChooserAspect::remoteChanged);

        // Filling the list is not the reader changing it, but the list having
        // arrived is worth saying once - the dialogs refresh on it.
        chooser.setChoices({});
        QCOMPARE(changed.count(), 0);

        chooser.setChoices(remoteChoices({{"origin", serverOn("a")},
                                          {"gerrit", serverOn("b")}}, true));
        QCOMPARE(changed.count(), 1);

        // Picking another one says so.
        chooser.remote.setValue(0);
        QTRY_COMPARE(changed.count(), 2);
    }

    void testTheChooserAndItsRefreshAreOneRow()
    {
        // The widget put the combo box and the reset button in one
        // QHBoxLayout; an inline container is what that is here.
        GerritRemoteChooserAspect chooser;
        QVERIFY2(chooser.presentation().inlineRow,
                 "the chooser would be drawn as a group of its own");
    }
};

QObject *createGerritRemoteChooserTest()
{
    return new GerritRemoteChooserTest;
}

#endif // WITH_TESTS

} // namespace Gerrit::Internal

#ifdef WITH_TESTS
#include "gerritremotechooser.moc"
#endif
