// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtsupport_global.h"

#include <utils/aspects.h>

namespace ProjectExplorer {
class BuildConfiguration;
class Kit;
}

namespace QtSupport {

// What to warn about debugging QML with \a kit at this setting, and whether
// the kit can do it at all. Free functions rather than the aspects' own, so
// that the answer can be asked for without a build configuration to hang one
// off - which is what makes them testable.
QTSUPPORT_EXPORT QString qmlDebuggingWarning(ProjectExplorer::Kit *kit,
                                             Utils::TriState value,
                                             bool *supported);
QTSUPPORT_EXPORT QString qtQuickCompilerWarning(ProjectExplorer::Kit *kit,
                                                Utils::TriState value,
                                                Utils::TriState qmlDebugging,
                                                bool *supported);

class QTSUPPORT_EXPORT QmlDebuggingAspect : public Utils::TriStateAspect
{
    Q_OBJECT

public:
    explicit QmlDebuggingAspect(ProjectExplorer::BuildConfiguration *buildConfig);

private:
    // Whether the kit can debug QML at all, and what to say about it. Not a
    // control the aspect builds: a row of its own, next to this one.
    void updateWarning();

    Utils::TextDisplay m_warning;
};

class QTSUPPORT_EXPORT QtQuickCompilerAspect : public Utils::TriStateAspect
{
    Q_OBJECT

public:
    QtQuickCompilerAspect(ProjectExplorer::BuildConfiguration *buildConfig);

private:
    void updateWarning();

    Utils::TextDisplay m_warning;
};

#ifdef WITH_TESTS
QObject *createQtBuildAspectsTest();
#endif

} // namespace QtSupport
