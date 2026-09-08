// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickoutputview.h"
#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/inavigationwidgetfactory.h>

#include <QQuickItem>
#include <QQuickWidget>

#include <extensionsystem/iplugin.h>

#include <qtcquick/aspectform.h>
#include <qtcquick/qtcquickwidget.h>

#include <utils/environment.h>

namespace QuickUi::Internal {


class QuickUiPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "QuickUi.json")

private:
    void initialize() final
    {
#ifdef WITH_TESTS
        addTestCreator(createQuickUiTest);
#endif
        // Every settings page that has been given its own QML renders through
        // it. createAspectForm() declines the rest, and IOptionsPage falls back
        // to their widget layout.
        Core::setAspectFormFactory([](Utils::AspectContainer *container) {
            return QtcQuick::createAspectForm(container);
        });
        // The views that name their own QML - the Open Documents sidebar,
        // and whatever the shell phase moves next - are hosted here for the
        // same reason: Core names the file and never links this library.
        Core::setQmlViewFactory([](const QUrl &source, QObject *controller,
                                   Core::QmlViewSizing sizing) -> QWidget * {
            auto * const host = new QtcQuick::QuickWidget;
            controller->setParent(host);
            // A scene that has to fit beside other widgets is measured by
            // what it asks for; one that is given a dock fills it.
            if (sizing == Core::QmlViewSizing::SizeToScene)
                host->quickWidget()->setResizeMode(QQuickWidget::SizeViewToRootObject);
            host->quickWidget()->setInitialProperties(
                {{"controller", QVariant::fromValue(controller)}});
            host->setSource(source);
            return host;
        });
        // The panes draw output into a QTextDocument and need something to
        // show it. Core cannot make one itself.
        installOutputViewFactory();
        // For the forms that list a container's aspects rather than naming a
        // page: a build configuration has no QML of its own to name.
        Core::setGenericAspectFormFactory([](Utils::AspectContainer *container) {
            return QtcQuick::createGenericAspectForm(container);
        });
#ifdef WITH_TESTS
        // What a plugin's own test drives its form through. The QML objects
        // hang off the root item, not off the widget, so nothing outside here
        // can find them.
        Core::setAspectFormRootProvider([](QWidget *form) -> QObject * {
            const auto quickWidget = form->findChild<QQuickWidget *>();
            return quickWidget ? quickWidget->rootObject() : nullptr;
        });
#endif
    }
};

} // namespace QuickUi::Internal

#include "quickuiplugin.moc"
