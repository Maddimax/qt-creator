// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "samplerforms_test.h"

#include "../callstacksampler.h"
#include "../combinedsampler.h"
#include "../perfsampler.h"
#include "../qmlprofilersampler.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>

#include <QTest>

#include <memory>

namespace QmlProfiler::Internal {

// Every recording backend's settings are drawn by a form of their own, and
// only the standalone Qt Profiler renders them - so on a machine where a
// backend is not offered (perf outside Linux, the call-stack sampler outside
// macOS and Windows) nothing else would ever load its QML.
//
// A name a form gets wrong is not a load error: the page still builds, the
// control is simply missing. So the evidence is the diagnostic.
class SamplerFormsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testEverySamplerFormNamesAspectsThatExist_data()
    {
        QTest::addColumn<int>("backend");
        QTest::newRow("call-stack") << 0;
        QTest::newRow("perf") << 1;
        QTest::newRow("qml profiler") << 2;
        QTest::newRow("combined") << 3;
    }

    void testEverySamplerFormNamesAspectsThatExist()
    {
        QFETCH(int, backend);

        std::unique_ptr<Utils::AspectContainer> settings;
        switch (backend) {
        case 0: settings = std::make_unique<CallStackSamplerSettings>(); break;
        case 1: settings = std::make_unique<PerfSamplerSettings>(); break;
        case 2: settings = std::make_unique<QmlProfilerSamplerSettings>(); break;
        case 3: settings = std::make_unique<CombinedSamplerSettings>(); break;
        }
        QVERIFY(settings);
        QVERIFY2(!settings->qmlSource().isEmpty(), "this backend still lays itself out");

        static QStringList messages;
        messages.clear();
        QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &text) {
                messages.append(text);
            });
        const std::unique_ptr<QWidget> form(Core::createAspectForm(settings.get()));
        qInstallMessageHandler(previous);

        QVERIFY(form);

        // Asked through the metaobject: the plugin has no reason to link Qt
        // Quick for a test.
        QWidget *quick = nullptr;
        for (QWidget *child : form->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0)
                quick = child;
        }
        // The Qt Quick form factory is installed by the QuickUi plugin, so
        // without it Core::createAspectForm() falls back to the widget path
        // and there is no QML to have got wrong. Said out loud rather than
        // asserting nothing: run this with -load all.
        if (!quick)
            QSKIP("no Qt Quick form factory is installed");

        constexpr int quickWidgetReady = 1; // QQuickWidget::Ready
        QCOMPARE(quick->property("status").toInt(), quickWidgetReady);

        const QStringList unresolved = Utils::filtered(messages, [](const QString &text) {
            return text.contains("Unable to assign") || text.contains("is not defined");
        });
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join("; ")));
    }

    // The perf backend is the only one that reuses settings it does not own,
    // and they have to be drawable on their own for the window to show them
    // beside its aspects.
    void testPerfReusesTheIdesPerfConfiguration()
    {
        PerfSamplerSettings settings;
        const QList<Utils::AspectContainer *> reused = settings.reusedSettings();
        QCOMPARE(reused.size(), 1);
        QVERIFY(!reused.first()->qmlSource().isEmpty());

        const std::unique_ptr<QWidget> form(Core::createAspectForm(reused.first()));
        QVERIFY2(form, "the reused settings name no form");
    }
};

QObject *createSamplerFormsTest()
{
    return new SamplerFormsTest;
}

} // namespace QmlProfiler::Internal

#include "samplerforms_test.moc"
