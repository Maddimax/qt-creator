// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"

#include <utils/aspects.h>

namespace LanguageClient {

class LANGUAGECLIENT_EXPORT MimeTypesAspect : public Utils::TypedAspect<QStringList>
{
    Q_OBJECT

public:
    explicit MimeTypesAspect(Utils::AspectContainer *container = nullptr);

    // The types that were picked, and the dialog that picks them.
    Utils::AspectPresentation presentation() const override;
    QString displayText() const override;
    void triggerAction() override;
};

} // namespace LanguageClient
