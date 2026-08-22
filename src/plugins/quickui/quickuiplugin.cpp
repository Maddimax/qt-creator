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
        if (!Utils::qtcEnvironmentVariableIsSet("QTC_QUICK_SETTINGS"))
            return;

        Core::setAspectFormFactory([](Utils::AspectContainer *container) {
            return QtcQuick::createAspectForm(container);
        });
    }
};

} // namespace QuickUi::Internal

#include "quickuiplugin.moc"
