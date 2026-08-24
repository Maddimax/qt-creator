// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Autotest::Internal {

class FrameworksAspectPrivate;

enum class RunAfterBuildMode
{
    None,
    All,
    Selected
};

// Which test frameworks and tools are active, and which frameworks group their
// tests. The frameworks register themselves, so the rows belong to the registry
// and only the check states are the aspect's - see tableModel().
class FrameworksAspect : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit FrameworksAspect(Utils::AspectContainer *container);
    ~FrameworksAspect() override;

    bool framework(Utils::Id id) const;
    bool frameworkGrouping(Utils::Id id) const;
    bool tool(Utils::Id id) const;

    struct Data
    {
        QHash<Utils::Id, bool> frameworks;
        QHash<Utils::Id, bool> frameworksGrouping;
        QHash<Utils::Id, bool> tools;
    };

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

    // What is wrong with what is ticked right now, and why. Empty when nothing
    // is: no framework at all leaves the plugin with nothing to do, and mixing
    // a framework with a tool duplicates run information.
    QString warning() const;
    QString warningToolTip() const;

private:
    void apply() final;
    void cancel() final;
    bool isDirty() const final;

    void writeSettings() const final;
    void readSettings() final;

    FrameworksAspectPrivate *d = nullptr;
};

class TestSettings : public Utils::AspectContainer
{
public:
    TestSettings();

    Utils::IntegerAspect scanThreadLimit{this};
    Utils::BoolAspect useTimeout{this};
    Utils::IntegerAspect timeout{this};
    Utils::BoolAspect omitInternalMsg{this};
    Utils::BoolAspect omitRunConfigWarn{this};
    Utils::BoolAspect limitResultOutput{this};
    Utils::BoolAspect limitResultDescription{this};
    Utils::IntegerAspect resultDescriptionMaxSize{this};
    Utils::BoolAspect autoScroll{this};
    Utils::BoolAspect processArgs{this};
    Utils::BoolAspect displayApplication{this};
    Utils::BoolAspect popupOnStart{this};
    Utils::BoolAspect popupOnFinish{this};
    Utils::BoolAspect popupOnFail{this};
    Utils::BoolAspect showTreeFilterTextInput{this};
    Utils::SelectionAspect runAfterBuild{this};
    FrameworksAspect frameworks{this};
    Utils::TextDisplay frameworksWarning{this};
    Utils::ActionAspect resetChoiceCache{this};

    RunAfterBuildMode runAfterBuildMode() const;
};

TestSettings &testSettings();

void setupTestSettings();

} // Autotest::Internal
