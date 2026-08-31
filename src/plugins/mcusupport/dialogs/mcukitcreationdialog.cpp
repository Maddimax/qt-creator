// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcukitcreationdialog.h"

#include "../mcuabstractpackage.h"
#include "../mcusupportconstants.h"
#include "../mcusupporttr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/filepath.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QVBoxLayout>

using namespace Utils;

namespace McuSupport::Internal {

// The headline of a message: what kind of thing it is, and which target it is
// about.
QString messageHeadline(const McuSupportMessage &message)
{
    return QString("<b>%1 %2</b> : %3")
        .arg(Tr::tr("Target"),
             message.status == McuSupportMessage::Warning ? Tr::tr("Warning") : Tr::tr("Error"),
             message.platform);
}

// What the message says: which package, and what is wrong with it.
QString messageBody(const McuSupportMessage &message)
{
    return QString("<b>%1</b>: %2<br><br><b>%3</b>: %4")
        .arg(Tr::tr("Package"), message.packageName, Tr::tr("Status"), message.message);
}

// Where the reader is in the list. One-based, because it is read and not
// indexed with.
QString messageCounter(int index, int count)
{
    return QString("%1 / %2").arg(QString::number(index + 1), QString::number(count));
}

class McuKitCreationSettings final : public AspectContainer
{
public:
    McuKitCreationSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/McuSupport/McuKitCreationDialog.qml"));

        headline.setQmlName("Headline");
        headline.setTextFormat(AspectControls::TextFormat::RichText);

        information.setQmlName("Information");
        information.setTextFormat(AspectControls::TextFormat::RichText);
        information.setWordWrap(true);

        qtMcusPath.setQmlName("QtMcusPath");

        counter.setQmlName("Counter");

        previous.setQmlName("Previous");
        previous.setActionText("<");
        next.setQmlName("Next");
        next.setActionText(">");
        fix.setQmlName("Fix");
        fix.setActionText(Tr::tr("Fix"));
        help.setQmlName("Help");
        help.setActionText(Tr::tr("Help"));
    }

    // Which way the reader may move from where they are. Paging exists at all
    // only where there is more than one message.
    void showMessage(const MessagesList &messages, int index)
    {
        const bool several = messages.size() > 1;
        previous.setVisible(several);
        next.setVisible(several);
        if (index < 0 || index >= messages.size())
            return;

        previous.setEnabled(index > 0);
        next.setEnabled(index < messages.size() - 1);

        const McuSupportMessage &message = messages.at(index);
        // The kind of thing it is is what drew the platform's warning or
        // critical pixmap beside the headline.
        headline.setIconType(message.status == McuSupportMessage::Warning ? InfoType::Warning
                                                                         : InfoType::Error);
        headline.setText(messageHeadline(message));
        information.setText(messageBody(message));
        counter.setText(messageCounter(index, messages.size()));
    }

    TextDisplay headline{this};
    TextDisplay information{this};
    TextDisplay qtMcusPath{this};
    TextDisplay counter{this};
    ActionAspect previous{this};
    ActionAspect next{this};
    ActionAspect fix{this};
    ActionAspect help{this};
};

McuKitCreationDialog::McuKitCreationDialog(const MessagesList &messages,
                                          const SettingsHandler::Ptr &settingsHandler,
                                          McuPackagePtr qtMCUPackage,
                                          QWidget *parent)
    : QDialog(parent)
    , m_settings(new McuKitCreationSettings)
    , m_messages(messages)
{
    resize(500, 300);
    setWindowTitle(Tr::tr("Qt for MCUs Kit Creation"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ignore);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    m_settings->next.setAction([this] { updateMessage(1); });
    m_settings->previous.setAction([this] { updateMessage(-1); });
    m_settings->fix.setAction([this, settingsHandler] {
        // Open the MCU Options widget on the current platform
        settingsHandler->setInitialPlatformName(m_messages[m_currentIndex].platform);
        Core::ICore::showSettings(Constants::SETTINGS_ID);
        // reset the initial platform name
        settingsHandler->setInitialPlatformName("");
    });
    m_settings->help.setAction([] {
        QDesktopServices::openUrl(QUrl("https://doc.qt.io/QtForMCUs/qtul-prerequisites.html"));
    });

    if (messages.empty()) {
        // Nothing to fix, and nothing to page through.
        m_settings->fix.setVisible(false);
        m_settings->counter.setVisible(false);
        m_settings->information.setText(
            QCoreApplication::translate("QtC::Autotest", "No errors detected."));
    }
    m_settings->showMessage(messages, -1);
    if (!messages.empty())
        updateMessage(1);

    m_settings->qtMcusPath.setVisible(qtMCUPackage->isValidStatus());
    if (qtMCUPackage->isValidStatus()) {
        m_settings->qtMcusPath.setText(
            Tr::tr("Qt for MCUs path %1").arg(qtMCUPackage->path().toUserOutput()));
    }
}

McuKitCreationDialog::~McuKitCreationDialog() = default;

void McuKitCreationDialog::updateMessage(const int inc)
{
    m_currentIndex += inc;
    m_settings->showMessage(m_messages, m_currentIndex);
}

#ifdef WITH_TESTS

class McuKitCreationDialogTest final : public QObject
{
    Q_OBJECT

private:
    static MessagesList twoMessages()
    {
        return {{"SDK", "STM32F7", "not found", McuSupportMessage::Error},
                {"Toolchain", "NXP1064", "out of date", McuSupportMessage::Warning}};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        McuKitCreationSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "McuKitCreationDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatAMessageSays()
    {
        const MessagesList messages = twoMessages();
        QCOMPARE(messageHeadline(messages.at(0)),
                 QString("<b>%1 %2</b> : STM32F7").arg(Tr::tr("Target"), Tr::tr("Error")));
        QCOMPARE(messageHeadline(messages.at(1)),
                 QString("<b>%1 %2</b> : NXP1064").arg(Tr::tr("Target"), Tr::tr("Warning")));

        QVERIFY2(messageBody(messages.at(0)).contains("SDK"), "the package is not named");
        QVERIFY2(messageBody(messages.at(0)).contains("not found"), "the reason is not given");
    }

    void testWhereTheReaderIsInTheList()
    {
        // One-based: it is read, not indexed with.
        QCOMPARE(messageCounter(0, 2), QString("1 / 2"));
        QCOMPARE(messageCounter(1, 2), QString("2 / 2"));
    }

    void testWhichWayTheReaderMayMove()
    {
        McuKitCreationSettings settings;
        const MessagesList messages = twoMessages();

        settings.showMessage(messages, 0);
        QVERIFY2(!settings.previous.isEnabled(), "there is something before the first message");
        QVERIFY(settings.next.isEnabled());
        QCOMPARE(settings.counter.displayText(), QString("1 / 2"));

        settings.showMessage(messages, 1);
        QVERIFY(settings.previous.isEnabled());
        QVERIFY2(!settings.next.isEnabled(), "there is something after the last message");
    }

    void testASingleMessageIsNotPagedThrough()
    {
        McuKitCreationSettings settings;
        const MessagesList one = {twoMessages().first()};
        settings.showMessage(one, 0);
        QVERIFY2(!settings.previous.isVisible(), "one message is offered a previous one");
        QVERIFY2(!settings.next.isVisible(), "one message is offered a next one");

        settings.showMessage(twoMessages(), 0);
        QVERIFY(settings.previous.isVisible());
        QVERIFY(settings.next.isVisible());
    }

    void testTheHeadlineSaysWhatKindOfThingItIs()
    {
        // The widget dialog drew the platform's warning or critical pixmap
        // beside it; the aspect says which and the form draws it.
        McuKitCreationSettings settings;
        const MessagesList messages = twoMessages();

        settings.showMessage(messages, 0);
        QCOMPARE(settings.headline.presentation().infoType, InfoType::Error);
        settings.showMessage(messages, 1);
        QCOMPARE(settings.headline.presentation().infoType, InfoType::Warning);
    }

    void testAnIndexThatIsNotThereChangesNothing()
    {
        // updateMessage() walks by one from -1 and is called by two buttons;
        // stepping off either end must not read past the list.
        McuKitCreationSettings settings;
        const MessagesList messages = twoMessages();
        settings.showMessage(messages, 1);
        const QString last = settings.headline.displayText();

        settings.showMessage(messages, 2);
        QCOMPARE(settings.headline.displayText(), last);
        settings.showMessage(messages, -1);
        QCOMPARE(settings.headline.displayText(), last);
    }
};

QObject *createMcuKitCreationDialogTest()
{
    return new McuKitCreationDialogTest;
}

#endif // WITH_TESTS

} // namespace McuSupport::Internal

#ifdef WITH_TESTS
#include "mcukitcreationdialog.moc"
#endif
