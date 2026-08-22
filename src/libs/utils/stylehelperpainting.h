// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QIcon>
#include <QStyle>

QT_BEGIN_NAMESPACE
class QLabel;
class QPainter;
class QStyleOption;
class QWidget;
QT_END_NAMESPACE

namespace Utils::StyleHelper {

class TextFormat;

// The QtWidgets-dependent part of StyleHelper. Kept out of stylehelper.h so
// that a file which only wants a token or a colour does not pull in QtWidgets.

// Draws a shaded anti-aliased arrow
QTCREATOR_UTILS_EXPORT void drawArrow(QStyle::PrimitiveElement element, QPainter *painter,
                                      const QStyleOption *option);
QTCREATOR_UTILS_EXPORT void drawMinimalArrow(QStyle::PrimitiveElement element, QPainter *painter,
                                             const QStyleOption *option);

QTCREATOR_UTILS_EXPORT void drawIconWithShadow(const QIcon &icon, const QRect &rect, QPainter *p,
                                               QIcon::Mode iconMode, QIcon::State iconState,
                                               int dipRadius = 3,
                                               const QColor &color = QColor(0, 0, 0, 130),
                                               const QPoint &dipOffset = QPoint(1, -2));

QTCREATOR_UTILS_EXPORT void applyTf(QLabel *label, const TextFormat &tf,
                                    bool singleLine = true);

// Sets the base color and makes sure all top level widgets are updated
QTCREATOR_UTILS_EXPORT void setBaseColor(const QColor &color);

QTCREATOR_UTILS_EXPORT QIcon getIconFromIconFont(const QString &fontName,
                                                 const QString &iconSymbol, int fontSize,
                                                 int iconSize);

QTCREATOR_UTILS_EXPORT void setPanelWidget(QWidget *widget, bool value = true);
QTCREATOR_UTILS_EXPORT void setPanelWidgetSingleRow(QWidget *widget, bool value = true);

// modifies widget's palette QPalette::Base to color, leaves other colors of palette untouched
QTCREATOR_UTILS_EXPORT void modifyPaletteBase(QWidget *widget, const QColor &color);
// sets widget's background to colorRole from theme
QTCREATOR_UTILS_EXPORT void setBackgroundColor(QWidget *widget, Theme::Color colorRole);

} // namespace Utils::StyleHelper
