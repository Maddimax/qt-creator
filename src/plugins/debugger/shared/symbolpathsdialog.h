// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <utils/filepath.h>

#include <memory>

namespace Debugger::Internal {

class SymbolPathsSettings;

#ifdef WITH_TESTS
QObject *createSymbolPathsDialogTest();
#endif

class SymbolPathsDialog : public QDialog
{
public:
    explicit SymbolPathsDialog(QWidget *parent = nullptr);
    ~SymbolPathsDialog() override;

    bool useSymbolCache() const;
    bool useSymbolServer() const;
    Utils::FilePath path() const;

    void setUseSymbolCache(bool useSymbolCache);
    void setUseSymbolServer(bool useSymbolServer);
    void setPath(const Utils::FilePath &path);

    static bool useCommonSymbolPaths(bool &useSymbolCache, bool &useSymbolServer, Utils::FilePath &path);

private:
    const std::unique_ptr<SymbolPathsSettings> m_settings;
};

} // Debugger::Internal
