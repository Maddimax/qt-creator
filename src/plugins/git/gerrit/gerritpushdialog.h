// Copyright (C) 2016 Petar Perisin.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QDialog>
#include <QMultiMap>
#include <QDate>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

namespace Git::Internal { class LogChangeWidget; }

namespace Gerrit::Internal {

class BranchComboBox;
class GerritRemoteChooser;

// A remote branch and when it was last committed to.
using BranchDate = QPair<QString, QDate>;
using RemoteBranchesMap = QMultiMap<QString, BranchDate>;

// What a push to Gerrit is asked for. Everything the dialog collects ends up
// in the refspec, and the options after '%' are what Gerrit reads.
class PushOptions
{
public:
    QString commit;        // empty means HEAD
    QString remoteBranch;
    QString topic;
    QString reviewers;     // comma separated, as typed
    Qt::CheckState draft = Qt::PartiallyChecked;
    Qt::CheckState workInProgress = Qt::PartiallyChecked;
};

QString pushTarget(const PushOptions &options);

// The local branches in \a output from `for-each-ref refs/heads/`. A detached
// HEAD - which is what an empty \a currentBranch means - is offered as "HEAD"
// ahead of them, because there is no branch name to push from.
QStringList localBranchChoices(const QString &output, const QString &currentBranch);

// The remote branches in \a output from
// `for-each-ref --format=%(refname)\t%(committerdate:raw) refs/remotes/`.
RemoteBranchesMap parseRemoteBranches(const QString &output);

// Which of \a branches the target list offers: the suggested one, the ones
// that are not stale, and - when any were left out - a sentinel asking for
// them. \a today is passed in so the rule can be asked without a calendar.
QStringList targetBranchChoices(const QList<BranchDate> &branches, const QString &suggested,
                                bool includeOld, const QDate &today);

// What the sentinel entry says.
QString includeOlderBranchesText();

#ifdef WITH_TESTS
QObject *createGerritPushDialogTest();
#endif

class GerritPushDialog : public QDialog
{
    Q_OBJECT

public:
    GerritPushDialog(const Utils::FilePath &workingDir, const QString &reviewerList,
                     QWidget *parent);

    QString selectedCommit() const;
    QString selectedRemoteName() const;
    QString selectedRemoteBranchName() const;
    QString selectedTopic() const;
    QString reviewers() const;
    QString initErrorMessage() const;
    QString pushTarget() const;
    void storeTopic();

private:
    void setChangeRange();
    void onRemoteChanged();
    void setRemoteBranches(bool includeOld = false);
    void updateCommits(int index);
    void validate();

    QString determineRemoteBranch(const QString &localBranch);
    void initRemoteBranches();
    QString calculateChangeRange(const QString &branch);

    BranchComboBox *m_localBranchComboBox;
    Gerrit::Internal::GerritRemoteChooser *m_remoteComboBox;
    QComboBox *m_targetBranchComboBox;
    Git::Internal::LogChangeWidget *m_commitView;
    QLabel *m_infoLabel;
    QLineEdit *m_topicLineEdit;
    QCheckBox *m_draftCheckBox;
    QCheckBox *m_wipCheckBox;
    QLineEdit *m_reviewersLineEdit;
    QDialogButtonBox *m_buttonBox;

    Utils::FilePath m_workingDir;
    QString m_suggestedRemoteBranch;
    QString m_initErrorMessage;
    RemoteBranchesMap m_remoteBranches;
    bool m_hasLocalCommits = false;
};

} // Gerrit::Internal
