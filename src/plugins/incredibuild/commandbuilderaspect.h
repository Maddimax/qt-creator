// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/buildstep.h>

#include <utils/aspects.h>

namespace IncrediBuild::Internal {

// Which helper works out the build command, and the command and arguments it
// came up with. Three rows of the step that holds them rather than a group of
// their own, so the container is flattened.
class CommandBuilderAspect final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit CommandBuilderAspect(ProjectExplorer::BuildStep *step);
    ~CommandBuilderAspect() final;

    QString fullCommandFlag(bool keepJobNum) const;

    Utils::SelectionAspect helper{this};
    Utils::FilePathAspect command{this};
    Utils::StringAspect arguments{this};

private:
    void fromMap(const Utils::Store &map) final;
    void toMap(Utils::Store &map) const final;

    // Drawn for the first time. A step that was just added adopts whatever
    // preceding step it can build for, and that is only knowable once the
    // settings have been read - or not read, for a step nobody restored.
    void requestDisplayText() final;

    // What the active helper says the command and arguments are, and what it
    // would use if they were left alone.
    void showActiveHelper();

    class CommandBuilderAspectPrivate *d = nullptr;
};

} // IncrediBuild::Internal
