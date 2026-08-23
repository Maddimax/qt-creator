// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QQmlEngine>
#include <QQmlPropertyMap>

namespace Utils { class AspectContainer; }

namespace QtcQuick {

// The aspects of a container, reachable by name so that a hand-written page can
// write "aspects.HeaderSuffix". The names come from BaseAspect::qmlName();
// aspects without one - typically those with no settings key - are not
// reachable and have to be named explicitly to become so.
class QTCQUICK_EXPORT NamedAspects : public QQmlPropertyMap
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created from C++ by createAspectForm() or AspectModels.named()")

public:
    explicit NamedAspects(Utils::AspectContainer *container, QObject *parent = nullptr);
};

} // namespace QtcQuick
