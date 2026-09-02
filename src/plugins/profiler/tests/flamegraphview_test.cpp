// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "flamegraphview_test.h"
#include "flamegraphmodel_test.h"

#include <profiler/qmlprofilertool.h>

#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWindow>

using namespace QmlDebug;
namespace Profiler::Internal {

FlameGraphViewTest::FlameGraphViewTest()
    : view(&manager)
{}

void FlameGraphViewTest::initTestCase()
{
    connect(&view, &QmlProfilerEventsView::showFullRange,
            this, [this](){ manager.restrictToRange(-1, -1); });
    FlameGraphModelTest::generateData(&manager, &aggregator);
    view.resize(500, 500);
    view.show();
    QTRY_VERIFY(QTest::qWaitForWindowExposed(&view));
}

void FlameGraphViewTest::testSelection()
{
    auto con1 = connect(&view, &QmlProfilerEventsView::gotoSourceLocation,
            [](const QString &file, int line, int column) {
        QCOMPARE(line, 0);
        QCOMPARE(column, 20);
        QCOMPARE(file, QLatin1String("somefile.js"));
    });

    int expectedType = 0;
    auto con2 = connect(&view, &QmlProfilerEventsView::typeSelected, [&](int selected) {
        QCOMPARE(selected, expectedType);
    });

    QSignalSpy spy(&view, SIGNAL(typeSelected(int)));

    // The graph is drawn by a child of the view, and it is shorter than the
    // view is - so the points below have to be in the child's coordinates or
    // they land outside it and select nothing.
    QWidget * const canvas = view.childAt(view.width() / 2, view.height() / 2);
    QVERIFY(canvas);

    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(15, canvas->height() - 15));
    if (spy.isEmpty())
        QVERIFY(spy.wait());

    // External setting of type should not send gotoSourceLocation or typeSelected
    view.selectByTypeId(1);
    QCOMPARE(spy.count(), 1);

    // Click in empty area deselects
    expectedType = -1;
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() - 15, 50));
    QCOMPARE(spy.count(), 2);

    view.onVisibleFeaturesChanged(1 << ProfileBinding);
    QCOMPARE(spy.count(), 2); // External event: still doesn't change anything

    disconnect(con1);
    disconnect(con2);

    // The mouse click will select a different event now, as the JS category has been hidden
    con1 = connect(&view, &QmlProfilerEventsView::gotoSourceLocation,
            [](const QString &file, int line, int column) {
        QCOMPARE(file, QLatin1String("somefile.js"));
        QCOMPARE(line, 2);
        QCOMPARE(column, 18);
    });

    con2 = connect(&view, &QmlProfilerEventsView::typeSelected, [](int selected) {
        QCOMPARE(selected, 2);
    });

    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(5, canvas->height() - 5));
    if (spy.count() == 1)
        QVERIFY(spy.wait());

    disconnect(con1);
    disconnect(con2);
}

void FlameGraphViewTest::testContextMenu()
{
    int targetWidth = 0;
    int targetHeight = 0;
    {
        QMenu testMenu;
        testMenu.addActions(QmlProfilerTool::profilerContextMenuActions());
        testMenu.addSeparator();
        testMenu.show();
        QVERIFY(QTest::qWaitForWindowExposed(testMenu.window()));
        targetWidth = testMenu.width() / 2;
        int prevHeight = testMenu.height();
        QAction dummy(QString("target"), this);
        testMenu.addAction(&dummy);
        targetHeight = (testMenu.height() + prevHeight) / 2;
    }

    QTest::mouseMove(&view, QPoint(250, 250));
    QSignalSpy spy(&view, SIGNAL(showFullRange()));

    QTimer timer;
    timer.setInterval(500);
    int menuClicks = 0;

    connect(&timer, &QTimer::timeout, this, [&]() {
        const QPointer<QWidget> activePopup = QApplication::activePopupWidget();
        if (!activePopup || !activePopup->windowHandle()
            || !activePopup->windowHandle()->isExposed()) {
            QContextMenuEvent *event = new QContextMenuEvent(QContextMenuEvent::Mouse,
                                                             QPoint(250, 250), QCursor::pos());
            QCoreApplication::postEvent(&view, event);
            return;
        }

        // FlameGraphView's menu is WA_DeleteOnClose, and every one of these
        // spins the event loop - so the popup can go away between any two of
        // them and has to be re-checked, not just checked once up front.
        QTest::mouseMove(activePopup, QPoint(targetWidth, targetHeight));
        if (!activePopup)
            return;
        QTest::mouseClick(activePopup, Qt::LeftButton, Qt::NoModifier,
                          QPoint(targetWidth, targetHeight));
        ++menuClicks;

        if (activePopup && !manager.isRestrictedToRange()) {
            // click somewhere else to remove the menu and return to outer function
            QTest::mouseMove(activePopup, QPoint(-10, -10));
            if (activePopup)
                QTest::mouseClick(activePopup, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
        }
    });

    timer.start();
    QTRY_VERIFY(menuClicks > 0);
    QCOMPARE(spy.count(), 0);
    manager.restrictToRange(1, 10);
    QVERIFY(manager.isRestrictedToRange());
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(menuClicks > 1);
    QVERIFY(!manager.isRestrictedToRange());
    timer.stop();
}

void FlameGraphViewTest::cleanupTestCase()
{
    manager.clearAll();
}

} // namespace Profiler::Internal
