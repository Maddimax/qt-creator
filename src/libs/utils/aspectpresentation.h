// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QHash>
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
    // A field for a value that is not kept in the aspect and has to be gone
    // and got - a secret from the keychain. Written like a line edit, read
    // through displayText() once requestDisplayText() has delivered it.
    Secret,
    // Rows and columns, from the model the aspect hands out. See
    // BaseAspect::tableModel() and AspectTable below.
    Table,
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

// Label. Mirrors Qt::TextFormat. Markdown is never auto-detected, so an aspect
// whose message is written in it has to say so.
enum class TextFormat {
    AutoText,
    PlainText,
    RichText,
    MarkdownText,
};
Q_ENUM_NS(TextFormat)

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

// What a table aspect's model tells a view about a cell, beyond its text. The
// knowledge of which cells offer a choice, and of what, belongs to the model:
// it is the same for a QTableView and for a Qt Quick TableView, and it often
// depends on the row's other cells.
namespace AspectTable {

enum Role {
    // The cell's choices, as a list of maps with a "display" and an "id". A
    // view offers a combo box where this is not empty and a plain field
    // otherwise, and writes the id of what was picked, not its text.
    ChoicesRole = Qt::UserRole + 900,
    // A regular expression the cell's text has to match.
    ValidatorRole,
    // Whether the user may change the cell. A widget view reads flags() for
    // this, which a Qt Quick view cannot, so a table model answers both; see
    // isWritable() for what it comes to.
    EditableRole,
    // Whether the cell is a check box, reading and writing Qt::CheckStateRole.
    // Also flags(), and also unreadable from QML.
    CheckableRole,
};

// What EditableRole answers, from the cell's own flags: a field or a choice is
// editable and a check box is checkable, and either means the user may change
// what the cell holds.
inline bool isWritable(Qt::ItemFlags flags)
{
    return flags.testAnyFlags(Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
}

// The names the roles go by in QML. A table model adds them to its
// roleNames(), so that a Qt Quick view can read them off the cell.
inline QHash<int, QByteArray> withRoleNames(QHash<int, QByteArray> names)
{
    names.insert(ChoicesRole, "choices");
    names.insert(ValidatorRole, "validator");
    names.insert(EditableRole, "editable");
    names.insert(CheckableRole, "checkable");
    // Qt's own, named here because QML addresses a role by name and the
    // default roleNames() leaves these out. A row that is meant to be read in
    // its own colours - a list of syntax formats, say - answers the last three.
    names.insert(Qt::CheckStateRole, "checkState");
    names.insert(Qt::ForegroundRole, "foreground");
    names.insert(Qt::BackgroundRole, "background");
    names.insert(Qt::FontRole, "cellFont");
    return names;
}

} // namespace AspectTable

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

    // Table. The text in an empty filter field; no filter where it is empty.
    QString filterPlaceholderText;

    // StringList and Table.
    bool allowAdding = true;
    bool allowRemoving = true;
    bool allowEditing = true;

    // Label.
    AspectControls::InfoType infoType = AspectControls::InfoType::None;
    bool wordWrap = false;
    AspectControls::TextFormat textFormat = AspectControls::TextFormat::AutoText;

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
