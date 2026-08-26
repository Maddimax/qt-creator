// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectcommentssettings.h"

#include "project.h"
#include "projectexplorertr.h"
#include "projectmanager.h"
#include "projectpanelfactory.h"
#include "projectsettings.h"
#include "useglobalaspect.h"

#include <texteditor/commentssettings.h>
#include <texteditor/texteditorconstants.h>

#include <utils/layoutbuilder.h>

using namespace TextEditor;
using namespace Utils;

namespace ProjectExplorer::Internal {

const char kUseGlobalKey[] = "UseGlobalKey";

class ProjectCommentsSettings : public CommentsSettings
{
public:
    explicit ProjectCommentsSettings(Project *project)
        : m_project(project)
    {
        const QVariant entry = project->namedSettings(CommentsSettings::mainSettingsKey());
        if (entry.isValid()) {
            const Store store = storeFromVariant(entry);
            fromMap(store);
            useGlobalSettings.setValue(store.value(kUseGlobalKey, true).toBool());
        }

        setAutoApply(true);
        setupUseGlobalSettings(this, &useGlobalSettings, [this] { save(); });
    }

    void save()
    {
        // Optimization: Don't save if user never switched away from the default.
        if (useGlobalSettings() && !m_project->namedSettings(CommentsSettings::mainSettingsKey()).isValid())
            return;
        Store data;
        data.insert(kUseGlobalKey, useGlobalSettings());
        if (!useGlobalSettings())
            toMap(data);
        m_project->setNamedSettings(CommentsSettings::mainSettingsKey(), variantFromStore(data));
    }

    UseGlobalAspect useGlobalSettings{TextEditor::Constants::TEXT_EDITOR_COMMENTS_SETTINGS};

private:
    Project * const m_project;
};

// What the panel shows. The flag is not one of the settings: setup
// UseGlobalSettings() disables the settings container as a whole while the
// global ones are in use, and a flag inside it would be disabled too, leaving
// no way back. So the panel is a container of its own holding both. It has no
// settings key, and neither does the flag, so what is stored stays the settings
// container's business.
class ProjectCommentsPanel final : public AspectContainer
{
public:
    explicit ProjectCommentsPanel(ProjectCommentsSettings *settings)
    {
        // Before registering anything: insertAspect() forces the container's
        // own auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/ProjectCommentsPanel.qml"));

        settings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&settings->useGlobalSettings);

        settings->setQmlName("Settings");
        registerAspect(settings);
    }
};

static ProjectCommentsSettings *commentsProjectSettings(Project *project)
{
    const Key key = "ProjectCommentsSettings";
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new ProjectCommentsSettings(project));
        project->setExtraData(key, v);
    }
    return v.value<ProjectCommentsSettings *>();
}

static ProjectCommentsPanel *commentsProjectPanel(Project *project)
{
    const Key key = "ProjectCommentsPanel";
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new ProjectCommentsPanel(commentsProjectSettings(project)));
        project->setExtraData(key, v);
    }
    return v.value<ProjectCommentsPanel *>();
}

class CommentsSettingsProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CommentsSettingsProjectPanelFactory()
    {
        setPriority(45);
        setDisplayName(Tr::tr("Documentation Comments"));
        setSettingsProvider([](Project *project) {
            return commentsProjectPanel(project);
        });
    }
};

void setupCommentsSettingsProjectPanel()
{
    static CommentsSettingsProjectPanelFactory theCommentsSettingsProjectPanelFactory;
}

} // namespace ProjectExplorer::Internal

TextEditor::CommentsSettings::Data ProjectExplorer::commentsSettingsForFile(const FilePath &filePath)
{
    using namespace Internal;
    Project * const project = ProjectManager::projectForFile(filePath);
    if (!project)
        return globalCommentsSettings().data();
    const auto *ps = commentsProjectSettings(project);
    return ps->useGlobalSettings() ? globalCommentsSettings().data() : ps->data();
}
