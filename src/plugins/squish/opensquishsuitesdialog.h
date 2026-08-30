// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
QT_END_NAMESPACE

namespace Squish::Internal {

class OpenSuitesSettings;

// What counts as a test suite under \a baseDir. See the definition.
Utils::FilePaths suiteDirectoriesIn(const Utils::FilePath &baseDir);

class OpenSquishSuitesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OpenSquishSuitesDialog(QWidget *parent = nullptr);
    ~OpenSquishSuitesDialog() override;

    Utils::FilePaths chosenSuites() const;

private:
    const std::unique_ptr<OpenSuitesSettings> m_settings;
    QDialogButtonBox * const m_buttonBox;
};

#ifdef WITH_TESTS
QObject *createOpenSquishSuitesTest();
#endif

} // namespace Squish::Internal
