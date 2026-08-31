// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace ClangTools::Internal {

class FilterSettings;

#ifdef WITH_TESTS
QObject *createFilterDialogTest();
#endif

class Check
{
public:
    QString name;
    QString displayName;
    int count = 0;
    bool isShown = false;
    bool hasFixit = false;
};
using Checks = QList<Check>;

class FilterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FilterDialog(const Checks &selectedChecks, QWidget *parent = nullptr);
    ~FilterDialog();

    QSet<QString> selectedChecks() const;

private:
    const std::unique_ptr<FilterSettings> d;
};

} // ClangTools::Internal
