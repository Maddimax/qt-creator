// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Perforce::Internal {

class ChangeNumberSettings;

// Asks for a change number to describe.
class ChangeNumberDialog : public QDialog
{
public:
    explicit ChangeNumberDialog(QWidget *parent = nullptr);
    ~ChangeNumberDialog() override;

    // The number typed, or 0 for none - which is what the caller treats as
    // "nothing to describe", the same as the -1 a line edit used to answer.
    int number() const;

private:
    std::unique_ptr<ChangeNumberSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createChangeNumberDialogTest();
#endif

} // Perforce::Internal
