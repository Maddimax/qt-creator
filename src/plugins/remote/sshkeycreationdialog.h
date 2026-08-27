// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Utils { class FilePath; }

namespace Remote {

class SshKeySettings;

class SshKeyCreationDialog : public QDialog
{
    Q_OBJECT
public:
    SshKeyCreationDialog(QWidget *parent = nullptr);
    ~SshKeyCreationDialog() override;

    Utils::FilePath privateKeyFilePath() const;
    Utils::FilePath publicKeyFilePath() const;

private:
    void generateKeys();
    void showError(const QString &details);

    std::unique_ptr<SshKeySettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createSshKeyCreationDialogTest();
#endif

} // namespace Remote
