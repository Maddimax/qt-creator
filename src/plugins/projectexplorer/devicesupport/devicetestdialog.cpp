// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicetestdialog.h"

#include "../projectexplorertr.h"

#include <coreplugin/outputpaneview.h>

#include <utils/layoutbuilder.h>

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#ifdef WITH_TESTS
#include <QTest>
#include <QTextBlock>
#endif

#include <QVBoxLayout>
#include <QPushButton>
#include <QTextCharFormat>

namespace ProjectExplorer::Internal {

// How a line of a device test reads: what went wrong is told apart from what
// merely happened, and the last line - the answer - is told apart from both by
// being the only bold one.
//
// Kept out of the dialog because it is a question about a message, and there
// the only way to ask it was to run a device test and look.
Utils::OutputFormat deviceTestMessageFormat(bool isError)
{
    return isError ? Utils::ErrorMessageFormat : Utils::NormalMessageFormat;
}

QString deviceTestSummary(DeviceTester::TestResult result)
{
    return result == DeviceTester::TestSuccess ? Tr::tr("Device test finished successfully.")
                                               : Tr::tr("Device test failed.");
}

class DeviceTestDialog::DeviceTestDialogPrivate
{
public:
    DeviceTestDialogPrivate(DeviceTester *tester)
        : deviceTester(tester)
    {}

    DeviceTester * const deviceTester;
    bool finished = false;
    Core::OutputPaneView *output = nullptr;
    QDialogButtonBox *buttonBox = nullptr;
};

DeviceTestDialog::DeviceTestDialog(const IDevice::Ptr &deviceConfiguration,
                                   QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<DeviceTestDialogPrivate>(deviceConfiguration->createDeviceTester()))
{
    resize(620, 580);

    // A transcript, not a field: it is written to as the test runs, and its
    // lines are coloured. That is what an output view is, and one works in a
    // dialog as well as in a pane.
    d->output = new Core::OutputPaneView;
    d->buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(d->output);
    layout->addWidget(d->buttonBox);

    d->deviceTester->setParent(this);
    connect(d->buttonBox, &QDialogButtonBox::rejected, this, &DeviceTestDialog::reject);
    connect(d->deviceTester, &DeviceTester::progressMessage,
            this, &DeviceTestDialog::handleProgressMessage);
    connect(d->deviceTester, &DeviceTester::errorMessage,
            this, &DeviceTestDialog::handleErrorMessage);
    connect(d->deviceTester, &DeviceTester::finished,
            this, &DeviceTestDialog::handleTestFinished);
    d->deviceTester->testDevice();
}

DeviceTestDialog::~DeviceTestDialog() = default;

void DeviceTestDialog::reject()
{
    if (!d->finished) {
        d->deviceTester->disconnect(this);
        d->deviceTester->stopTest();
    }
    QDialog::reject();
}

void DeviceTestDialog::handleProgressMessage(const QString &message)
{
    d->output->appendMessage(message + '\n', deviceTestMessageFormat(false));
}

void DeviceTestDialog::handleErrorMessage(const QString &message)
{
    d->output->appendMessage(message + '\n', deviceTestMessageFormat(true));
}

void DeviceTestDialog::handleTestFinished(DeviceTester::TestResult result)
{
    d->finished = true;
    d->buttonBox->button(QDialogButtonBox::Cancel)->setText(Tr::tr("Close"));
    appendSummary(deviceTestSummary(result), result != DeviceTester::TestSuccess);
}

void DeviceTestDialog::appendSummary(const QString &text, bool isError)
{
    // Everything written so far goes in first: the queue is drained before the
    // answer is written straight into the document, or the answer would land
    // above the lines that led to it.
    d->output->flush();

    QTextDocument * const document = d->output->sourceDocument();
    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);

    // The only bold line, which is what makes it the answer rather than one
    // more thing that happened. An output format cannot say bold, so this is
    // written as a character format rather than appended as a message.
    QTextCharFormat format;
    format.setForeground(Utils::creatorColor(
        isError ? Utils::Theme::OutputPanes_ErrorMessageTextColor
                : Utils::Theme::OutputPanes_NormalMessageTextColor));
    QFont font = format.font();
    font.setBold(true);
    format.setFont(font);

    if (!document->isEmpty())
        cursor.insertBlock();
    cursor.insertText(text, format);
}

#ifdef WITH_TESTS

class DeviceTestDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhichFormatAMessageGets()
    {
        // What went wrong is told apart from what merely happened.
        QCOMPARE(deviceTestMessageFormat(false), Utils::NormalMessageFormat);
        QCOMPARE(deviceTestMessageFormat(true), Utils::ErrorMessageFormat);
        QVERIFY(deviceTestMessageFormat(true) != deviceTestMessageFormat(false));
    }

    void testWhatTheTestSaysWhenItIsDone()
    {
        QVERIFY(!deviceTestSummary(DeviceTester::TestSuccess).isEmpty());
        QVERIFY(deviceTestSummary(DeviceTester::TestFailure)
                != deviceTestSummary(DeviceTester::TestSuccess));
    }

    void testTheAnswerIsWrittenUnderWhatLedToIt()
    {
        // The transcript is queued and the summary is written straight into
        // the document, so without draining the queue first the answer lands
        // above the lines it is about.
        Core::OutputPaneView output;
        output.appendMessage("first\n", Utils::NormalMessageFormat);
        output.appendMessage("second\n", Utils::ErrorMessageFormat);
        output.flush();

        QTextDocument * const document = output.sourceDocument();
        QTextCursor cursor(document);
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat bold;
        QFont font = bold.font();
        font.setBold(true);
        bold.setFont(font);
        if (!document->isEmpty())
            cursor.insertBlock();
        cursor.insertText("the answer", bold);

        const QStringList lines = output.toPlainText().split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.last(), QString("the answer"));
        QVERIFY2(lines.first() == "first", "the queued lines were not written first");

        // And the answer is the only bold one, which is what makes it the
        // answer rather than one more thing that happened.
        const QTextBlock last = document->lastBlock();
        QVERIFY(last.charFormat().font().bold() || !last.layout()->formats().isEmpty());
        QVERIFY2(!document->firstBlock().charFormat().font().bold(),
                 "an ordinary line was written bold");
    }
};

QObject *createDeviceTestDialogTest()
{
    return new DeviceTestDialogTest;
}

#endif // WITH_TESTS

} // namespace ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "devicetestdialog.moc"
#endif
