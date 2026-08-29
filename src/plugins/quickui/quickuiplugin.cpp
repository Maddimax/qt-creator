// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/iplugin.h>

#include <qtcquick/aspectform.h>

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
        // For the forms that list a container's aspects rather than naming a
        // page: a build configuration has no QML of its own to name.
        Core::setGenericAspectFormFactory([](Utils::AspectContainer *container) {
            return QtcQuick::createGenericAspectForm(container);
        });
    }
};

} // namespace QuickUi::Internal

#include "quickuiplugin.moc"
