// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Core::Internal {

class EnvVarSeparatorAspect : public Utils::StringListAspect
{
public:
    EnvVarSeparatorAspect(Utils::AspectContainer *container = nullptr);

    // A summary of the separators, and the dialog that edits them - the same
    // shape as EnvironmentChangesAspect, so the same control.
    Utils::AspectPresentation presentation() const override;
    QString displayText() const override;
    void triggerAction() override;

    void writeSettings() const override { Utils::StringListAspect::writeSettings(); }
    void readSettings() override { Utils::StringListAspect::readSettings(); }
};

#ifdef WITH_TESTS
QObject *createEnvVarSeparatorsTest();
#endif

} // namespace Core::Internal
