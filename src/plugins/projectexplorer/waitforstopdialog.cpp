// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "waitforstopdialog.h"

#include "projectexplorertr.h"
#include "runcontrol.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QTimer>
#include <QVBoxLayout>

using namespace ProjectExplorer;
using namespace ProjectExplorer::Internal;
using namespace Utils;

namespace ProjectExplorer::Internal {

// What the dialog says while it waits: the sentence, then the applications it
// is still waiting for, one per line.
QString waitForStopText(const QStringList &names)
{
    return Tr::tr("Waiting for applications to stop.") + "\n\n" + names.join('\n');
}

// How long to keep the dialog up once the last application has gone. It is
// shown for a moment even when everything stops at once, so that it does not
// flash past unread.
int remainingShowTime(qint64 elapsedMs)
{
    // Both ends: a clock that has not moved leaves the whole moment, and one
    // that has gone past it leaves none. Neither end may hand back more than
    // the moment or less than nothing.
    const qint64 elapsed = qMax(qint64(0), elapsedMs);
    return int(qMax(qint64(0), 1000 - elapsed));
}

class WaitForStopSettings final : public AspectContainer
{
public:
    WaitForStopSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/WaitForStopDialog.qml"));

        progress.setQmlName("Progress");
    }

    TextDisplay progress{this};
};

} // ProjectExplorer::Internal

WaitForStopDialog::WaitForStopDialog(const QList<ProjectExplorer::RunControl *> &runControls)
    : m_runControls(runControls)
    , m_settings(new WaitForStopSettings)
{
    setWindowTitle(Tr::tr("Waiting for Applications to Stop"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    updateProgressText();

    for (const RunControl *rc : runControls)
        connect(rc, &RunControl::stopped, this, [this, rc] { runControlFinished(rc); });

    m_timer.start();
}

WaitForStopDialog::~WaitForStopDialog() = default;

bool WaitForStopDialog::canceled()
{
    return !m_runControls.isEmpty();
}

void WaitForStopDialog::updateProgressText()
{
    m_settings->progress.setText(
        waitForStopText(Utils::transform(m_runControls, &RunControl::displayName)));
}

void WaitForStopDialog::runControlFinished(const RunControl *runControl)
{
    m_runControls.removeOne(runControl);
    if (!m_runControls.isEmpty()) {
        updateProgressText();
        return;
    }

    const int remaining = remainingShowTime(m_timer.elapsed());
    if (remaining > 0)
        QTimer::singleShot(remaining, this, &QDialog::close);
    else
        QDialog::close();
}

namespace ProjectExplorer::Internal {

#ifdef WITH_TESTS

class WaitForStopDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        WaitForStopSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "WaitForStopDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheDialogSaysWhileItWaits()
    {
        // The applications still running, one per line, under the sentence.
        QCOMPARE(waitForStopText({"One", "Two"}),
                 Tr::tr("Waiting for applications to stop.") + "\n\nOne\nTwo");

        // A single one is still on its own line rather than on the sentence's.
        QVERIFY2(waitForStopText({"Only"}).endsWith("\n\nOnly"), "the name ran into the sentence");

        // And nothing left to wait for leaves the sentence alone. The dialog
        // is on its way out at that point, so it must not read as an error.
        QCOMPARE(waitForStopText({}), Tr::tr("Waiting for applications to stop.") + "\n\n");
    }

    void testHowLongTheDialogStaysUpAfterTheLastOneStops()
    {
        // A moment, so that stopping instantly does not flash the dialog past
        // unread - and no wait at all once that moment has already passed.
        QCOMPARE(remainingShowTime(0), 1000);
        QCOMPARE(remainingShowTime(400), 600);
        QCOMPARE(remainingShowTime(1000), 0);
        QCOMPARE(remainingShowTime(5000), 0);

        // A clock that has gone backwards is not a reason to wait forever.
        QCOMPARE(remainingShowTime(-1), 1000);
    }
};

QObject *createWaitForStopDialogTest()
{
    return new WaitForStopDialogTest;
}

#endif // WITH_TESTS

} // ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "waitforstopdialog.moc"
#endif
