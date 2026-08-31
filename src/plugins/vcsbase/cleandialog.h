// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"

#include <QDialog>

namespace Utils { class FilePath; }

namespace VcsBase {

namespace Internal {
class CleanDialogPrivate;
#ifdef WITH_TESTS
QObject *createCleanDialogTest();
#endif
} // namespace Internal

class VCSBASE_EXPORT CleanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CleanDialog(QWidget *parent = nullptr);
    ~CleanDialog() override;

    void setFileList(const Utils::FilePath &workingDirectory, const QStringList &files,
                     const QStringList &ignoredFiles);

public slots:
    void accept() override;

private:
    QStringList checkedFiles() const;
    bool promptToDelete();

    Internal::CleanDialogPrivate *const d;
};

} // namespace VcsBase
