// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "filepath.h"

#include <QColor>
#include <QHash>
#include <QIcon>
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
    // No renderer knows what to do with it. An aspect that means to show
    // nothing says Invisible; Custom is what an aspect that has not been
    // described yet answers, and a page census treats it as a hole.
    Custom,
    // Shows nothing at all, which some aspects legitimately do: a value the
    // page reaches some other way, a container a page draws itself.
    Invisible,
    Container,
    Label,
    CheckBox,
    // On, off, or neither - a setting a project may leave to the global one,
    // a plugin the language server has not been told about. Only a check box
    // can say "neither" without a label per state; see Utils::TriStateAspect.
    TriStateCheckBox,
    RadioButton,
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
    // A list of sub-aspects one under the other, each drawn in full, with a
    // remove beside each and an add at the end. For a short list whose items
    // are one control each - the key sequences a command is mapped to.
    AspectInlineList,
    // A summary of the value plus one button that acts on it, for an aspect
    // edited through a dialog rather than in place.
    TextWithAction,
    // One button and nothing else, for a page action that has no value.
    Button,
    // A key sequence, typed in or recorded. A field with a button that starts
    // recording; what is being recorded is exactly the keys that would
    // otherwise be shortcuts, so the aspect takes them itself. See
    // BaseAspect::isRecording().
    KeySequence,
    // A field for a value that is not kept in the aspect and has to be gone
    // and got - a secret from the keychain. Written like a line edit, read
    // through displayText() once requestDisplayText() has delivered it.
    Secret,
    // Rows and columns, from the model the aspect hands out. See
    // BaseAspect::tableModel() and AspectTable below.
    Table,
    // Items in named groups - what was found and what the user added - with
    // one of them current. See Utils::GroupedListAspect.
    GroupedList,
    // A tree of values the page reports rather than lets the user set, from
    // the model the aspect hands out. See BaseAspect::tableModel().
    Tree,
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
    // A label of its own, next to the control rather than on it. A check box
    // draws its own text and draws it plain, so a label with a link in it -
    // "Use <a href=...>global settings</a>" - cannot be the box's own text.
    BesideControl,
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

// PathChooser. Mirrors Utils::PathChooserKind, which cannot be used here:
// pathvalidation.h pulls in more than a descriptor should.
enum class PathKind {
    ExistingDirectory,
    Directory,
    File,
    SaveFile,
    ExistingCommand,
    Command,
    Any,
};
Q_ENUM_NS(PathKind)

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
    // Text a row should be found by beyond what it shows - the glob patterns
    // of a MIME type, which are not a column but are what someone types into
    // the filter field. Asked of column 0 only.
    FilterTextRole,
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
    names.insert(FilterTextRole, "filterText");
    // Qt's own, named here because QML addresses a role by name and the
    // default roleNames() leaves these out. A row that is meant to be read in
    // its own colours - a list of syntax formats, say - answers the last three.
    names.insert(Qt::CheckStateRole, "checkState");
    names.insert(Qt::ForegroundRole, "foreground");
    names.insert(Qt::BackgroundRole, "background");
    names.insert(Qt::FontRole, "cellFont");
    // What a QTableView would have shown on hover. A cell whose text is elided,
    // or which stands for something longer - a name for a file path - has more
    // to say than it shows.
    names.insert(Qt::ToolTipRole, "cellToolTip");
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
    // A value shown as a label is elided when it does not fit, and then the
    // only way to read it is a tooltip of it. Aspects showing a path ask for
    // this; what they have to say about themselves is the path.
    bool toolTipShowsValue = false;
    QString placeholderText;

    // LineEdit. An icon drawn inside the field at its right, which the reader
    // can click - the aspect is told and decides what it means. Empty where
    // there is none, which is every aspect in this tree: it is an API a Lua
    // extension's settings can use, and one the widget line edit has always
    // drawn.
    FilePath rightSideIconPath;

    // LineEdit and TextEdit. What typing in the control completes against.
    // Empty where the aspect offers no completion, which is most of them.
    QStringList completions;

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
        // What a QComboBox would show beside the text. Only a list of things
        // that are told apart by more than their names has one - the device
        // types a kit can build for, so most choices leave it alone.
        QIcon icon = {};
    };
    QList<Choice> choices;

    // Whether the aspect's value is the id of the selected choice rather than
    // its index. An encoding selector stores the encoding name, for example.
    bool valueIsChoiceId = false;

    // SpinBox and DoubleSpinBox. Unset when the aspect has no bound.
    QVariant minimum;
    QVariant maximum;
    QVariant singleStep;

    // TextWithAction and Button. The button's label, and what it shows instead
    // of - or beside - it. Only a button that stands for something with a
    // picture has an icon: the kit icon, which is also what its menu changes.
    QString actionText;
    QIcon actionIcon;
    // Button. Whether the button does something of its own as well as offering
    // the choices below: clicking it acts, and the arrow beside it opens the
    // menu. Adding a device starts a wizard; the menu is the shortcut per kind.
    bool actionIsDefault = false;

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

    // PathChooser. What is being asked for, and what to put on the dialog that
    // asks for it.
    AspectControls::PathKind pathKind = AspectControls::PathKind::Any;
    QString promptDialogTitle;
    QString promptDialogFilter;
    // Whether the reader may pick a path on a device rather than on this
    // machine. The platform's own file dialog cannot: it only knows the
    // machine it runs on, which is why there is a file dialog of our own.
    bool allowPathFromDevice = false;

    // Table. The text in an empty filter field; no filter where it is empty.
    QString filterPlaceholderText;

    // Container. Whether the aspects inside it go side by side rather than one
    // under the other, and without a group box around them. Five choices that
    // read as one value - the parts of an ABI - are one row, not five.
    bool inlineRow = false;

    // Container. Whether what it holds goes straight into the layout around
    // it, a row each, rather than into a box of its own. An executable and the
    // alternative to it on a device are two rows of the form they sit in, not
    // a group of two settings. Ignored where inlineRow is set: that is the
    // same question answered the other way.
    bool flattened = false;

    // GroupedList. Whether one of the items is the default one, which not
    // every such list has - a list of toolchains does not.
    bool showsDefault = false;

    // StringList and Table.
    bool allowAdding = true;
    bool allowRemoving = true;
    bool allowEditing = true;

    // Table. The background the rows are meant to be *read against*, for a list
    // that shows what it is describing - the syntax formats of a colour scheme
    // are the case this exists for. Not the same as a row's own background: a
    // format that sets none is still read on the editor's background and not on
    // the form's, and the model has no way to say that per cell. Invalid to
    // leave the form's own background alone, which is the usual answer.
    QColor rowBackground;

    // AspectList. Whether the order of the items is the user's - path mappings
    // are tried in order, a list of servers is not - and so whether the list
    // offers Move Up and Move Down.
    bool allowReordering = false;

    // Label.
    AspectControls::InfoType infoType = AspectControls::InfoType::None;
    bool wordWrap = false;
    AspectControls::TextFormat textFormat = AspectControls::TextFormat::AutoText;

    // ColorPicker.
    bool alphaAllowed = true;
    QSize minimumSize;
    // ColorPicker and LineEdit.
    bool withResetButton = false;
    // What that button goes back to. Held here rather than asked of the aspect
    // because a renderer has a BaseAspect and defaultValue() is the typed
    // subclass's.
    QVariant defaultValue;

    // FontFamilyPicker.
    AspectControls::FontFilters fontFilters = AspectControls::AllFonts;

    // A checkable entry on the control's context menu, for a state that is
    // about the setting rather than about its value: whether a kit aspect may
    // be changed per run configuration. Empty text means no menu.
    QString contextActionText;
    bool contextActionChecked = false;
    bool contextActionEnabled = true;

    bool readOnly = false;
    bool visible = true;
    bool enabled = true;
};

} // namespace Utils

Q_DECLARE_OPERATORS_FOR_FLAGS(Utils::AspectControls::FontFilters)
