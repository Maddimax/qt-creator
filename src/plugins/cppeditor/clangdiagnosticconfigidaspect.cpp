// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangdiagnosticconfigidaspect.h"

#include "clangdiagnosticconfigswidget.h"
#include "cppeditortr.h"

#include <coreplugin/icore.h>

#include <utils/guiutils.h>
#include <utils/qtcsettings.h>
#include <utils/store.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QWidget>

using namespace Utils;

namespace CppEditor {

// ClangDiagnosticConfigIdAspect

ClangDiagnosticConfigIdAspect::ClangDiagnosticConfigIdAspect(AspectContainer *container)
    : TypedAspect(container)
{}

void ClangDiagnosticConfigIdAspect::setModelFactory(ModelFactory factory)
{
    m_modelFactory = std::move(factory);
}

void ClangDiagnosticConfigIdAspect::setEditWidgetFactory(EditWidgetFactory factory)
{
    m_editFactory = std::move(factory);
}

// The identifier the page should show: the stored one where it still exists,
// and the default where whatever was chosen has since been removed.
static Id effectiveId(const ClangDiagnosticConfigsModel &model, const Id &wanted, const Id &fallback)
{
    return model.hasConfigWithId(wanted) ? wanted : fallback;
}

Utils::AspectPresentation ClangDiagnosticConfigIdAspect::presentation() const
{
    Utils::AspectPresentation p = TypedAspect::presentation();
    p.control = Utils::AspectControls::TextWithAction;
    p.actionText = Tr::tr("Change...");
    return p;
}

QString ClangDiagnosticConfigIdAspect::displayText() const
{
    if (!m_modelFactory)
        return {};
    const ClangDiagnosticConfigsModel model = m_modelFactory();
    return model.configWithId(effectiveId(model, volatileValue(), defaultValue())).displayName();
}

void ClangDiagnosticConfigIdAspect::triggerAction()
{
    if (!m_modelFactory || !m_editFactory)
        return;

    ClangDiagnosticConfigsModel model = m_modelFactory();
    const Id current = effectiveId(model, volatileValue(), defaultValue());
    const ClangDiagnosticConfigs oldConfigs = model.allConfigs();

    ClangDiagnosticConfigsWidget *widget = m_editFactory(oldConfigs, current);
    widget->sync();
    widget->layout()->setContentsMargins(0, 0, 0, 0);

    QDialog dialog(Utils::dialogParent());
    dialog.setWindowTitle(Tr::tr("Diagnostic Configurations"));
    dialog.setLayout(new QVBoxLayout);
    dialog.layout()->addWidget(widget);
    auto buttonsBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialog.layout()->addWidget(buttonsBox);
    connect(buttonsBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonsBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const Id chosen = widget->currentConfig().id();
    const ClangDiagnosticConfigs chosenConfigs = widget->configs();
    const bool anyChange = chosen != current || chosenConfigs != oldConfigs;

    m_customConfigs = ClangDiagnosticConfigsModel(chosenConfigs).customConfigs();
    m_customConfigsKnown = true;
    setVolatileValue(chosen);
    emit displayTextChanged();
    if (anyChange)
        Utils::checkSettingsDirty();
}

bool ClangDiagnosticConfigIdAspect::isDirty() const
{
    return TypedAspect<Id>::isDirty() || m_customConfigs != m_committedCustomConfigs;
}

void ClangDiagnosticConfigIdAspect::apply()
{
    TypedAspect<Id>::apply();
    m_committedCustomConfigs = m_customConfigs;
}

void ClangDiagnosticConfigIdAspect::refresh()
{
    // The name is what is drawn, and it is worked out from the value rather
    // than stored, so a value that changed behind the page's back has to say so.
    emit displayTextChanged();
}

void ClangDiagnosticConfigIdAspect::fromMap(const Store &map)
{
    if (!settingsKey().isEmpty()) {
        const auto it = map.find(settingsKey());
        if (it != map.end())
            setValue(Id::fromSetting(it.value()), BeQuiet);
    }
}

void ClangDiagnosticConfigIdAspect::toMap(Store &map) const
{
    if (!settingsKey().isEmpty())
        map.insert(settingsKey(), value().toSetting());
}

void ClangDiagnosticConfigIdAspect::readSettings()
{
    TypedAspect<Id>::readSettings();
    if (m_persistCustomConfigs) {
        m_customConfigs = m_committedCustomConfigs
            = diagnosticConfigsFromSettings(&Utils::userSettings());
        m_customConfigsKnown = true;
    }
}

void ClangDiagnosticConfigIdAspect::writeSettings() const
{
    TypedAspect<Id>::writeSettings();
    if (m_persistCustomConfigs)
        diagnosticConfigsToSettings(&Utils::userSettings(), m_customConfigs);
}

} // namespace CppEditor

#include "clangdiagnosticconfigidaspect.moc"
