// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace ProjectExplorer { class SshParametersAspectContainer; }

namespace Remote::Internal {

// Makes the container's "Create New..." offer real: a key is made in a dialog,
// and where that dialog lives is this plugin's business rather than the
// container's. Without this the offer is not shown at all.
void setupSshKeyCreation(ProjectExplorer::SshParametersAspectContainer &ssh);

#ifdef WITH_TESTS
QObject *createSshKeyCreationTest();
#endif

} // namespace Remote::Internal
