// Copyright (C) 2019 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <debugger/debuggerconstants.h>

#include <QtTaskTree/QBarrier>

#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/qtcprocess.h>
#include <utils/result.h>
#include <utils/store.h>

#include <QSet>
#include <QUrl>

namespace Debugger { class DebuggerRunParameters; }

namespace ProjectExplorer { class RunControl; }

namespace BareMetal::Internal {

class BareMetalDevice;

// IDebugServerProvider

class IDebugServerProvider : public Utils::AspectContainer
{
    Q_OBJECT

protected:
    explicit IDebugServerProvider(const QString &id);

public:
    ~IDebugServerProvider() override;

    QString displayName() const;
    void setDisplayName(const QString &name);

    QUrl channel() const;
    void setChannel(const QUrl &channel);
    void setChannel(const QString &host, int port);

    virtual QString channelPipe() const;

    QString id() const;
    QString typeDisplayName() const;
    Debugger::DebuggerEngineType engineType() const;

    virtual bool operator==(const IDebugServerProvider &other) const;

    // The rows a page shows for this provider, in the order it wants them.
    // This is what the configuration widget was: which settings, and in what
    // order. Every kind answers with its own aspects.
    virtual void addSettingsRows(Utils::AspectContainer &rows);

    virtual void toMap(Utils::Store &data) const;
    virtual void fromMap(const Utils::Store &data);

    virtual Utils::Result<> setupDebuggerRunParameters(Debugger::DebuggerRunParameters &rp,
            ProjectExplorer::RunControl *runControl) const = 0;
    virtual std::optional<QtTaskTree::BarrierKickerGetter> serverRunner(
            ProjectExplorer::RunControl *runControl) const = 0;

    virtual bool isValid() const = 0;
    virtual bool isSimulator() const { return false; }

    // Text the debug server writes once it accepts debugger connections. When
    // it is empty, the server counts as ready as soon as it has started.
    virtual QString readyMessage() const { return {}; }

protected:
    void connectReadyBarrier(ProjectExplorer::RunControl *runControl, Utils::Process &process,
                             QtTaskTree::QBarrier *barrier) const;

    void setTypeDisplayName(const QString &typeDisplayName);
    void setEngineType(Debugger::DebuggerEngineType engineType);

    void providerUpdated();
    void resetId();

    // What every provider is asked, whatever kind it is.
    Utils::StringAspect providerName{this};
    // The host and the port read as one address, so they are one row.
    Utils::AspectContainer address{this};
    Utils::StringAspect host{&address};
    Utils::IntegerAspect port{&address};

    QString m_id;
    QString m_typeDisplayName;
    Debugger::DebuggerEngineType m_engineType = Debugger::NoEngineType;

    friend class DebugServerProvidersSettingsWidget;
    friend class IDebugServerProviderFactory;
};

// IDebugServerProviderFactory

class IDebugServerProviderFactory
{
public:
    ~IDebugServerProviderFactory();

    QString id() const;
    QString displayName() const;

    IDebugServerProvider *create() const;
    IDebugServerProvider *restore(const Utils::Store &data) const;

    bool canRestore(const Utils::Store &data) const;

    static QString idFromMap(const Utils::Store &data);
    static void idToMap(Utils::Store &data, const QString &id);

    static const QList<IDebugServerProviderFactory *> factories();

protected:
    IDebugServerProviderFactory();

    void setId(const QString &id);
    void setDisplayName(const QString &name);
    void setCreator(const std::function<IDebugServerProvider *()> &creator);

private:
    IDebugServerProviderFactory(const IDebugServerProviderFactory &) = delete;
    IDebugServerProviderFactory &operator=(const IDebugServerProviderFactory &) = delete;

    QString m_displayName;
    QString m_id;
    std::function<IDebugServerProvider *()> m_creator;
};

} // namespace BareMetal::Internal
