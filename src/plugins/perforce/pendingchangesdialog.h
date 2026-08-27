// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Perforce::Internal {

class PendingChangesSettings;

class PendingChangesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PendingChangesDialog(const QString &data, QWidget *parent = nullptr);
    ~PendingChangesDialog() override;

    // The change the user picked, or -1 when there was nothing to pick.
    int changeNumber() const;

private:
    std::unique_ptr<PendingChangesSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createPendingChangesDialogTest();
#endif

} // Perforce::Internal
