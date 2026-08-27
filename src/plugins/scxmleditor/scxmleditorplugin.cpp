// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "scxmleditor.h"

#ifdef WITH_TESTS
#include "common/statistics.h"
#endif

#include <extensionsystem/iplugin.h>

#include <coreplugin/designmode.h>

namespace ScxmlEditor::Internal {

class ScxmlEditorPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ScxmlEditor.json")

private:
    void initialize() final
    {
        setupScxmlEditor(this);
#ifdef WITH_TESTS
        addTestCreator(ScxmlEditor::Common::createStatisticsTest);
#endif
    }

    void extensionsInitialized() final
    {
        Core::DesignMode::setDesignModeIsRequired();
    }
};

} // ScxmlEditor::Internal

#include "scxmleditorplugin.moc"
