// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "environmentaspect.h"
#include "kitaspect.h"

#include <utils/aspects.h>

#include <QPointer>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
QT_END_NAMESPACE


namespace ProjectExplorer {

class ProjectConfiguration;

class PROJECTEXPLORER_EXPORT TerminalAspect : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit TerminalAspect(Utils::AspectContainer *container = nullptr);

    Utils::AspectPresentation presentation() const override;

    bool useTerminal() const;
    void setUseTerminalHint(bool useTerminal);

    bool isUserSet() const;

    // Used by MCP, mirroring what toggling the check box does.
    QVariant variantValue() const override;
    void setVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) override;

    // A run configuration applies as it is edited, so there is no separate
    // value being typed: what the control shows is what the aspect holds.
    QVariant volatileVariantValue() const override { return variantValue(); }
    void setVolatileVariantValue(const QVariant &value,
                                 Announcement howToAnnounce = DoEmit) override
    {
        setVariantValue(value, howToAnnounce);
    }

    struct Data : BaseAspect::Data
    {
        bool useTerminal;
        bool isUserSet;
    };

private:
    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    void calculateUseTerminal();

    bool m_useTerminalHint = false;
    bool m_useTerminal = false;
    bool m_userSet = false;
};

class PROJECTEXPLORER_EXPORT WorkingDirectoryAspect : public Utils::FilePathAspect
{
    Q_OBJECT

public:
    explicit WorkingDirectoryAspect(Utils::AspectContainer *container = nullptr);

    // The directory with macros and environment variables expanded, which is
    // what a run needs. FilePathAspect::operator()() expands only macros.
    Utils::FilePath operator()() const { return workingDirectory(); }
    Utils::FilePath workingDirectory() const;
    Utils::FilePath defaultWorkingDirectory() const;
    Utils::FilePath unexpandedWorkingDirectory() const;
    void setDefaultWorkingDirectory(const Utils::FilePath &defaultWorkingDirectory);

    // The environment the directory is expanded in follows a run
    // configuration's own, which is an aspect rather than a value.
    using Utils::FilePathAspect::setEnvironment;
    void setEnvironment(EnvironmentAspect *envAspect);

private:
    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    EnvironmentAspect *m_envAspect = nullptr;
};

// The arguments, and the two ways of typing them. One row: a field, the button
// that swaps it for a taller one, and - where a run configuration knows what
// the arguments should be - the button that puts them back.
class PROJECTEXPLORER_EXPORT ArgumentsAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit ArgumentsAspect(Utils::AspectContainer *container = nullptr);

    void setFocusToInputField();

    // Only one is shown at a time; both hold the same arguments, so which one
    // is on screen is a matter of how much room the user wants.
    Utils::StringAspect oneLine{this};
    Utils::StringAspect manyLines{this};
    Utils::ActionAspect expand{this};
    Utils::ActionAspect reset{this};

    QString operator()() const { return arguments(); }
    QString arguments() const;
    QString unexpandedArguments() const;

    void setArguments(const QString &arguments);
    void setResetter(const std::function<QString()> &resetter);
    void resetArguments();

    struct Data : BaseAspect::Data
    {
        QString arguments;
    };

private:
    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    // Puts the arguments in both fields and shows the one that is wanted.
    void showArguments();

    QString m_arguments;
    bool m_multiLine = false;
    mutable bool m_currentlyExpanding = false;
    // Set while the fields are being written, so that what they emit on the
    // way is not read back as something the user typed.
    bool m_showing = false;
    std::function<QString()> m_resetter;
};

class PROJECTEXPLORER_EXPORT UseLibraryPathsAspect : public Utils::BoolAspect
{
    Q_OBJECT

public:
    UseLibraryPathsAspect(Utils::AspectContainer *container = nullptr);

    void setOsType(Utils::OsType osType);

    bool operator()() const { return isEnabled() && Utils::BoolAspect::operator()(); }
};

class PROJECTEXPLORER_EXPORT UseVncDisplayAspect : public Utils::BoolAspect
{
    Q_OBJECT

public:
    UseVncDisplayAspect(Utils::AspectContainer *container = nullptr);

    bool operator()() const { return isEnabled() && Utils::BoolAspect::operator()(); }
};

class PROJECTEXPLORER_EXPORT UseDyldSuffixAspect : public Utils::BoolAspect
{
    Q_OBJECT

public:
    UseDyldSuffixAspect(Utils::AspectContainer *container = nullptr);

    // Makes the aspect drive DYLD_IMAGE_SUFFIX in the given environment.
    void applyTo(EnvironmentAspect &environment);
};

class PROJECTEXPLORER_EXPORT RunAsAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    RunAsAspect(Utils::AspectContainer *container = nullptr);

    // Empty means default
    QString user() const;
    void setUser(const QString &user);

private:
    QVariant variantValue() const override { return user(); }
    void fromMap(const Utils::Store &map) override;

    void updateUserNameEnabled();

    Utils::SelectionAspect m_selection{this};
    Utils::StringAspect m_user{this};

};

class PROJECTEXPLORER_EXPORT EnableCategoriesFilterAspect : public Utils::BoolAspect
{
    Q_OBJECT

public:
    EnableCategoriesFilterAspect(Utils::AspectContainer *container = nullptr);
};

// An executable, and on a device the alternative to it: two settings that are
// one thing to whatever runs them, so a container rather than an aspect that
// draws two of its own. Flattened, because the two are rows of the page around
// them and not a group.
class PROJECTEXPLORER_EXPORT ExecutableAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    enum ExecutionDeviceSelector { HostDevice, BuildDevice, RunDevice };

    explicit ExecutableAspect(Utils::AspectContainer *container = nullptr);
    ~ExecutableAspect() override;

    Utils::FilePath operator()() const { return executable(); }
    Utils::FilePath executable() const;
    void setExecutable(const Utils::FilePath &executable);

    void setDeviceSelector(Kit *kit, ExecutionDeviceSelector selector);
    void setSettingsKey(const Utils::Key &key);
    void makeOverridable(const Utils::Key &overridingKey, const Utils::Key &useOverridableKey);
    void setLabelText(const QString &labelText);
    void setPlaceHolderText(const QString &placeHolderText);
    void setHistoryCompleter(const Utils::Key &historyCompleterKey);
    void setExpectedKind(const Utils::PathChooserKind expectedKind);
    void setEnvironment(const Utils::Environment &env);
    void setReadOnly(bool readOnly);

    void setFocusToInputField();

    struct Data : BaseAspect::Data
    {
        Utils::FilePath executable;
    };

private:
    QString executableText() const;

    Utils::FilePathAspect m_executable;
    Utils::FilePathAspect *m_alternativeExecutable = nullptr;
    Kit *m_kit = nullptr;
    ExecutionDeviceSelector m_selector = RunDevice;
};

class PROJECTEXPLORER_EXPORT SymbolFileAspect : public Utils::FilePathAspect
{
    Q_OBJECT

public:
     SymbolFileAspect(Utils::AspectContainer *container = nullptr);
};

class PROJECTEXPLORER_EXPORT Interpreter
{
public:
    Interpreter();
    Interpreter(const QString &id,
                const QString &name,
                const Utils::FilePath &command,
                const DetectionSource &detectionSource = {});

    inline bool operator==(const Interpreter &other) const
    {
        return id == other.id && name == other.name && command == other.command
               && detectionSource == other.detectionSource;
    }

    void fromMap(const Utils::Store &);
    void toMap(Utils::Store &) const;

    QString id;
    QString name;
    Utils::FilePath command;
    DetectionSource detectionSource;
};

class PROJECTEXPLORER_EXPORT LauncherAspect : public Utils::BaseAspect
{
    Q_OBJECT

public:
    LauncherAspect(Utils::AspectContainer *container = nullptr);

    Launcher currentLauncher() const;
    void updateLaunchers(const QList<Launcher> &launchers);
    void setDefaultLauncher(const Launcher &launcher);
    void setCurrentLauncher(const Launcher &launcher);
    void setSettingsDialogId(Utils::Id id) { m_settingsDialogId = id; }

    void fromMap(const Utils::Store &) override;
    void toMap(Utils::Store &) const override;
    Utils::AspectPresentation presentation() const override;

    // The value is the launcher's id, which is what the descriptor's choices
    // are keyed by. A run configuration applies as it is edited, so what the
    // combo shows is what the aspect holds.
    QVariant variantValue() const override { return m_currentId; }
    void setVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) override;
    QVariant volatileVariantValue() const override { return variantValue(); }
    void setVolatileVariantValue(const QVariant &value,
                                 Announcement howToAnnounce = DoEmit) override
    {
        setVariantValue(value, howToAnnounce);
    }

    struct Data : Utils::BaseAspect::Data { Launcher launcher; };

private:
    void setCurrentLauncherId(const QString &id);
    QList<Launcher> m_launchers;
    QString m_defaultId;
    QString m_currentId;
    Utils::Id m_settingsDialogId;
};

class PROJECTEXPLORER_EXPORT MainScriptAspect : public Utils::FilePathAspect
{
    Q_OBJECT

public:
    MainScriptAspect(Utils::AspectContainer *container = nullptr);
};

class PROJECTEXPLORER_EXPORT X11ForwardingAspect : public Utils::StringAspect
{
    Q_OBJECT

public:
    X11ForwardingAspect(Utils::AspectContainer *container = nullptr);

    struct Data : StringAspect::Data { QString display; };

    QString display() const;
};

#ifdef WITH_TESTS
QObject *createArgumentsAspectTest();
QObject *createWorkingDirectoryAspectTest();
#endif

} // namespace ProjectExplorer
