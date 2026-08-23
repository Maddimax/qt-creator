// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectform.h"

#include "aspectcontainermodel.h"
#include "namedaspects.h"
#include "qtcquickwidget.h"

#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/qtcassert.h>

#include <QQuickWidget>

namespace QtcQuick {

QWidget *createGenericAspectForm(Utils::AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    auto widget = new QuickWidget;
    auto model = new AspectContainerModel(container, widget);
    widget->quickWidget()->setInitialProperties({{"model", QVariant::fromValue(model)}});
    widget->setSource(QUrl("qrc:/qt/qml/QtCreator/Ui/AspectForm.qml"));
    return widget;
}

QWidget *createAspectForm(Utils::AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    // A page with its own QML lays itself out and reaches its aspects by name.
    if (const QUrl source = container->qmlSource(); !source.isEmpty()) {
        auto widget = new QuickWidget;
        auto named = new NamedAspects(container, widget);
        widget->quickWidget()->setInitialProperties({{"aspects", QVariant::fromValue(named)}});
        widget->setSource(source);
        return widget;
    }

    // A page without one keeps its widget layout, even where the generic form
    // could show all of its aspects. It is the layouter that decides what a
    // page shows: it picks aspects, arranges them, labels them, and may build
    // widgets of its own - the tool table on the Qt Creator MCP Server page,
    // for one. The generic form shows the container's aspects in order and
    // nothing else, so it drops all of that, and none of it can be seen from
    // the aspects to tell the two cases apart. Set QTC_QUICK_SETTINGS to render
    // every page generically anyway, which is how to see what one looks like
    // before writing its QML.
    if (Utils::qtcEnvironmentVariableIsSet("QTC_QUICK_SETTINGS"))
        return createGenericAspectForm(container);
    return nullptr;
}

} // namespace QtcQuick
