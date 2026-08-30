// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Utils { class FilePath; }

namespace Git::Internal {

class RemoteDialogSettings;

class RemoteModel;

#ifdef WITH_TESTS
QObject *createRemoteDialogTest();
#endif

class RemoteDialog : public QDialog
{
public:
    explicit RemoteDialog(QWidget *parent = nullptr);
    ~RemoteDialog() override;

    void refresh(const Utils::FilePath &repository, bool force);

private:
    void refreshRemotes();
    void addRemote();
    void removeRemote();
    void pushToRemote();
    void fetchFromRemote();

    void updateButtonState();

    RemoteModel *m_remoteModel;

    const std::unique_ptr<RemoteDialogSettings> m_settings;
};

} // Git::Internal
