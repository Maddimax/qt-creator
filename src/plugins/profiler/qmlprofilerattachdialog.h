// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/id.h>

#include <QDialog>

#include <memory>

namespace ProjectExplorer { class Kit; }

namespace Profiler::Internal {

class AttachSettings;


class QmlProfilerAttachDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QmlProfilerAttachDialog(QWidget *parent = nullptr);
    ~QmlProfilerAttachDialog() override;

    int port() const;
    void setPort(const int port);

    ProjectExplorer::Kit *kit() const;
    void setKitId(Utils::Id id);

private:
    const std::unique_ptr<AttachSettings> m_settings;
};

#ifdef WITH_TESTS
QObject *createQmlProfilerAttachSettingsTest();
#endif

} // namespace Profiler::Internal
