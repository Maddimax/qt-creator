// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectform.h"

#include "aspectcontainermodel.h"
#include "namedaspects.h"
#include "qtcquickwidget.h"

#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/qtcassert.h>

#include <QPointer>
#include <QQuickWidget>
#include <QShowEvent>

namespace QtcQuick {

namespace {

// Tells the container it is on screen. Building a page is not showing it: the
// page census builds every one of them and shows none, so work that costs
// something must not happen at construction. See AspectContainer::pageShown().
class ShowReportingQuickWidget final : public QuickWidget
{
public:
    explicit ShowReportingQuickWidget(Utils::AspectContainer *container)
        : m_container(container)
    {}

private:
    void showEvent(QShowEvent *event) override
    {
        QuickWidget::showEvent(event);
        if (m_container)
            m_container->pageShown();
    }

    const QPointer<Utils::AspectContainer> m_container;
};

} // namespace

QWidget *createGenericAspectForm(Utils::AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    auto widget = new ShowReportingQuickWidget(container);
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
        auto widget = new ShowReportingQuickWidget(container);
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
