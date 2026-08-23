// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Utils { class AspectContainer; }

namespace QtcQuick {

// Renders a settings page with Qt Quick, or returns nullptr to leave it to its
// widget layout. Only a page that names its own QML file is taken: see the
// implementation for why the generic form is not offered to the rest.
QTCQUICK_EXPORT QWidget *createAspectForm(Utils::AspectContainer *container);

// Renders a container's aspects with the generic form built from
// AspectContainerModel, whatever they are, placeholders included. This is what
// a page looks like with no QML of its own written for it yet.
QTCQUICK_EXPORT QWidget *createGenericAspectForm(Utils::AspectContainer *container);

} // namespace QtcQuick
