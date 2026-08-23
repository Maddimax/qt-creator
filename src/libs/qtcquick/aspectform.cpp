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

QWidget *createAspectForm(Utils::AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    auto widget = new QuickWidget;

    // A page with its own QML lays itself out and reaches its aspects by name;
    // one without gets the generic form driven by the model. Each path passes
    // only what its root component declares.
    if (const QUrl source = container->qmlSource(); !source.isEmpty()) {
        auto named = new NamedAspects(container, widget);
        widget->quickWidget()->setInitialProperties({{"aspects", QVariant::fromValue(named)}});
        widget->setSource(source);
        return widget;
    }

    // A page with an aspect the generic form cannot show would lose a control,
    // so decline it and let the caller keep its widget layout. Those pages are
    // the migration backlog: give them their own QML, or teach the form the
    // aspect. QTC_QUICK_SETTINGS renders them anyway, placeholders and all, so
    // that the gaps are visible while working on them.
    if (!AspectContainerModel::isFullyRenderable(container)
        && !Utils::qtcEnvironmentVariableIsSet("QTC_QUICK_SETTINGS")) {
        delete widget;
        return nullptr;
    }

    auto model = new AspectContainerModel(container, widget);
    widget->quickWidget()->setInitialProperties({{"model", QVariant::fromValue(model)}});
    widget->setSource(QUrl("qrc:/qt/qml/QtCreator/Ui/AspectForm.qml"));
    return widget;
}

} // namespace QtcQuick
