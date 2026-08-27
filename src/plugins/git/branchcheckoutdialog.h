// Copyright (C) 2016 Petar Perisin <petar.perisin@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Git::Internal {

class CheckoutSettings;

class BranchCheckoutDialog : public QDialog
{
public:
    explicit BranchCheckoutDialog(QWidget *parent, const QString &currentBranch,
                                  const QString &nextBranch);
    ~BranchCheckoutDialog() override;

    void foundNoLocalChanges();
    void foundStashForNextBranch();

    bool makeStashOfCurrentBranch() const;
    bool moveLocalChangesToNextBranch() const;
    bool discardLocalChanges() const;
    bool popStashOfNextBranch() const;

    bool hasStashForNextBranch() const;
    bool hasLocalChanges() const;
    bool diffRequested() const;

private:
    std::unique_ptr<CheckoutSettings> m_settings;
    bool m_diffRequested = false;
};

#ifdef WITH_TESTS
QObject *createBranchCheckoutDialogTest();
#endif

} // Git::Internal
