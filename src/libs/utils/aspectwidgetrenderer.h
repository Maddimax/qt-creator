// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

namespace Layouting { class Layout; }

namespace Utils {

class BaseAspect;

namespace Internal {

// Builds the QtWidgets control described by aspect.presentation(). Returns
// false for a control it does not handle.
QTCREATOR_UTILS_EXPORT bool renderAspectToLayout(BaseAspect &aspect, Layouting::Layout &parent);

} // namespace Internal

// Installs the QtWidgets implementation of the aspect renderer. Call once at startup.
QTCREATOR_UTILS_EXPORT void installAspectWidgetRenderer();

} // namespace Utils
