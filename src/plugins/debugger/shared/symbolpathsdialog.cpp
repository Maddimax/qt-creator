// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "symbolpathsdialog.h"

#include "../debuggertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/pathchooser.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>

using namespace Utils;

namespace Debugger::Internal {

// What the dialog explains before it asks. Rich text, because the note in the
// middle is set apart from the rest of it.
QString symbolPathsExplanation()
{
    return Tr::tr("<p>The debugger is not configured to use the "
        "public Microsoft Symbol Server.<br/>This is recommended for retrieval of the symbols "
        "of the operating system libraries.</p>"
        "<p><span style=\" font-style:italic;\">Note:</span> It is recommended, that if you use "
        "the Microsoft Symbol Server, to also use a local symbol cache.<br/>"
        "A fast internet connection is required for this to work smoothly,<br/>"
        "and a delay might occur when connecting for the first time and caching the symbols.</p>"
        "<p>What would you like to set up?</p>");
}

class SymbolPathsSettings final : public AspectContainer
{
public:
    SymbolPathsSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/SymbolPathsDialog.qml"));

        explanation.setQmlName("Explanation");
        explanation.setText(symbolPathsExplanation());
        explanation.setTextFormat(AspectControls::TextFormat::RichText);
        // The widget dialog drew the platform's question icon beside this. An
        // aspect says what kind of thing it is and the form draws the icon.
        explanation.setIconType(InfoType::Information);

        useSymbolCache.setQmlName("UseSymbolCache");
        useSymbolCache.setLabelText(Tr::tr("Use Local Symbol Cache"));

        useSymbolServer.setQmlName("UseSymbolServer");
        useSymbolServer.setLabelText(Tr::tr("Use Microsoft Symbol Server"));

        path.setQmlName("Path");
        path.setExpectedKind(PathChooserKind::ExistingDirectory);
    }

    TextDisplay explanation{this};
    BoolAspect useSymbolCache{this};
    BoolAspect useSymbolServer{this};
    FilePathAspect path{this};
};

SymbolPathsDialog::SymbolPathsDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new SymbolPathsSettings)
{
    setWindowTitle(Tr::tr("Set up Symbol Paths", nullptr));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);
}

SymbolPathsDialog::~SymbolPathsDialog() = default;

bool SymbolPathsDialog::useSymbolCache() const
{
    return m_settings->useSymbolCache();
}

bool SymbolPathsDialog::useSymbolServer() const
{
    return m_settings->useSymbolServer();
}

FilePath SymbolPathsDialog::path() const
{
    return m_settings->path();
}

void SymbolPathsDialog::setUseSymbolCache(bool useSymbolCache)
{
    m_settings->useSymbolCache.setValue(useSymbolCache);
}

void SymbolPathsDialog::setUseSymbolServer(bool useSymbolServer)
{
    m_settings->useSymbolServer.setValue(useSymbolServer);
}

void SymbolPathsDialog::setPath(const FilePath &path)
{
    m_settings->path.setValue(path);
}

bool SymbolPathsDialog::useCommonSymbolPaths(bool &useSymbolCache,
                                             bool &useSymbolServer,
                                             FilePath &path)
{
    SymbolPathsDialog dialog;
    dialog.setUseSymbolCache(useSymbolCache);
    dialog.setUseSymbolServer(useSymbolServer);
    dialog.setPath(path);
    int ret = dialog.exec();
    useSymbolCache = dialog.useSymbolCache();
    useSymbolServer = dialog.useSymbolServer();
    path = dialog.path();
    return ret == QDialog::Accepted;
}

#ifdef WITH_TESTS

class SymbolPathsDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        SymbolPathsSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "SymbolPathsDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheExplanationIsReadAsMarkup()
    {
        // It is one paragraph in italics inside three, so read as plain text
        // it is a wall of angle brackets.
        SymbolPathsSettings settings;
        QCOMPARE(settings.explanation.presentation().textFormat,
                 AspectControls::TextFormat::RichText);
        QVERIFY2(symbolPathsExplanation().contains("<p>"), "the explanation is not markup");

        // And it says what kind of thing it is, which is what draws the icon
        // the widget dialog put beside it.
        QCOMPARE(settings.explanation.presentation().infoType, InfoType::Information);
    }

    void testTheCacheIsADirectoryToPickNotAPathToType()
    {
        SymbolPathsSettings settings;
        QCOMPARE(settings.path.presentation().pathKind,
                 AspectControls::PathKind::ExistingDirectory);
    }

    void testWhatTheDialogHandsBack()
    {
        // The three answers the caller reads, in and out. The caller passes
        // them by reference and reads them whether or not it was accepted, so
        // what the dialog was opened on has to survive being closed.
        SymbolPathsDialog dialog;
        dialog.setUseSymbolCache(true);
        dialog.setUseSymbolServer(false);
        dialog.setPath(FilePath::fromString("/tmp/symbols"));

        QCOMPARE(dialog.useSymbolCache(), true);
        QCOMPARE(dialog.useSymbolServer(), false);
        QCOMPARE(dialog.path(), FilePath::fromString("/tmp/symbols"));

        dialog.setUseSymbolCache(false);
        QCOMPARE(dialog.useSymbolCache(), false);
    }
};

QObject *createSymbolPathsDialogTest()
{
    return new SymbolPathsDialogTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

#ifdef WITH_TESTS
#include "symbolpathsdialog.moc"
#endif
