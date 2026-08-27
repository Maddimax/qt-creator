// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/pathchooser.h>

#include <vcsbase/vcsbaseplugin.h>

#include <QDialog>

#include <memory>

namespace Mercurial::Internal {

class SrcDestSettings;

class SrcDestDialog : public QDialog
{
public:
    enum Direction { outgoing, incoming };

    explicit SrcDestDialog(const VcsBase::VcsBasePluginState &state, Direction dir, QWidget *parent = nullptr);
    ~SrcDestDialog() override;

    QString getRepositoryString() const;
    Utils::FilePath workingDir() const;

private:
    QUrl getRepoUrl() const;

    Direction m_direction;
    mutable QString m_workingdir;
    VcsBase::VcsBasePluginState m_state;
    std::unique_ptr<SrcDestSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createSrcDestDialogTest();
#endif

} // Mercurial::Internal
