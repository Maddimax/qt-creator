// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/id.h>

#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
QT_END_NAMESPACE

namespace ProjectExplorer {
class IDeviceFactory;

namespace Internal {

class DeviceFactorySelectionSettings;

#ifdef WITH_TESTS
QObject *createDeviceFactorySelectionDialogTest();
#endif

class DeviceFactorySelectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceFactorySelectionDialog(QWidget *parent = nullptr);
    ~DeviceFactorySelectionDialog() override;
    Utils::Id selectedId() const;

private:
    const std::unique_ptr<DeviceFactorySelectionSettings> m_settings;
    QDialogButtonBox *m_buttonBox;
};

} // namespace Internal
} // namespace ProjectExplorer
