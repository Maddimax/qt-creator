// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/processinfo.h>

#include <QDialog>

#include <optional>

#include <memory>

QT_BEGIN_NAMESPACE
class QPushButton;
QT_END_NAMESPACE

namespace Profiler::Internal {

class ProcessPickerSettings;

#ifdef WITH_TESTS
QObject *createProcessPickerDialogTest();
#endif

// A minimal "attach to process" picker: lists the running processes with a
// type-to-filter field and returns the one the user chooses. Self-contained so
// it needs no ProjectExplorer dependency.
class ProcessPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProcessPickerDialog(QWidget *parent = nullptr);
    ~ProcessPickerDialog() override;

    std::optional<Utils::ProcessInfo> selectedProcess() const;

    // Shows the dialog modally; returns the chosen process, or nullopt if the
    // user cancelled.
    static std::optional<Utils::ProcessInfo> pickProcess(QWidget *parent = nullptr);

private:
    void updateOkButton();

    const std::unique_ptr<ProcessPickerSettings> d;
    QPushButton *m_okButton = nullptr;
};

} // namespace Profiler::Internal
