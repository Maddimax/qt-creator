// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "compilerexploreroptions.h"

#include "compilerexplorersettings.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <QVBoxLayout>

#ifdef WITH_TESTS
#include "api/config.h"



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

        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "CompilerExplorerOptions.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
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
