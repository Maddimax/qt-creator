// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectcontainermodel.h"

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <limits>

using namespace Utils;

namespace QtcQuick {

namespace {

enum BoundKind { Lower, Upper };

QVariant widestBound(AspectControls::Control control, BoundKind kind)
{
    if (control == AspectControls::DoubleSpinBox) {
        return kind == Lower ? std::numeric_limits<double>::lowest()
                             : std::numeric_limits<double>::max();
    }
    return kind == Lower ? std::numeric_limits<int>::lowest()
                         : std::numeric_limits<int>::max();
}

} // namespace

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

    const AspectPresentation p = aspect->presentation();
    switch (role) {
    case KindRole:
        return int(kindOf(aspect));
    // labelText, toolTip and visibility are Q_PROPERTYs on the aspect, so the
    // delegates read them from there: one source of truth, and they follow
    // their NOTIFY signals rather than needing this model to emit dataChanged.
    // The delegates expect a plain string model; per-choice metadata stays
    // behind until they grow roles for it.
    case OptionsRole:
        return Utils::transform<QStringList>(p.choices, &AspectPresentation::Choice::display);
    // Ids as strings rather than as their own types: a QByteArray id reaches
    // QML as an ArrayBuffer, which cannot be compared with indexOf(), and
    // every aspect that takes an id converts from a string.
    case OptionIdsRole:
        return Utils::transform<QStringList>(p.choices, [](const AspectPresentation::Choice &c) {
            return c.id.toString();
        });
    case ValueIsChoiceIdRole:
        return p.valueIsChoiceId;
    case AllowAddingRole:   return p.allowAdding;
    case AllowRemovingRole: return p.allowRemoving;
    case AllowEditingRole:  return p.allowEditing;
    // An aspect with no bound presents an unset minimum/maximum. The delegates
    // bind these straight into SpinBox.from/to and DoubleValidator, so
    // substitute the widest value of the right type rather than passing
    // undefined into QML.
    case MinimumRole:   return p.minimum.isValid() ? p.minimum : widestBound(p.control, Lower);
    case MaximumRole:   return p.maximum.isValid() ? p.maximum : widestBound(p.control, Upper);
    case StepRole:      return p.singleStep.isValid() ? p.singleStep : QVariant(1);
    case ChildModelRole: {
        auto container = qobject_cast<AspectContainer *>(aspect);
        if (!container)
            return {};
        AspectContainerModel *&child = m_childModels[aspect];
        if (!child)
            child = new AspectContainerModel(container, const_cast<AspectContainerModel *>(this));
        return QVariant::fromValue(child);
    }
    default:            return {};
    }
}

QHash<int, QByteArray> AspectContainerModel::roleNames() const
{
    return {
        {AspectRole, "aspect"},
        {KindRole, "kind"},
        {OptionsRole, "options"},
        {OptionIdsRole, "optionIds"},
        {ValueIsChoiceIdRole, "valueIsChoiceId"},
        {AllowAddingRole, "allowAdding"},
        {AllowRemovingRole, "allowRemoving"},
        {AllowEditingRole, "allowEditing"},
        {MinimumRole, "minimum"},
        {MaximumRole, "maximum"},
        {StepRole, "step"},
        {ChildModelRole, "childModel"},
    };
}

AspectContainerModel::Kind AspectContainerModel::kindOf(const BaseAspect *aspect)
{
    QTC_ASSERT(aspect, return Unsupported);
    const AspectPresentation p = aspect->presentation();
    const Kind kind = kindOf(p.control);

    // A selection whose choices are not in its presentation cannot be rendered
    // generically: StringSelectionAspect fills a QStandardItemModel from an
    // async callback and reports no choices, which would otherwise give an
    // empty combo box and a type error from binding its value to currentIndex.
    if ((kind == Selection || kind == MultiSelection) && p.choices.isEmpty())
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
    case AspectControls::RadioButton:
    case AspectControls::Toggle:                 return Bool;
    case AspectControls::LineEdit:
    case AspectControls::PasswordLineEdit:
    case AspectControls::TextEdit:               return String;
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
