// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>

#include <utils/filepath.h>

namespace Android::Internal {

// What the reader typed into the keystore dialog, as the checks see it. A
// struct because the checks are about the whole of it: whether a certificate
// password matters at all depends on the "same as keystore" box.
struct KeystoreCertificateInput
{
    QString keystorePassword;
    QString keystoreRetype;
    bool certificateUsesKeystorePassword = false;
    QString certificatePassword;
    QString certificateRetype;
    QString certificateAlias;
    QString countryCode;
};

// The first thing wrong with it, and which field to put the cursor in. An
// empty message means nothing is wrong.
//
// Kept out of the dialog because these are questions about what was typed, and
// there each one also showed and hid a label, so the only way to ask was to
// fill the dialog in and press OK.
struct KeystoreCertificateIssue
{
    QString message;
    QString field;
};
KeystoreCertificateIssue keystoreCertificateIssue(const KeystoreCertificateInput &input);

// The distinguished name keytool is given: the four parts it always has, the
// two it has only when they were filled in, and every comma escaped - a comma
// inside a value would otherwise start a new part.
QString distinguishedName(const QString &commonName, const QString &organization,
                          const QString &locality, const QString &country,
                          const QString &organizationUnit, const QString &state);

class KeystoreData
{
public:
    Utils::FilePath keystoreFilePath;
    QString keystorePassword;
    QString certificateAlias;
    QString certificatePassword;
};

std::optional<KeystoreData> executeKeystoreCertificateDialog();

#ifdef WITH_TESTS
QObject *createKeystoreCertificateTest();
#endif

} // Android::Internal
