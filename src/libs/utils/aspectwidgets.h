// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "aspects.h"

#include <QList>

#include <functional>
#include <type_traits>

QT_BEGIN_NAMESPACE
class QAbstractButton;
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

// Installed by a front end that can render an AspectContainer itself, e.g. in
// Qt Quick. Consulted before the container's own layouter; returning nullptr
// falls back to the widget path.
//
// Here rather than in coreplugin because the layers below it have forms too -
// the plugin view, and Utils' own dialogs - and none of them can call up into
// a plugin. Core::createAspectForm() forwards to this.
using AspectFormFactory = std::function<QWidget *(AspectContainer *)>;
QTCREATOR_UTILS_EXPORT void setAspectFormFactory(const AspectFormFactory &factory);

// A settings form for a container. Uses the factory where it takes the
// container and the container's own layouter otherwise, so that every surface
// shows the same thing.
QTCREATOR_UTILS_EXPORT QWidget *createAspectForm(AspectContainer *container);

// The same, for a container that has no page of its own to name: its aspects
// are listed generically, in order. Null where Qt Quick is not there to draw
// it, so a caller can keep whatever it did before.
QTCREATOR_UTILS_EXPORT void setGenericAspectFormFactory(const AspectFormFactory &factory);
QTCREATOR_UTILS_EXPORT QWidget *createGenericAspectForm(AspectContainer *container);

// Places an existing check box or radio button according to the aspect's label
// placement and ties the two together. The renderer and adoptButton() differ
// only in who creates the button.
QTCREATOR_UTILS_EXPORT void addButtonToLayout(BaseAspect *aspect,
                                              Layouting::Layout &parent,
                                              QAbstractButton *button);

// A layouter that places a button the caller owns - for example one that is
// already in a QButtonGroup with a button of its own.
QTCREATOR_UTILS_EXPORT std::function<void(Layouting::Layout *)> adoptButton(
    BoolAspect *aspect, QAbstractButton *button);

// A layouter that turns a QGroupBox into the aspect's check box.
QTCREATOR_UTILS_EXPORT std::function<void(QObject *)> groupChecker(BoolAspect *aspect);

// A controller that ties a widget's visibility to the aspect's, for
// Layouting's visibleOn().
QTCREATOR_UTILS_EXPORT std::function<void(QObject *)> visibleController(BaseAspect *aspect);

// How a container lays itself out with the widget backend. Held as opaque
// backend data on the container, so that the aspects themselves carry no
// Layouting dependency.
using Layouter = std::function<Layouting::Layout()>;
QTCREATOR_UTILS_EXPORT void setLayouter(AspectContainer *container, const Layouter &layouter);
QTCREATOR_UTILS_EXPORT Layouter layouter(const AspectContainer *container);
// Whether one was set. layouter() never hands back an empty function - it
// falls back to a column of the aspects - so it cannot answer this, and a
// caller that wants to know whether the container said anything has to ask.
QTCREATOR_UTILS_EXPORT bool hasLayouter(const AspectContainer *container);

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
