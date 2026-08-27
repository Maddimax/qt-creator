// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "branchcheckoutdialog.h"

#include "gittr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Git::Internal {

// What to do with local changes when checking a branch out. One choice of
// three, and a stash to pop or not - which only means anything for a branch
// that has one, and not when the changes are being carried across instead.
class CheckoutSettings final : public AspectContainer
{
public:
    enum Action { MakeStash, MoveChanges, DiscardChanges };

    CheckoutSettings(const QString &currentBranch, const QString &nextBranch)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/BranchCheckoutDialog.qml"));

        action.setQmlName("Action");
        action.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        action.setLabelText(Tr::tr("Local Changes Found. Choose Action:"));
        action.addOption(currentBranch.isEmpty()
                             ? Tr::tr("Create Branch Stash for Current Branch")
                             : Tr::tr("Create Branch Stash for \"%1\"").arg(currentBranch));
        action.addOption(Tr::tr("Move Local Changes to \"%1\"").arg(nextBranch));
        action.addOption(Tr::tr("Discard Local Changes"));
        action.setDefaultValue(MakeStash);

        popStash.setQmlName("PopStash");
        popStash.setLabelText(Tr::tr("Pop Stash of \"%1\"").arg(nextBranch));

        action.addOnChanged(this, [this] { followState(); });
        followState();
    }

    // There is nothing to pop unless the next branch has a stash, and nothing
    // to pop *into* if the local changes are being carried across.
    void followState()
    {
        const bool poppable = m_hasStashForNextBranch && action() != MoveChanges;
        popStash.setValue(poppable);
        popStash.setEnabled(poppable);
        // Nothing to choose between when there are no local changes.
        action.setEnabled(m_hasLocalChanges);
    }

    void setHasLocalChanges(bool has)
    {
        m_hasLocalChanges = has;
        if (!has)
            action.setValue(DiscardChanges);
        followState();
    }

    void setHasStashForNextBranch(bool has)
    {
        m_hasStashForNextBranch = has;
        followState();
    }

    bool hasLocalChanges() const { return m_hasLocalChanges; }
    bool hasStashForNextBranch() const { return m_hasStashForNextBranch; }

    // Discarding is only a choice while there is something to discard: with no
    // local changes the option is the one left selected, and means nothing.
    bool discardLocalChanges() const { return action() == DiscardChanges && m_hasLocalChanges; }

    SelectionAspect action{this};
    BoolAspect popStash{this};

private:
    bool m_hasLocalChanges = true;
    bool m_hasStashForNextBranch = false;
};

BranchCheckoutDialog::BranchCheckoutDialog(QWidget *parent, const QString &currentBranch,
                                           const QString &nextBranch)
    : QDialog(parent)
    , m_settings(new CheckoutSettings(currentBranch, nextBranch))
{
    setWindowModality(Qt::WindowModal);
    resize(394, 199);
    setModal(true);
    setWindowTitle(Tr::tr("Checkout branch \"%1\"").arg(nextBranch));

    if (currentBranch.isEmpty())
        foundNoLocalChanges();

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    QPushButton *diffButton = buttonBox->addButton(Tr::tr("&Diff && Cancel"),
                                                   QDialogButtonBox::ActionRole);
    diffButton->setEnabled(m_settings->hasLocalChanges());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(diffButton, &QPushButton::clicked, this, [this] {
        m_diffRequested = true;
        reject();
    });
    // Diffing needs something to diff, and popping a stash gives it something.
    connect(&m_settings->popStash, &BoolAspect::changed, diffButton, [this, diffButton] {
        diffButton->setEnabled(m_settings->hasLocalChanges()
                               || m_settings->hasStashForNextBranch());
    });
}

BranchCheckoutDialog::~BranchCheckoutDialog() = default;

void BranchCheckoutDialog::foundNoLocalChanges()
{
    m_settings->setHasLocalChanges(false);
}

void BranchCheckoutDialog::foundStashForNextBranch()
{
    m_settings->setHasStashForNextBranch(true);
}

bool BranchCheckoutDialog::makeStashOfCurrentBranch() const
{
    return m_settings->action() == CheckoutSettings::MakeStash;
}

bool BranchCheckoutDialog::moveLocalChangesToNextBranch() const
{
    return m_settings->action() == CheckoutSettings::MoveChanges;
}

bool BranchCheckoutDialog::discardLocalChanges() const
{
    return m_settings->discardLocalChanges();
}

bool BranchCheckoutDialog::popStashOfNextBranch() const
{
    return m_settings->popStash();
}

bool BranchCheckoutDialog::hasStashForNextBranch() const
{
    return m_settings->hasStashForNextBranch();
}

bool BranchCheckoutDialog::hasLocalChanges() const
{
    return m_settings->hasLocalChanges();
}

bool BranchCheckoutDialog::diffRequested() const
{
    return m_diffRequested;
}

#ifdef WITH_TESTS

class BranchCheckoutDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testThereIsNothingToPopUntilThereIsAStash()
    {
        CheckoutSettings settings("current", "next");
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "BranchCheckoutDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        QVERIFY2(!settings.popStash.isEnabled(),
                 "a stash was offered for a branch that has none");
        QVERIFY(!settings.popStash());

        settings.setHasStashForNextBranch(true);
        QVERIFY(settings.popStash.isEnabled());
        QVERIFY2(settings.popStash(), "a stash that exists is not popped by default");

        // Carrying the changes across leaves nothing to pop into.
        settings.action.setValue(CheckoutSettings::MoveChanges);
        QVERIFY2(!settings.popStash.isEnabled(),
                 "a stash was offered while the changes are being moved");
        QVERIFY(!settings.popStash());
    }

    void testDiscardingIsOnlyAChoiceWhileThereIsSomethingToDiscard()
    {
        CheckoutSettings settings("current", "next");

        settings.action.setValue(CheckoutSettings::DiscardChanges);
        QVERIFY(settings.discardLocalChanges());

        // With no local changes the discard option is the one left selected,
        // and it means nothing - the caller must not be told to discard.
        settings.setHasLocalChanges(false);
        QCOMPARE(settings.action(), int(CheckoutSettings::DiscardChanges));
        QVERIFY2(!settings.discardLocalChanges(),
                 "the caller was told to discard changes that do not exist");
        QVERIFY2(!settings.action.isEnabled(),
                 "an action was offered when there is nothing to act on");
    }
};

QObject *createBranchCheckoutDialogTest()
{
    return new BranchCheckoutDialogTest;
}

#endif // WITH_TESTS

} // Git::Internal

#ifdef WITH_TESTS
#include "branchcheckoutdialog.moc"
#endif
