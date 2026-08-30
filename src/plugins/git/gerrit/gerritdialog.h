// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QDialog>
#include <QTimer>

#include <functional>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QModelIndex;
class QObject;
QT_END_NAMESPACE

namespace Gerrit::Internal {

class GerritChange;
class GerritDialogSettings;
class GerritModel;
class GerritServer;

// Whether the three fetch buttons are offered: a change has to be picked, and
// nothing may be fetching already - two gerrit operations at once mix up.
bool canFetchChange(bool fetchRunning, bool hasCurrentChange);

// Puts \a query at the front of \a queries, which is the order they are
// offered back in. Nothing is remembered twice.
QStringList rememberedQueries(const QStringList &queries, const QString &query);

#ifdef WITH_TESTS
QObject *createGerritDialogTest();
#endif

class GerritDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GerritDialog(const std::shared_ptr<GerritServer> &s,
                          const Utils::FilePath &repository,
                          QWidget *parent = nullptr);
    ~GerritDialog() override;
    Utils::FilePath repositoryPath() const;
    void setCurrentPath(const Utils::FilePath &path);
    void fetchStarted(const QString &changeTitle);
    void fetchFinished();
    void refresh();
    void scheduleUpdateRemotes();

signals:
    // The changes have arrived; the tree opens itself out.
    void expandChanges();
    void fetchDisplay(const std::shared_ptr<Gerrit::Internal::GerritChange> &);
    void fetchCherryPick(const std::shared_ptr<Gerrit::Internal::GerritChange> &);
    void fetchCheckout(const std::shared_ptr<Gerrit::Internal::GerritChange> &);

private:
    void slotCurrentChanged();
    void slotActivated(const QModelIndex &);
    void slotRefreshStateChanged(bool);
    void slotFetchDisplay();
    void slotFetchCherryPick();
    void slotFetchCheckout();
    void remoteChanged();
    void updateRemotes(bool forceReload = false);
    void showEvent(QShowEvent *event) override;

    void manageProgressIndicator();

    void setProgressIndicatorVisible(bool v);
    QModelIndex currentIndex() const;
    void updateCompletions(const QString &query);
    void updateButtons();

    const std::shared_ptr<GerritServer> m_server;
    GerritModel *m_model;
    const std::unique_ptr<GerritDialogSettings> m_settings;
    QTimer m_progressIndicatorTimer;
    Utils::FilePath m_repository;
    bool m_fetchRunning = false;
    bool m_updatingRemotes = false;
    bool m_shouldUpdateRemotes = false;

    QDialogButtonBox *m_buttonBox;

#ifdef WITH_TESTS
    friend class GerritDialogTest;
#endif
};

} // Gerrit::Internal
