// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Utils { class AspectContainer; }

namespace QtcQuick {

// Renders a container's aspects as a Qt Quick form, hosted in a widget so that
// it can stand in for a hand-written settings page.
QTCQUICK_EXPORT QWidget *createAspectForm(Utils::AspectContainer *container);

} // namespace QtcQuick
