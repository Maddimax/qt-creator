// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "profilertr.h"
#include "qmlprofilerattachdialog.h"

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/kitmanager.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace ProjectExplorer;

namespace Profiler::Internal {

// Which kits can be attached to: a QML application to attach to is running
// somewhere, so a kit with no device is no use here.
bool kitCanBeAttachedTo(const Kit *kit)
{
    return RunDeviceKitAspect::device(kit) != nullptr;
}

class AttachSettings final : public Utils::AspectContainer
{
public:
    AttachSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Profiler/QmlProfilerAttachDialog.qml"));

        hint.setQmlName("Hint");
        hint.setText(Tr::tr("Select an externally started QML-debug enabled application.<p>"
                            "Commonly used command-line arguments are:")
                     + "<p><tt>-qmljsdebugger=port:&lt;port&gt;,block,<br>"
                       "&nbsp;&nbsp;services:CanvasFrameRate,EngineControl,DebugMessages</tt>");

        kitChooser.setQmlName("Kit");
        kitChooser.setKitPredicate(&kitCanBeAttachedTo);

        port.setQmlName("Port");
        port.setLabelText(Tr::tr("&Port:"));
        port.setRange(0, 65535);
        // The port qmljsdebugger is usually told to listen on.
        port.setDefaultValue(3768);
        port.setValue(3768);
    }

    Utils::TextDisplay hint{this};
    KitChooserAspect kitChooser{this};
    Utils::IntegerAspect port{this};
};

QmlProfilerAttachDialog::QmlProfilerAttachDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new AttachSettings)
{
    setWindowTitle(Tr::tr("Start QML Profiler"));

    const auto buttonBox
        = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);
}

QmlProfilerAttachDialog::~QmlProfilerAttachDialog() = default;

int QmlProfilerAttachDialog::port() const
{
    return m_settings->port();
}

void QmlProfilerAttachDialog::setPort(const int port)
{
    m_settings->port.setValue(port);
}

Kit *QmlProfilerAttachDialog::kit() const
{
    return m_settings->kitChooser.currentKit();
}

void QmlProfilerAttachDialog::setKitId(Utils::Id id)
{
    m_settings->kitChooser.setCurrentKitId(id);
}

#ifdef WITH_TESTS

class QmlProfilerAttachSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        // The first form to draw a kit chooser, so that the chooser renders as
        // part of a page - and not only that its aspects exist - is what this
        // is for.
        AttachSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "QmlProfilerAttachDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichKitsCanBeAttachedTo()
    {
        // A kit whose device type has no device is no use: the application to
        // attach to is running somewhere.
        Kit withoutDevice(Utils::Id::fromName("kit.nodevice"));
        RunDeviceTypeKitAspect::setDeviceTypeId(&withoutDevice,
                                                Utils::Id::fromName("kit.novicetype"));
        QVERIFY2(!kitCanBeAttachedTo(&withoutDevice),
                 "a kit with no device was offered to attach with");

        // A kit that keeps the default device type does resolve a device
        // without one being set, and is offered.
        Kit withDefaultDevice(Utils::Id::fromName("kit.defaultdevice"));
        QVERIFY2(kitCanBeAttachedTo(&withDefaultDevice),
                 "a kit with a usable device was not offered");

        // And the chooser is given that rule rather than keeping its own.
        AttachSettings settings;
        for (int i = 0; i < settings.kitChooser.kit.optionCount(); ++i) {
            settings.kitChooser.kit.setValue(i);
            Kit *const kit = settings.kitChooser.currentKit();
            QVERIFY2(kit && kitCanBeAttachedTo(kit), "a kit the dialog cannot use was offered");
        }
    }

    void testThePortItOpensOn()
    {
        AttachSettings settings;

        // qmljsdebugger's usual port, so the reader usually has nothing to
        // change.
        QCOMPARE(settings.port(), 3768);

        // And the whole range is available: a port is not always the usual
        // one, and the widget spin box allowed up to 65535.
        QCOMPARE(settings.port.presentation().maximum.toInt(), 65535);

        settings.port.setValue(1234);
        QCOMPARE(settings.port(), 1234);
    }
};

QObject *createQmlProfilerAttachSettingsTest()
{
    return new QmlProfilerAttachSettingsTest;
}

#endif // WITH_TESTS

} // namespace Profiler::Internal

#ifdef WITH_TESTS
#include "qmlprofilerattachdialog.moc"
#endif
