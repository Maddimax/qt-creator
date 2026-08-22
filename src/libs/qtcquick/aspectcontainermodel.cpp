// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectcontainermodel.h"

#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <limits>

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
    switch (role) {
    case AspectRole:
        return QVariant::fromValue(static_cast<QObject *>(aspect));
    case KindRole:
        return int(kindOf(aspect));
    case LabelTextRole:
        return aspect->labelText();
    case ToolTipRole:
        return aspect->toolTip();
    case VisibleRole:
        return aspect->isVisible();
    case MinimumRole:
        if (auto integer = qobject_cast<IntegerAspect *>(aspect))
            return integer->minimumValue().value_or(std::numeric_limits<int>::lowest());
        if (auto real = qobject_cast<DoubleAspect *>(aspect))
            return real->minimumValue().value_or(std::numeric_limits<double>::lowest());
        return {};
    case MaximumRole:
        if (auto integer = qobject_cast<IntegerAspect *>(aspect))
            return integer->maximumValue().value_or(std::numeric_limits<int>::max());
        if (auto real = qobject_cast<DoubleAspect *>(aspect))
            return real->maximumValue().value_or(std::numeric_limits<double>::max());
        return {};
    case StepRole:
        if (auto integer = qobject_cast<IntegerAspect *>(aspect))
            return integer->singleStep();
        if (auto real = qobject_cast<DoubleAspect *>(aspect))
            return real->singleStep();
        return {};
    case OptionsRole: {
        // SelectionAspect cannot be registered with QML because its base is a
        // template instantiation, so its options come through the model.
        auto selection = qobject_cast<SelectionAspect *>(aspect);
        if (!selection)
            return QStringList();
        QStringList options;
        for (int i = 0, n = selection->optionCount(); i < n; ++i)
            options.append(selection->displayForIndex(i));
        return options;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> AspectContainerModel::roleNames() const
{
    return {
        {AspectRole, "aspect"},
        {KindRole, "kind"},
        {LabelTextRole, "labelText"},
        {ToolTipRole, "toolTip"},
        {VisibleRole, "aspectVisible"},
        {OptionsRole, "options"},
        {MinimumRole, "minimum"},
        {MaximumRole, "maximum"},
        {StepRole, "step"},
    };
}

AspectContainerModel::Kind AspectContainerModel::kindOf(BaseAspect *aspect)
{
    // Order matters: the more derived aspects have to be tested first.
    if (qobject_cast<AspectContainer *>(aspect))
        return Container;
    if (qobject_cast<Utils::TextDisplay *>(aspect))
        return TextDisplay;
    if (qobject_cast<FilePathAspect *>(aspect))
        return FilePath;
    if (qobject_cast<BoolAspect *>(aspect))
        return Bool;
    if (qobject_cast<SelectionAspect *>(aspect))
        return Selection;
    if (qobject_cast<IntegerAspect *>(aspect))
        return Integer;
    if (qobject_cast<DoubleAspect *>(aspect))
        return Double;
    if (qobject_cast<StringListAspect *>(aspect))
        return StringList;
    if (qobject_cast<StringAspect *>(aspect))
        return String;
    return Unsupported;
}

} // namespace QtcQuick
