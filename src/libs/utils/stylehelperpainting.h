// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QStyle>

QT_BEGIN_NAMESPACE
class QPainter;
class QStyleOption;
QT_END_NAMESPACE

namespace Utils::StyleHelper {

// Kept out of stylehelper.h because QStyle::PrimitiveElement is a nested enum
// and cannot be forward declared, so the include would put QtWidgets in every
// file that only wants a token or a colour.

// Draws a shaded anti-aliased arrow
QTCREATOR_UTILS_EXPORT void drawArrow(QStyle::PrimitiveElement element, QPainter *painter,
                                      const QStyleOption *option);
QTCREATOR_UTILS_EXPORT void drawMinimalArrow(QStyle::PrimitiveElement element, QPainter *painter,
                                             const QStyleOption *option);

} // namespace Utils::StyleHelper
