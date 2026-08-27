// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sshkeycreationdialog.h"

#include "remotelinuxtr.h"

#include <projectexplorer/devicesupport/sshsettings.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/pathvalidation.h>
#include <utils/qtcprocess.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardPaths>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace Remote {

// What key to make and where to put it. The sizes on offer depend on the
// algorithm - an RSA key and an ECDSA key have nothing in common about their
// length - so the list is refilled when the algorithm changes.
class SshKeySettings final : public AspectContainer
{
public:
    enum Algorithm { Rsa, Ecdsa };

    SshKeySettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Remote/SshKeyCreationDialog.qml"));

        algorithm.setQmlName("Algorithm");
        algorithm.setLabelText(Tr::tr("Key algorithm:"));
        algorithm.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        algorithm.addOption(Tr::tr("RSA"));
        algorithm.addOption(Tr::tr("ECDSA"));
        algorithm.setDefaultValue(Rsa);

        keySize.setQmlName("KeySize");
        keySize.setLabelText(Tr::tr("Key size:"));
        keySize.setFillCallback([this](const StringSelectionAspect::ResultCallback &cb) {
            QList<QStandardItem *> items;
            for (const QString &size : sizesFor(Algorithm(algorithm()))) {
                const auto item = new QStandardItem(size);
                // The value is the item's data, not its text: an item without
                // it reads back as empty and the choice is lost.
                item->setData(size);
                items.append(item);
            }
            cb(items);
        });

        privateKeyFile.setQmlName("PrivateKeyFile");
        privateKeyFile.setLabelText(Tr::tr("Private key file:"));
        privateKeyFile.setExpectedKind(PathChooserKind::SaveFile);
        privateKeyFile.setPromptDialogTitle(Tr::tr("Choose Private Key File Name"));

        publicKeyFile.setQmlName("PublicKeyFile");
        publicKeyFile.setLabelText(Tr::tr("Public key file:"));

        // Refilling is enough to move off a size the new algorithm has no use
        // for: the aspect re-selects among the entries it is given.
        algorithm.addOnChanged(this, [this] { keySize.refill(); });
        privateKeyFile.addOnChanged(this, [this] { followPrivateKey(); });

        privateKeyFile.setValue(FilePath::fromString(
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.ssh/qtc_id"));
        keySize.setValue(sizesFor(Rsa).first());
        followPrivateKey();
    }

    // ssh-keygen takes -b in bits, and what is sensible depends entirely on
    // the algorithm.
    static QStringList sizesFor(Algorithm algorithm)
    {
        switch (algorithm) {
        case Rsa:
            return {"1024", "2048", "4096"};
        case Ecdsa:
            return {"256", "384", "521"};
        }
        return {};
    }

    // ssh-keygen writes the public key beside the private one, so this is
    // derived rather than chosen - and derived from the *path*, not from what
    // a label happened to be showing.
    FilePath publicKeyPath() const
    {
        const FilePath privateKey = privateKeyFile();
        return privateKey.isEmpty() ? FilePath()
                                    : privateKey.stringAppended(".pub");
    }

    void followPrivateKey() { publicKeyFile.setText(publicKeyPath().toUserOutput()); }

    QString keyTypeArgument() const { return algorithm() == Rsa ? QLatin1String("rsa") : QLatin1String("ecdsa"); }

    SelectionAspect algorithm{this};
    StringSelectionAspect keySize{this};
    FilePathAspect privateKeyFile{this};
    TextDisplay publicKeyFile{this};
};

SshKeyCreationDialog::SshKeyCreationDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new SshKeySettings)
{
    setWindowTitle(Tr::tr("SSH Key Configuration"));
    resize(385, 231);

    auto buttonBox = new QDialogButtonBox;
    QPushButton *generateButton = buttonBox->addButton(Tr::tr("&Generate And Save Key Pair"),
                                                       QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    const auto followPrivateKey = [this, generateButton] {
        generateButton->setEnabled(!m_settings->privateKeyFile().isEmpty());
    };
    m_settings->privateKeyFile.addOnChanged(this, followPrivateKey);
    followPrivateKey();

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SshKeyCreationDialog::generateKeys);
}

SshKeyCreationDialog::~SshKeyCreationDialog() = default;

void SshKeyCreationDialog::generateKeys()
{
    if (sshSettings().keygenFilePath().isEmpty()) {
        showError(Tr::tr("The ssh-keygen tool was not found."));
        return;
    }
    if (privateKeyFilePath().exists()) {
        showError(Tr::tr("Refusing to overwrite existing private key file \"%1\".")
                      .arg(privateKeyFilePath().toUserOutput()));
        return;
    }

    QApplication::setOverrideCursor(Qt::BusyCursor);
    Process keygen;
    keygen.setCommand({sshSettings().keygenFilePath(),
                       {"-t", m_settings->keyTypeArgument(), "-b", m_settings->keySize(), "-N",
                        QString(), "-f", privateKeyFilePath().path()}});
    keygen.start();
    Result<> result = ResultOk;
    if (!keygen.waitForFinished()) {
        result = ResultError(keygen.errorString().isEmpty() ? Tr::tr("Unknown error")
                                                            : keygen.errorString());
    } else if (keygen.exitCode() != 0) {
        result = ResultError(QString::fromLocal8Bit(keygen.rawStdErr()));
    }
    if (!result) {
        showError(Tr::tr("The ssh-keygen tool at \"%1\" failed: %2")
                      .arg(sshSettings().keygenFilePath().toUserOutput(), result.error()));
    }
    QApplication::restoreOverrideCursor();
    accept();
}

void SshKeyCreationDialog::showError(const QString &details)
{
    QMessageBox::critical(this, Tr::tr("Key Generation Failed"), details);
}

FilePath SshKeyCreationDialog::privateKeyFilePath() const
{
    return m_settings->privateKeyFile();
}

FilePath SshKeyCreationDialog::publicKeyFilePath() const
{
    return m_settings->publicKeyPath();
}

#ifdef WITH_TESTS

class SshKeyCreationDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheSizesOfferedFollowTheAlgorithm()
    {
        SshKeySettings settings;
        QCOMPARE(settings.keySize(), QString("1024"));

        // Drawing the form must not decide anything. Filling the choices makes
        // the aspect look its value up among them, and an entry that does not
        // carry one reads back as empty.
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "SshKeyCreationDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
        QCOMPARE(settings.keySize(), QString("1024"));

        // An RSA key and an ECDSA key have nothing in common about their
        // length, so the list is not one list with some entries disabled.
        QCOMPARE(SshKeySettings::sizesFor(SshKeySettings::Rsa),
                 QStringList({"1024", "2048", "4096"}));
        QCOMPARE(SshKeySettings::sizesFor(SshKeySettings::Ecdsa),
                 QStringList({"256", "384", "521"}));

        QCOMPARE(settings.keySize(), QString("1024"));
        QCOMPARE(settings.keyTypeArgument(), QString("rsa"));

        // Changing the algorithm leaves a size that is valid for it: 1024 is
        // not an ECDSA key length, and ssh-keygen would refuse it.
        settings.algorithm.setValue(SshKeySettings::Ecdsa);
        QCOMPARE(settings.keyTypeArgument(), QString("ecdsa"));
        QVERIFY2(SshKeySettings::sizesFor(SshKeySettings::Ecdsa).contains(settings.keySize()),
                 qPrintable("left at " + settings.keySize()));
    }

    void testThePublicKeyIsDerivedFromThePathNotFromALabel()
    {
        SshKeySettings settings;

        settings.privateKeyFile.setValue(FilePath::fromString("/tmp/some key"));
        QCOMPARE(settings.publicKeyPath(), FilePath::fromString("/tmp/some key.pub"));

        // The path this came from is the value, not the text a label was
        // showing: the widget dialog read its own label back with
        // fromUserInput(), which is a round trip through display form.
        QCOMPARE(settings.publicKeyPath().parentDir(), FilePath::fromString("/tmp"));

        // Nothing chosen means nothing derived, rather than a bare ".pub".
        settings.privateKeyFile.setValue(FilePath());
        QVERIFY2(settings.publicKeyPath().isEmpty(),
                 qPrintable("derived " + settings.publicKeyPath().toUserOutput()));
    }
};

QObject *createSshKeyCreationDialogTest()
{
    return new SshKeyCreationDialogTest;
}

#endif // WITH_TESTS

} // namespace Remote

#ifdef WITH_TESTS
#include "sshkeycreationdialog.moc"
#endif
