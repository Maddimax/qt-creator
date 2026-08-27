// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pullorpushdialog.h"

#include "constants.h"
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

// Where a pull or push goes. Three mutually exclusive answers, and the two that
// need a value have a field each - which is enablement rather than layout, so
// it is arranged here rather than in the form.
class RemoteLocationSettings final : public AspectContainer
{
public:
    enum Location { Default, LocalFilesystem, Url };

    RemoteLocationSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Fossil/PullOrPushDialog.qml"));

        location.setQmlName("Location");
        location.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        location.addOption(Tr::tr("Default location"));
        location.addOption(Tr::tr("Local filesystem:"));
        location.addOption(Tr::tr("Specify URL:"),
                           Tr::tr("For example: \"https://[user[:pass]@]host[:port]/[path]\"."));
        location.setDefaultValue(Default);

        localPath.setQmlName("LocalPath");
        localPath.setExpectedKind(PathChooserKind::File);
        localPath.setPromptDialogFilter(Tr::tr(Constants::FOSSIL_FILE_FILTER));

        url.setQmlName("Url");
        url.setDisplayStyle(StringAspect::LineEditDisplay);
        url.setToolTip(
            Tr::tr("For example: \"https://[user[:pass]@]host[:port]/[path]\"."));

        remember.setQmlName("Remember");
        remember.setLabelText(Tr::tr("Remember specified location as default"));

        includePrivate.setQmlName("IncludePrivate");
        includePrivate.setLabelText(Tr::tr("Include private branches"));
        includePrivate.setToolTip(Tr::tr("Allow transfer of private branches."));

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

    // Empty for the default location, which is what tells fossil to use the
    // one it already has.
    QString remoteLocation() const
    {
        switch (location()) {
        case LocalFilesystem:
            return localPath().toUrlishString();
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
    BoolAspect includePrivate{this};
};

PullOrPushDialog::PullOrPushDialog(FossilCommand command, QWidget *parent)
    : QDialog(parent)
    , m_settings(new RemoteLocationSettings)
{
    setWindowTitle(command == FossilCommand::Pull ? Tr::tr("Pull Source")
                                                  : Tr::tr("Push Destination"));
    resize(600, 0);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PullOrPushDialog::~PullOrPushDialog() = default;

QString PullOrPushDialog::remoteLocation() const
{
    return m_settings->remoteLocation();
}

bool PullOrPushDialog::isRememberOptionEnabled() const
{
    return m_settings->rememberLocation();
}

bool PullOrPushDialog::isPrivateOptionEnabled() const
{
    return m_settings->includePrivate();
}

void PullOrPushDialog::setDefaultRemoteLocation(const QString &url)
{
    m_settings->url.setValue(url);
}

void PullOrPushDialog::setLocalBaseDirectory(const FilePath &dir)
{
    m_settings->localPath.setBaseDirectory(dir);
}

#ifdef WITH_TESTS

class PullOrPushDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheAnswerDecidesWhichFieldIsUsed()
    {
        RemoteLocationSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "PullOrPushDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // Only the field belonging to the current answer can be typed into.
        QVERIFY2(!settings.localPath.isEnabled(), "a path was editable for the default location");
        QVERIFY2(!settings.url.isEnabled(), "a URL was editable for the default location");
        QVERIFY2(!settings.remember.isEnabled(),
                 "there is nothing to remember about the default location");

        settings.location.setValue(RemoteLocationSettings::Url);
        QVERIFY(settings.url.isEnabled());
        QVERIFY(!settings.localPath.isEnabled());
        QVERIFY(settings.remember.isEnabled());

        // And what the caller is given is the field that answer names. Values
        // left in the others are not passed to fossil.
        settings.url.setValue("https://example.invalid/repo");
        settings.localPath.setValue(FilePath::fromString("/tmp/repo.fossil"));
        QCOMPARE(settings.remoteLocation(), QString("https://example.invalid/repo"));

        settings.location.setValue(RemoteLocationSettings::LocalFilesystem);
        QCOMPARE(settings.remoteLocation(), QString("/tmp/repo.fossil"));

        // The default location is "wherever fossil already points", said by
        // giving no location at all - and nothing is remembered about it, even
        // if the box was ticked while another answer was current.
        settings.remember.setValue(true);
        settings.location.setValue(RemoteLocationSettings::Default);
        QCOMPARE(settings.remoteLocation(), QString());
        QCOMPARE(settings.rememberLocation(), false);
    }
};

QObject *createPullOrPushDialogTest()
{
    return new PullOrPushDialogTest;
}

#endif // WITH_TESTS

} // Fossil::Internal

#ifdef WITH_TESTS
#include "pullorpushdialog.moc"
#endif
