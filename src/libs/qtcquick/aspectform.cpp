// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectform.h"

#include "aspectcontainermodel.h"
#include "qtcquickwidget.h"

#include <utils/qtcassert.h>

#include <QQuickWidget>

namespace QtcQuick {

QWidget *createAspectForm(Utils::AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    auto widget = new QuickWidget;
    auto model = new AspectContainerModel(container, widget);
    widget->quickWidget()->setInitialProperties({{"model", QVariant::fromValue(model)}});
    widget->setSource(QUrl("qrc:/qt/qml/QtCreator/Ui/AspectForm.qml"));
    return widget;
}

} // namespace QtcQuick
