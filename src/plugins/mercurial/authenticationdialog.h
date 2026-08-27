// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Mercurial::Internal {

class AuthenticationSettings;

class AuthenticationDialog : public QDialog
{
public:
    explicit AuthenticationDialog(const QString &username, const QString &password,
                                  QWidget *parent = nullptr);
    ~AuthenticationDialog() override;

    void setPasswordEnabled(bool enabled);

    QString getUserName();
    QString getPassword();

private:
    std::unique_ptr<AuthenticationSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createAuthenticationDialogTest();
#endif

} // Mercurial::Internal
