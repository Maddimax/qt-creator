// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../mcusupport_global.h"
#include "../settingshandler.h"

#include <QDialog>

#include <memory>

namespace McuSupport::Internal {

class McuKitCreationSettings;

#ifdef WITH_TESTS
QObject *createMcuKitCreationDialogTest();
#endif

class McuKitCreationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit McuKitCreationDialog(const MessagesList &messages,
                                  const SettingsHandler::Ptr &settingsHandler,
                                  McuPackagePtr qtMCUPackage,
                                  QWidget *parent = nullptr);
    ~McuKitCreationDialog() override;

private slots:
    void updateMessage(const int inc);

private:
    int m_currentIndex = -1;
    const std::unique_ptr<McuKitCreationSettings> m_settings;
    const MessagesList &m_messages;
};
} // namespace McuSupport::Internal
