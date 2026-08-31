// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ilocatorfilter.h"

namespace Core::Internal {

#ifdef WITH_TESTS
QObject *createFileSystemFilterTest();
#endif

class FileSystemFilter final : public ILocatorFilter
{
public:
    FileSystemFilter();
    bool openConfigDialog(QWidget *parent, bool &needsRefresh) final;

protected:
    void saveState(QJsonObject &object) const final;
    void restoreState(const QJsonObject &object) final;

private:
    LocatorMatcherTasks matchers() final;

    static const bool s_includeHiddenDefault = true;
    bool m_includeHidden = s_includeHiddenDefault;

#ifdef WITH_TESTS
    friend class FileSystemFilterTest;
#endif
};

} // namespace Core::Internal
