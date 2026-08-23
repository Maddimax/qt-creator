// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QString>
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
    // A list of sub-aspects with a details pane, add and remove.
    AspectList,
    // A summary of the value plus one button that acts on it, for an aspect
    // edited through a dialog rather than in place.
    TextWithAction,
    // One button and nothing else, for a page action that has no value.
    Button,
};
Q_ENUM_NS(Control)

// Where the label goes. The semantics are BoolAspect::LabelPlacement's:
// AtControl puts the text on the control itself in the field column, Compact
// does the same without reserving a label column, ShowTip repeats the tool
// tip in a sub-label under the control.
enum class LabelPlacement {
    InExtraLabel,
    AtControl,
    Compact,
    ShowTip,
};
Q_ENUM_NS(LabelPlacement)

// Label. Mirrors Utils::InfoLabelType, reordered so that "no icon" is the
// value-initialized default.
enum class InfoType {
    None,
    Information,
    Warning,
    Error,
    Ok,
    NotOk,
};
Q_ENUM_NS(InfoType)

// ComboBox. Mirrors QComboBox::SizeAdjustPolicy, with only the two values
// aspects ask for.
enum class SizeAdjustPolicy {
    ToMinimumContentsLengthWithIcon,
    ToContents,
};
Q_ENUM_NS(SizeAdjustPolicy)

// FontFamilyPicker. Mirrors FontFamilyAspect::FontFilter.
enum FontFilter {
    AllFonts = 0,
    ScalableFonts = 0x1,
    NonScalableFonts = 0x2,
    MonospacedFonts = 0x4,
    ProportionalFonts = 0x8,
};
Q_DECLARE_FLAGS(FontFilters, FontFilter)
Q_FLAG_NS(FontFilters)

} // namespace AspectControls

using InfoType = AspectControls::InfoType;

// A backend-neutral description of an aspect's control, so that a renderer does
// not have to know the aspect types. Only fields the control needs are filled.
class QTCREATOR_UTILS_EXPORT AspectPresentation
{
public:
    AspectControls::Control control = AspectControls::Custom;

    QString labelText;
    QPixmap labelPixmap;
    AspectControls::LabelPlacement labelPlacement = AspectControls::LabelPlacement::InExtraLabel;
    int spanX = 1;
    int spanY = 1;

    QString toolTip;
    QString placeholderText;

    // Object name for the control itself, for styling and for tests that
    // address it by name. Empty leaves whatever the renderer picked.
    QString objectName;

    // ComboBox, RadioButtonGroup and MultiSelection. Writes back to the
    // aspect must use the id where it is set, not the display text.
    class Choice
    {
    public:
        QString display;
        QString toolTip;
        bool enabled = true;
        QVariant id;
    };
    QList<Choice> choices;

    // Whether the aspect's value is the id of the selected choice rather than
    // its index. An encoding selector stores the encoding name, for example.
    bool valueIsChoiceId = false;

    // SpinBox and DoubleSpinBox. Unset when the aspect has no bound.
    QVariant minimum;
    QVariant maximum;
    QVariant singleStep;

    // TextWithAction. The button's label.
    QString actionText;

    QString prefix;
    QString suffix;
    QString specialValueText;

    // SpinBox. The control shows the value divided by displayScaleFactor.
    int displayIntegerBase = 10;
    qint64 displayScaleFactor = 1;

    // ComboBox.
    AspectControls::SizeAdjustPolicy sizeAdjustPolicy
        = AspectControls::SizeAdjustPolicy::ToMinimumContentsLengthWithIcon;
    int minimumContentsLength = 0;

    // StringList.
    bool allowAdding = true;
    bool allowRemoving = true;
    bool allowEditing = true;

    // Label.
    AspectControls::InfoType infoType = AspectControls::InfoType::None;
    bool wordWrap = false;

    // ColorPicker.
    bool alphaAllowed = true;
    QSize minimumSize;
    // ColorPicker and LineEdit.
    bool withResetButton = false;

    // FontFamilyPicker.
    AspectControls::FontFilters fontFilters = AspectControls::AllFonts;

    bool readOnly = false;
    bool visible = true;
    bool enabled = true;
};

} // namespace Utils

Q_DECLARE_OPERATORS_FOR_FLAGS(Utils::AspectControls::FontFilters)
