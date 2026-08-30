// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "keystorecertificatedialog.h"

#include "androidconfigurations.h"
#include "androidtr.h"

#include <coreplugin/icore.h>

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

using namespace Utils;

namespace Android::Internal {

class AndroidCreateKeystoreCertificate : public QDialog
{
    enum PasswordStatus
    {
        Invalid,
        NoMatch,
        Match
    };

public:
    explicit AndroidCreateKeystoreCertificate();

    KeystoreData keystoreData() const;

private:
    KeystoreCertificateInput typedIn() const;
    // Shows the first problem, or hides the label when there is none.
    KeystoreCertificateIssue showIssue();

    void keystoreShowPassStateChanged(int state);
    void certificateShowPassStateChanged(int state);
    void buttonBoxAccepted();
    void samePasswordStateChanged(int state);

    bool validateUserInput();

    Utils::FilePath m_keystoreFilePath;

    QLineEdit *m_commonNameLineEdit;
    QLineEdit *m_organizationUnitLineEdit;
    QLineEdit *m_organizationNameLineEdit;
    QLineEdit *m_localityNameLineEdit;
    QLineEdit *m_stateNameLineEdit;
    QLineEdit *m_countryLineEdit;
    QLineEdit *m_certificateRetypePassLineEdit;
    QCheckBox *m_certificateShowPassCheckBox;
    QSpinBox *m_validitySpinBox;
    QLineEdit *m_certificateAliasLineEdit;
    QLineEdit *m_certificatePassLineEdit;
    QSpinBox *m_keySizeSpinBox;
    QCheckBox *m_samePasswordCheckBox;
    QLineEdit *m_keystorePassLineEdit;
    QLineEdit *m_keystoreRetypePassLineEdit;
    Utils::InfoLabel *m_infoLabel;
};

AndroidCreateKeystoreCertificate::AndroidCreateKeystoreCertificate()
    : QDialog(Core::ICore::dialogParent())
{
    resize(638, 473);
    setWindowTitle(Tr::tr("Create a keystore and a certificate"));

    m_commonNameLineEdit = new QLineEdit;

    m_organizationUnitLineEdit = new QLineEdit;

    m_organizationNameLineEdit = new QLineEdit;

    m_localityNameLineEdit = new QLineEdit;

    m_stateNameLineEdit = new QLineEdit;

    m_countryLineEdit = new QLineEdit;
    m_countryLineEdit->setMaxLength(2);
    m_countryLineEdit->setInputMask(QString());

    m_certificateRetypePassLineEdit = new QLineEdit;
    m_certificateRetypePassLineEdit->setEchoMode(QLineEdit::Password);

    m_certificateShowPassCheckBox = new QCheckBox(Tr::tr("Show password"));

    m_validitySpinBox = new QSpinBox;
    m_validitySpinBox->setRange(10000, 100000);

    m_certificateAliasLineEdit = new QLineEdit;
    m_certificateAliasLineEdit->setInputMask({});
    m_certificateAliasLineEdit->setMaxLength(32);

    m_certificatePassLineEdit = new QLineEdit;
    m_certificatePassLineEdit->setEchoMode(QLineEdit::Password);

    m_keySizeSpinBox = new QSpinBox;
    m_keySizeSpinBox->setRange(2048, 2097152);

    m_samePasswordCheckBox = new QCheckBox(Tr::tr("Use Keystore password"));

    m_keystorePassLineEdit = new QLineEdit;
    m_keystorePassLineEdit->setEchoMode(QLineEdit::Password);

    m_keystoreRetypePassLineEdit = new QLineEdit;
    m_keystoreRetypePassLineEdit->setEchoMode(QLineEdit::Password);

    m_infoLabel = new InfoLabel;
    m_infoLabel->setType(InfoLabelType::Error);
    m_infoLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_infoLabel->hide();

    auto keystoreShowPassCheckBox = new QCheckBox(Tr::tr("Show password"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close|QDialogButtonBox::Save);

    using namespace Layouting;

    Column {
        Group {
            title(Tr::tr("Keystore")),
            Form {
                Tr::tr("Password:"), m_keystorePassLineEdit, br,
                Tr::tr("Retype password:"), m_keystoreRetypePassLineEdit, br,
                Span(2, keystoreShowPassCheckBox), br,
            }
        },

        Group {
            title(Tr::tr("Certificate")),
            Form {
                Tr::tr("Alias name:"), m_certificateAliasLineEdit, br,
                Tr::tr("Keysize:"), m_keySizeSpinBox, br,
                Tr::tr("Validity (days):"), m_validitySpinBox, br,
                Tr::tr("Password:"), m_certificatePassLineEdit, br,
                Tr::tr("Retype password:"), m_certificateRetypePassLineEdit, br,
                Span(2, m_samePasswordCheckBox), br,
                Span(2, m_certificateShowPassCheckBox), br,
            }
        },

        Group {
            title(Tr::tr("Certificate Distinguished Names")),
            Form {
                Tr::tr("First and last name:"), m_commonNameLineEdit, br,
                Tr::tr("Organizational unit (e.g. Necessitas):"),  m_organizationUnitLineEdit, br,
                Tr::tr("Organization (e.g. KDE):"), m_organizationNameLineEdit, br,
                Tr::tr("City or locality:"), m_localityNameLineEdit, br,
                Tr::tr("State or province:"), m_stateNameLineEdit, br,
                Tr::tr("Two-letter country code for this unit (e.g. RO):"), m_countryLineEdit,
            }
        },

        Row { m_infoLabel, buttonBox }
    }.attachTo(this);

    // Every field says the same thing now: what is the first problem with all
    // of this. Before, each field reported only its own, so typing into one
    // could hide a complaint about another.
    for (QLineEdit * const field : {m_keystorePassLineEdit, m_keystoreRetypePassLineEdit,
                                    m_certificatePassLineEdit, m_certificateRetypePassLineEdit,
                                    m_certificateAliasLineEdit, m_countryLineEdit}) {
        connect(field, &QLineEdit::textChanged,
                this, &AndroidCreateKeystoreCertificate::showIssue);
    }
    connect(keystoreShowPassCheckBox, &QCheckBox::stateChanged,
            this, &AndroidCreateKeystoreCertificate::keystoreShowPassStateChanged);
    connect(m_certificateShowPassCheckBox, &QCheckBox::stateChanged,
            this, &AndroidCreateKeystoreCertificate::certificateShowPassStateChanged);
    connect(m_samePasswordCheckBox, &QCheckBox::stateChanged,
            this, &AndroidCreateKeystoreCertificate::samePasswordStateChanged);
    connect(buttonBox, &QDialogButtonBox::accepted,
            this, &AndroidCreateKeystoreCertificate::buttonBoxAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(m_keystorePassLineEdit, &QLineEdit::editingFinished,
            m_keystoreRetypePassLineEdit, QOverload<>::of(&QWidget::setFocus));
}

KeystoreData AndroidCreateKeystoreCertificate::keystoreData() const
{
    const QString certPassword = m_samePasswordCheckBox->checkState() == Qt::Checked
                               ? m_keystorePassLineEdit->text() : m_certificatePassLineEdit->text();
    return {m_keystoreFilePath, m_keystorePassLineEdit->text(), m_certificateAliasLineEdit->text(),
            certPassword};
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

void AndroidCreateKeystoreCertificate::keystoreShowPassStateChanged(int state)
{
    m_keystorePassLineEdit->setEchoMode(state == Qt::Checked ? QLineEdit::Normal : QLineEdit::Password);
    m_keystoreRetypePassLineEdit->setEchoMode(m_keystorePassLineEdit->echoMode());
}

void AndroidCreateKeystoreCertificate::certificateShowPassStateChanged(int state)
{
    m_certificatePassLineEdit->setEchoMode(state == Qt::Checked ? QLineEdit::Normal : QLineEdit::Password);
    m_certificateRetypePassLineEdit->setEchoMode(m_certificatePassLineEdit->echoMode());
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
    const QString distinguishedNames = distinguishedName(m_commonNameLineEdit->text(),
                                                         m_organizationNameLineEdit->text(),
                                                         m_localityNameLineEdit->text(),
                                                         m_countryLineEdit->text(),
                                                         m_organizationUnitLineEdit->text(),
                                                         m_stateNameLineEdit->text());

    const KeystoreData data = keystoreData();
    // clang-format off
    const CommandLine command(AndroidConfig::keytoolPath(),
                             {"-genkey", "-keyalg", "RSA",
                              "-keystore",  m_keystoreFilePath.path(),
                              "-storepass", data.keystorePassword,
                              "-alias", data.certificateAlias,
                              "-keysize", m_keySizeSpinBox->text(),
                              "-validity", m_validitySpinBox->text(),
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

void AndroidCreateKeystoreCertificate::samePasswordStateChanged(int state)
{
    if (state == Qt::Checked) {
        m_certificatePassLineEdit->setDisabled(true);
        m_certificateRetypePassLineEdit->setDisabled(true);
        m_certificateShowPassCheckBox->setDisabled(true);
    }

    if (state == Qt::Unchecked) {
        m_certificatePassLineEdit->setEnabled(true);
        m_certificateRetypePassLineEdit->setEnabled(true);
        m_certificateShowPassCheckBox->setEnabled(true);
    }

    validateUserInput();
}

KeystoreCertificateInput AndroidCreateKeystoreCertificate::typedIn() const
{
    return {m_keystorePassLineEdit->text(),
            m_keystoreRetypePassLineEdit->text(),
            m_samePasswordCheckBox->checkState() == Qt::Checked,
            m_certificatePassLineEdit->text(),
            m_certificateRetypePassLineEdit->text(),
            m_certificateAliasLineEdit->text(),
            m_countryLineEdit->text()};
}

KeystoreCertificateIssue AndroidCreateKeystoreCertificate::showIssue()
{
    const KeystoreCertificateIssue issue = keystoreCertificateIssue(typedIn());
    m_infoLabel->setVisible(!issue.message.isEmpty());
    m_infoLabel->setText(issue.message);
    return issue;
}

bool AndroidCreateKeystoreCertificate::validateUserInput()
{
    const KeystoreCertificateIssue issue = showIssue();
    if (issue.message.isEmpty())
        return true;

    // Which field the problem is about, so the cursor lands on it.
    static const QHash<QString, QLineEdit *AndroidCreateKeystoreCertificate::*> fields{
        {"KeystorePassword", &AndroidCreateKeystoreCertificate::m_keystorePassLineEdit},
        {"KeystoreRetype", &AndroidCreateKeystoreCertificate::m_keystoreRetypePassLineEdit},
        {"CertificateAlias", &AndroidCreateKeystoreCertificate::m_certificateAliasLineEdit},
        {"CertificatePassword", &AndroidCreateKeystoreCertificate::m_certificatePassLineEdit},
        {"CertificateRetype", &AndroidCreateKeystoreCertificate::m_certificateRetypePassLineEdit},
        {"Country", &AndroidCreateKeystoreCertificate::m_countryLineEdit},
    };
    if (QLineEdit *AndroidCreateKeystoreCertificate::*const member = fields.value(issue.field))
        (this->*member)->setFocus();

    return false;
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
