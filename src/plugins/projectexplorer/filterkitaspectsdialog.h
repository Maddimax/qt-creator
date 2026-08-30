// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/id.h>

#include <QDialog>

#include <memory>

namespace ProjectExplorer {
class Kit;
namespace Internal {

class FilterKitAspectsSettings;

class FilterKitAspectsDialog : public QDialog
{
public:
    FilterKitAspectsDialog(const Kit *kit, QWidget *parent);
    ~FilterKitAspectsDialog() override;

    QSet<Utils::Id> irrelevantAspects() const;

private:
    const std::unique_ptr<FilterKitAspectsSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createFilterKitAspectsTest();
#endif

} // namespace Internal
} // namespace ProjectExplorer
