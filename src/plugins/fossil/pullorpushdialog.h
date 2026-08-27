// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QDialog>

#include <memory>

namespace Fossil::Internal {

enum class FossilCommand { Pull, Push };

class RemoteLocationSettings;

class PullOrPushDialog : public QDialog
{
public:
    explicit PullOrPushDialog(FossilCommand command, QWidget *parent = nullptr);
    ~PullOrPushDialog() override;

    // Common parameters and options
    QString remoteLocation() const;
    bool isRememberOptionEnabled() const;
    bool isPrivateOptionEnabled() const;

    void setDefaultRemoteLocation(const QString &url);
    void setLocalBaseDirectory(const Utils::FilePath &dir);

private:
    std::unique_ptr<RemoteLocationSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createPullOrPushDialogTest();
#endif

} // Fossil::Internal
