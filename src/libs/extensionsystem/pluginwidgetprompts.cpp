// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pluginwidgetprompts.h"

#include "extensionsystemtr.h"
#include "pluginprompts.h"
#include "pluginspec.h"

#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>

using namespace Utils;

namespace ExtensionSystem {

static bool disableCrashed(PluginSpec *spec, const QSet<PluginSpec *> &dependents)
{
    auto dependentsNames = Utils::transform<QStringList>(dependents, &PluginSpec::name);
    std::sort(dependentsNames.begin(), dependentsNames.end());
    const QString dependentsList = dependentsNames.join(", ");
    const QString pluginsMenu = HostOsInfo::isMacHost()
                                    ? Tr::tr("%1 > About Plugins")
                                          .arg(QGuiApplication::applicationDisplayName())
                                    : Tr::tr("Help > About Plugins");
    const QString otherPluginsText
        = Tr::tr("If you temporarily disable %1, the following plugins that depend on "
                 "it are also disabled: %2.").arg(spec->name(), dependentsList) + "\n\n";
    const QString detailsText = (dependents.isEmpty() ? QString() : otherPluginsText)
                                + Tr::tr("Disable plugins permanently in %1.").arg(pluginsMenu);
    const QString text = Tr::tr("The last time you started %1, it seems to have closed because "
                                "of a problem with the \"%2\" "
                                "plugin. Temporarily disable the plugin?")
                             .arg(QGuiApplication::applicationDisplayName(), spec->name());

    QMessageBox dialog;
    dialog.setIcon(QMessageBox::Question);
    dialog.setText(text);
    dialog.setDetailedText(detailsText);
    QPushButton *disableButton = dialog.addButton(Tr::tr("Disable Plugin"),
                                                  QMessageBox::AcceptRole);
    dialog.addButton(Tr::tr("Continue"), QMessageBox::RejectRole);
    dialog.exec();
    return dialog.clickedButton() == disableButton;
}

static bool acceptTermsAndConditions(PluginSpec *spec)
{
    using namespace Layouting;

    QDialog dialog(Utils::dialogParent());
    dialog.setWindowTitle(Tr::tr("Terms and Conditions"));

    QDialogButtonBox buttonBox;
    QCheckBox *acceptCheckBox;
    QPushButton *acceptButton
        = buttonBox.addButton(Tr::tr("Accept"), QDialogButtonBox::ButtonRole::YesRole);
    QPushButton *decline
        = buttonBox.addButton(Tr::tr("Decline"), QDialogButtonBox::ButtonRole::NoRole);
    acceptButton->setAutoDefault(false);
    acceptButton->setDefault(false);
    acceptButton->setEnabled(false);
    decline->setAutoDefault(true);
    decline->setDefault(true);
    QObject::connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const QLatin1String legal = QLatin1String(
        "I confirm that I have reviewed and accept the terms and conditions\n"
        "of this extension. I confirm that I have the authority and ability to\n"
        "accept the terms and conditions of this extension for the customer.\n"
        "I acknowledge that if the customer and the Qt Company already have a\n"
        "valid agreement in place, that agreement shall apply, but these terms\n"
        "shall govern the use of this extension.");

    // clang-format off
    Column {
        Tr::tr("The plugin %1 requires you to accept the following terms and conditions:").arg(spec->name()), br,
        TextEdit {
            markdown(spec->termsAndConditions()->text),
            readOnly(true),
        }, br,
        Row {
            acceptCheckBox = new QCheckBox(legal), &buttonBox,
        }
    }.attachTo(&dialog);
    // clang-format on

    QObject::connect(acceptCheckBox, &QCheckBox::toggled, acceptButton, &QPushButton::setEnabled);

    return dialog.exec() == QDialog::Accepted;
}

void installWidgetPrompts()
{
    PluginPrompts::setDisableCrashed(&disableCrashed);
    PluginPrompts::setAcceptTermsAndConditions(&acceptTermsAndConditions);
}

} // namespace ExtensionSystem
