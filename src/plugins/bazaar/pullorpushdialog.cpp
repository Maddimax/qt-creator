// Copyright (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pullorpushdialog.h"

#include "bazaartr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/pathvalidation.h>
#include <utils/qtcassert.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Bazaar::Internal {

// Where a pull or push goes, and what to do when it gets there. Which options
// are shown depends on the direction: a pull can be local, a push can create
// the path it needs. That is the dialog's own knowledge and is arranged here
// rather than in the form.
class BranchLocationSettings final : public AspectContainer
{
public:
    enum Location { Default, LocalFilesystem, Url };

    explicit BranchLocationSettings(PullOrPushDialog::Mode mode)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Bazaar/PullOrPushDialog.qml"));

        const QString urlExample =
            Tr::tr("For example: \"https://[user[:pass]@]host[:port]/[path]\".");

        location.setQmlName("Location");
        location.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        location.addOption(Tr::tr("Default location"));
        location.addOption(Tr::tr("Local filesystem:"));
        location.addOption(Tr::tr("Specify URL:"), urlExample);
        location.setDefaultValue(Default);

        localPath.setQmlName("LocalPath");
        localPath.setExpectedKind(PathChooserKind::Directory);

        url.setQmlName("Url");
        url.setDisplayStyle(StringAspect::LineEditDisplay);
        url.setToolTip(urlExample);

        remember.setQmlName("Remember");
        remember.setLabelText(Tr::tr("Remember specified location as default"));

        overwrite.setQmlName("Overwrite");
        overwrite.setLabelText(Tr::tr("Overwrite"));
        overwrite.setToolTip(Tr::tr("Ignores differences between branches and overwrites\n"
                                    "unconditionally."));

        local.setQmlName("Local");
        local.setLabelText(Tr::tr("Local"));
        local.setToolTip(Tr::tr("Performs a local pull in a bound branch.\n"
                                "Local pulls are not applied to the master branch."));

        useExistingDirectory.setQmlName("UseExistingDirectory");
        useExistingDirectory.setLabelText(Tr::tr("Use existing directory"));
        useExistingDirectory.setToolTip(
            Tr::tr("By default, push will fail if the target directory exists, but does not "
                   "already have a control directory.\n"
                   "This flag will allow push to proceed."));

        createPrefix.setQmlName("CreatePrefix");
        createPrefix.setLabelText(Tr::tr("Create prefix"));
        createPrefix.setToolTip(
            Tr::tr("Creates the path leading up to the branch if it does not already exist."));

        revision.setQmlName("Revision");
        revision.setLabelText(Tr::tr("Revision:"));
        revision.setDisplayStyle(StringAspect::LineEditDisplay);

        // Half of the options belong to one direction only.
        local.setVisible(mode == PullOrPushDialog::PullMode);
        useExistingDirectory.setVisible(mode == PullOrPushDialog::PushMode);
        createPrefix.setVisible(mode == PullOrPushDialog::PushMode);

        // A field belongs to the answer above it, and there is nothing to
        // remember about a default location.
        const auto followLocation = [this] {
            localPath.setEnabled(location() == LocalFilesystem);
            url.setEnabled(location() == Url);
            remember.setEnabled(location() != Default);
        };
        location.addOnChanged(this, followLocation);
        followLocation();
    }

    // Empty for the default location, which is how bzr is told to use the one
    // the branch already has.
    QString branchLocation() const
    {
        switch (location()) {
        case LocalFilesystem:
            return localPath().path();
        case Url:
            return url();
        default:
            return {};
        }
    }

    // Nothing to remember about a location that was not given.
    bool rememberLocation() const { return location() != Default && remember(); }

    SelectionAspect location{this};
    FilePathAspect localPath{this};
    StringAspect url{this};
    BoolAspect remember{this};
    BoolAspect overwrite{this};
    BoolAspect local{this};
    BoolAspect useExistingDirectory{this};
    BoolAspect createPrefix{this};
    StringAspect revision{this};
};

PullOrPushDialog::PullOrPushDialog(Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_settings(new BranchLocationSettings(mode))
{
    resize(477, 388);
    setWindowTitle(mode == PullMode ? Tr::tr("Pull Source") : Tr::tr("Push Destination"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    setSizeGripEnabled(true);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PullOrPushDialog::~PullOrPushDialog() = default;

QString PullOrPushDialog::branchLocation() const
{
    return m_settings->branchLocation();
}

bool PullOrPushDialog::isRememberOptionEnabled() const
{
    return m_settings->rememberLocation();
}

bool PullOrPushDialog::isOverwriteOptionEnabled() const
{
    return m_settings->overwrite();
}

QString PullOrPushDialog::revision() const
{
    return m_settings->revision().simplified();
}

bool PullOrPushDialog::isLocalOptionEnabled() const
{
    QTC_ASSERT(m_mode == PullMode, return false);
    return m_settings->local();
}

bool PullOrPushDialog::isUseExistingDirectoryOptionEnabled() const
{
    QTC_ASSERT(m_mode == PushMode, return false);
    return m_settings->useExistingDirectory();
}

bool PullOrPushDialog::isCreatePrefixOptionEnabled() const
{
    QTC_ASSERT(m_mode == PushMode, return false);
    return m_settings->createPrefix();
}

#ifdef WITH_TESTS

class PullOrPushDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDirectionDecidesWhichOptionsAreOffered()
    {
        BranchLocationSettings pulling(PullOrPushDialog::PullMode);
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&pulling, "PullOrPushDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // A pull can be local; only a push creates the path it needs.
        QVERIFY(pulling.local.isVisible());
        QVERIFY(!pulling.useExistingDirectory.isVisible());
        QVERIFY(!pulling.createPrefix.isVisible());

        BranchLocationSettings pushing(PullOrPushDialog::PushMode);
        QVERIFY(!pushing.local.isVisible());
        QVERIFY(pushing.useExistingDirectory.isVisible());
        QVERIFY(pushing.createPrefix.isVisible());
    }

    void testTheAnswerDecidesWhichFieldIsUsed()
    {
        BranchLocationSettings settings(PullOrPushDialog::PullMode);

        QVERIFY2(!settings.localPath.isEnabled(), "a path was editable for the default location");
        QVERIFY2(!settings.url.isEnabled(), "a URL was editable for the default location");
        QVERIFY2(!settings.remember.isEnabled(),
                 "there is nothing to remember about the default location");

        settings.location.setValue(BranchLocationSettings::Url);
        QVERIFY(settings.url.isEnabled());
        QVERIFY(settings.remember.isEnabled());

        settings.url.setValue("https://example.invalid/branch");
        settings.localPath.setValue(FilePath::fromString("/tmp/branch"));
        QCOMPARE(settings.branchLocation(), QString("https://example.invalid/branch"));

        settings.location.setValue(BranchLocationSettings::LocalFilesystem);
        QCOMPARE(settings.branchLocation(), QString("/tmp/branch"));

        settings.remember.setValue(true);
        settings.location.setValue(BranchLocationSettings::Default);
        QCOMPARE(settings.branchLocation(), QString());
        QCOMPARE(settings.rememberLocation(), false);
    }
};

QObject *createPullOrPushDialogTest()
{
    return new PullOrPushDialogTest;
}

#endif // WITH_TESTS

} // Bazaar::Internal

#ifdef WITH_TESTS
#include "pullorpushdialog.moc"
#endif
