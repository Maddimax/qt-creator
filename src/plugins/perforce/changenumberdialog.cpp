// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "changenumberdialog.h"

#include "perforcetr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Perforce::Internal {

// The range is the aspect's rather than a validator's, so the field cannot hold
// a number the dialog would then have to reject.
class ChangeNumberSettings final : public AspectContainer
{
public:
    ChangeNumberSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Perforce/ChangeNumberDialog.qml"));

        number.setQmlName("Number");
        number.setLabelText(Tr::tr("Change number:"));
        number.setRange(0, 1000000);
    }

    IntegerAspect number{this};
};

ChangeNumberDialog::ChangeNumberDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new ChangeNumberSettings)
{
    setWindowTitle(Tr::tr("Change Number"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(320, 75);
}

ChangeNumberDialog::~ChangeNumberDialog() = default;

int ChangeNumberDialog::number() const
{
    return m_settings->number();
}

#ifdef WITH_TESTS

class ChangeNumberDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItAsksForANumberInRange()
    {
        ChangeNumberSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "ChangeNumberDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // Nothing typed reads as nothing to describe, which is what the caller
        // tests for - it used to be the -1 a line edit answered for empty.
        ChangeNumberDialog dialog;
        QCOMPARE(dialog.number(), 0);

        // And the range the QIntValidator used to enforce is now the aspect's,
        // which is what the spin box on the form honours. Measured rather than
        // assumed: setRange() bounds what can be *typed*, and setValue() from
        // C++ is not clamped by it - so this asserts what the field offers, not
        // what the value does.
        const AspectPresentation presentation = settings.number.presentation();
        QCOMPARE(presentation.control, AspectControls::SpinBox);
        QCOMPARE(presentation.minimum.toInt(), 0);
        QCOMPARE(presentation.maximum.toInt(), 1000000);
    }
};

QObject *createChangeNumberDialogTest()
{
    return new ChangeNumberDialogTest;
}

#endif // WITH_TESTS

} // Perforce::Internal

#ifdef WITH_TESTS
#include "changenumberdialog.moc"
#endif
