// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcpkgmanifesteditor.h"

#include "vcpkgconstants.h"
#include "vcpkgsearch.h"
#include "vcpkgsettings.h"
#include "vcpkgtr.h"

#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icore.h>

#include <utils/icon.h>
#include <utils/qtcassert.h>
#include <utils/layoutbuilder.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/projecttree.h>

#include <texteditor/fontsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Vcpkg::Internal {

static QString cmakeCodeForPackage(const QString &package)
{
    QString result;

    Project *currentProject = ProjectTree::currentProject();
    const FilePath usageFile =
        vcpkgSettingsForProject(currentProject)->vcpkgRoot.expandedValue() / "ports" / package / "usage";
    if (usageFile.exists()) {
        if (const Result<QByteArray> res = usageFile.fileContents())
            result = QString::fromUtf8(*res);
    } else {
        result = QString(
R"(The package %1 provides CMake targets:

    # this is heuristically generated, and may not be correct
    find_package(%1 CONFIG REQUIRED)
    target_link_libraries(main PRIVATE %1::%1))" ).arg(package);
    }

    return result;
}

QString cmakeCodeForPackages(const QStringList &packages)
{
    QString result;
    for (const QString &package : packages)
        result.append(cmakeCodeForPackage(package) + "\n\n");
    return result;
}

class CMakeCodeSettings final : public AspectContainer
{
public:
    explicit CMakeCodeSettings(const QStringList &packages)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Vcpkg/CMakeCodeDialog.qml"));

        hint.setQmlName("Hint");
        hint.setText(Tr::tr("Copy paste the required lines into your CMakeLists.txt:"));

        code.setQmlName("Code");
        code.setDisplayStyle(StringAspect::TextEditDisplay);
        // Read, not written: the reader copies it out.
        code.setReadOnly(true);
        // CMake lines whose arguments line up.
        code.setMonospace(true);
        code.setValue(cmakeCodeForPackages(packages));
    }

    TextDisplay hint{this};
    StringAspect code{this};
};

class CMakeCodeDialog final : public QDialog
{
public:
    explicit CMakeCodeDialog(const QStringList &packages)
        : m_settings(new CMakeCodeSettings(packages))
    {
        resize(600, 600);

        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);

        auto layout = new QVBoxLayout(this);
        layout->addWidget(Core::createAspectForm(m_settings.get()));
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    const std::unique_ptr<CMakeCodeSettings> m_settings;
};

// The two buttons a manifest gets beside every text editor's, and the settings
// button beside them. Parented to the editor, so it goes when the editor does.
class VcpkgManifestDecorations final : public QObject
{
public:
    explicit VcpkgManifestDecorations(Core::IEditor *editor)
        : QObject(editor)
        , m_document(qobject_cast<TextDocument *>(editor->document()))
    {
        QTC_ASSERT(m_document, return);

        const QIcon vcpkgIcon = Utils::Icon({{":/vcpkg/images/vcpkgicon.png",
                                              Utils::Theme::IconsBaseColor}}).icon();
        m_searchPkgAction = new QAction(vcpkgIcon, Tr::tr("Add vcpkg Package..."), this);
        connect(m_searchPkgAction, &QAction::triggered, this, [this] {
            const Search::VcpkgManifest package =
                Search::showVcpkgPackageSearchDialog(documentToManifest());
            if (!package.name.isEmpty()) {
                const QByteArray modifiedDocument =
                    addDependencyToManifest(m_document->contents(), package.name);
                m_document->setContents(modifiedDocument);
            }
        });
        TextEditor::insertExtraToolBarActionIn(editor, TextEditorWidget::Left, m_searchPkgAction);

        const QIcon cmakeIcon = ProjectExplorer::Icons::CMAKE_LOGO_TOOLBAR.icon();
        m_cmakeCodeAction = new QAction(cmakeIcon, Tr::tr("CMake Code..."), this);
        connect(m_cmakeCodeAction, &QAction::triggered, this, [this] {
            CMakeCodeDialog dlg(documentToManifest().dependencies);
            dlg.exec();
        });
        TextEditor::insertExtraToolBarActionIn(editor, TextEditorWidget::Left, m_cmakeCodeAction);

        auto *optionsAction = new QAction(Utils::Icons::SETTINGS_TOOLBAR.icon(),
                                          Core::ICore::msgShowSettings(), this);
        connect(optionsAction, &QAction::triggered, [] {
            Core::ICore::showSettings(Constants::Settings::GENERAL_ID);
        });
        TextEditor::insertExtraToolBarActionIn(editor, TextEditorWidget::Left, optionsAction);

        updateActions();
        connect(&vcpkgSettingsForProject(ProjectTree::currentProject())->vcpkgRoot,
                &Utils::BaseAspect::changed,
                this, &VcpkgManifestDecorations::updateActions);
    }

    void updateActions()
    {
        const Utils::FilePath vcpkgRoot =
            vcpkgSettingsForProject(ProjectTree::currentProject())->vcpkgRoot.expandedValue();
        const Utils::FilePath vcpkg = vcpkgRoot.pathAppended("vcpkg").withExecutableSuffix();
        const bool vcpkgEnabled = vcpkg.isExecutableFile();
        m_searchPkgAction->setEnabled(vcpkgEnabled);
        m_cmakeCodeAction->setEnabled(vcpkgEnabled);
    }

private:
    Search::VcpkgManifest documentToManifest() const
    {
        return Search::parseVcpkgManifest(m_document->contents());
    }

    TextDocument * const m_document;
    QAction *m_searchPkgAction = nullptr;
    QAction *m_cmakeCodeAction = nullptr;
};

static TextDocument *createVcpkgManifestDocument()
{
    auto doc = new TextDocument;
    doc->setId(Constants::VCPKGMANIFEST_EDITOR_ID);
    return doc;
}

QByteArray addDependencyToManifest(const QByteArray &manifest, const QString &package)
{
    constexpr char dependenciesKey[] = "dependencies";
    QJsonObject jsonObject = QJsonDocument::fromJson(manifest).object();
    QJsonArray dependencies = jsonObject.value(dependenciesKey).toArray();
    dependencies.append(package);
    jsonObject.insert(dependenciesKey, dependencies);
    return QJsonDocument(jsonObject).toJson();
}

class VcpkgManifestEditorFactory final : public TextEditorFactory
{
public:
    VcpkgManifestEditorFactory()
    {
        setId(Constants::VCPKGMANIFEST_EDITOR_ID);
        setDisplayName(Tr::tr("Vcpkg Manifest Editor"));
        addMimeType(Constants::VCPKGMANIFEST_MIMETYPE);
        setDocumentCreator(createVcpkgManifestDocument);
        // vcpkg.json opens in the Qt Quick view, like every other JSON file.
        // Its widget subclass only ever added tool bar buttons, and those are
        // an editor decorator now.
        setUsesQuickEditor(true);
        setEditorDecorator([](Core::IEditor *editor) {
            new VcpkgManifestDecorations(editor);
        });
        setUseGenericHighlighter(true);
    }
};

void setupVcpkgManifestEditor()
{
    static VcpkgManifestEditorFactory theVcpkgManifestEditorFactory;
}

} // namespace Vcpkg::Internal
