// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <qtcquick/aspectform.h>

#include <utils/aspects.h>

#include <QQuickWidget>
#include <QTest>

namespace QuickUi::Internal {

// Runs inside a fully initialised Qt Creator, so it exercises the real
// registered options pages rather than a synthetic container.
class QuickUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAspectDrivenPagesRenderWithQuick();
};

void QuickUiTest::testAspectDrivenPagesRenderWithQuick()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int aspectDriven = 0;
    int renderedWithQuick = 0;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        ++aspectDriven;

        Core::IOptionsPageWidget *widget = page->createWidget();
        if (!widget)
            continue;
        if (widget->findChild<QQuickWidget *>())
            ++renderedWithQuick;
    }

    qInfo() << "aspect-driven pages:" << aspectDriven
            << "rendered with Qt Quick:" << renderedWithQuick;

    // How many aspect-driven pages exist depends on which plugins this test run
    // loads, which is only QuickUi's dependency closure. The invariant is that
    // every one of them goes through the Qt Quick path.
    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, aspectDriven);

    Core::setAspectFormFactory({});
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
