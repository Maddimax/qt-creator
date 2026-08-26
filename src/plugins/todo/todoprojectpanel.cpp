// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "todoprojectpanel.h"

#include "constants.h"
#include "todoitemsprovider.h"
#include "todotr.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectsettings.h>
#include <projectexplorer/useglobalaspect.h>

#include <utils/aspects.h>
#include <utils/layoutbuilder.h>

#include <QGroupBox>
#include <QVariant>

using namespace ProjectExplorer;
using namespace Utils;

namespace Todo::Internal {

class TodoProjectSettings : public AspectContainer
{
public:
    explicit TodoProjectSettings(Project *project)
        : m_project(project)
    {
        setAutoApply(true);
        excludePatterns.setQmlName("ExcludePatterns");
        excludePatterns.setUiAllowAdding(true);
        excludePatterns.setUiAllowRemoving(true);
        excludePatterns.setUiAllowEditing(true);
        excludePatterns.setToolTip(
            Tr::tr("Regular expressions for file paths to be excluded from scanning."));

        const QVariantMap s =
            project->namedSettings(Constants::SETTINGS_NAME_KEY).toMap();
        useGlobalSettings.setValue(s.value(Constants::USE_GLOBAL_KEY, true).toBool());
        excludePatterns.setValue(s.value(Constants::EXCLUDES_LIST_KEY).toStringList());

        setupUseGlobalSettings(this, &useGlobalSettings, [this] { save(); });
    }

    void save()
    {
        QVariantMap s;
        s[Constants::USE_GLOBAL_KEY] = useGlobalSettings();
        if (!useGlobalSettings())
            s[Constants::EXCLUDES_LIST_KEY] = QVariant(excludePatterns());
        m_project->setNamedSettings(Constants::SETTINGS_NAME_KEY, s);
        todoItemsProvider().projectSettingsChanged();
    }

    static Key extraDataKey() { return "TodoProjectSettings"; }

    UseGlobalAspect useGlobalSettings{Constants::TODO_SETTINGS};
    StringListAspect excludePatterns{this};

private:
    Project * const m_project;
};

static TodoProjectSettings *todoProjectSettings(Project *project)
{
    return projectSettings<TodoProjectSettings>(project);
}

// What the panel shows. The flag is kept out of the settings container because
// that container turns itself off as a whole. See ProjectCommentsPanel.
class TodoProjectPanel final : public AspectContainer
{
public:
    explicit TodoProjectPanel(TodoProjectSettings *settings)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Todo/TodoProjectPanel.qml"));

        settings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&settings->useGlobalSettings);

        settings->setQmlName("Settings");
        registerAspect(settings);
    }

    static Key extraDataKey() { return "TodoProjectPanel"; }
};

static TodoProjectPanel *todoProjectPanel(Project *project)
{
    const Key key = TodoProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new TodoProjectPanel(todoProjectSettings(project)));
        project->setExtraData(key, v);
    }
    return v.value<TodoProjectPanel *>();
}

class TodoProjectPanelFactory final : public ProjectPanelFactory
{
public:
    TodoProjectPanelFactory()
    {
        setPriority(100);
        setDisplayName(Tr::tr("To-Do"));
        setSettingsProvider([](Project *project) {
            return todoProjectPanel(project);
        });
    }
};

void setupTodoProjectPanel()
{
    static TodoProjectPanelFactory theTodoProjectPanelFactory;
}

} // Todo::Internal
