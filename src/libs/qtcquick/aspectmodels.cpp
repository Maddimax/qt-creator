// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectmodels.h"

#include "aspectcontainermodel.h"
#include "aspectitemlistmodel.h"
#include "namedaspects.h"
#include "qtciconprovider.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>

#include <utils/qtcassert.h>

#include <QMimeData>

#include <QMetaEnum>

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


AspectItemListModel *AspectModels::itemList(BaseAspect *aspect)
{
    auto list = qobject_cast<AspectList *>(aspect);
    QTC_ASSERT(list, return nullptr);

    if (auto existing = list->findChild<AspectItemListModel *>({}, Qt::FindDirectChildrenOnly))
        return existing;
    return new AspectItemListModel(list, list);
}

NamedAspects *AspectModels::named(BaseAspect *aspect)
{
    auto container = qobject_cast<AspectContainer *>(aspect);
    QTC_ASSERT(container, return nullptr);

    if (auto existing = container->findChild<NamedAspects *>({}, Qt::FindDirectChildrenOnly))
        return existing;
    return new NamedAspects(container, container);
}

AspectContainerModel *AspectModels::container(BaseAspect *aspect)
{
    auto container = qobject_cast<AspectContainer *>(aspect);
    QTC_ASSERT(container, return nullptr);

    if (auto existing = container->findChild<AspectContainerModel *>({},
                                                                     Qt::FindDirectChildrenOnly))
        return existing;
    return new AspectContainerModel(container, container);
}

QAbstractItemModel *AspectModels::tableModel(BaseAspect *aspect)
{
    QTC_ASSERT(aspect, return nullptr);
    QAbstractItemModel *model = aspect->tableModel();
    if (model)
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

bool AspectModels::moveRow(QAbstractItemModel *model,
                           const QModelIndex &from,
                           const QModelIndex &toParent,
                           int toRow)
{
    QTC_ASSERT(model, return false);
    if (!from.isValid())
        return false;

    const std::unique_ptr<QMimeData> data(model->mimeData({from}));
    if (!data)
        return false;
    return model->dropMimeData(data.get(), Qt::MoveAction, toRow, 0, toParent);
}

bool AspectModels::namesItsColumns(QAbstractItemModel *model)
{
    if (!model)
        return false;
    // Validity, not emptiness: a model that says nothing about its columns
    // answers an invalid variant, where QAbstractItemModel's own default
    // answers the column number.
    for (int column = 0, count = model->columnCount(); column < count; ++column) {
        if (model->headerData(column, Qt::Horizontal).isValid())
            return true;
    }
    return false;
}

QString AspectModels::localPath(const QUrl &url)
{
    return url.toLocalFile();
}

QVariantMap AspectModels::presentation(BaseAspect *aspect)
{
    QTC_ASSERT(aspect, return {});
    const AspectPresentation p = aspect->presentation();

    // Ids as strings: a QByteArray id reaches QML as an ArrayBuffer, which
    // cannot be compared with indexOf().
    return {
        {"options", Utils::transform<QStringList>(p.choices,
                                                  &AspectPresentation::Choice::display)},
        {"optionIds", Utils::transform<QStringList>(p.choices,
                                                    [](const AspectPresentation::Choice &c) {
                                                        return c.id.toString();
                                                    })},
        // A choice can be there but not offered - ClearCase's external diff
        // where there is no "diff" on the PATH.
        {"optionsEnabled", Utils::transform<QVariantList>(
                               p.choices, [](const AspectPresentation::Choice &c) {
                                   return QVariant(c.enabled);
                               })},
        // As URLs: QML has no QIcon. Empty where a choice has none, which is
        // most of them. See QtcQuick::iconUrl().
        {"optionIcons", Utils::transform<QStringList>(
                            p.choices, [](const AspectPresentation::Choice &c) {
                                return c.icon.isNull() ? QString() : iconUrl(c.icon);
                            })},
        {"optionToolTips", Utils::transform<QStringList>(
                               p.choices, &AspectPresentation::Choice::toolTip)},
        // A control's context menu, for a state that is about the setting
        // rather than about its value.
        {"contextActionText", p.contextActionText},
        {"contextActionChecked", p.contextActionChecked},
        {"contextActionEnabled", p.contextActionEnabled},
        {"valueIsChoiceId", p.valueIsChoiceId},
        // An aspect with no bound presents an unset minimum or maximum. The
        // delegates bind these straight into SpinBox.from/to, so substitute the
        // widest value of the right type rather than passing undefined to QML.
        {"minimum", p.minimum.isValid() ? p.minimum : widestBound(p.control, Lower)},
        {"maximum", p.maximum.isValid() ? p.maximum : widestBound(p.control, Upper)},
        {"step", p.singleStep.isValid() ? p.singleStep : QVariant(1)},
        {"filterPlaceholderText", p.filterPlaceholderText},
        {"showsDefault", p.showsDefault},
        {"allowAdding", p.allowAdding},
        {"allowRemoving", p.allowRemoving},
        {"allowEditing", p.allowEditing},
        {"allowReordering", p.allowReordering},
        // ColorPicker and LineEdit: a control that can be put back to its
        // default offers a button for it.
        {"withResetButton", p.withResetButton},
        {"actionText", p.actionText},
        // A password shares the String kind, and so the delegate, with an
        // ordinary line edit: it differs only in not echoing what it holds.
        {"password", p.control == AspectControls::PasswordLineEdit},
        {"placeholderText", p.placeholderText},
        {"completions", p.completions},
        // SpinBox. Qt Quick's has no prefix or suffix of its own, so the
        // delegates put them beside it.
        {"prefix", p.prefix},
        {"suffix", p.suffix},
        // SpinBox. What a QSpinBox does with these, a Qt Quick SpinBox has to
        // be told: the stored value is displayScaleFactor times what is shown,
        // the minimum shows specialValueText instead of a number, and
        // displayIntegerBase is the base to write it in.
        {"specialValueText", p.specialValueText},
        {"displayScaleFactor", qlonglong(p.displayScaleFactor)},
        {"displayIntegerBase", p.displayIntegerBase},
        // As a name rather than a number, so the delegates can read it.
        {"infoType", QString::fromLatin1(
                         QMetaEnum::fromType<AspectControls::InfoType>().valueToKey(
                             int(p.infoType)))},
        {"textFormat", QString::fromLatin1(
                           QMetaEnum::fromType<AspectControls::TextFormat>().valueToKey(
                               int(p.textFormat)))},
        {"wordWrap", p.wordWrap},
        // PathChooser. What the browse button should ask for, as a name.
        {"pathKind", QString::fromLatin1(
                         QMetaEnum::fromType<AspectControls::PathKind>().valueToKey(
                             int(p.pathKind)))},
        {"promptDialogTitle", p.promptDialogTitle},
        {"promptDialogFilter", p.promptDialogFilter},
    };
}

} // namespace QtcQuick
