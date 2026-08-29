// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpppreprocessordialog.h"

#include "cppeditorconstants.h"
#include "cppeditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/session.h>

#include <texteditor/snippets/snippetprovider.h>

#include <utils/aspects.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS


#include <QFile>
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

class CppPreProcessorDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDirectivesAreEditedAsCode()
    {
        const FilePath file = FilePath::fromString("/tmp/whatever.cpp");
        PreProcessorSettings settings(file);
        settings.directives.setValue("#define ONE 1");

        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "CppPreProcessorDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

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

    // The widget dialog ran decorateCppEditor() over its editor, which is what
    // made the directives indent and complete like C++. That call went when
    // the dialog stopped being built out of widgets; the form asks for the
    // same thing by naming the group instead.
    //
    // Read out of the form's source, because a plugin that does not link Qt
    // Quick cannot ask the loaded form what it bound. That the group is
    // honoured once bound is TextViewport's test.
    void testTheDirectivesAreEditedAsCppRatherThanAsText()
    {
        QFile form(":/qt/qml/QtCreator/CppEditor/CppPreProcessorDialog.qml");
        QVERIFY2(form.open(QIODevice::ReadOnly), "the form is not in the resources");
        const QString source = QString::fromUtf8(form.readAll());

        const QString binding = QString("snippetGroup: \"%1\"")
                                    .arg(QLatin1String(Constants::CPP_SNIPPETS_GROUP_ID));
        QVERIFY2(source.contains(binding),
                 qPrintable("the form does not say the directives are C++: " + binding));
        // And that group is one that exists - the string is only a key.
        QVERIFY2(!TextEditor::SnippetProvider::mimeTypeForGroup(Constants::CPP_SNIPPETS_GROUP_ID).isEmpty(),
                 "no C++ snippet group is registered under that id");
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
