// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "aspects.h"
#include "result.h"

#include <QMap>

namespace Utils {

// A list of things that have to be true, and one line saying whether they are.
// Each point is a row that reads either as what it checks or, when it fails,
// as why - which is what a page shows instead of an error nobody can act on.
//
// The label text is that one line, so a page draws this as a group and gets
// the summary as its title.
class QTCREATOR_UTILS_EXPORT SummaryAspect : public AspectContainer
{
    Q_OBJECT

public:
    SummaryAspect(AspectContainer *container,
                  const QMap<int, QString> &validationPoints,
                  const QString &validText,
                  const QString &invalidText);

    template<class T>
    void setPointValid(int key, const Result<T> &test)
    {
        setPointValid(key, test.has_value(), test.has_value() ? QString{} : test.error());
    }

    void setPointValid(int key, bool valid, const QString &errorText = {});
    bool rowsOk(const QList<int> &keys) const;
    bool allRowsOk() const;

    // Appended to the summary while everything is in order - what was found,
    // rather than that something was.
    void setInfoText(const QString &text);
    // Says that the checks are still running, so the summary reports neither.
    void setInProgressText(const QString &text);

signals:
    // Whether every point holds. A page listens to decide what else to offer.
    void allRowsOkChanged(bool ok);

private:
    void updateSummary();

    class Point
    {
    public:
        TextDisplay *row = nullptr;
        QString validText;
        bool valid = false;
    };

    QString m_validText;
    QString m_invalidText;
    QString m_infoText;
    QString m_inProgressText;
    QMap<int, Point> m_points;
    bool m_lastAllOk = false;
};

} // namespace Utils
