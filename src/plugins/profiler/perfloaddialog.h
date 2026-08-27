// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace ProjectExplorer {
class Kit;
class KitChooser;
} // ProjectExplorer

namespace Profiler::Internal {

class PerfLoadSettings;

class PerfLoadDialog : public QDialog
{
public:
    explicit PerfLoadDialog(QWidget *parent = nullptr);
    ~PerfLoadDialog();

    QString traceFilePath() const;
    QString executableDirPath() const;
    ProjectExplorer::Kit *kit() const;

private:
    std::unique_ptr<PerfLoadSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createPerfLoadDialogTest();
#endif

} // namespace Profiler::Internal
