// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "toolchain.h"

#include <utils/aspects.h>

#include <memory>

namespace ProjectExplorer {

// What every toolchain is asked, whatever kind it is: what it is called, where
// its compilers are, and whether the C++ one was given by hand rather than
// derived from the C one. A kind with more to ask derives from this and adds
// its own; the Toolchains page draws whatever it finds and knows about none
// of them.
class PROJECTEXPLORER_EXPORT ToolchainConfigAspects : public Utils::AspectContainer
{
public:
    explicit ToolchainConfigAspects(const ToolchainBundle &bundle);
    ~ToolchainConfigAspects() override;

    ToolchainBundle bundle() const;

    Utils::StringAspect &displayName();
    // The compiler command for a language, or null where the bundle has no
    // toolchain for it. MSVC has none of these: it is found, not pointed at.
    Utils::FilePathAspect *compilerCommand(Utils::Id language);
    // Null unless the bundle has both a C and a C++ compiler, which is what
    // makes deriving one from the other possible.
    Utils::BoolAspect *manualCxxCompiler();
    // Every compiler command in the bundle's own order, for a renderer that
    // wants to lay them out itself.
    QList<std::pair<const Toolchain *, Utils::FilePathAspect *>> compilerCommands() const;

    bool isDirty() const override;
    void apply() override;
    // A kind with settings of its own turns those off too.
    virtual void makeReadOnly();

    void setFallbackBrowsePath(const Utils::FilePath &path);
    void setCommandVersionArguments(const QStringList &args);
    bool hasAnyCompiler() const;
    // Command-line arguments as the host shell would read them, forgiving of
    // the half-typed quoting a field is bound to see.
    static QStringList splitString(const QString &s);
    // What the page shows when a kind has something to complain about.
    Utils::TextDisplay &errorMessage() { return m_errorMessage; }
    // The C++ compiler that goes with the C one, unless the user gave it by
    // hand. Done here because only the factory knows the correspondence.
    void deriveCxxCompilerCommand();

private:
    class Private;
    std::unique_ptr<Private> d;

    Utils::StringAspect m_displayName{this};
    Utils::TextDisplay m_errorMessage{this};
};

#ifdef WITH_TESTS
QObject *createToolchainConfigAspectsTest();
#endif

} // namespace ProjectExplorer
