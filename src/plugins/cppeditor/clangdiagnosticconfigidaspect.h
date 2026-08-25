// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include "clangdiagnosticconfigsmodel.h"

#include <utils/aspects.h>
#include <utils/id.h>

namespace CppEditor {

class ClangDiagnosticConfigsWidget;

// Aspect holding a ClangDiagnosticConfig identifier.

class CPPEDITOR_EXPORT ClangDiagnosticConfigIdAspect final
    : public Utils::TypedAspect<Utils::Id>
{
public:
    explicit ClangDiagnosticConfigIdAspect(Utils::AspectContainer *container = nullptr);

    using ModelFactory      = std::function<ClangDiagnosticConfigsModel()>;
    using EditWidgetFactory = std::function<ClangDiagnosticConfigsWidget *(
        const ClangDiagnosticConfigs &, const Utils::Id &)>;

    void setModelFactory(ModelFactory factory);
    void setEditWidgetFactory(EditWidgetFactory factory);

    ClangDiagnosticConfigs customConfigs() const { return m_customConfigs; }
    void setCustomConfigs(const ClangDiagnosticConfigs &configs)
    {
        m_customConfigs = configs;
        m_customConfigsKnown = true;
    }
    // Whether the list above is this aspect's answer or just its empty
    // starting state. A project's settings are loaded straight into the page's
    // data and never reach the aspect, so saving them back from here would
    // wipe them.
    bool customConfigsAreKnown() const { return m_customConfigsKnown; }

    void setPersistCustomConfigs(bool persist) { m_persistCustomConfigs = persist; }

    void fromMap(const Utils::Store &map) final;
    void toMap(Utils::Store &map) const final;
    void readSettings() final;
    void writeSettings() const final;

    // A name and the dialog that changes it, which is what the widget editor is
    // too: a button carrying the current configuration's name.
    Utils::AspectPresentation presentation() const final;
    QString displayText() const final;
    void triggerAction() final;

    void refresh();

private:
    bool isDirty() const final;
    void apply() final;

    ModelFactory      m_modelFactory;
    EditWidgetFactory m_editFactory;
    ClangDiagnosticConfigs m_customConfigs;
    ClangDiagnosticConfigs m_committedCustomConfigs;
    bool m_persistCustomConfigs = false;
    bool m_customConfigsKnown = false;
};

} // namespace CppEditor
