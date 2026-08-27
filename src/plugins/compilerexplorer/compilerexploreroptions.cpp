// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "compilerexploreroptions.h"

#include "compilerexplorersettings.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <QVBoxLayout>

#ifdef WITH_TESTS
#include "api/config.h"

#include <utils/algorithm.h>

#include <QNetworkAccessManager>
#include <QTest>
#endif

namespace CompilerExplorer {

CompilerExplorerOptions::CompilerExplorerOptions(CompilerSettings &compilerSettings, QWidget *parent)
    : QDialog(parent, Qt::Popup)
{
    // The settings name their own form; this only has to put it in a window.
    // The popup itself stays a QDialog until what shows it is Qt Quick too.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(Core::createAspectForm(&compilerSettings));
}


#ifdef WITH_TESTS

// Every QML warning raised while a form is built. A wrong aspect name is not a
// load error - the component still instantiates - it is a warning saying the
// binding could not be resolved, and the control is simply missing. Nothing
// else notices, so this is what has to.
static QStringList s_qmlComplaints;

static void collectComplaints(QtMsgType, const QMessageLogContext &, const QString &message)
{
    s_qmlComplaints.append(message);
}

// The popup a compiler's settings are edited in. It is not an options page, so
// the page census never sees it - which is why it is asserted here.
class CompilerExplorerOptionsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testEveryCompilerSettingIsDrawn()
    {
        // A config that is never asked anything: nothing here fetches, and a
        // settings object needs one to be constructed.
        QNetworkAccessManager network;
        CompilerSettings settings([&network] { return Api::Config(&network); });

        s_qmlComplaints.clear();
        QtMessageHandler previous = qInstallMessageHandler(collectComplaints);
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        qInstallMessageHandler(previous);

        QVERIFY2(form, "the settings produced no form at all");

        // A QQuickWidget rather than a widget layout: without a qmlSource the
        // container would have been drawn the old way, and nothing else would
        // have said so.
        QObject * const quick = form->findChild<QObject *>(QString(), Qt::FindChildrenRecursively);
        QVERIFY(quick);
        QObject *quickWidget = nullptr;
        const QList<QObject *> children = form->findChildren<QObject *>();
        for (QObject * const child : children) {
            if (QLatin1String(child->metaObject()->className()) == QLatin1String("QQuickWidget"))
                quickWidget = child;
        }
        QVERIFY2(quickWidget, "the settings produced a widget form, so the Quick one was declined");

        // Loaded, and without complaint. Ready alone would not do: a form that
        // names an aspect that is not there loads perfectly well and leaves the
        // control out, which is exactly the mistake this catches.
        const int ready = 1; // QQuickWidget::Ready
        QCOMPARE(quickWidget->property("status").toInt(), ready);
        const QStringList aboutThisForm = Utils::filtered(s_qmlComplaints,
                                                          [](const QString &complaint) {
                                                              return complaint.contains(
                                                                  "CompilerExplorerOptions.qml");
                                                          });
        QVERIFY2(aboutThisForm.isEmpty(), qPrintable(aboutThisForm.join("; ")));
    }
};

QObject *createCompilerExplorerOptionsTest()
{
    return new CompilerExplorerOptionsTest;
}

#endif // WITH_TESTS

} // namespace CompilerExplorer

#ifdef WITH_TESTS
#include "compilerexploreroptions.moc"
#endif
