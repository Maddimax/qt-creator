// Copyright (C) 2017 Orgad Shaneh <orgads@gmail.com>.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "gerritserver.h"

#include <utils/aspects.h>
#include <utils/filepath.h>

#include <QComboBox>
#include <QToolButton>
#include <QWidget>

#include <vector>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace Gerrit::Internal {

// One entry of the chooser: the remote's name in git, the server it resolved
// to, and what the reader sees.
class RemoteChoice
{
public:
    QString name;
    GerritServer server;
    QString displayName;
};

// The entries for \a remotes, in order, dropping the ones that resolved to a
// server already listed unless \a allowDups.
QList<RemoteChoice> remoteChoices(
    const QList<QPair<QString, GerritServer>> &remotes, bool allowDups);

// Which entry the chooser opens on: the remote actually called "gerrit" if
// there is one, otherwise the first. -1 when there is nothing to choose.
int defaultRemoteIndex(const QList<RemoteChoice> &choices);

// Which remote to talk to, as a form rather than a widget: a choice and a way
// to ask the repository again. Mirrors GerritRemoteChooser, which it replaces.
class GerritRemoteChooserAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit GerritRemoteChooserAspect(Utils::AspectContainer *container = nullptr);

    void setRepository(const Utils::FilePath &repository);
    void setFallbackEnabled(bool value);
    void setAllowDups(bool value);
    bool setCurrentRemote(const QString &remoteName);

    void updateRemotes(bool forceReload);
    // The entries the chooser is showing, whatever the reader has picked.
    void setChoices(const QList<RemoteChoice> &choices);

    GerritServer currentServer() const;
    QString currentRemoteName() const;
    bool isEmpty() const { return m_choices.isEmpty(); }

    Utils::SelectionAspect remote{this};
    Utils::ActionAspect refresh{this};

signals:
    void remoteChanged();

private:
    Utils::FilePath m_repository;
    QList<RemoteChoice> m_choices;
    bool m_enableFallback = false;
    bool m_allowDups = false;
    bool m_updatingRemotes = false;
};

#ifdef WITH_TESTS
QObject *createGerritRemoteChooserTest();
#endif

class GerritRemoteChooser : public QWidget
{
    Q_OBJECT

public:
    GerritRemoteChooser(QWidget *parent = nullptr);
    void setRepository(const Utils::FilePath &repository);
    void setFallbackEnabled(bool value);
    void setAllowDups(bool value);
    bool setCurrentRemote(const QString &remoteName);

    void updateRemotes(bool forceReload);
    GerritServer currentServer() const;
    QString currentRemoteName() const;
    bool isEmpty() const;

signals:
    void remoteChanged();

private:
    void addRemote(const GerritServer &server, const QString &name);
    void handleRemoteChanged();

    Utils::FilePath m_repository;
    QComboBox *m_remoteComboBox = nullptr;
    QToolButton *m_resetRemoteButton = nullptr;
    bool m_updatingRemotes = false;
    bool m_enableFallback = false;
    bool m_allowDups = false;
    using NameAndServer = std::pair<QString, GerritServer>;
    std::vector<NameAndServer> m_remotes;
};

} // namespace Gerrit::Internal
