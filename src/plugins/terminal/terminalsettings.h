// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Terminal {

class TerminalSettings : public Utils::AspectContainer
{
public:
    TerminalSettings();

    Utils::BoolAspect enableTerminal{this};

    Utils::FontFamilyAspect fontFamily{this};
    Utils::IntegerAspect fontSize{this};
    Utils::FilePathAspect shell{this};
    Utils::StringAspect shellArguments{this};

    Utils::ColorAspect foregroundColor;
    Utils::ColorAspect backgroundColor;
    Utils::ColorAspect selectionColor;
    Utils::ColorAspect findMatchColor;

    Utils::ColorAspect colors[16];

    Utils::BoolAspect allowBlinkingCursor{this};

    Utils::ToggleAspect sendEscapeToTerminal{this};
    Utils::BoolAspect audibleBell{this};
    Utils::ToggleAspect lockKeyboard{this};

    Utils::BoolAspect enableMouseTracking{this};

    Utils::BoolAspect allowClipboardWrite{this};
    Utils::BoolAspect confirmUnsafePaste{this};

    // Windows only, see consolehost.h. The status says which console host is
    // in use and the action fetches one; both are hidden where there is no
    // choice to make.
    Utils::FilePathAspect consoleHostDirectory{this};
    Utils::StringAspect consoleHostStatus{this};
    Utils::ActionAspect downloadConsoleHost{this};

    Utils::ActionAspect loadTheme{this};
    Utils::ActionAspect resetTheme{this};
    Utils::ActionAspect copyTheme{this};
};

TerminalSettings &settings();

} // Terminal
