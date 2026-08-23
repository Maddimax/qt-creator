// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace ProjectExplorer { class Project; }

namespace Copilot::Internal {

class CopilotClient;

class CopilotSettings final : public Utils::AspectContainer
{
public:
    CopilotSettings();

    Utils::FilePathAspect nodeJsPath{this};
    Utils::FilePathAspect distPath{this};
    Utils::BoolAspect autoComplete{this};
    Utils::BoolAspect enableCopilot{this};

    Utils::StringAspect proxy{this};
    Utils::BoolAspect proxyRejectUnauthorized{this};

    Utils::StringAspect githubEnterpriseUrl{this};

    // The sign-in state, which used to live in AuthWidget. The button's label
    // is the state; the display carries whatever went wrong.
    Utils::TextDisplay warning{this};
    Utils::TextDisplay help{this};
    Utils::ActionAspect signIn{this};
    Utils::TextDisplay authStatus{this};

private:
    enum class AuthState { SignedIn, SignedOut, Unknown };

    void setAuthState(const QString &buttonText, const QString &message, bool working);
    void restartClient();
    void checkAuthStatus();
    void requestSignIn();
    void requestSignOut();

    AuthState m_authState = AuthState::Unknown;
    CopilotClient *m_client = nullptr;
};

CopilotSettings &settings();

bool isCopilotEnabled(ProjectExplorer::Project *project);
bool isCopilotEnabledByProject();

void setupCopilotSettings();

} // namespace Copilot::Internal

