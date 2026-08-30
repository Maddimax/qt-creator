// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace Git::Internal {

class StashModel;
class StashSettings;
#ifdef WITH_TESTS
class StashDialogTest;
QObject *createStashDialogTest();
#endif

/* StashDialog: Non-modal dialog that manages the list of stashes
 * of the repository. Offers to show, restore, restore to branch
 * (in case restore fails due to conflicts) on current and
 * delete on selection/all. */

class StashDialog : public QDialog
{
public:
    explicit StashDialog(QWidget *parent = nullptr);
    ~StashDialog() override;

    void refresh(const Utils::FilePath &repository, bool force);

private:
    // Prompt dialog for modified repositories. Ask to undo or stash away.
    enum ModifiedRepositoryAction {
        ModifiedRepositoryCancel,
        ModifiedRepositoryStash,
        ModifiedRepositoryDiscard
    };

    void deleteAll();
    void deleteSelection();
    void showCurrent();
    void restoreCurrent();
    void restoreCurrentInBranch();
    void enableButtons();
    void forceRefresh();

    ModifiedRepositoryAction promptModifiedRepository(const QString &stash);
    bool promptForRestore(QString *stash, QString *branch /* = 0 */, QString *errorMessage);
    bool ask(const QString &title, const QString &what, bool defaultButton = true);
    void warning(const QString &title, const QString &what, const QString &details = QString());
    int currentRow() const;
    QList<int> selectedRows() const;    \

    StashModel *m_model;
    const std::unique_ptr<StashSettings> m_settings;
    Utils::FilePath m_repository;

#ifdef WITH_TESTS
    friend class StashDialogTest;
#endif
};

} // Git::Internal
