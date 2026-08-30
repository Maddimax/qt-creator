// Copyright (C) 2016 BlackBerry Limited. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevicefwd.h>

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace Qnx::Internal {

#ifdef WITH_TESTS
QObject *createDeployQtLibrariesTest();
#endif

void executeQnxDeployQtLibrariesDialog(const ProjectExplorer::IDeviceConstPtr &device);

} // Qnx::Internal
