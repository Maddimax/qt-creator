// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "aspects.h"

namespace Utils {

// Which terminal to open, and with what. On the page it is one row: what the
// three fields come to, a button that opens them in a dialog, and a menu of
// the emulators this machine has. The fields themselves are only shown in that
// dialog, which is why they are a container of their own - a container can be
// hidden where it is listed without hiding what it holds.
class QTCREATOR_UTILS_EXPORT TerminalCommandAspect : public Utils::AspectContainer
{
public:
    TerminalCommandAspect(AspectContainer *parentContainer);

    Utils::AspectContainer command{this};
    Utils::FilePathAspect terminalEmulator{&command};
    Utils::StringAspect terminalOpenArgs{&command};
    Utils::StringAspect terminalExecuteArgs{&command};

    Utils::ActionAspect customize{this};
    Utils::ActionAspect presets{this};

private:
    QString summary() const;
    void openDialog();
};

} // Utils
