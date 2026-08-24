// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "profiler_global.h"

#include <projectexplorer/runconfiguration.h>

namespace Profiler {

// The events to record, as rows rather than as the strings they are stored as.
// The model does the translating; see Profiler::Internal::PerfConfigEventsModel.
class PROFILER_EXPORT PerfEventsAspect final : public Utils::StringListAspect
{
public:
    using StringListAspect::StringListAspect;

    void setTableModel(QAbstractItemModel *model) { m_model = model; }
    QAbstractItemModel *tableModel() override { return m_model; }

    Utils::AspectPresentation presentation() const override;

private:
    QAbstractItemModel *m_model = nullptr;
};

class PROFILER_EXPORT PerfSettings final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit PerfSettings(ProjectExplorer::Target *target = nullptr);
    ~PerfSettings() final;

    void readGlobalSettings();
    void writeGlobalSettings() const;

    void toMap(Utils::Store &map) const override;
    QString perfRecordArguments() const;

    QWidget *createPerfConfigWidget(ProjectExplorer::Target *target);

    Utils::IntegerAspect period{this};
    Utils::IntegerAspect stackSize{this};
    Utils::SelectionAspect sampleMode{this};
    Utils::SelectionAspect callgraphMode{this};
    PerfEventsAspect events{this};
    Utils::StringAspect extraArguments{this};
    Utils::ActionAspect resetToDefaults{this};
};

PerfSettings &globalSettings();

} // namespace Profiler
