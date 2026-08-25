// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Beautifier::Internal {

class AbstractSettings;

// The named configurations of a beautifier tool: which one it uses, what is in
// it, and what the option under the cursor means.
//
// This was a combo box with Add, Edit and Remove opening a modal dialog, and
// only the dialog's editor knew how to complete an option or explain one.
// Everything is on the page now.
class ConfigurationsAspect final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit ConfigurationsAspect(AbstractSettings *settings);

    // The one the tool will use, which is what the tool's customStyle stores.
    QString currentConfiguration() const;
    void setCurrentConfiguration(const QString &name);

    // What the word under the cursor means. Which word that is only the editor
    // knows; what it means only the settings do.
    Q_INVOKABLE void showDocumentationFor(const QString &word);

    // The configurations are files on disk, so the page reads them when it is
    // built rather than holding a copy.
    void reload();

    Utils::SelectionAspect current{this};
    Utils::StringAspect name{this};
    Utils::StringAspect value{this};
    Utils::TextDisplay documentation{this};
    Utils::ActionAspect add{this};
    Utils::ActionAspect remove{this};

private:
    void showConfiguration(const QString &name);
    void storeName();
    void storeValue();
    QString uniqueName() const;

    AbstractSettings *m_settings = nullptr;
    // The configuration the form is showing, and nothing while it is being
    // filled in.
    QString m_loaded;
};

#ifdef WITH_TESTS
QObject *createConfigurationsAspectTest();
#endif

} // namespace Beautifier::Internal
