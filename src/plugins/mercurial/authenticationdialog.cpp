// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "authenticationdialog.h"

#include "mercurialtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Mercurial::Internal {

// What the dialog asks for. The password is a password because the aspect says
// so, rather than because a line edit was told to echo differently.
class AuthenticationSettings final : public AspectContainer
{
public:
    AuthenticationSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Mercurial/AuthenticationDialog.qml"));

        username.setQmlName("Username");
        username.setLabelText(Tr::tr("Username:"));
        username.setDisplayStyle(StringAspect::LineEditDisplay);

        password.setQmlName("Password");
        password.setLabelText(Tr::tr("Password:"));
        password.setDisplayStyle(StringAspect::PasswordLineEditDisplay);
    }

    StringAspect username{this};
    StringAspect password{this};
};

AuthenticationDialog::AuthenticationDialog(const QString &username, const QString &password,
                                           QWidget *parent)
    : QDialog(parent)
    , m_settings(new AuthenticationSettings)
{
    resize(312, 116);
    m_settings->username.setValue(username);
    m_settings->password.setValue(password);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

AuthenticationDialog::~AuthenticationDialog() = default;

void AuthenticationDialog::setPasswordEnabled(bool enabled)
{
    m_settings->password.setEnabled(enabled);
}

QString AuthenticationDialog::getUserName()
{
    return m_settings->username();
}

QString AuthenticationDialog::getPassword()
{
    return m_settings->password();
}

#ifdef WITH_TESTS

class AuthenticationDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheFormAsksForBothAndHidesOne()
    {
        AuthenticationSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "AuthenticationDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // The password is asked for as one: the delegate reads that from the
        // aspect's presentation, so that is where it has to say so.
        QCOMPARE(settings.password.presentation().control,
                 AspectControls::PasswordLineEdit);
        QCOMPARE(settings.username.presentation().control, AspectControls::LineEdit);

        AuthenticationDialog dialog("someone", "secret", nullptr);
        QCOMPARE(dialog.getUserName(), QString("someone"));
        QCOMPARE(dialog.getPassword(), QString("secret"));

        // Over ssh there is nothing to type, which the dialog says by disabling
        // the field. What was already there stays there: disabled is not
        // cleared, and a caller still reads it back.
        dialog.setPasswordEnabled(false);
        QCOMPARE(dialog.getPassword(), QString("secret"));
    }
};

QObject *createAuthenticationDialogTest()
{
    return new AuthenticationDialogTest;
}

#endif // WITH_TESTS

} // Mercurial::Internal

#ifdef WITH_TESTS
#include "authenticationdialog.moc"
#endif
