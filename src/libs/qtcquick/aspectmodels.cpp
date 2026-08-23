// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectmodels.h"

#include "aspectcontainermodel.h"
#include "aspectitemlistmodel.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
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


AspectItemListModel *AspectModels::itemList(BaseAspect *aspect)
{
    auto list = qobject_cast<AspectList *>(aspect);
    QTC_ASSERT(list, return nullptr);

    if (auto existing = list->findChild<AspectItemListModel *>({}, Qt::FindDirectChildrenOnly))
        return existing;
    return new AspectItemListModel(list, list);
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
        {"valueIsChoiceId", p.valueIsChoiceId},
        // An aspect with no bound presents an unset minimum or maximum. The
        // delegates bind these straight into SpinBox.from/to, so substitute the
        // widest value of the right type rather than passing undefined to QML.
        {"minimum", p.minimum.isValid() ? p.minimum : widestBound(p.control, Lower)},
        {"maximum", p.maximum.isValid() ? p.maximum : widestBound(p.control, Upper)},
        {"step", p.singleStep.isValid() ? p.singleStep : QVariant(1)},
        {"allowAdding", p.allowAdding},
        {"allowRemoving", p.allowRemoving},
        {"allowEditing", p.allowEditing},
        {"actionText", p.actionText},
        // A password shares the String kind, and so the delegate, with an
        // ordinary line edit: it differs only in not echoing what it holds.
        {"password", p.control == AspectControls::PasswordLineEdit},
        {"placeholderText", p.placeholderText},
    };
}

} // namespace QtcQuick
