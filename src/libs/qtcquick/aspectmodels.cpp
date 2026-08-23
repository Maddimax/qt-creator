// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectmodels.h"

#include "aspectcontainermodel.h"
#include "aspectitemlistmodel.h"

#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

using namespace Utils;

namespace QtcQuick {

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

} // namespace QtcQuick
