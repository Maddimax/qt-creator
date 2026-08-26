// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "editorsettingspropertiespage.h"

#include "editorconfiguration.h"
#include "project.h"
#include "projectexplorertr.h"
#include "projectpanelfactory.h"

#include <texteditor/tabsettings.h>

#include <utils/aspects.h>

using namespace Utils;

namespace ProjectExplorer::Internal {

// What the panel shows: the same five settings objects the Text Editor
// Behavior page shows, the margin, and the flag that decides whether the
// project's own are used at all. The flag is kept out of them because they are
// disabled as a whole while the global ones are in use. See
// ProjectCommentsPanel.
class EditorProjectPanel final : public Utils::AspectContainer
{
public:
    explicit EditorProjectPanel(Project *project)
        : m_config(project->editorConfiguration())
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/EditorProjectPanel.qml"));

        m_config->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&m_config->useGlobalSettings);

        m_restoreGlobal.setQmlName("RestoreGlobal");
        m_restoreGlobal.setActionText(Tr::tr("Restore Global"));
        m_restoreGlobal.setAction([this] {
            m_config->cloneGlobalSettings();
            m_tabSettings.setPreferences(m_config->codeStyle());
        });
        registerAspect(&m_restoreGlobal);

        // The tab settings are not the configuration's: they follow whichever
        // code style the project uses, which is what setPreferences() wires up.
        m_tabSettings.setQmlName("Tabs");
        registerAspect(&m_tabSettings);

        m_config->typingSettings.setQmlName("Typing");
        registerAspect(&m_config->typingSettings);

        m_config->storageSettings.setQmlName("Storage");
        registerAspect(&m_config->storageSettings);

        m_config->extraEncodingSettings.setQmlName("Encoding");
        registerAspect(&m_config->extraEncodingSettings);

        m_config->behaviorSettings.setQmlName("Behavior");
        registerAspect(&m_config->behaviorSettings);

        m_config->marginSettings.setQmlName("Margins");
        registerAspect(&m_config->marginSettings);

        // Behaviour, not layout.
        m_tabSettings.setPreferences(m_config->codeStyle());
        updateForUseGlobal();
        m_config->useGlobalSettings.addOnChanged(this, [this] { updateForUseGlobal(); });
    }

    static Utils::Key extraDataKey() { return "EditorProjectPanel"; }

private:
    void updateForUseGlobal()
    {
        const bool useGlobal = m_config->useGlobalSettings();
        m_restoreGlobal.setEnabled(!useGlobal);
        m_tabSettings.setEnabled(!useGlobal);
        m_config->typingSettings.setEnabled(!useGlobal);
        m_config->storageSettings.setEnabled(!useGlobal);
        m_config->extraEncodingSettings.setEnabled(!useGlobal);
        m_config->behaviorSettings.setEnabled(!useGlobal);
        m_config->marginSettings.setEnabled(!useGlobal);
    }

    EditorConfiguration * const m_config;
    Utils::ActionAspect m_restoreGlobal;
    TextEditor::TabSettings m_tabSettings;
};

static EditorProjectPanel *editorProjectPanel(Project *project)
{
    const Utils::Key key = EditorProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new EditorProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<EditorProjectPanel *>();
}

class EditorSettingsProjectPanelFactory final : public ProjectPanelFactory
{
public:
    EditorSettingsProjectPanelFactory()
    {
        setPriority(30);
        setDisplayName(Tr::tr("Editor"));
        setSettingsProvider([](Project *project) { return editorProjectPanel(project); });
    }
};

void setupEditorSettingsProjectPanel()
{
    static EditorSettingsProjectPanelFactory theEditorSettingsProjectPanelFactory;
}

} // ProjectExplorer::Internal
