// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "branchadddialog.h"

#include "branchmodel.h"
#include "gitplugin.h"
#include "gittr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QRegularExpression>
#include <QVBoxLayout>
#include <QValidator>

namespace Git::Internal {

/*!
 * \brief The BranchNameValidator class validates the corresponding string as
 * a valid Git branch name.
 *
 * The class does this by a couple of rules that are applied on the string.
 *
 */
static QString mustNotEndWithMessage(const QString &suffix)
{
    return Tr::tr("References must not end with \"%1\".").arg(suffix);
}

QString sanitisedReferenceName(const QString &name)
{
    static const QRegularExpression invalidChars(
        '(' + invalidBranchAndRemoteNamePattern() + ")+");
    QString sanitised = name;
    sanitised.replace(invalidChars, "_");
    return sanitised;
}

QString referenceNameIssue(const QString &name, const QStringList &existing)
{
    // Nothing typed yet is not something to complain about.
    if (name.isEmpty())
        return {};

    // Allowed in the middle, not at the end - so these are things the reader
    // is part way through typing, not mistakes.
    if (name.endsWith(".lock"))
        return mustNotEndWithMessage(".lock");
    if (name.endsWith('.'))
        return mustNotEndWithMessage(".");
    if (name.endsWith('/'))
        return mustNotEndWithMessage("/");

    // Whether two names are the same is the file system's opinion, and on
    // Windows it ignores case.
    const Qt::CaseSensitivity sensitivity
        = Utils::HostOsInfo::isWindowsHost() ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (existing.contains(name, sensitivity))
        return Tr::tr("Reference \"%1\" already exists.").arg(name);

    return {};
}

bool isAcceptableReferenceName(const QString &name, const QStringList &existing)
{
    return !name.isEmpty() && referenceNameIssue(name, existing).isEmpty();
}

// What the line edit in the branch tree uses. Kept as a validator because an
// item view's editor is a widget and takes one.
class BranchNameValidator : public QValidator
{
public:
    BranchNameValidator(const QStringList &localBranches, QObject *parent = nullptr)
        : QValidator(parent)
        , m_localBranches(localBranches)
    {}

    State validate(QString &input, int &pos) const override
    {
        Q_UNUSED(pos)
        input = sanitisedReferenceName(input);
        return isAcceptableReferenceName(input, m_localBranches) ? Acceptable : Intermediate;
    }

private:
    QStringList m_localBranches;
};

BranchValidationDelegate::BranchValidationDelegate(QWidget *parent, BranchModel *model)
    : QItemDelegate(parent)
    , m_model(model)
{
}

QWidget *BranchValidationDelegate::createEditor(QWidget *parent,
                                                const QStyleOptionViewItem & /*option*/,
                                                const QModelIndex & /*index*/) const
{
    auto lineEdit = new Utils::FancyLineEdit(parent);
    BranchNameValidator *validator = new BranchNameValidator(m_model->localBranchNames(), lineEdit);
    lineEdit->setValidator(validator);
    return lineEdit;
}

// The four things the dialog draws. Which of them are shown depends on what is
// being named: a tag can be annotated, a branch can be checked out.
class BranchAddSettings final : public Utils::AspectContainer
{
public:
    BranchAddSettings(const QStringList &existing, BranchAddDialog::Type type)
        : m_existing(existing)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/BranchAddDialog.qml"));

        const bool isTag = type == BranchAddDialog::AddTag
                           || type == BranchAddDialog::RenameTag;

        name.setQmlName("Name");
        name.setLabelText(isTag ? Tr::tr("Tag name:") : Tr::tr("Branch Name:"));
        name.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
        // Says what is wrong instead of only colouring the text red, which is
        // what the widget did - the reason was in a tooltip nobody opens.
        name.setValidationFunction([this](const QString &candidate) -> Utils::Result<> {
            const QString issue = referenceNameIssue(candidate, m_existing);
            return issue.isEmpty() ? Utils::ResultOk : Utils::ResultError(issue);
        });

        checkout.setQmlName("Checkout");
        checkout.setLabelText(Tr::tr("Checkout new branch"));
        checkout.setVisible(false);

        tracking.setQmlName("Tracking");
        tracking.setVisible(false);

        annotation.setQmlName("Annotation");
        annotation.setLabelText(Tr::tr("Annotation:"));
        annotation.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
        annotation.setPlaceHolderText(Tr::tr("Annotation (Optional)"));
        // Only a tag carries one.
        annotation.setVisible(type == BranchAddDialog::AddTag);
    }

    bool isAcceptable() const { return isAcceptableReferenceName(name(), m_existing); }

    Utils::StringAspect name{this};
    Utils::BoolAspect checkout{this};
    Utils::BoolAspect tracking{this};
    Utils::StringAspect annotation{this};

private:
    const QStringList m_existing;
};

BranchAddDialog::BranchAddDialog(const QStringList &localBranches, Type type, QWidget *parent)
    : QDialog(parent)
    , m_settings(new BranchAddSettings(localBranches, type))
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this))
{
    resize(590, 138);

    switch (type) {
    case AddBranch: setWindowTitle(Tr::tr("Add Branch")); break;
    case RenameBranch: setWindowTitle(Tr::tr("Rename Branch")); break;
    case AddTag: setWindowTitle(Tr::tr("Add Tag")); break;
    case RenameTag: setWindowTitle(Tr::tr("Rename Tag")); break;
    }

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto refreshOk = [this] {
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_settings->isAcceptable());
    };
    m_settings->name.addOnChanged(this, refreshOk);
    refreshOk();
}

BranchAddDialog::~BranchAddDialog() = default;

void BranchAddDialog::setBranchName(const QString &n)
{
    m_settings->name.setValue(n);
}

QString BranchAddDialog::branchName() const
{
    return m_settings->name();
}

QString BranchAddDialog::annotation() const
{
    return m_settings->annotation();
}

void BranchAddDialog::setTrackedBranchName(const QString &name, bool remote)
{
    m_settings->tracking.setLabelText(name.isEmpty()
                                          ? Tr::tr("Track local branch")
                                          : (remote ? Tr::tr("Track remote branch \"%1\"")
                                                    : Tr::tr("Track local branch \"%1\""))
                                                .arg(name));
    m_settings->tracking.setVisible(!name.isEmpty());
    m_settings->tracking.setValue(!name.isEmpty() && remote);
}

bool BranchAddDialog::track() const
{
    return m_settings->tracking.isVisible() && m_settings->tracking();
}

void BranchAddDialog::setCheckoutVisible(bool visible)
{
    m_settings->checkout.setVisible(visible);
}

bool BranchAddDialog::checkout() const
{
    return m_settings->checkout.isVisible() && m_settings->checkout();
}

#ifdef WITH_TESTS

class BranchAddDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        BranchAddSettings settings({}, BranchAddDialog::AddBranch);
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "BranchAddDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatGitRefusesInAName()
    {
        // Replaced rather than refused, which is what the reader sees happen
        // as they type: a space becomes an underscore.
        QCOMPARE(sanitisedReferenceName("my branch"), QString("my_branch"));
        QCOMPARE(sanitisedReferenceName("fine/name"), QString("fine/name"));
        QCOMPARE(sanitisedReferenceName("two  spaces"), QString("two_spaces"));
        QCOMPARE(sanitisedReferenceName({}), QString());
    }

    void testWhatIsWrongWithAName()
    {
        const QStringList existing{"main", "feature/one"};

        QCOMPARE(referenceNameIssue("feature/two", existing), QString());

        // Allowed in the middle, not at the end - the reader is part way
        // through typing, so these are complaints and not refusals.
        QVERIFY(!referenceNameIssue("wip.lock", existing).isEmpty());
        QVERIFY(!referenceNameIssue("wip.", existing).isEmpty());
        QVERIFY(!referenceNameIssue("wip/", existing).isEmpty());
        QCOMPARE(referenceNameIssue("wip.lock.fine", existing), QString());

        // A name that is taken says so, and says which.
        QVERIFY(referenceNameIssue("main", existing).contains("main"));

        // Nothing typed yet is not something to complain about...
        QCOMPARE(referenceNameIssue({}, existing), QString());
        // ...but it is not a usable name either, and only the second of those
        // decides whether the dialog can be accepted.
        QVERIFY2(!isAcceptableReferenceName({}, existing),
                 "an empty name was accepted because it drew no complaint");
        QVERIFY(isAcceptableReferenceName("feature/two", existing));
        QVERIFY(!isAcceptableReferenceName("main", existing));
    }

    void testWhichFieldsAreShownForWhatIsBeingNamed()
    {
        // A tag can be annotated; a branch cannot.
        BranchAddSettings tag({}, BranchAddDialog::AddTag);
        QVERIFY(tag.annotation.isVisible());

        BranchAddSettings branch({}, BranchAddDialog::AddBranch);
        QVERIFY2(!branch.annotation.isVisible(), "a branch was offered an annotation");

        // Renaming a tag does not annotate it either - the annotation belongs
        // to the tag that is being made.
        BranchAddSettings renamed({}, BranchAddDialog::RenameTag);
        QVERIFY(!renamed.annotation.isVisible());

        // Neither offers to check out or track until something says so.
        QVERIFY(!branch.checkout.isVisible());
        QVERIFY(!branch.tracking.isVisible());
    }
};

QObject *createBranchAddDialogTest()
{
    return new BranchAddDialogTest;
}

#endif // WITH_TESTS

} // Git::Internal

#ifdef WITH_TESTS
#include "branchadddialog.moc"
#endif
