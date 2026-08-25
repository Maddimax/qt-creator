// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "toolchain.h"

#include <utils/aspects.h>

#include <QScrollArea>

#include <memory>

QT_BEGIN_NAMESPACE
class QFormLayout;
QT_END_NAMESPACE

namespace ProjectExplorer {

// What every toolchain is asked, whatever kind it is: what it is called, where
// its compilers are, and whether the C++ one was given by hand rather than
// derived from the C one. The state lives here rather than in the widgets
// showing it, so that a page can draw it however it likes; see
// ToolchainConfigWidget, which is one such way.
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
    Utils::TextDisplay &errorMessage();

    // Every compiler command in the bundle's own order, for a renderer that
    // wants to lay them out itself.
    QList<std::pair<const Toolchain *, Utils::FilePathAspect *>> compilerCommands() const;

    bool isDirty() const override;
    void apply() override;
    void makeReadOnly();

    void setFallbackBrowsePath(const Utils::FilePath &path);
    void setCommandVersionArguments(const QStringList &args);
    bool hasAnyCompiler() const;
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

class PROJECTEXPLORER_EXPORT ToolchainConfigWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit ToolchainConfigWidget(const ToolchainBundle &bundle);
    ~ToolchainConfigWidget() override;

    ToolchainBundle bundle() const;
    QString currentDisplayName() const;

    void apply();
    void makeReadOnly();
    void setFallbackBrowsePath(const Utils::FilePath &path);

    // FIXME: This should be re-implemented in all derived classes.
    virtual bool isDirty() const;

signals:
    void compilerCommandChanged(Utils::Id language);
    void dirty();

protected:
    void setErrorMessage(const QString &);
    void clearErrorMessage();

    virtual void applyImpl() = 0;
    virtual void makeReadOnlyImpl() = 0;

    void addErrorLabel();
    static QStringList splitString(const QString &s);
    Utils::FilePath compilerCommand(Utils::Id language);
    bool hasAnyCompiler() const;
    void setCommandVersionArguments(const QStringList &args);
    void deriveCxxCompilerCommand();

    ToolchainConfigAspects &aspects();

    QFormLayout *m_mainLayout;

private:
    void setupCompilerPathChoosers();

    ToolchainConfigAspects m_aspects;
    bool m_errorLabelAdded = false;
};

} // namespace ProjectExplorer
