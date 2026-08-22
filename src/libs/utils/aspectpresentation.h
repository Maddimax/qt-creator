// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace Utils {

namespace AspectControls {
Q_NAMESPACE_EXPORT(QTCREATOR_UTILS_EXPORT)

// What an aspect wants drawn. Deliberately names controls rather than value
// types: two aspects of the same value type can want different controls, and
// the same control serves several value types.
enum Control {
    Custom,
    Container,
    Label,
    CheckBox,
    RadioButton,
    Toggle,
    LineEdit,
    PasswordLineEdit,
    TextEdit,
    PathChooser,
    ComboBox,
    RadioButtonGroup,
    SpinBox,
    DoubleSpinBox,
    ColorPicker,
    FontPicker,
    FontFamilyPicker,
    StringList,
    CommaSeparatedLineEdit,
    FilePathList,
    MultiSelection,
    IntegerList,
};
Q_ENUM_NS(Control)

} // namespace AspectControls

// A backend-neutral description of an aspect's control, so that a renderer does
// not have to know the aspect types. Only fields the control needs are filled.
class QTCREATOR_UTILS_EXPORT AspectPresentation
{
public:
    AspectControls::Control control = AspectControls::Custom;

    QString labelText;
    QString toolTip;
    QString placeholderText;

    // ComboBox, RadioButtonGroup and MultiSelection.
    QStringList choices;

    // SpinBox and DoubleSpinBox. Unset when the aspect has no bound.
    QVariant minimum;
    QVariant maximum;
    QVariant singleStep;

    bool readOnly = false;
    bool visible = true;
    bool enabled = true;
};

} // namespace Utils
