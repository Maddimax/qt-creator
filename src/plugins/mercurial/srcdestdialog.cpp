// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "srcdestdialog.h"

#include "authenticationdialog.h"
#include "mercurialtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/pathvalidation.h>

#include <QDialogButtonBox>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;
using namespace VcsBase;

namespace Mercurial::Internal {

// Where a pull or push goes. The same three-way question Fossil's and Bazaar's
// dialogs ask, with the repository hg already knows about shown against the
// default answer.
class SrcDestSettings final : public AspectContainer
{
public:
    enum Location { Default, LocalFilesystem, Url };

    SrcDestSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Mercurial/SrcDestDialog.qml"));

        const QString urlExample =
            Tr::tr("For example: \"https://[user[:pass]@]host[:port]/[path]\".");

        location.setQmlName("Location");
        location.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        location.addOption(Tr::tr("Default Location"));
        location.addOption(Tr::tr("Local filesystem:"));
        location.addOption(Tr::tr("Specify URL:"), urlExample);
        location.setDefaultValue(Default);

        defaultLocation.setQmlName("DefaultLocation");

        promptForCredentials.setQmlName("PromptForCredentials");
        promptForCredentials.setLabelText(Tr::tr("Prompt for credentials"));

        localPath.setQmlName("LocalPath");
        localPath.setExpectedKind(PathChooserKind::ExistingDirectory);
        localPath.setHistoryCompleter("Hg.SourceDir.History");

        url.setQmlName("Url");
        url.setDisplayStyle(StringAspect::LineEditDisplay);
        url.setToolTip(urlExample);

        const auto followLocation = [this] {
            localPath.setEnabled(location() == LocalFilesystem);
            url.setEnabled(location() == Url);
            // Credentials are only asked for about the repository hg already
            // knows; a location typed in here carries its own.
            promptForCredentials.setEnabled(location() == Default);
        };
        location.addOnChanged(this, followLocation);
        followLocation();
    }

    // What the user picked, before any credentials are asked for. \a repoUrl is
    // the repository hg already knows about, which is what the default answer
    // means.
    QString chosenLocation(const QUrl &repoUrl) const
    {
        switch (location()) {
        case LocalFilesystem:
            return localPath().toUrlishString();
        case Url:
            return url();
        default:
            return repoUrl.toString();
        }
    }

    // Whether the default location needs a user and password asked for. A local
    // repository does not, and neither does one the user typed in.
    bool needsCredentials(const QUrl &repoUrl) const
    {
        return location() == Default && promptForCredentials()
               && !repoUrl.scheme().isEmpty() && repoUrl.scheme() != "file";
    }

    SelectionAspect location{this};
    TextDisplay defaultLocation{this};
    BoolAspect promptForCredentials{this};
    FilePathAspect localPath{this};
    StringAspect url{this};
};

SrcDestDialog::SrcDestDialog(const VcsBasePluginState &state, Direction dir, QWidget *parent)
    : QDialog(parent)
    , m_direction(dir)
    , m_state(state)
    , m_settings(new SrcDestSettings)
{
    resize(400, 187);

    QUrl repoUrl = getRepoUrl();
    if (!repoUrl.password().isEmpty())
        repoUrl.setPassword("***");
    m_settings->defaultLocation.setText(repoUrl.toString());
    m_settings->promptForCredentials.setValue(!repoUrl.scheme().isEmpty()
                                              && repoUrl.scheme() != "file");

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SrcDestDialog::~SrcDestDialog() = default;

QString SrcDestDialog::getRepositoryString() const
{
    QUrl repoUrl(getRepoUrl());
    if (!m_settings->needsCredentials(repoUrl))
        return m_settings->chosenLocation(repoUrl);

    // Asking is the dialog's, not the settings': it opens another dialog, and
    // what comes back is merged into the repository hg already knows about.
    QScopedPointer<AuthenticationDialog> authDialog(
        new AuthenticationDialog(repoUrl.userName(), repoUrl.password()));
    authDialog->setPasswordEnabled(repoUrl.scheme() != "ssh");
    if (authDialog->exec() == 0)
        return repoUrl.toString();

    const QString user = authDialog->getUserName();
    if (user.isEmpty())
        return repoUrl.toString();
    if (user != repoUrl.userName())
        repoUrl.setUserName(user);
    const QString pass = authDialog->getPassword();
    if (!pass.isEmpty() && pass != repoUrl.password())
        repoUrl.setPassword(pass);
    return repoUrl.toString();
}

FilePath SrcDestDialog::workingDir() const
{
    return FilePath::fromString(m_workingdir);
}

QUrl SrcDestDialog::getRepoUrl() const
{
    // Repo to use: Default to the project repo, but use the current
    const QString projectLoc = m_state.currentProjectPath().toUrlishString();
    const QString fileLoc = m_state.currentFileTopLevel().toUrlishString();
    m_workingdir = projectLoc;
    if (!fileLoc.isEmpty())
        m_workingdir = fileLoc;
    if (!projectLoc.isEmpty() && fileLoc.startsWith(projectLoc + '/'))
        m_workingdir = projectLoc;

    QSettings settings(QString("%1/.hg/hgrc").arg(m_workingdir), QSettings::IniFormat);
    QUrl url;
    if (m_direction == outgoing)
        url = settings.value("paths/default-push").toUrl();
    if (url.isEmpty())
        url = settings.value("paths/default").toUrl();
    return url;
}

#ifdef WITH_TESTS

class SrcDestDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheAnswerDecidesTheLocationAndWhoIsAsked()
    {
        SrcDestSettings settings;
        const Utils::Result<> rendered = Core::aspectFormRenders(&settings, "SrcDestDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        const QUrl repo("https://user@example.invalid/hg");

        // The default answer means the repository hg already knows about.
        QCOMPARE(settings.chosenLocation(repo), repo.toString());
        QVERIFY2(!settings.localPath.isEnabled(), "a path was editable for the default location");

        settings.location.setValue(SrcDestSettings::Url);
        settings.url.setValue("https://elsewhere.invalid/hg");
        QCOMPARE(settings.chosenLocation(repo), QString("https://elsewhere.invalid/hg"));

        settings.location.setValue(SrcDestSettings::LocalFilesystem);
        settings.localPath.setValue(FilePath::fromString("/tmp/hg"));
        QCOMPARE(settings.chosenLocation(repo), QString("/tmp/hg"));
    }

    void testCredentialsAreOnlyAskedForWhereTheyCanBeUsed()
    {
        SrcDestSettings settings;
        settings.promptForCredentials.setValue(true);

        // A remote repository hg already knows about: the one case worth
        // asking about.
        QVERIFY(settings.needsCredentials(QUrl("https://user@example.invalid/hg")));

        // A local repository has no credentials to give.
        QVERIFY2(!settings.needsCredentials(QUrl("file:///tmp/hg")),
                 "credentials were asked for about a local repository");
        QVERIFY2(!settings.needsCredentials(QUrl()),
                 "credentials were asked for about no repository at all");

        // And a location the user typed carries its own.
        settings.location.setValue(SrcDestSettings::Url);
        QVERIFY2(!settings.needsCredentials(QUrl("https://user@example.invalid/hg")),
                 "credentials were asked for about a location the user typed");
        QVERIFY2(!settings.promptForCredentials.isEnabled(),
                 "the credentials box is offered for a location it does not apply to");
    }
};

QObject *createSrcDestDialogTest()
{
    return new SrcDestDialogTest;
}

#endif // WITH_TESTS

} // Mercurial::Internal

#ifdef WITH_TESTS
#include "srcdestdialog.moc"
#endif
