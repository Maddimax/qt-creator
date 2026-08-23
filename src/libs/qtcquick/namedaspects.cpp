// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "namedaspects.h"

#include <utils/aspects.h>
#include <utils/qtcassert.h>

using namespace Utils;

namespace QtcQuick {

NamedAspects::NamedAspects(AspectContainer *container, QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    QTC_ASSERT(container, return);

    const QList<BaseAspect *> aspects = container->aspects();
    for (BaseAspect *aspect : aspects) {
        const QString name = aspect->qmlName();
        if (name.isEmpty())
            continue;
        // Two aspects deriving the same name from their settings keys would
        // leave one of them silently unreachable. Say so; the fix is
        // setQmlName() on one of them.
        QTC_ASSERT(!contains(name), continue);
        insert(name, QVariant::fromValue(static_cast<QObject *>(aspect)));
    }
}

} // namespace QtcQuick
