// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QVariantMap>
#include <QDialog>

#include <memory>

namespace QbsProjectManager::Internal {

class CustomPropertiesSettings;

class CustomQbsPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CustomQbsPropertiesDialog(const QVariantMap &properties, QWidget *parent = nullptr);
    ~CustomQbsPropertiesDialog() override;

    QVariantMap properties() const;

private:
    const std::unique_ptr<CustomPropertiesSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createCustomQbsPropertiesTest();
#endif

} // namespace QbsProjectManager::Internal
