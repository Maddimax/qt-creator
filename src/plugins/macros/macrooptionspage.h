// Copyright (C) 2016 Nicolas Arnaud-Cormos
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QMap>

namespace Macros::Internal {

class MacroTableModel;

// A macro's description is the only thing the page edits, so the value is what
// each macro should be called: the ones missing from it are the ones the user
// removed. Removals and edits are both deferred that way.
using MacroDescriptions = QMap<QString, QString>;

class MacrosAspect final : public Utils::TypedAspect<MacroDescriptions>
{
public:
    explicit MacrosAspect(Utils::AspectContainer *container = nullptr);

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

protected:
    void volatileValueToGui() override;

private:
    void takeFromModel();

    // The model is what produced the current value, so reloading it from that
    // value would reset the view under an open cell editor.
    bool m_takingFromModel = false;
    MacroTableModel *m_model = nullptr;
};

class MacroOptionsPage final : public Core::IOptionsPage
{
public:
    MacroOptionsPage();
};

} // Macros::Internal
