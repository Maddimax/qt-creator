// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"
#include "ilocatorfilter.h"

namespace Core {

namespace Internal { class DirectoryFilterTest; }

class CORE_EXPORT DirectoryFilter : public ILocatorFilter
{
public:
    DirectoryFilter(Utils::Id id);
    bool openConfigDialog(QWidget *parent, bool &needsRefresh) override;

protected:
    void setIsCustomFilter(bool value);
    void addDirectory(const Utils::FilePath &directory);
    void removeDirectory(const Utils::FilePath &directory);
    void setFilters(const QStringList &filters);
    void setExclusionFilters(const QStringList &exclusionFilters);

    void saveState(QJsonObject &object) const override;
    void restoreState(const QJsonObject &object) override;

private:
    LocatorMatcherTasks matchers() final { return {m_cache.matcher()}; }
    void setDirectories(const Utils::FilePaths &directories);

    Utils::FilePaths m_directories;
    QStringList m_filters;
    QStringList m_exclusionFilters;
    bool m_isCustomFilter = true;
    LocatorFileCache m_cache;

#ifdef WITH_TESTS
    friend class Internal::DirectoryFilterTest;
#endif
};

namespace Internal {

#ifdef WITH_TESTS
QObject *createDirectoryFilterTest();
#endif

class DirectoryFilterOptions final : public LocatorFilterOptions
{
public:
    DirectoryFilterOptions(DirectoryFilter *filter,
                           bool isCustomFilter,
                           const Utils::FilePaths &directories,
                           const QStringList &filters,
                           const QStringList &exclusionFilters);

    // What the fields come to, in the shapes the filter keeps them in.
    Utils::FilePaths chosenDirectories() const;
    QStringList chosenFilters() const;
    QStringList chosenExclusionFilters() const;

    Utils::StringAspect name{this};
    Utils::StringListAspect directories{this};
    Utils::StringAspect filePattern{this};
    Utils::StringAspect exclusionPattern{this};
};

} // namespace Internal

} // namespace Core
