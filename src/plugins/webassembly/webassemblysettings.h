// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace WebAssembly::Internal {

class WebAssemblySettings final : public Utils::AspectContainer
{
public:
    WebAssemblySettings();

    Utils::FilePathAspect emSdk{this};

private:
    Utils::TextDisplay instruction{this};
    Utils::TextDisplay statusIsEmsdkDir{this};
    Utils::TextDisplay statusSdkInstalled{this};
    Utils::TextDisplay statusSdkActivated{this};
    Utils::TextDisplay statusSdkInvalid{this};
    Utils::TextDisplay emSdkVersionDisplay{this};
    Utils::StringAspect emSdkEnvDisplay{this};
    Utils::TextDisplay qtVersionDisplay{this};

    void updateStatus();
};

WebAssemblySettings &settings();

} // WebAssmbly::Internal
