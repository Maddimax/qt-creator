// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QObject>
#include <QQmlEngine>

namespace Utils { class BaseAspect; }

namespace QtcQuick {

class AspectContainerModel;
class AspectItemListModel;

// The models a delegate needs for an aspect that holds other aspects. Reached
// from QML so that a delegate works both inside the generic form, where the
// repeater supplies the aspect, and in a hand-written page, where only the
// aspect is at hand.
class QTCQUICK_EXPORT AspectModels : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Both are cached on the aspect, so a page and the generic form share one.
    Q_INVOKABLE QtcQuick::AspectItemListModel *itemList(Utils::BaseAspect *aspect);
    Q_INVOKABLE QtcQuick::AspectContainerModel *container(Utils::BaseAspect *aspect);

    // Everything a delegate needs beyond the aspect's own properties: the
    // bounds, the choices, what may be added or removed. Read from the aspect
    // rather than taken as model roles, because a hand-written page has no
    // roles to give.
    Q_INVOKABLE QVariantMap presentation(Utils::BaseAspect *aspect);
};

} // namespace QtcQuick
