// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

namespace Debugger::Internal {

class CdbBreakEventsAspectPrivate;

// Which "sxe" events stop the debugger, as CDB spells them: "eh" for an event
// that is simply on, "out:Needle" for one with a filter. The rows are fixed -
// the events CDB knows about - so tableModel() hands out check states and
// filter text rather than a list to add to.
class CdbBreakEventsAspect final : public Utils::TypedAspect<QStringList>
{
public:
    explicit CdbBreakEventsAspect(Utils::AspectContainer *container = nullptr);
    ~CdbBreakEventsAspect() override;

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

    bool isDirty() const override;

private:
    bool guiToVolatileValue() override;
    void volatileValueToGui() override;

    CdbBreakEventsAspectPrivate *d = nullptr;
};

class CdbOptionsPage final : public Core::IOptionsPage
{
public:
    CdbOptionsPage();
};

class CdbPathsPage final : public Core::IOptionsPage
{
public:
    CdbPathsPage();
};

} // namespace Debugger::Internal
