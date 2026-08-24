// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectcontainermodel.h"

#include "aspectitemlistmodel.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>


using namespace Utils;

namespace QtcQuick {


AspectContainerModel::AspectContainerModel(AspectContainer *container, QObject *parent)
    : QAbstractListModel(parent)
{
    QTC_ASSERT(container, return);
    m_aspects = container->aspects();
}

int AspectContainerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_aspects.size());
}

QVariant AspectContainerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_aspects.size())
        return {};

    BaseAspect *aspect = m_aspects.at(index.row());
    if (role == AspectRole)
        return QVariant::fromValue(static_cast<QObject *>(aspect));

    switch (role) {
    case KindRole:
        return int(kindOf(aspect));
    default:
        return {};
    }
}

QHash<int, QByteArray> AspectContainerModel::roleNames() const
{
    // Only what picks a delegate and what it is for. Everything else a delegate
    // needs it reads from the aspect through AspectModels, so that the same
    // delegate works in a hand-written page, which has no roles to give.
    return {
        {AspectRole, "aspect"},
        {KindRole, "kind"},
    };
}

AspectContainerModel::Kind AspectContainerModel::kindOf(const BaseAspect *aspect)
{
    QTC_ASSERT(aspect, return Unsupported);
    const AspectPresentation p = aspect->presentation();
    const Kind kind = kindOf(p.control);

    // An id-valued selection with nothing to match against cannot show which
    // choice is current. An index-valued one with no choices is just an empty
    // combo box, which is what the widget editor draws for it too.
    if (kind == Selection && p.valueIsChoiceId && p.choices.isEmpty())
        return Unsupported;
    if (kind == MultiSelection && p.choices.isEmpty())
        return Unsupported;

    return kind;
}

bool AspectContainerModel::isFullyRenderable(const AspectContainer *container)
{
    QTC_ASSERT(container, return false);

    const QList<BaseAspect *> aspects = container->aspects();
    for (const BaseAspect *aspect : aspects) {
        const Kind kind = kindOf(aspect);
        if (kind == Unsupported)
            return false;
        if (kind == Container) {
            auto nested = qobject_cast<const AspectContainer *>(aspect);
            if (!nested || !isFullyRenderable(nested))
                return false;
        }
    }
    return true;
}

AspectContainerModel::Kind AspectContainerModel::kindOf(AspectControls::Control control)
{
    switch (control) {
    case AspectControls::Container:              return Container;
    case AspectControls::Label:                  return TextDisplay;
    case AspectControls::CheckBox:
    case AspectControls::Toggle:                 return Bool;
    case AspectControls::RadioButton:            return Radio;
    case AspectControls::LineEdit:
    case AspectControls::PasswordLineEdit:       return String;
    case AspectControls::TextEdit:               return Text;
    case AspectControls::PathChooser:            return FilePath;
    case AspectControls::ComboBox:
    case AspectControls::RadioButtonGroup:       return Selection;
    case AspectControls::SpinBox:                return Integer;
    case AspectControls::DoubleSpinBox:          return Double;
    case AspectControls::CommaSeparatedLineEdit: return StringList;
    case AspectControls::FilePathList:           return FilePathList;
    case AspectControls::MultiSelection:         return MultiSelection;
    case AspectControls::ColorPicker:            return Color;
    case AspectControls::FontFamilyPicker:       return FontFamily;
    case AspectControls::StringList:             return StringListEditor;
    case AspectControls::AspectList:             return AspectList;
    case AspectControls::TextWithAction:         return TextWithAction;
    case AspectControls::Button:                 return Button;
    case AspectControls::Secret:                 return Secret;
    case AspectControls::Table:                  return Table;
    // IntegersAspect draws nothing in the widget path either, so drawing
    // nothing here is parity rather than a gap.
    case AspectControls::IntegerList:            return Invisible;
    // FontAspect is an AspectContainer, not a TypedAspect: it has no
    // volatileVariantValue() override, so its "value" property is always
    // invalid and writing to it hits BaseAspect's QTC_CHECK(false).
    case AspectControls::FontPicker:
    case AspectControls::Custom:                 return Unsupported;
    }
    return Unsupported;
}

} // namespace QtcQuick
