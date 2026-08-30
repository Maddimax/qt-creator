// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "buildsettings.h"

#include <projectexplorer/buildstep.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/project.h>

#include <utils/aspects.h>

#include <QPointer>

namespace Coco::Internal {

class CocoBuildStep : public ProjectExplorer::BuildStep
{
    Q_OBJECT

public:
    static CocoBuildStep *create(ProjectExplorer::BuildConfiguration *buildConfig);

    CocoBuildStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id);

    bool init() override;
    void display();

public slots:
    void buildSystemUpdated();
    void onButtonClicked();

private:
    void updateDisplay();
    QtTaskTree::GroupItem runRecipe() override;

    // The button, and what it says: it used to be a QPushButton inside a
    // widget the step rebuilt every time the form was opened, told what to say
    // over a signal.
    Utils::ActionAspect m_toggleCoverage{this};

    QPointer<BuildSettings> m_buildSettings;
    bool m_valid;
};

void setupCocoBuildSteps();

#ifdef WITH_TESTS
QObject *createCocoBuildStepPageTest();
#endif

} // namespace Coco::Internal
