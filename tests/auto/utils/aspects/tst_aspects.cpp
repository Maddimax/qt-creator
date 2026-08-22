// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/aspects.h>

#include <QSignalSpy>
#include <QTest>

using namespace Utils;

class tst_Aspects : public QObject
{
    Q_OBJECT

private slots:
    void valuePropertyIsVolatile();
    void valuePropertyEmitsOnce();
    void valuePropertyIgnoresWrongType();
    void metadataProperties();
};

// Writing the "value" property must not commit, so that a settings page can
// still cancel.
void tst_Aspects::valuePropertyIsVolatile()
{
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue(1);

    QVERIFY(aspect.setProperty("value", 42));
    QCOMPARE(aspect.volatileValue(), 42);
    QCOMPARE(aspect.value(), 1);
    QVERIFY(aspect.isDirty());
    QCOMPARE(aspect.property("value").toLongLong(), 42);

    aspect.cancel();
    QCOMPARE(aspect.volatileValue(), 1);
    QVERIFY(!aspect.isDirty());

    QVERIFY(aspect.setProperty("value", 7));
    aspect.apply();
    QCOMPARE(aspect.value(), 7);
    QVERIFY(!aspect.isDirty());
}

void tst_Aspects::valuePropertyEmitsOnce()
{
    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue("before");

    QSignalSpy spy(&aspect, &BaseAspect::volatileValueChanged);
    QVERIFY(aspect.setProperty("value", "after"));
    QCOMPARE(spy.count(), 1);

    // Writing the same value again must not signal.
    QVERIFY(aspect.setProperty("value", "after"));
    QCOMPARE(spy.count(), 1);
}

void tst_Aspects::valuePropertyIgnoresWrongType()
{
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue(5);

    // A value that cannot become a qint64 must leave the aspect alone.
    aspect.setProperty("value", QVariant::fromValue(QStringList{"a", "b"}));
    QCOMPARE(aspect.volatileValue(), 5);
}

void tst_Aspects::metadataProperties()
{
    BoolAspect aspect;
    aspect.setLabelText("Label");
    aspect.setToolTip("Tip");

    QCOMPARE(aspect.property("labelText").toString(), QString("Label"));
    QCOMPARE(aspect.property("toolTip").toString(), QString("Tip"));
    QCOMPARE(aspect.property("enabled").toBool(), true);
    QCOMPARE(aspect.property("visible").toBool(), true);
    QCOMPARE(aspect.property("readOnly").toBool(), false);

    QSignalSpy enabledSpy(&aspect, &BaseAspect::enabledChanged);
    QVERIFY(aspect.setProperty("enabled", false));
    QCOMPARE(aspect.property("enabled").toBool(), false);
    QCOMPARE(enabledSpy.count(), 1);
}

QTEST_GUILESS_MAIN(tst_Aspects)

#include "tst_aspects.moc"
