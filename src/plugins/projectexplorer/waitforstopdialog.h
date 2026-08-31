// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QList>
#include <QDialog>
#include <QElapsedTimer>

#include <memory>

namespace ProjectExplorer {

class RunControl;

namespace Internal {

class WaitForStopSettings;

#ifdef WITH_TESTS
QObject *createWaitForStopDialogTest();
#endif

class WaitForStopDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WaitForStopDialog(const QList<RunControl *> &runControls);
    ~WaitForStopDialog() override;

    bool canceled();
private:
    void updateProgressText();
    void runControlFinished(const RunControl *runControl);

    QList<ProjectExplorer::RunControl *> m_runControls;
    const std::unique_ptr<WaitForStopSettings> m_settings;
    QElapsedTimer m_timer;
};

} // namespace Internal
} // namespace ProjectExplorer
