// Copyright (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Bazaar::Internal {

class BranchLocationSettings;

class PullOrPushDialog : public QDialog
{
public:
    enum Mode {
        PullMode,
        PushMode
    };

    explicit PullOrPushDialog(Mode mode, QWidget *parent = nullptr);
    ~PullOrPushDialog() override;

    // Common parameters and options
    QString branchLocation() const;
    bool isRememberOptionEnabled() const;
    bool isOverwriteOptionEnabled() const;
    QString revision() const;

    // Pull-specific options
    bool isLocalOptionEnabled() const;

    // Push-specific options
    bool isUseExistingDirectoryOptionEnabled() const;
    bool isCreatePrefixOptionEnabled() const;

private:
    Mode m_mode;
    std::unique_ptr<BranchLocationSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createPullOrPushDialogTest();
#endif

} // Bazaar::Internal
