// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpppreprocessordialog.h"

#include "cppeditorconstants.h"
#include "cppeditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/session.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <utils/algorithm.h>

#include <QTest>
#endif

using namespace Utils;

namespace CppEditor::Internal {

// What the dialog edits: one string, shown as C++ so that it is read the way it
// will be compiled. A container of its own rather than the session store,
// because a form needs an aspect to bind to and the store is not one.
class PreProcessorSettings final : public AspectContainer
{
public:
    explicit PreProcessorSettings(const FilePath &filePath)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CppEditor/CppPreProcessorDialog.qml"));

        directives.setQmlName("Directives");
        directives.setDisplayStyle(StringAspect::TextEditDisplay);
        directives.setLabelText(
            Tr::tr("Additional C++ Preprocessor Directives for %1:").arg(filePath.fileName()));
    }

    StringAspect directives{this};
};

CppPreProcessorDialog::CppPreProcessorDialog(const FilePath &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_settings(new PreProcessorSettings(filePath))
{
    resize(400, 300);
    setWindowTitle(Tr::tr("Additional C++ Preprocessor Directives"));

    const Key key = Constants::EXTRA_PREPROCESSOR_DIRECTIVES
                    + keyFromString(m_filePath.toUrlishString());
    m_settings->directives.setValue(Core::SessionManager::value(key).toString());

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

CppPreProcessorDialog::~CppPreProcessorDialog() = default;

int CppPreProcessorDialog::exec()
{
    if (QDialog::exec() == Rejected)
        return Rejected;
    const Key key = Constants::EXTRA_PREPROCESSOR_DIRECTIVES
                    + keyFromString(m_filePath.toUrlishString());
    Core::SessionManager::setValue(key, extraPreprocessorDirectives());
    return Accepted;
}

QString CppPreProcessorDialog::extraPreprocessorDirectives() const
{
    return m_settings->directives();
}

#ifdef WITH_TESTS

// Every QML warning raised while the form is built. A wrong aspect name is not
// a load error - the form still instantiates - it is a warning saying the
// binding could not be resolved, and the control is simply missing.
static QStringList s_qmlComplaints;

static void collectComplaints(QtMsgType, const QMessageLogContext &, const QString &message)
{
    s_qmlComplaints.append(message);
}

class CppPreProcessorDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDirectivesAreEditedAsCode()
    {
        const FilePath file = FilePath::fromString("/tmp/whatever.cpp");
        PreProcessorSettings settings(file);
        settings.directives.setValue("#define ONE 1");

        s_qmlComplaints.clear();
        QtMessageHandler previous = qInstallMessageHandler(collectComplaints);
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        qInstallMessageHandler(previous);

        QVERIFY2(form, "the settings produced no form at all");

        QObject *quickWidget = nullptr;
        const QList<QObject *> children = form->findChildren<QObject *>();
        for (QObject * const child : children) {
            if (QLatin1String(child->metaObject()->className()) == QLatin1String("QQuickWidget"))
                quickWidget = child;
        }
        QVERIFY2(quickWidget, "the dialog produced a widget form, so the Quick one was declined");

        const int ready = 1; // QQuickWidget::Ready
        QCOMPARE(quickWidget->property("status").toInt(), ready);

        const QStringList aboutThisForm
            = Utils::filtered(s_qmlComplaints, [](const QString &complaint) {
                  return complaint.contains("CppPreProcessorDialog.qml");
              });
        QVERIFY2(aboutThisForm.isEmpty(), qPrintable(aboutThisForm.join("; ")));

        // The label names the file, which is what tells one of these dialogs
        // from another when several are open.
        QVERIFY2(settings.directives.labelText().contains("whatever.cpp"),
                 qPrintable(settings.directives.labelText()));

        // And the whole round trip through the dialog: what the session holds
        // is what the form is given, and what the dialog hands back. The text
        // lives in the aspect now rather than in a widget, so a dialog that
        // read the widget would come back empty.
        const Key key = Constants::EXTRA_PREPROCESSOR_DIRECTIVES
                        + keyFromString(file.toUrlishString());
        Core::SessionManager::setValue(key, QString("#define TWO 2"));
        CppPreProcessorDialog dialog(file, nullptr);
        QCOMPARE(dialog.extraPreprocessorDirectives(), QString("#define TWO 2"));
    }
};

QObject *createCppPreProcessorDialogTest()
{
    return new CppPreProcessorDialogTest;
}

#endif // WITH_TESTS

} // CppEditor::Internal

#ifdef WITH_TESTS
#include "cpppreprocessordialog.moc"
#endif
