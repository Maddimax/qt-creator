// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ilocatorfilter.h"

#include "../core_global.h"

namespace Core {

class CORE_EXPORT UrlLocatorFilter final : public Core::ILocatorFilter
{
public:
    UrlLocatorFilter(Utils::Id id);
    UrlLocatorFilter(const QString &displayName, Utils::Id id);

    bool openConfigDialog(QWidget *parent, bool &needsRefresh) final;

    void addDefaultUrl(const QString &urlTemplate);
    QStringList remoteUrls() const { return m_remoteUrls; }
    void setRemoteUrls(const QStringList &urls) { m_remoteUrls = urls; }

    void setIsCustomFilter(bool value) { m_isCustomFilter = value; }
    bool isCustomFilter() const { return m_isCustomFilter; }

protected:
    void saveState(QJsonObject &object) const final;
    void restoreState(const QJsonObject &object) final;

private:
    LocatorMatcherTasks matchers() final;

    QString m_defaultDisplayName;
    QStringList m_defaultUrls;
    QStringList m_remoteUrls;
    bool m_isCustomFilter = false;
};

namespace Internal {

#ifdef WITH_TESTS
QObject *createUrlFilterTest();
#endif

class UrlFilterOptions final : public LocatorFilterOptions
{
public:
    explicit UrlFilterOptions(UrlLocatorFilter *filter);

    void applyTo(UrlLocatorFilter *filter) const;

    // What Add starts a new entry as.
    static QString newUrlTemplate();

    Utils::StringAspect name{this};
    Utils::StringListAspect urls{this};
};

} // namespace Internal
} // namespace Core
