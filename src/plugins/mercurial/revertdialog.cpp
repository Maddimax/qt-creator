// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "revertdialog.h"

#include "mercurialtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Mercurial::Internal {

// Revert to the default revision, or to one the user names. The field is
// enabled by the flag rather than by a checkable group box, which is the same
// arrangement with the enabling written down instead of implied.
class RevertSettings final : public AspectContainer
{
public:
    RevertSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Mercurial/RevertDialog.qml"));

        specifyRevision.setQmlName("SpecifyRevision");
        specifyRevision.setLabelText(Tr::tr("Specify a revision other than the default?"));

        revision.setQmlName("Revision");
        revision.setLabelText(Tr::tr("Revision:"));
        revision.setDisplayStyle(StringAspect::LineEditDisplay);
        revision.setEnabler(&specifyRevision);
    }

    // What was typed while the flag was off is not what was asked for. A
    // method rather than a line in the dialog, so the rule can be asked about
    // without one - the dialog has no way to be given a revision from a test.
    QString effectiveRevision() const
    {
        return specifyRevision() ? revision() : QString();
    }

    BoolAspect specifyRevision{this};
    StringAspect revision{this};
};

RevertDialog::RevertDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new RevertSettings)
{
    resize(400, 162);
    setWindowTitle(Tr::tr("Revert"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

RevertDialog::~RevertDialog() = default;

QString RevertDialog::revision() const
{
    return m_settings->effectiveRevision();
}

#ifdef WITH_TESTS

class RevertDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testARevisionIsOnlyUsedWhenAskedFor()
    {
        RevertSettings settings;
        const Utils::Result<> rendered = Core::aspectFormRenders(&settings, "RevertDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // The field follows the flag, so it cannot be typed into until the
        // question above it has been answered.
        QVERIFY2(!settings.revision.isEnabled(),
                 "the revision field is editable before it was asked for");
        settings.specifyRevision.setValue(true);
        QVERIFY(settings.revision.isEnabled());

        // And what was typed while the flag was off is not what was asked
        // for. The group box this replaced only disabled the field, so text
        // left behind there was still passed to hg - a caller that asked for
        // the default revision got a revision.
        settings.revision.setValue("abc");
        QCOMPARE(settings.effectiveRevision(), QString("abc"));
        settings.specifyRevision.setValue(false);
        QCOMPARE(settings.effectiveRevision(), QString());

        RevertDialog dialog;
        QCOMPARE(dialog.revision(), QString());
    }
};

QObject *createRevertDialogTest()
{
    return new RevertDialogTest;
}

#endif // WITH_TESTS

} // Mercurial::Internal

#ifdef WITH_TESTS
#include "revertdialog.moc"
#endif
