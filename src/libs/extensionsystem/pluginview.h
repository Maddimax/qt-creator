// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystemwidgets_global.h"
#include "pluginspec.h"

#include <utils/aspects.h>
#include <utils/treemodel.h>

#include <QMetaType>
#include <QSet>
#include <QWidget>

#include <unordered_map>

namespace Utils {
class CategorySortFilterModel;
} // Utils

namespace ExtensionSystem {

class PluginSpec;
class PluginView;

namespace Internal {
class CollectionItem;
class PluginItem;
class PluginTreeAspect;
class PluginTreeModel;
} // Internal

class EXTENSIONSYSTEM_WIDGETS_EXPORT PluginData
{
public:
    explicit PluginData(QWidget *parent, PluginView *pluginView = nullptr);

    bool setPluginsEnabled(const QSet<PluginSpec *> &plugins, bool enable);

private:
    QWidget *m_parent = nullptr;
    PluginView *m_pluginView = nullptr;
    Internal::PluginTreeModel *m_model;
    Utils::CategorySortFilterModel *m_sortModel;
    std::unordered_map<PluginSpec *, bool> m_affectedPlugins;

    friend class Internal::CollectionItem;
    friend class Internal::PluginItem;
    friend class PluginView;
};

class EXTENSIONSYSTEM_WIDGETS_EXPORT PluginView : public QWidget
{
    Q_OBJECT

public:
    explicit PluginView(QWidget *parent = nullptr);
    ~PluginView() override;

    PluginSpec *currentPlugin() const;
    void setFilter(const QString &filter);
    void cancelChanges();

    PluginData &data();

#ifdef WITH_TESTS
    // The rows as the drawn tree sees them, so that a test can ask what a cell
    // says about itself.
    QAbstractItemModel *modelForTest() const;
#endif

signals:
    void currentPluginChanged(ExtensionSystem::PluginSpec *spec);
    void pluginActivated(ExtensionSystem::PluginSpec *spec);
    void pluginsChanged(const QSet<ExtensionSystem::PluginSpec *> &spec, bool enabled);

private:
    PluginSpec *pluginForIndex(const QModelIndex &index) const;
    void updatePlugins();

    PluginData m_data;
    std::unique_ptr<Utils::AspectContainer> m_aspects;
    Internal::PluginTreeAspect *m_tree = nullptr;
};

} // namespace ExtensionSystem

Q_DECLARE_METATYPE(ExtensionSystem::PluginSpec *)
