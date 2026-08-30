// Copyright (C) 2016 Petar Perisin.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritpushdialog.h"

#include "branchcombobox.h"
#include "gerritremotechooser.h"

#include "../gitclient.h"
#include "../gitconstants.h"
#include "../gittr.h"
#include "../logchangedialog.h"

#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/theme/theme.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>

using namespace Git::Internal;

namespace Gerrit::Internal {

static const int ReasonableDistance = 100;

QString includeOlderBranchesText()
{
    return ::Git::Tr::tr("... Include older branches ...");
}

QString pushTarget(const PushOptions &options)
{
    QStringList extras;
    QString target = options.commit.isEmpty() ? QString("HEAD") : options.commit;
    target += ":refs/for";

    // Three states, and the middle one says nothing: leave the change as it is
    // rather than making it private or taking that back.
    if (options.draft == Qt::Checked)
        extras << "private";
    else if (options.draft == Qt::Unchecked)
        extras << "remove-private";

    if (options.workInProgress == Qt::Checked)
        extras << "wip";
    else if (options.workInProgress == Qt::Unchecked)
        extras << "ready";

    target += '/' + options.remoteBranch;
    if (!options.topic.isEmpty())
        extras << "topic=" + options.topic;

    const QStringList reviewers = options.reviewers.split(',', Qt::SkipEmptyParts);
    for (const QString &reviewer : reviewers)
        extras << "r=" + reviewer;

    if (!extras.isEmpty())
        target += '%' + extras.join(',');
    return target;
}

QStringList localBranchChoices(const QString &output, const QString &currentBranch)
{
    QStringList branches;
    if (currentBranch.isEmpty())
        branches << "HEAD";

    const QString branchPrefix("refs/heads/");
    const QStringList refs = output.trimmed().split('\n');
    for (const QString &ref : refs) {
        if (ref.startsWith(branchPrefix))
            branches << ref.mid(branchPrefix.size());
    }
    return branches;
}

RemoteBranchesMap parseRemoteBranches(const QString &output)
{
    RemoteBranchesMap remoteBranches;
    const QString head = "/HEAD";
    const QString remotesPrefix("refs/remotes/");

    const QStringList refs = output.split('\n');
    for (const QString &reference : refs) {
        const QStringList entries = reference.split('\t');
        // The symbolic HEAD of a remote is not a branch to push to.
        if (entries.count() < 2 || entries.first().endsWith(head))
            continue;
        const QString ref = entries.at(0).mid(remotesPrefix.size());
        const int refBranchIndex = ref.indexOf('/');
        const qint64 timeT = entries.at(1).left(entries.at(1).indexOf(' ')).toLongLong();
        remoteBranches.insert(ref.left(refBranchIndex),
                              {ref.mid(refBranchIndex + 1),
                               QDateTime::fromSecsSinceEpoch(timeT).date()});
    }
    return remoteBranches;
}

QStringList targetBranchChoices(const QList<BranchDate> &branches, const QString &suggested,
                                bool includeOld, const QDate &today)
{
    QStringList choices;
    bool excluded = false;
    for (const BranchDate &bd : branches) {
        const bool isSuggested = bd.first == suggested;
        // A branch nobody has touched in a long time is probably not where
        // this change goes - unless it is the one being suggested.
        const bool stale = bd.second.isValid()
                           && bd.second.daysTo(today) > Git::Constants::OBSOLETE_COMMIT_AGE_IN_DAYS;
        if (includeOld || isSuggested || !stale)
            choices << bd.first;
        else
            excluded = true;
    }
    if (excluded)
        choices << includeOlderBranchesText();
    return choices;
}


QString GerritPushDialog::determineRemoteBranch(const QString &localBranch)
{
    const QString earliestCommit = m_commitView->earliestCommit();
    if (earliestCommit.isEmpty())
        return {};

    QString output;
    QString error;

    if (!gitClient().synchronousBranchCmd(
                m_workingDir, {"-r", "--contains", earliestCommit + '^'}, &output, &error)) {
        return {};
    }
    const QString head = "/HEAD";
    const QStringList refs = output.split('\n');

    QString remoteTrackingBranch;
    if (localBranch != "HEAD")
        remoteTrackingBranch = gitClient().synchronousTrackingBranch(m_workingDir, localBranch);

    QString remoteBranch;
    for (const QString &reference : refs) {
        const QString ref = reference.trimmed();
        if (ref.contains(head) || ref.isEmpty())
            continue;

        if (remoteBranch.isEmpty())
            remoteBranch = ref;

        // Prefer remote tracking branch if it exists and contains the latest remote commit
        if (ref == remoteTrackingBranch)
            return ref;
    }
    return remoteBranch;
}

void GerritPushDialog::initRemoteBranches()
{
    QString output;
    const QString remotesPrefix("refs/remotes/");
    if (!gitClient().synchronousForEachRefCmd(
                m_workingDir, {"--format=%(refname)\t%(committerdate:raw)", remotesPrefix}, &output)) {
        return;
    }

    m_remoteBranches = parseRemoteBranches(output);
    m_remoteComboBox->updateRemotes(false);
}

GerritPushDialog::GerritPushDialog(const Utils::FilePath &workingDir,
                                   const QString &reviewerList,
                                   QWidget *parent)
    : QDialog(parent)
    , m_localBranchComboBox(new BranchComboBox)
    , m_remoteComboBox(new GerritRemoteChooser)
    , m_targetBranchComboBox(new QComboBox)
    , m_commitView(new LogChangeWidget)
    , m_infoLabel(new QLabel(::Git::Tr::tr("Number of commits")))
    , m_topicLineEdit(new QLineEdit)
    , m_draftCheckBox(new QCheckBox(::Git::Tr::tr("&Draft/private")))
    , m_wipCheckBox(new QCheckBox(::Git::Tr::tr("&Work-in-progress")))
    , m_reviewersLineEdit(new QLineEdit)
    , m_buttonBox(new QDialogButtonBox)
    , m_workingDir(workingDir)
{
    m_draftCheckBox->setToolTip(::Git::Tr::tr("Checked - Mark change as private.\n"
                                              "Unchecked - Remove mark.\n"
                                              "Partially checked - Do not change current state."));
    m_draftCheckBox->setTristate(true);
    m_draftCheckBox->setCheckState(Qt::PartiallyChecked);
    m_wipCheckBox->setToolTip(::Git::Tr::tr("Checked - Mark change as WIP.\n"
                                            "Unchecked - Mark change as ready for review.\n"
                                            "Partially checked - Do not change current state."));
    m_commitView->setToolTip(::Git::Tr::tr(
            "Pushes the selected commit and all commits it depends on."));
    m_reviewersLineEdit->setToolTip(::Git::Tr::tr("Comma-separated list of reviewers.\n"
            "\n"
            "Reviewers can be specified by nickname or email address. Spaces not allowed.\n"
            "\n"
            "Partial names can be used if they are unambiguous."));
    m_wipCheckBox->setTristate(true);
    m_buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    using namespace Layouting;

    Grid {
        ::Git::Tr::tr("Push:"), workingDir.toUserOutput(), m_localBranchComboBox, br,
        ::Git::Tr::tr("To:"), m_remoteComboBox, m_targetBranchComboBox, br,
        ::Git::Tr::tr("Commits:"), br,
        Span(3, m_commitView), br,
        Span(3, m_infoLabel), br,
        Span(3, Form {
            ::Git::Tr::tr("&Topic:"), Row { m_topicLineEdit, m_draftCheckBox, m_wipCheckBox }, br,
            ::Git::Tr::tr("&Reviewers:"), m_reviewersLineEdit, br
        }), br,
        Span(3, m_buttonBox)
    }.attachTo(this);

    m_remoteComboBox->setRepository(workingDir);
    m_remoteComboBox->setAllowDups(true);

    // Whatever is picked is what gets pushed, so the picked rows are marked.
    m_commitView->setMarks(LogRowMarks::IconOnSelected);

    initRemoteBranches();

    if (m_remoteComboBox->isEmpty()) {
        m_initErrorMessage = Git::Tr::tr("Cannot find a Gerrit remote. Add one and try again.");
        return;
    }

    m_localBranchComboBox->init(workingDir);
    connect(m_localBranchComboBox, &QComboBox::currentIndexChanged,
            this, &GerritPushDialog::updateCommits);
    connect(m_targetBranchComboBox, &QComboBox::currentIndexChanged,
            this, &GerritPushDialog::setChangeRange);
    connect(m_targetBranchComboBox, &QComboBox::currentTextChanged,
            this, &GerritPushDialog::validate);

    updateCommits(m_localBranchComboBox->currentIndex());
    onRemoteChanged();

    QRegularExpressionValidator *noSpaceValidator = new QRegularExpressionValidator(QRegularExpression("^\\S+$"), this);
    m_reviewersLineEdit->setText(reviewerList);
    m_reviewersLineEdit->setValidator(noSpaceValidator);
    m_topicLineEdit->setValidator(noSpaceValidator);
    m_wipCheckBox->setCheckState(Qt::PartiallyChecked);

    connect(m_remoteComboBox, &GerritRemoteChooser::remoteChanged,
            this, [this] { onRemoteChanged(); });

    resize(740, 410);
}

QString GerritPushDialog::selectedCommit() const
{
    return m_commitView->commit();
}

QString GerritPushDialog::calculateChangeRange(const QString &branch)
{
    const QString remote = selectedRemoteName() + '/' + selectedRemoteBranchName();
    QString number;
    QString error;
    gitClient().synchronousRevListCmd(
                m_workingDir, { remote + ".." + branch, "--count" }, &number, &error);
    number.chop(1);
    return number;
}

void GerritPushDialog::setChangeRange()
{
    if (m_targetBranchComboBox->itemData(m_targetBranchComboBox->currentIndex()) == 1) {
        setRemoteBranches(true);
        return;
    }
    const QString remoteBranchName = selectedRemoteBranchName();
    if (remoteBranchName.isEmpty())
        return;
    const QString branch = m_localBranchComboBox->currentText();
    const QString range = calculateChangeRange(branch);
    if (range.isEmpty()) {
        m_infoLabel->hide();
        return;
    }
    m_infoLabel->show();
    const QString remote = selectedRemoteName() + '/' + remoteBranchName;
    QString labelText =
        Git::Tr::tr("Number of commits between %1 and %2: %3").arg(branch, remote, range);
    const int currentRange = range.toInt();
    QPalette palette = QApplication::palette();
    if (currentRange > ReasonableDistance) {
        const QColor errorColor = Utils::creatorColor(Utils::Theme::TextColorError);
        palette.setColor(QPalette::WindowText, errorColor);
        palette.setColor(QPalette::ButtonText, errorColor);
        labelText.append("\n" + Git::Tr::tr("Are you sure you selected the right target branch?"));
    }
    m_infoLabel->setPalette(palette);
    m_targetBranchComboBox->setPalette(palette);
    m_infoLabel->setText(labelText);
}

void GerritPushDialog::onRemoteChanged()
{
    setRemoteBranches();
    const QString remote = m_remoteComboBox->currentRemoteName();

    m_commitView->setExcludedRemote(remote);
    const QString branch = m_localBranchComboBox->itemText(m_localBranchComboBox->currentIndex());
    m_hasLocalCommits = m_commitView->init(m_workingDir, branch, LogChangeWidget::Silent);
    validate();
}

QString GerritPushDialog::initErrorMessage() const
{
    return m_initErrorMessage;
}

QString GerritPushDialog::pushTarget() const
{
    return Gerrit::Internal::pushTarget({selectedCommit(), selectedRemoteBranchName(),
                                         selectedTopic(), reviewers(),
                                         m_draftCheckBox->checkState(),
                                         m_wipCheckBox->checkState()});
}

void GerritPushDialog::storeTopic()
{
    const QString branch = m_localBranchComboBox->currentText();
    gitClient().setConfigValue(
                m_workingDir, QString("branch.%1.topic").arg(branch), selectedTopic());
}

void GerritPushDialog::setRemoteBranches(bool includeOld)
{
    {
        QSignalBlocker blocker(m_targetBranchComboBox);
        m_targetBranchComboBox->clear();

        const QString remoteName = selectedRemoteName();
        if (!m_remoteBranches.contains(remoteName)) {
            const QStringList remoteBranches =
                    gitClient().synchronousRepositoryBranches(remoteName, m_workingDir);
            for (const QString &branch : remoteBranches)
                m_remoteBranches.insert(remoteName, {branch, {}});
            if (remoteBranches.isEmpty()) {
                m_targetBranchComboBox->setEditable(true);
                m_targetBranchComboBox->setToolTip(
                    Git::Tr::tr("No remote branches found. This is probably the initial commit."));
                if (QLineEdit *lineEdit = m_targetBranchComboBox->lineEdit())
                    lineEdit->setPlaceholderText(Git::Tr::tr("Branch name"));
            }
        }

        const QStringList choices = targetBranchChoices(m_remoteBranches.values(remoteName),
                                                       m_suggestedRemoteBranch, includeOld,
                                                       QDate::currentDate());
        for (const QString &choice : choices) {
            // The sentinel is not a branch: it carries the marker the change
            // range check looks for.
            if (choice == includeOlderBranchesText())
                m_targetBranchComboBox->addItem(choice, 1);
            else
                m_targetBranchComboBox->addItem(choice);
        }
        const int suggested = m_targetBranchComboBox->findText(m_suggestedRemoteBranch);
        if (suggested != -1)
            m_targetBranchComboBox->setCurrentIndex(suggested);
        setChangeRange();
    }
    validate();
}

void GerritPushDialog::updateCommits(int index)
{
    const QString branch = m_localBranchComboBox->itemText(index);
    m_hasLocalCommits = m_commitView->init(m_workingDir, branch, LogChangeWidget::Silent);
    const QString topic = gitClient().readConfigValue(
                m_workingDir, QString("branch.%1.topic").arg(branch));
    if (!topic.isEmpty())
        m_topicLineEdit->setText(topic);

    const QString remoteBranch = determineRemoteBranch(branch);
    if (!remoteBranch.isEmpty()) {
        const int slash = remoteBranch.indexOf('/');

        m_suggestedRemoteBranch = remoteBranch.mid(slash + 1);
        const QString remote = remoteBranch.left(slash);

        if (!m_remoteComboBox->setCurrentRemote(remote))
            onRemoteChanged();
    }
    validate();
}

void GerritPushDialog::validate()
{
    const bool valid = m_hasLocalCommits && !selectedRemoteBranchName().isEmpty();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

QString GerritPushDialog::selectedRemoteName() const
{
    return m_remoteComboBox->currentRemoteName();
}

QString GerritPushDialog::selectedRemoteBranchName() const
{
    return m_targetBranchComboBox->currentText();
}

QString GerritPushDialog::selectedTopic() const
{
    return m_topicLineEdit->text().trimmed();
}

QString GerritPushDialog::reviewers() const
{
    return m_reviewersLineEdit->text();
}

#ifdef WITH_TESTS

class GerritPushDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhatIsPushed()
    {
        // The refspec is the whole output of this dialog; everything else is
        // the form collecting it.
        PushOptions options;
        options.remoteBranch = "master";
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master"));

        // A chosen commit is pushed instead of HEAD, with everything it
        // depends on.
        options.commit = "abc1234";
        QCOMPARE(pushTarget(options), QString("abc1234:refs/for/master"));
    }

    void testTheMiddleStateOfABoxSaysNothing()
    {
        // Three states: mark it, unmark it, or leave it as it is. The last is
        // the default, and has to add nothing at all - otherwise opening the
        // dialog and pressing Ok would change the review.
        PushOptions options;
        options.remoteBranch = "master";
        QVERIFY2(!pushTarget(options).contains('%'),
                 qPrintable(pushTarget(options)));

        options.draft = Qt::Checked;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%private"));
        options.draft = Qt::Unchecked;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%remove-private"));

        options.draft = Qt::PartiallyChecked;
        options.workInProgress = Qt::Checked;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%wip"));
        options.workInProgress = Qt::Unchecked;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%ready"));
    }

    void testTheTopicAndTheReviewers()
    {
        PushOptions options;
        options.remoteBranch = "master";
        options.topic = "my-topic";
        options.reviewers = "alice,bob";
        QCOMPARE(pushTarget(options),
                 QString("HEAD:refs/for/master%topic=my-topic,r=alice,r=bob"));

        // An empty reviewer between commas is not a reviewer.
        options.reviewers = "alice,,bob,";
        QCOMPARE(pushTarget(options),
                 QString("HEAD:refs/for/master%topic=my-topic,r=alice,r=bob"));

        // And everything together, in the order Gerrit is given them.
        options.draft = Qt::Checked;
        options.workInProgress = Qt::Checked;
        options.reviewers = "alice";
        QCOMPARE(pushTarget(options),
                 QString("HEAD:refs/for/master%private,wip,topic=my-topic,r=alice"));
    }

    void testWhichLocalBranchesAreOffered()
    {
        const QString output = "refs/heads/master\nrefs/heads/feature/one\n";
        QCOMPARE(localBranchChoices(output, "master"),
                 (QStringList{"master", "feature/one"}));

        // A detached HEAD has no branch name to push from, so it is offered as
        // itself, first.
        QCOMPARE(localBranchChoices(output, {}),
                 (QStringList{"HEAD", "master", "feature/one"}));

        // Nothing but the prefix is nothing to offer.
        QVERIFY(localBranchChoices({}, "master").isEmpty());
    }

    void testWhichRemoteBranchesThereAre()
    {
        // The format is "refname\tcommitterdate:raw", and the raw date is
        // seconds then a timezone.
        const QString output =
            "refs/remotes/origin/master\t1700000000 +0200\n"
            "refs/remotes/origin/old\t1500000000 +0200\n"
            "refs/remotes/gerrit/master\t1700000000 +0200\n";

        const RemoteBranchesMap branches = parseRemoteBranches(output);
        // keys() on a QMultiMap is one entry per value, not per remote.
        QCOMPARE(QSet<QString>(branches.keyBegin(), branches.keyEnd()).size(), 2);
        QCOMPARE(branches.values("origin").size(), 2);
        QCOMPARE(branches.values("gerrit").size(), 1);
        QCOMPARE(branches.values("gerrit").first().first, QString("master"));
        QCOMPARE(branches.values("gerrit").first().second,
                 QDateTime::fromSecsSinceEpoch(1700000000).date());

        // A remote's symbolic HEAD is not a branch to push to.
        QVERIFY(parseRemoteBranches("refs/remotes/origin/HEAD\t1700000000 +0200\n").isEmpty());
        // Nor is a line without a date.
        QVERIFY(parseRemoteBranches("refs/remotes/origin/master\n").isEmpty());
    }

    void testWhichTargetBranchesAreOffered()
    {
        const QDate today(2026, 8, 30);
        const QList<BranchDate> branches = {
            {"master", today.addDays(-1)},
            {"ancient", today.addDays(-Git::Constants::OBSOLETE_COMMIT_AGE_IN_DAYS - 1)},
            {"undated", {}},
        };

        // A branch nobody has touched in a long time is left out, and the
        // sentinel says so.
        const QStringList offered = targetBranchChoices(branches, {}, false, today);
        QCOMPARE(offered, (QStringList{"master", "undated", includeOlderBranchesText()}));

        // Asking for them lists them all, and then there is nothing left to
        // ask for.
        const QStringList all = targetBranchChoices(branches, {}, true, today);
        QCOMPARE(all, (QStringList{"master", "ancient", "undated"}));

        // The branch this change is suggested for is always offered, however
        // old it is - it is where the change actually goes.
        const QStringList suggested = targetBranchChoices(branches, "ancient", false, today);
        QVERIFY2(suggested.contains("ancient"), qPrintable(suggested.join(", ")));
        QVERIFY2(!suggested.contains(includeOlderBranchesText()),
                 "nothing was left out, but the list still offered to include more");
    }
};

QObject *createGerritPushDialogTest()
{
    return new GerritPushDialogTest;
}

#endif // WITH_TESTS

} // Gerrit::Internal

#ifdef WITH_TESTS
#include "gerritpushdialog.moc"
#endif
