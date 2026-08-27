// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Mercurial::Internal {

class RevertSettings;

class RevertDialog : public QDialog
{
public:
    RevertDialog(QWidget *parent = nullptr);
    ~RevertDialog() override;

    // Empty unless a revision other than the default was asked for, which is
    // what the caller passes straight to hg.
    QString revision() const;

private:
    std::unique_ptr<RevertSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createRevertDialogTest();
#endif

} // Mercurial::Internal
