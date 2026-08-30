// Copyright (C) 2016 Petar Perisin <petar.perisin@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>
#include <QSet>
#include <QTimer>

#include <utils/processinfo.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
class QPushButton;
QT_END_NAMESPACE

namespace ProjectExplorer { class Kit; }

namespace Debugger::Internal {

class UnstartedAppWatcherSettings;

enum UnstartedAppWatcherState
{
    InvalidWatcherState,
    NotWatchingState,
    WatchingState,
    FoundState
};

#ifdef WITH_TESTS
class UnstartedAppWatcherDialogTest;
QObject *createUnstartedAppWatcherDialogTest();
#endif

class UnstartedAppWatcherDialog : public QDialog
{
    Q_OBJECT

public:
    UnstartedAppWatcherDialog(std::optional<QPoint> pos, QWidget *parent = nullptr);
    ~UnstartedAppWatcherDialog() override;

    ProjectExplorer::Kit *currentKit() const;
    Utils::ProcessInfo currentProcess() const;
    bool hideOnAttach() const;
    bool continueOnAttach() const;
    void startWatching();

    bool event(QEvent *) override;

signals:
    void processFound();

private:
    void pidFound(const Utils::ProcessInfo &p);
    void startStopWatching(bool start);
    void findProcess();
    void stopAndCheckExecutable();
    void kitChanged();

    void startStopTimer(bool start);
    bool checkExecutableString() const;
    void setWaitingState(UnstartedAppWatcherState state);

    const std::unique_ptr<UnstartedAppWatcherSettings> m_settings;
    QPushButton *m_watchingPushButton;
    Utils::ProcessInfo m_process;
    QSet<int> m_excluded;
    QTimer m_timer;
    std::optional<QPoint> m_lastPosition;

#ifdef WITH_TESTS
    friend class UnstartedAppWatcherDialogTest;
#endif
};

} // Debugger::Internal
