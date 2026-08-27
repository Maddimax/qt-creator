// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "configuredialog.h"

#include "fossilsettings.h"
#include "fossiltr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/pathvalidation.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Fossil::Internal {

// One repository's settings. The struct is what the rest of the plugin passes
// around; these aspects are how the dialog shows it, and the two are converted
// at the edges rather than kept in step.
class RepositoryConfiguration final : public AspectContainer
{
public:
    RepositoryConfiguration()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Fossil/ConfigureDialog.qml"));

        user.setQmlName("User");
        user.setLabelText(Tr::tr("User:"));
        user.setDisplayStyle(StringAspect::LineEditDisplay);
        user.setToolTip(
            Tr::tr("Existing user to become an author of changes made to the repository."));

        sslIdentityFile.setQmlName("SslIdentityFile");
        sslIdentityFile.setLabelText(Tr::tr("SSL/TLS identity:"));
        sslIdentityFile.setExpectedKind(PathChooserKind::File);
        sslIdentityFile.setPromptDialogTitle(Tr::tr("SSL/TLS Identity Key"));
        sslIdentityFile.setToolTip(
            Tr::tr("SSL/TLS client identity key to use if requested by the server."));

        disableAutosync.setQmlName("DisableAutosync");
        disableAutosync.setLabelText(Tr::tr("Disable auto-sync"));
        disableAutosync.setToolTip(
            Tr::tr("Disable automatic pull prior to commit or update and automatic push "
                   "after commit or tag or branch creation."));
    }

    RepositorySettings toSettings() const
    {
        return {user().trimmed(),
                sslIdentityFile().toUrlishString(),
                disableAutosync() ? RepositorySettings::AutosyncOff : RepositorySettings::AutosyncOn};
    }

    void fromSettings(const RepositorySettings &settings)
    {
        user.setValue(settings.user.trimmed());
        sslIdentityFile.setValue(FilePath::fromUserInput(settings.sslIdentityFile));
        disableAutosync.setValue(settings.autosync == RepositorySettings::AutosyncOff);
    }

    StringAspect user{this};
    FilePathAspect sslIdentityFile{this};
    BoolAspect disableAutosync{this};
};

ConfigureDialog::ConfigureDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new RepositoryConfiguration)
{
    setWindowTitle(Tr::tr("Configure Repository"));
    resize(600, 0);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ConfigureDialog::~ConfigureDialog() = default;

const RepositorySettings ConfigureDialog::settings() const
{
    return m_settings->toSettings();
}

void ConfigureDialog::setSettings(const RepositorySettings &settings)
{
    m_settings->fromSettings(settings);
}

#ifdef WITH_TESTS

class ConfigureDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItShowsAndReturnsARepositorysSettings()
    {
        RepositoryConfiguration configuration;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&configuration, "ConfigureDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // The struct goes in and comes back out unchanged, which is the whole
        // contract: the plugin passes the struct around and the dialog is only
        // a way of editing one.
        const RepositorySettings before{"someone", "/tmp/key.pem",
                                        RepositorySettings::AutosyncOff};
        ConfigureDialog dialog;
        dialog.setSettings(before);
        QCOMPARE(dialog.settings(), before);

        // Auto-sync is stored as a mode and shown as "disable it", so the two
        // are opposites and a port that dropped the inversion would leave every
        // repository syncing when it was told not to.
        RepositorySettings syncing = before;
        syncing.autosync = RepositorySettings::AutosyncOn;
        dialog.setSettings(syncing);
        QCOMPARE(dialog.settings().autosync, RepositorySettings::AutosyncOn);
    }
};

QObject *createConfigureDialogTest()
{
    return new ConfigureDialogTest;
}

#endif // WITH_TESTS

} // namespace Fossil::Internal

#ifdef WITH_TESTS
#include "configuredialog.moc"
#endif
