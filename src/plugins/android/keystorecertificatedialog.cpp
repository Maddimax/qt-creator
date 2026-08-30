// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "keystorecertificatedialog.h"

#include "androidconfigurations.h"
#include "androidtr.h"

#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcprocess.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Utils;

namespace Android::Internal {

// The three groups the dialog shows, as aspects. The "same password" box is
// the only thing here that does anything: it takes the certificate's own
// password away, because there is then nothing to type into.
class KeystoreCertificateSettings final : public AspectContainer
{
public:
    KeystoreCertificateSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Android/KeystoreCertificateDialog.qml"));

        const auto password = [](StringAspect &aspect, const QString &qmlName,
                                 const QString &label) {
            aspect.setQmlName(qmlName);
            aspect.setLabelText(label);
            aspect.setDisplayStyle(StringAspect::PasswordLineEditDisplay);
        };
        const auto text = [](StringAspect &aspect, const QString &qmlName,
                             const QString &label) {
            aspect.setQmlName(qmlName);
            aspect.setLabelText(label);
            aspect.setDisplayStyle(StringAspect::LineEditDisplay);
        };

        keystore.setQmlName("Keystore");
        keystore.setLabelText(Tr::tr("Keystore"));
        password(keystorePassword, "KeystorePassword", Tr::tr("Password:"));
        password(keystoreRetype, "KeystoreRetype", Tr::tr("Retype password:"));

        certificate.setQmlName("Certificate");
        certificate.setLabelText(Tr::tr("Certificate"));
        text(certificateAlias, "CertificateAlias", Tr::tr("Alias name:"));
        keySize.setQmlName("KeySize");
        keySize.setLabelText(Tr::tr("Keysize:"));
        keySize.setRange(2048, 2097152);
        keySize.setDefaultValue(2048);
        validity.setQmlName("Validity");
        validity.setLabelText(Tr::tr("Validity (days):"));
        validity.setRange(10000, 100000);
        validity.setDefaultValue(10000);
        password(certificatePassword, "CertificatePassword", Tr::tr("Password:"));
        password(certificateRetype, "CertificateRetype", Tr::tr("Retype password:"));
        samePassword.setQmlName("SamePassword");
        samePassword.setLabelText(Tr::tr("Use Keystore password"));

        names.setQmlName("Names");
        names.setLabelText(Tr::tr("Certificate Distinguished Names"));
        text(commonName, "CommonName", Tr::tr("First and last name:"));
        text(organizationUnit, "OrganizationUnit",
             Tr::tr("Organizational unit (e.g. Necessitas):"));
        text(organizationName, "OrganizationName", Tr::tr("Organization (e.g. KDE):"));
        text(localityName, "LocalityName", Tr::tr("City or locality:"));
        text(stateName, "StateName", Tr::tr("State or province:"));
        text(country, "Country", Tr::tr("Two-letter country code for this unit (e.g. RO):"));

        issue.setQmlName("Issue");
        issue.setIconType(AspectControls::InfoType::Error);
        issue.setVisible(false);

        // A certificate that borrows the keystore's password has none of its
        // own to type, so the two fields go away rather than sit there
        // ignored. The widget dialog did this in a slot; it is the same thing
        // and it still is not the form's business.
        samePassword.addOnChanged(this, [this] { refreshCertificatePassword(); });
        refreshCertificatePassword();
    }

    void refreshCertificatePassword()
    {
        certificatePassword.setEnabled(!samePassword());
        certificateRetype.setEnabled(!samePassword());
    }

    KeystoreCertificateInput typedIn() const
    {
        return {keystorePassword(), keystoreRetype(), samePassword(),
                certificatePassword(), certificateRetype(),
                certificateAlias(), country()};
    }

    AspectContainer keystore{this};
    StringAspect keystorePassword{&keystore};
    StringAspect keystoreRetype{&keystore};

    AspectContainer certificate{this};
    StringAspect certificateAlias{&certificate};
    IntegerAspect keySize{&certificate};
    IntegerAspect validity{&certificate};
    StringAspect certificatePassword{&certificate};
    StringAspect certificateRetype{&certificate};
    BoolAspect samePassword{&certificate};

    AspectContainer names{this};
    StringAspect commonName{&names};
    StringAspect organizationUnit{&names};
    StringAspect organizationName{&names};
    StringAspect localityName{&names};
    StringAspect stateName{&names};
    StringAspect country{&names};

    TextDisplay issue{this};
};

class AndroidCreateKeystoreCertificate : public QDialog
{
public:
    explicit AndroidCreateKeystoreCertificate();

    KeystoreData keystoreData() const;

private:
    // Shows the first problem, or hides the label when there is none.
    KeystoreCertificateIssue showIssue();
    void buttonBoxAccepted();
    bool validateUserInput();

    Utils::FilePath m_keystoreFilePath;
    KeystoreCertificateSettings m_settings;
};

AndroidCreateKeystoreCertificate::AndroidCreateKeystoreCertificate()
    : QDialog(Core::ICore::dialogParent())
{
    resize(638, 473);
    setWindowTitle(Tr::tr("Create a keystore and a certificate"));

    const auto buttonBox
        = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Save, this);
    connect(buttonBox, &QDialogButtonBox::accepted,
            this, &AndroidCreateKeystoreCertificate::buttonBoxAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&m_settings));
    layout->addWidget(buttonBox);

    // Every field says the same thing: what is the first problem with all of
    // this. Before, each field reported only its own, so typing into one could
    // hide a complaint about another.
    const QList<BaseAspect *> watched{&m_settings.keystorePassword,
                                      &m_settings.keystoreRetype,
                                      &m_settings.certificatePassword,
                                      &m_settings.certificateRetype,
                                      &m_settings.certificateAlias,
                                      &m_settings.country,
                                      &m_settings.samePassword};
    for (BaseAspect * const field : watched) {
        field->addOnChanged(this, [this] { showIssue(); });
    }
}

KeystoreData AndroidCreateKeystoreCertificate::keystoreData() const
{
    const QString certPassword = m_settings.samePassword() ? m_settings.keystorePassword()
                                                           : m_settings.certificatePassword();
    return {m_keystoreFilePath, m_settings.keystorePassword(),
            m_settings.certificateAlias(), certPassword};
}

KeystoreCertificateIssue keystoreCertificateIssue(const KeystoreCertificateInput &input)
{
    // In the order the reader meets the fields, so the first complaint is
    // about the first thing that is wrong rather than the last.
    if (input.keystorePassword.size() < 6)
        return {Tr::tr("Keystore password is too short."), "KeystorePassword"};
    if (input.keystorePassword != input.keystoreRetype)
        return {Tr::tr("Keystore passwords do not match."), "KeystoreRetype"};

    if (input.certificateAlias.isEmpty())
        return {Tr::tr("Certificate alias is missing."), "CertificateAlias"};

    // A certificate reusing the keystore password has no password of its own
    // to be wrong.
    if (!input.certificateUsesKeystorePassword) {
        if (input.certificatePassword.size() < 6)
            return {Tr::tr("Certificate password is too short."), "CertificatePassword"};
        if (input.certificatePassword != input.certificateRetype)
            return {Tr::tr("Certificate passwords do not match."), "CertificateRetype"};
    }

    static const QRegularExpression twoLetters("[A-Z]{2}");
    if (!input.countryCode.contains(twoLetters))
        return {Tr::tr("Invalid country code."), "Country"};

    return {};
}

QString distinguishedName(const QString &commonName, const QString &organization,
                          const QString &locality, const QString &country,
                          const QString &organizationUnit, const QString &state)
{
    // A comma inside a value would start a new part of the name, so every one
    // of them is escaped.
    const auto escaped = [](const QString &value) {
        return QString(value).replace(',', QLatin1String("\\,"));
    };

    QString name = QString("CN=%1, O=%2, L=%3, C=%4")
                       .arg(escaped(commonName), escaped(organization),
                            escaped(locality), escaped(country));

    if (!organizationUnit.isEmpty())
        name += ", OU=" + escaped(organizationUnit);
    if (!state.isEmpty())
        name += ", S=" + escaped(state);

    return name;
}



void AndroidCreateKeystoreCertificate::buttonBoxAccepted()
{
    if (!validateUserInput())
        return;

    m_keystoreFilePath = FileUtils::getSaveFilePath(Tr::tr("Keystore Filename"),
                                                    FileUtils::homePath() / "android_release.keystore",
                                                    Tr::tr("Keystore files (*.keystore *.jks)"));
    if (m_keystoreFilePath.isEmpty())
        return;
    const QString distinguishedNames = distinguishedName(m_settings.commonName(),
                                                         m_settings.organizationName(),
                                                         m_settings.localityName(),
                                                         m_settings.country(),
                                                         m_settings.organizationUnit(),
                                                         m_settings.stateName());

    const KeystoreData data = keystoreData();
    // clang-format off
    const CommandLine command(AndroidConfig::keytoolPath(),
                             {"-genkey", "-keyalg", "RSA",
                              "-keystore",  m_keystoreFilePath.path(),
                              "-storepass", data.keystorePassword,
                              "-alias", data.certificateAlias,
                              "-keysize", QString::number(m_settings.keySize()),
                              "-validity", QString::number(m_settings.validity()),
                              "-keypass", data.certificatePassword,
                              "-dname", distinguishedNames});
    // clang-format off

    Process genKeyCertProc;
    genKeyCertProc.setCommand(command);
    using namespace std::chrono_literals;
    genKeyCertProc.runBlocking(15s);

    if (genKeyCertProc.result() != ProcessResult::FinishedWithSuccess) {
        QMessageBox::critical(this, Tr::tr("Error"), genKeyCertProc.verboseExitMessage());
        return;
    }
    accept();
}


KeystoreCertificateIssue AndroidCreateKeystoreCertificate::showIssue()
{
    const KeystoreCertificateIssue issue = keystoreCertificateIssue(m_settings.typedIn());
    m_settings.issue.setVisible(!issue.message.isEmpty());
    m_settings.issue.setText(issue.message);
    return issue;
}

bool AndroidCreateKeystoreCertificate::validateUserInput()
{
    return showIssue().message.isEmpty();
}

std::optional<KeystoreData> executeKeystoreCertificateDialog()
{
    AndroidCreateKeystoreCertificate dialog;
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.keystoreData();
}

#ifdef WITH_TESTS

class KeystoreCertificateTest final : public QObject
{
    Q_OBJECT

    static KeystoreCertificateInput filledIn()
    {
        return {"secret1", "secret1", false, "secret2", "secret2", "myalias", "DE"};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        KeystoreCertificateSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "KeystoreCertificateDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testBorrowingTheKeystorePasswordTakesTheCertificateFieldsAway()
    {
        // Nothing to type into a field whose value comes from somewhere else.
        // The widget dialog disabled three widgets in a slot; the third was
        // its "Show password" box, which no longer exists.
        KeystoreCertificateSettings settings;
        QVERIFY(settings.certificatePassword.isEnabled());
        QVERIFY(settings.certificateRetype.isEnabled());

        settings.samePassword.setValue(true);
        QVERIFY2(!settings.certificatePassword.isEnabled(),
                 "the certificate password stayed editable while it is borrowed");
        QVERIFY(!settings.certificateRetype.isEnabled());

        settings.samePassword.setValue(false);
        QVERIFY(settings.certificatePassword.isEnabled());
        QVERIFY(settings.certificateRetype.isEnabled());
    }

    void testWhatTheFormHandsBack()
    {
        // The struct the checks are run against comes off the aspects, so the
        // form and the checks cannot drift apart.
        KeystoreCertificateSettings settings;
        settings.keystorePassword.setValue("secret1");
        settings.keystoreRetype.setValue("secret1");
        settings.certificateAlias.setValue("myalias");
        settings.certificatePassword.setValue("secret2");
        settings.certificateRetype.setValue("secret2");
        settings.country.setValue("DE");

        QCOMPARE(keystoreCertificateIssue(settings.typedIn()).message, QString());

        settings.country.setValue("Germany");
        QCOMPARE(keystoreCertificateIssue(settings.typedIn()).field, QString("Country"));
    }

    void testWhatIsWrongWithWhatWasTyped()
    {
        QCOMPARE(keystoreCertificateIssue(filledIn()).message, QString());

        // Six characters is the shortest a keystore password may be, and the
        // complaint names the field the cursor should go to.
        KeystoreCertificateInput input = filledIn();
        input.keystorePassword = input.keystoreRetype = "short";
        QVERIFY(!keystoreCertificateIssue(input).message.isEmpty());
        QCOMPARE(keystoreCertificateIssue(input).field, QString("KeystorePassword"));

        input = filledIn();
        input.keystoreRetype = "secret1-but-not";
        QCOMPARE(keystoreCertificateIssue(input).field, QString("KeystoreRetype"));

        input = filledIn();
        input.certificateAlias.clear();
        QCOMPARE(keystoreCertificateIssue(input).field, QString("CertificateAlias"));

        input = filledIn();
        input.certificateRetype = "different";
        QCOMPARE(keystoreCertificateIssue(input).field, QString("CertificateRetype"));
    }

    void testACertificateThatBorrowsTheKeystorePassword()
    {
        // With the box ticked the certificate has no password of its own, so
        // one that would be refused on its own must not be looked at.
        KeystoreCertificateInput input = filledIn();
        input.certificatePassword = "no";
        input.certificateRetype = "nope";

        QVERIFY2(!keystoreCertificateIssue(input).message.isEmpty(),
                 "fixture: this is refused while the certificate has its own password");

        input.certificateUsesKeystorePassword = true;
        QCOMPARE(keystoreCertificateIssue(input).message, QString());
    }

    void testTheFirstProblemIsTheOneReported()
    {
        // Everything wrong at once: the reader is told about the field they
        // meet first, not the last one checked.
        KeystoreCertificateInput input;
        QCOMPARE(keystoreCertificateIssue(input).field, QString("KeystorePassword"));

        // The country code is only reached once the rest is right.
        input = filledIn();
        input.countryCode = "Germany";
        QCOMPARE(keystoreCertificateIssue(input).field, QString("Country"));
        input.countryCode = "de";
        QVERIFY2(!keystoreCertificateIssue(input).message.isEmpty(),
                 "a lower-case country code was accepted");
    }

    void testTheNameKeytoolIsGiven()
    {
        QCOMPARE(distinguishedName("Nemo", "Acme", "Berlin", "DE", {}, {}),
                 QString("CN=Nemo, O=Acme, L=Berlin, C=DE"));

        // The two optional parts, in the order they are appended.
        QCOMPARE(distinguishedName("Nemo", "Acme", "Berlin", "DE", "Research", "Berlin State"),
                 QString("CN=Nemo, O=Acme, L=Berlin, C=DE, OU=Research, S=Berlin State"));

        // A comma inside a value would otherwise start a new part of the name,
        // so every one is escaped - including in the optional parts, which is
        // easy to miss because they are appended separately.
        QCOMPARE(distinguishedName("Nemo, Captain", "Acme", "Berlin", "DE", "R,D", {}),
                 QString("CN=Nemo\\, Captain, O=Acme, L=Berlin, C=DE, OU=R\\,D"));
    }
};

QObject *createKeystoreCertificateTest()
{
    return new KeystoreCertificateTest;
}

#endif // WITH_TESTS

} // Android::Internal

#ifdef WITH_TESTS
#include "keystorecertificatedialog.moc"
#endif
