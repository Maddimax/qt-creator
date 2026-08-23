// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "aspects.h"

#include <QList>

#include <type_traits>

QT_BEGIN_NAMESPACE
class QAbstractSpinBox;
class QComboBox;
class QLabel;
class QWidget;
QT_END_NAMESPACE

namespace Layouting { class Layout; }

namespace Utils::AspectWidgets {

// The widget side of an aspect, for aspects that build their own control
// instead of leaving it to the renderer. Free functions rather than BaseAspect
// members because an exported class cannot have its member definitions split
// across two libraries, and these belong with the widgets, not the aspects.

// Applies the aspect's enabled state, tool tip, visibility and read-only state
// to a control, and ties the control's lifetime to the aspect's.
QTCREATOR_UTILS_EXPORT void registerSubWidget(BaseAspect *aspect, QWidget *widget);

// Stops a control from reacting to the wheel while it does not have focus.
QTCREATOR_UTILS_EXPORT void improveWheelScrolling(QWidget *widget);

// The aspect's label, or nullptr when it has neither text nor pixmap.
QTCREATOR_UTILS_EXPORT QLabel *createLabel(BaseAspect *aspect);

// createLabel() plus the control, spanning the aspect's remaining columns.
// Returns the label, or nullptr when there is none.
QTCREATOR_UTILS_EXPORT QLabel *addLabeledItem(BaseAspect *aspect,
                                              Layouting::Layout &parent,
                                              QWidget *widget);
QTCREATOR_UTILS_EXPORT void addLabeledItems(BaseAspect *aspect,
                                            Layouting::Layout &parent,
                                            const QList<QWidget *> &widgets);

// Lets the user pick a macro for the control's text.
QTCREATOR_UTILS_EXPORT void addMacroExpansion(BaseAspect *aspect, QWidget *widget);

// The widget from setConfigWidgetCreator(), or nullptr when none was set.
QTCREATOR_UTILS_EXPORT QWidget *createConfigWidget(BaseAspect *aspect);

template<class Widget, typename... Args>
Widget *createSubWidget(BaseAspect *aspect, Args &&...args)
{
    auto w = new Widget(args...);
    registerSubWidget(aspect, w);
    if constexpr (std::is_base_of_v<QComboBox, Widget>
                  || std::is_base_of_v<QAbstractSpinBox, Widget>) {
        improveWheelScrolling(w);
    }
    return w;
}

} // namespace Utils::AspectWidgets
