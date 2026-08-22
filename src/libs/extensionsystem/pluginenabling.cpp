// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pluginenabling.h"

#include "extensionsystemtr.h"
#include "pluginmanager.h"
#include "pluginspec.h"

#include <utils/algorithm.h>

#include <QMessageBox>

namespace ExtensionSystem {

static QString pluginListString(const QSet<PluginSpec *> &plugins)
{
    QStringList names = Utils::transform<QList>(plugins, &PluginSpec::name);
    names.sort();
    return names.join(QLatin1Char('\n'));
}

/*!
    Collects the dependencies of the \a plugins and asks the user if the
    corresponding plugins should be enabled or disabled (dependening on
    \a enable and using \a dialogParent as the parent for the dialog).

    Returns a (possibly) empty set of additional plugins that should be enabled or disabled
    respectively. Returns \c{std::nullopt} if the user canceled.
 */
std::optional<QSet<PluginSpec *>> askForEnablingPlugins(
    QWidget *dialogParent, const QSet<PluginSpec *> &plugins, bool enable)
{
    QSet<PluginSpec *> additionalPlugins;
    if (enable) {
        for (PluginSpec *spec : plugins) {
            for (PluginSpec *other : PluginManager::pluginsToEnableForPlugin(spec)) {
                if (!other->isEnabledBySettings())
                    additionalPlugins.insert(other);
            }
        }
        additionalPlugins.subtract(plugins);
        if (!additionalPlugins.isEmpty()) {
            if (QMessageBox::question(
                    dialogParent,
                    Tr::tr("Enabling Plugins"),
                    Tr::tr("Enabling\n%1\nwill also enable the following plugins:\n\n%2")
                        .arg(pluginListString(plugins), pluginListString(additionalPlugins)),
                    QMessageBox::Ok | QMessageBox::Cancel,
                    QMessageBox::Ok)
                != QMessageBox::Ok) {
                return {};
            }
        }
    } else {
        for (PluginSpec *spec : plugins) {
            for (PluginSpec *other : PluginManager::pluginsRequiringPlugin(spec)) {
                if (other->isEnabledBySettings())
                    additionalPlugins.insert(other);
            }
        }
        additionalPlugins.subtract(plugins);
        if (!additionalPlugins.isEmpty()) {
            if (QMessageBox::question(
                    dialogParent,
                    Tr::tr("Disabling Plugins"),
                    Tr::tr("Disabling\n%1\nwill also disable the following plugins:\n\n%2")
                        .arg(pluginListString(plugins), pluginListString(additionalPlugins)),
                    QMessageBox::Ok | QMessageBox::Cancel,
                    QMessageBox::Ok)
                != QMessageBox::Ok) {
                return {};
            }
        }
    }
    return additionalPlugins;
}

} // namespace ExtensionSystem
