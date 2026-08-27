// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/result.h>

#include <QTest>

using namespace Utils;

class tst_Result : public QObject
{
    Q_OBJECT

private slots:
    // The macros are handed calls, not values - removing a file watch, closing
    // something - so asking a failed result for its error a second time would
    // do the work again. That cost a soft assert per failure once.
    void checkEvaluatesItsArgumentOnce()
    {
        int calls = 0;
        const auto failing = [&calls] {
            ++calls;
            return Result<>(ResultError(QString("no")));
        };

        QTC_CHECK_RESULT(failing());
        QCOMPARE(calls, 1);

        calls = 0;
        const auto succeeding = [&calls] {
            ++calls;
            return Result<>(ResultOk);
        };
        QTC_CHECK_RESULT(succeeding());
        QCOMPARE(calls, 1);
    }

    void assertEvaluatesItsArgumentOnce()
    {
        int calls = 0;
        const auto failing = [&calls] {
            ++calls;
            return Result<>(ResultError(QString("no")));
        };

        QTC_ASSERT_RESULT(failing(), /*no action*/);
        QCOMPARE(calls, 1);
    }

    // The action must not be swallowed by a loop of the macro's own.
    void assertActionCanLeaveTheCallersLoop()
    {
        int seen = 0;
        for (int i = 0; i < 3; ++i) {
            QTC_ASSERT_RESULT(Result<>(ResultError(QString("no"))), break);
            ++seen;
        }
        QCOMPARE(seen, 0);
    }
};

QTEST_GUILESS_MAIN(tst_Result)

#include "tst_result.moc"
