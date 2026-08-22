// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "aspects.h"

namespace Utils {

class QTCREATOR_UTILS_EXPORT TerminalCommandAspect : public Utils::AspectContainer
{
public:
    TerminalCommandAspect(AspectContainer *parentContainer);

    void addToLayoutImpl(Layouting::Layout &parent) override;

    Utils::FilePathAspect terminalEmulator{this};
    Utils::StringAspect terminalOpenArgs{this};
    Utils::StringAspect terminalExecuteArgs{this};
};

} // Utils
