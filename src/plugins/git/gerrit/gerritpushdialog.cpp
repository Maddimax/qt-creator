// Copyright (C) 2016 Petar Perisin.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritpushdialog.h"

#include "gerritremotechooser.h"

#include "../gitclient.h"
#include "../gitconstants.h"
#include "../gittr.h"
#include "../logchangedialog.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/icon.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDateTime>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStandardItem>
#include <QVBoxLayout>

using namespace Git::Internal;
using namespace Utils;

namespace Gerrit::Internal {

static const int ReasonableDistance = 100;

class GerritPushSettings final : public AspectContainer
{
public:
    explicit GerritPushSettings(QAbstractItemModel *model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/GerritPushDialog.qml"));

        localBranch.setQmlName("LocalBranch");
        localBranch.setLabelText(::Git::Tr::tr("Push:"));
        localBranch.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        remote.setQmlName("Remote");
        remote.remote.setLabelText(::Git::Tr::tr("To:"));

        targetBranch.setQmlName("TargetBranch");
        targetBranch.setLabelText(::Git::Tr::tr("Target branch:"));
        targetBranch.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        commits.setQmlName("Commits");
        commits.setLabelText(::Git::Tr::tr("Commits:"));
        commits.setModel(model);
        commits.setToolTip(
            ::Git::Tr::tr("Pushes the selected commit and all commits it depends on."));

        info.setQmlName("Info");
        info.setText(::Git::Tr::tr("Number of commits"));

        topic.setQmlName("Topic");
        topic.setLabelText(::Git::Tr::tr("&Topic:"));
        topic.setDisplayStyle(StringAspect::LineEditDisplay);

        draft.setQmlName("Draft");
        draft.setLabelText(::Git::Tr::tr("&Draft/private"));
        draft.setUseCheckBox(true);
        draft.setToolTip(::Git::Tr::tr("Checked - Mark change as private.\n"
                                       "Unchecked - Remove mark.\n"
                                       "Partially checked - Do not change current state."));
        draft.setValue(TriState::Default);

        wip.setQmlName("Wip");
        wip.setLabelText(::Git::Tr::tr("&Work-in-progress"));
        wip.setUseCheckBox(true);
        wip.setToolTip(::Git::Tr::tr("Checked - Mark change as WIP.\n"
                                     "Unchecked - Mark change as ready for review.\n"
                                     "Partially checked - Do not change current state."));
        wip.setValue(TriState::Default);

        reviewers.setQmlName("Reviewers");
        reviewers.setLabelText(::Git::Tr::tr("&Reviewers:"));
        reviewers.setDisplayStyle(StringAspect::LineEditDisplay);
        reviewers.setToolTip(::Git::Tr::tr("Comma-separated list of reviewers.\n"
            "\n"
            "Reviewers can be specified by nickname or email address. Spaces not allowed.\n"
            "\n"
            "Partial names can be used if they are unambiguous."));
    }

    SelectionAspect localBranch{this};
    GerritRemoteChooserAspect remote{this};
    SelectionAspect targetBranch{this};
    TableAspect commits{this};
    TextDisplay info{this};
    StringAspect topic{this};
    TriStateAspect draft{this};
    TriStateAspect wip{this};
    StringAspect reviewers{this};
};


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
    if (options.draft == TriState::Enabled)
        extras << "private";
    else if (options.draft == TriState::Disabled)
        extras << "remove-private";

    if (options.workInProgress == TriState::Enabled)
        extras << "wip";
    else if (options.workInProgress == TriState::Disabled)
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
    const QStandardItem *const last = m_model->item(m_model->rowCount() - 1, 0);
    const QString earliestCommit = last ? last->text() : QString();
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
    m_settings->remote.updateRemotes(false);
}

GerritPushDialog::GerritPushDialog(const Utils::FilePath &workingDir,
                                   const QString &reviewerList,
                                   QWidget *parent)
    : QDialog(parent)
    , m_model(new LogChangeModel(this))
    , m_settings(new GerritPushSettings(m_model))
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this))
    , m_workingDir(workingDir)
{
    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Whatever is picked is what gets pushed, so the picked rows are marked.
    m_model->setMarks(LogRowMarks::IconOnSelected);
    m_model->setMarkIcon(Utils::Icons::PLUS.icon());
    connect(&m_settings->commits, &TableAspect::chosenChanged, this, [this] {
        m_model->setChosen(m_settings->commits.currentRow(),
                           m_settings->commits.selectedRows());
    });

    m_settings->remote.setRepository(workingDir);
    m_settings->remote.setAllowDups(true);

    initRemoteBranches();

    if (m_settings->remote.isEmpty()) {
        m_initErrorMessage = Git::Tr::tr("Cannot find a Gerrit remote. Add one and try again.");
        return;
    }

    const QString current = gitClient().synchronousCurrentLocalBranch(workingDir);
    QString output;
    gitClient().synchronousForEachRefCmd(workingDir, {"--format=%(refname)", "refs/heads/"},
                                         &output);
    m_settings->localBranch.clearOptions();
    for (const QString &branch : localBranchChoices(output, current))
        m_settings->localBranch.addOption(branch);
    m_settings->localBranch.setValue(qMax(0, m_settings->localBranch.indexForDisplay(current)));

    connect(&m_settings->localBranch, &BaseAspect::changed,
            this, &GerritPushDialog::updateCommits);
    connect(&m_settings->targetBranch, &BaseAspect::changed, this, [this] {
        setChangeRange();
        validate();
    });

    updateCommits();
    onRemoteChanged();

    m_settings->reviewers.setValue(reviewerList);
    m_settings->wip.setValue(TriState::Default);

    connect(&m_settings->remote, &GerritRemoteChooserAspect::remoteChanged,
            this, [this] { onRemoteChanged(); });

    resize(740, 410);
}

GerritPushDialog::~GerritPushDialog() = default;

QString GerritPushDialog::currentLocalBranch() const
{
    return m_settings->localBranch.stringValue();
}

QString GerritPushDialog::selectedCommit() const
{
    const QStandardItem *const item = m_model->item(m_settings->commits.currentRow(), 0);
    return item ? item->text() : QString();
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
    // The sentinel entry is not a branch: picking it asks for the ones that
    // were left out.
    if (m_settings->targetBranch.stringValue() == includeOlderBranchesText()) {
        setRemoteBranches(true);
        return;
    }
    const QString remoteBranchName = selectedRemoteBranchName();
    if (remoteBranchName.isEmpty())
        return;
    const QString branch = currentLocalBranch();
    const QString range = calculateChangeRange(branch);
    if (range.isEmpty()) {
        m_settings->info.setVisible(false);
        return;
    }
    m_settings->info.setVisible(true);
    const QString remote = selectedRemoteName() + '/' + remoteBranchName;
    QString labelText =
        Git::Tr::tr("Number of commits between %1 and %2: %3").arg(branch, remote, range);
    // Far more commits than a review usually holds normally means the wrong
    // target branch, so the line says so rather than only turning red.
    if (range.toInt() > ReasonableDistance) {
        m_settings->info.setIconType(Utils::InfoType::Warning);
        labelText.append("\n" + Git::Tr::tr("Are you sure you selected the right target branch?"));
    } else {
        m_settings->info.setIconType(Utils::InfoType::None);
    }
    m_settings->info.setText(labelText);
}

void GerritPushDialog::onRemoteChanged()
{
    setRemoteBranches();
    updateCommits();
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
                                         m_settings->draft(), m_settings->wip()});
}

void GerritPushDialog::storeTopic()
{
    const QString branch = currentLocalBranch();
    gitClient().setConfigValue(
                m_workingDir, QString("branch.%1.topic").arg(branch), selectedTopic());
}

void GerritPushDialog::setRemoteBranches(bool includeOld)
{
    const QString remoteName = selectedRemoteName();
    if (!m_remoteBranches.contains(remoteName)) {
        const QStringList remoteBranches =
                gitClient().synchronousRepositoryBranches(remoteName, m_workingDir);
        for (const QString &branch : remoteBranches)
            m_remoteBranches.insert(remoteName, {branch, {}});
        if (remoteBranches.isEmpty()) {
            m_settings->targetBranch.setToolTip(
                Git::Tr::tr("No remote branches found. This is probably the initial commit."));
        }
    }

    const QStringList choices = targetBranchChoices(m_remoteBranches.values(remoteName),
                                                    m_suggestedRemoteBranch, includeOld,
                                                    QDate::currentDate());
    m_settings->targetBranch.clearOptions();
    for (const QString &choice : choices)
        m_settings->targetBranch.addOption(choice);
    m_settings->targetBranch.setValue(
        qMax(0, m_settings->targetBranch.indexForDisplay(m_suggestedRemoteBranch)));

    setChangeRange();
    validate();
}

void GerritPushDialog::updateCommits()
{
    const QString branch = currentLocalBranch();
    const Result<int> selected = m_model->populate(m_workingDir, branch,
                                                   LogChangeWidget::Silent,
                                                   selectedRemoteName(), {});
    m_hasLocalCommits = selected && m_model->rowCount() > 0;
    if (m_hasLocalCommits)
        m_settings->commits.showRow(*selected);

    const QString topic = gitClient().readConfigValue(
                m_workingDir, QString("branch.%1.topic").arg(branch));
    if (!topic.isEmpty())
        m_settings->topic.setValue(topic);

    const QString remoteBranch = determineRemoteBranch(branch);
    if (!remoteBranch.isEmpty()) {
        const int slash = remoteBranch.indexOf('/');
        m_suggestedRemoteBranch = remoteBranch.mid(slash + 1);
        if (!m_settings->remote.setCurrentRemote(remoteBranch.left(slash)))
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
    return m_settings->remote.currentRemoteName();
}

QString GerritPushDialog::selectedRemoteBranchName() const
{
    const QString branch = m_settings->targetBranch.stringValue();
    // The sentinel is not somewhere to push to.
    return branch == includeOlderBranchesText() ? QString() : branch;
}

QString GerritPushDialog::selectedTopic() const
{
    return m_settings->topic().trimmed();
}

QString GerritPushDialog::reviewers() const
{
    return m_settings->reviewers();
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

        options.draft = TriState::Enabled;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%private"));
        options.draft = TriState::Disabled;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%remove-private"));

        options.draft = TriState::Default;
        options.workInProgress = TriState::Enabled;
        QCOMPARE(pushTarget(options), QString("HEAD:refs/for/master%wip"));
        options.workInProgress = TriState::Disabled;
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
        options.draft = TriState::Enabled;
        options.workInProgress = TriState::Enabled;
        options.reviewers = "alice";
        QCOMPARE(pushTarget(options),
                 QString("HEAD:refs/for/master%private,wip,topic=my-topic,r=alice"));
    }

    void testTheDialogDrawsWithTheQmlItNames()
    {
        LogChangeModel model;
        GerritPushSettings settings(&model);
        const Result<> rendered = Core::aspectFormRenders(&settings, "GerritPushDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheBoxesStartWhereTheyChangeNothing()
    {
        // Both are three-state and both start in the middle, so opening the
        // dialog and pressing Ok leaves the review as it was.
        LogChangeModel model;
        GerritPushSettings settings(&model);
        QCOMPARE(settings.draft(), TriState::Default);
        QCOMPARE(settings.wip(), TriState::Default);

        // And they are drawn as check boxes, not as a combo of three names.
        QCOMPARE(settings.draft.presentation().control, AspectControls::TriStateCheckBox);
        QCOMPARE(settings.wip.presentation().control, AspectControls::TriStateCheckBox);
    }

    void testTheDrawnTableHandsBackWhatWasPicked()
    {
        // The three lines in the .qml that carry the picked commits.
        LogChangeModel model;
        for (int row = 0; row < 3; ++row) {
            model.appendRow({new QStandardItem(QString("hash%1").arg(row)),
                             new QStandardItem("subject")});
        }
        GerritPushSettings settings(&model);

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("commitTable"));

        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));
        QTRY_COMPARE(settings.commits.currentRow(), 1);

        settings.commits.showRow(0);
        QTRY_COMPARE(table->property("currentRow").toInt(), 0);
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
