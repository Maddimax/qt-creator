// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include <utils/id.h>
#include <utils/treemodel.h>

#include <functional>
#include <optional>

class QLabel;

namespace Utils { class AspectContainer; }

namespace ProjectExplorer {

class Project;

class PROJECTEXPLORER_EXPORT ProjectPanelFactory
{
public:
    ProjectPanelFactory();

    Utils::Id id() const;
    void setId(Utils::Id id);

    // simple properties
    QString displayName() const;
    void setDisplayName(const QString &name);
    int priority() const;
    void setPriority(int priority);

    // interface for users of ProjectPanelFactory
    bool supports(Project *project);
    // Widgets created by this function should use setWindowTitle() to specify
    // their tab title.
    QWidget *createWidget(Project *project) const;

    // interface for "implementations" of ProjectPanelFactory
    // by default all projects are supported, only set a custom supports function
    // if you need something different
    using SupportsFunction = std::function<bool (Project *)>;
    void setSupportsFunction(std::function<bool (Project *)> function);

    static QList<ProjectPanelFactory *> factories();

    Utils::TreeItem *createPanelItem(Project *project);

    using WidgetCreator = std::function<QWidget *(Project *)>;
    void setCreateWidgetFunction(const WidgetCreator &createWidgetFunction);

    // A panel that is aspect-driven says so here instead of building a widget,
    // the way IOptionsPage::setSettingsProvider() does for a settings page.
    // Its form is then the container's - Qt Quick where the container names a
    // QML file, the widget layout otherwise - and what the panel shows can be
    // asked for without opening it, which is what lets a test check the panels
    // the way one checks the pages.
    using SettingsProvider = std::function<Utils::AspectContainer *(Project *)>;
    void setSettingsProvider(const SettingsProvider &provider);

    // The container this panel shows for \a project, or nothing if the panel
    // builds its own widget.
    std::optional<Utils::AspectContainer *> aspects(Project *project) const;

private:
    Utils::Id m_id;
    int m_priority = 0;
    QString m_displayName;
    SupportsFunction m_supportsFunction;
    WidgetCreator m_widgetCreator;
    SettingsProvider m_settingsProvider;
};

// Re-usable helpers for project settings page items.
PROJECTEXPLORER_EXPORT QLabel *createGlobalSettingsLink(Utils::Id globalId);

#ifdef WITH_TESTS
QObject *createProjectPanelFactoryTest();
#endif

} // namespace ProjectExplorer
