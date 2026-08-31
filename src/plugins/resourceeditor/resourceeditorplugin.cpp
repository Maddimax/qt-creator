// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "resourceeditor.h"
#include "resourceeditorconstants.h"
#include "resourceeditortr.h"
#include "resourcenode.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectnodes.h>

#include <utils/action.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QDebug>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

using namespace ProjectExplorer;
using namespace Utils;
using namespace Core;

namespace ResourceEditor::Internal {

const char resourcePrefix[] = ":";
const char urlPrefix[] = "qrc:";

class PrefixLangSettings final : public AspectContainer
{
public:
    PrefixLangSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ResourceEditor/PrefixLangDialog.qml"));

        prefix.setQmlName("Prefix");
        prefix.setLabelText(Tr::tr("Prefix:"));
        prefix.setDisplayStyle(StringAspect::LineEditDisplay);

        lang.setQmlName("Language");
        lang.setLabelText(Tr::tr("Language:"));
        lang.setDisplayStyle(StringAspect::LineEditDisplay);
    }

    StringAspect prefix{this};
    StringAspect lang{this};
};

class PrefixLangDialog final : public QDialog
{
public:
    PrefixLangDialog(const QString &title, const QString &prefix, const QString &lang)
        : QDialog(ICore::dialogParent())
    {
        setWindowTitle(title);

        m_settings.prefix.setValue(prefix);
        m_settings.lang.setValue(lang);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                            Qt::Horizontal, this);

        auto layout = new QVBoxLayout(this);
        layout->addWidget(Core::createAspectForm(&m_settings));
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QString prefix() const { return m_settings.prefix(); }
    QString lang() const { return m_settings.lang(); }

private:
    PrefixLangSettings m_settings;
};

#ifdef WITH_TESTS

class PrefixLangDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        PrefixLangSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "PrefixLangDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheDialogOpensOnWhatItWasGiven()
    {
        // Two callers: adding a prefix opens on nothing, renaming one opens on
        // what the node already has. Both read the two fields back.
        const PrefixLangDialog add(Tr::tr("Add Prefix"), {}, {});
        QVERIFY(add.prefix().isEmpty());
        QVERIFY(add.lang().isEmpty());

        const PrefixLangDialog rename(Tr::tr("Rename Prefix"), "/images", "de");
        QCOMPARE(rename.prefix(), QString("/images"));
        QCOMPARE(rename.lang(), QString("de"));
    }

    void testAPrefixIsNotALanguage()
    {
        // Two fields that are both plain strings and are read back in order;
        // swapping them is the mistake a port of this makes.
        const PrefixLangDialog dialog(Tr::tr("Rename Prefix"), "/images", "de");
        QVERIFY2(dialog.prefix().startsWith('/'), "the language came back as the prefix");
        QVERIFY2(!dialog.lang().startsWith('/'), "the prefix came back as the language");
    }
};

QObject *createPrefixLangDialogTest()
{
    return new PrefixLangDialogTest;
}

#endif // WITH_TESTS

// ResourceEditorPlugin

class ResourceEditorPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ResourceEditor.json")

    void initialize() final;

    void extensionsInitialized() final;

    void onUndo();
    void onRedo();
    void onRefresh();

    void addPrefixContextMenu();
    void renamePrefixContextMenu();
    void removePrefixContextMenu();
    void renameFileContextMenu();
    void removeFileContextMenu();
    void removeNonExisting();

    void openEditorContextMenu();

    void copyPathContextMenu();
    void copyUrlContextMenu();

    void updateContextActions(Node *node);

    // project tree's folder context menu
    QAction *m_addPrefix = nullptr;
    QAction *m_removePrefix = nullptr;
    QAction *m_renamePrefix = nullptr;
    QAction *m_removeNonExisting = nullptr;

    QAction *m_renameResourceFile = nullptr;
    QAction *m_removeResourceFile = nullptr;

    QAction *m_openInEditor = nullptr;
    QMenu *m_openWithMenu = nullptr;

    // file context menu
    Action *m_copyPath = nullptr;
    Action *m_copyUrl = nullptr;
};

void ResourceEditorPlugin::initialize()
{
#ifdef WITH_TESTS
    addTestCreator(createPrefixLangDialogTest);
#endif

    setupResourceEditor(this);

    const Context projectTreeContext(ProjectExplorer::Constants::C_PROJECT_TREE);
    const Id folderContextMenu = ProjectExplorer::Constants::M_FOLDERCONTEXT;
    const Id folderFilesGroup = ProjectExplorer::Constants::G_FOLDER_FILES;
    const Id fileContextMenu = ProjectExplorer::Constants::M_FILECONTEXT;

    ActionBuilder(this, Constants::C_ADD_PREFIX)
        .setText(Tr::tr("Add Prefix..."))
        .bindContextAction(&m_addPrefix)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::addPrefixContextMenu);

    ActionBuilder(this, Constants::C_RENAME_PREFIX)
        .setText(Tr::tr("Change Prefix..."))
        .bindContextAction(&m_renamePrefix)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::renamePrefixContextMenu);

    ActionBuilder(this, Constants::C_REMOVE_PREFIX)
        .setText(Tr::tr("Remove Prefix..."))
        .bindContextAction(&m_removePrefix)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::removePrefixContextMenu);

    ActionBuilder(this, Constants::C_REMOVE_NON_EXISTING)
        .setText(Tr::tr("Remove Missing Files"))
        .bindContextAction(&m_removeNonExisting)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::removeNonExisting);

    ActionBuilder(this, Constants::C_RENAME_FILE)
        .setText(Tr::tr("Rename..."))
        .bindContextAction(&m_renameResourceFile)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::renameFileContextMenu);

    ActionBuilder(this, Constants::C_REMOVE_FILE)
        .setText(Tr::tr("Remove File..."))
        .bindContextAction(&m_removeResourceFile)
        .setContext(projectTreeContext)
        .setEnabled(false)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::removeFileContextMenu);

    ActionBuilder(this, Constants::C_OPEN_EDITOR)
        .setText(Tr::tr("Open in Editor"))
        .bindContextAction(&m_openInEditor)
        .setContext(projectTreeContext)
        .addToContainer(folderContextMenu, folderFilesGroup)
        .addOnTriggered(this, &ResourceEditorPlugin::openEditorContextMenu);

    ActionContainer *menu = ActionManager::actionContainer(folderContextMenu);
    m_openWithMenu = new QMenu(Tr::tr("Open With"), menu->menu());
    menu->menu()->insertMenu(
        menu->insertLocation(ProjectExplorer::Constants::G_FOLDER_FILES),
        m_openWithMenu);

    ActionBuilder(this, Constants::C_COPY_PATH)
        .setParameterText(Tr::tr("Copy Path \"%1\""),
                          Tr::tr("Copy Path"),
                          ActionBuilder::AlwaysEnabled)
        .bindContextAction(&m_copyPath)
        .setContext(projectTreeContext)
        .setCommandAttribute(Command::CA_UpdateText)
        .addToContainer(fileContextMenu, ProjectExplorer::Constants::G_FILE_OTHER)
        .addOnTriggered(this, &ResourceEditorPlugin::copyPathContextMenu);

    ActionBuilder(this, Constants::C_COPY_URL)
        .setParameterText(Tr::tr("Copy URL \"%1\""),
                          Tr::tr("Copy URL"),
                          ActionBuilder::AlwaysEnabled)
        .bindContextAction(&m_copyUrl)
        .setContext(projectTreeContext)
        .setCommandAttribute(Command::CA_UpdateText)
        .addToContainer(fileContextMenu, ProjectExplorer::Constants::G_FILE_OTHER)
        .addOnTriggered(this, &ResourceEditorPlugin::copyUrlContextMenu);

    connect(ProjectTree::instance(), &ProjectTree::currentNodeChanged,
            this, &ResourceEditorPlugin::updateContextActions);
}

void ResourceEditorPlugin::addPrefixContextMenu()
{
    auto topLevel = dynamic_cast<ResourceTopLevelNode *>(ProjectTree::currentNode());
    QTC_ASSERT(topLevel, return);
    PrefixLangDialog dialog(Tr::tr("Add Prefix"), QString(), QString());
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString prefix = dialog.prefix();
    if (prefix.isEmpty())
        return;
    topLevel->addPrefix(prefix, dialog.lang());
}

void ResourceEditorPlugin::removePrefixContextMenu()
{
    auto rfn = dynamic_cast<ResourceFolderNode *>(ProjectTree::currentNode());
    QTC_ASSERT(rfn, return);
    if (QMessageBox::question(ICore::dialogParent(),
                              Tr::tr("Remove Prefix"),
                              Tr::tr("Remove prefix %1 and all its files?").arg(rfn->displayName()))
            == QMessageBox::Yes) {
        ResourceTopLevelNode *rn = rfn->resourceNode();
        rn->removePrefix(rfn->prefix(), rfn->lang());
    }
}

void ResourceEditorPlugin::removeNonExisting()
{
    auto topLevel = dynamic_cast<ResourceTopLevelNode *>(ProjectTree::currentNode());
    QTC_ASSERT(topLevel, return);
    topLevel->removeNonExistingFiles();
}

void ResourceEditorPlugin::renameFileContextMenu()
{
    ProjectExplorerPlugin::initiateInlineRenaming();
}

void ResourceEditorPlugin::removeFileContextMenu()
{
    auto rfn = dynamic_cast<ResourceTopLevelNode *>(ProjectTree::currentNode());
    QTC_ASSERT(rfn, return);
    FilePath path = rfn->filePath();
    FolderNode *parent = rfn->parentFolderNode();
    QTC_ASSERT(parent, return);
    if (parent->removeFiles({path}) != RemovedFilesFromProject::Ok)
        QMessageBox::warning(ICore::dialogParent(),
                             Tr::tr("File Removal Failed"),
                             Tr::tr("Removing file %1 from the project failed.").arg(path.toUserOutput()));
}

void ResourceEditorPlugin::openEditorContextMenu()
{
    EditorManager::openEditor(ProjectTree::currentNode()->filePath());
}

void ResourceEditorPlugin::copyPathContextMenu()
{
    auto node = dynamic_cast<ResourceFileNode *>(ProjectTree::currentNode());
    QTC_ASSERT(node, return);
    setClipboardAndSelection(QLatin1String(resourcePrefix) + node->qrcPath());
}

void ResourceEditorPlugin::copyUrlContextMenu()
{
    auto node = dynamic_cast<ResourceFileNode *>(ProjectTree::currentNode());
    QTC_ASSERT(node, return);
    setClipboardAndSelection(QLatin1String(urlPrefix) + node->qrcPath());
}

void ResourceEditorPlugin::renamePrefixContextMenu()
{
    auto node = dynamic_cast<ResourceFolderNode *>(ProjectTree::currentNode());
    QTC_ASSERT(node, return);

    PrefixLangDialog dialog(Tr::tr("Rename Prefix"), node->prefix(), node->lang());
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString prefix = dialog.prefix();
    if (prefix.isEmpty())
        return;

    node->renamePrefix(prefix, dialog.lang());
}

void ResourceEditorPlugin::updateContextActions(Node *node)
{
    const bool isResourceNode = dynamic_cast<const ResourceTopLevelNode *>(node);
    m_addPrefix->setEnabled(isResourceNode);
    m_addPrefix->setVisible(isResourceNode);

    bool enableRename = false;
    bool enableRemove = false;

    if (isResourceNode) {
        FolderNode *parent = node ? node->parentFolderNode() : nullptr;
        enableRename = parent && parent->supportsAction(Rename, node);
        enableRemove = parent && parent->supportsAction(RemoveFile, node);
    }

    m_renameResourceFile->setEnabled(isResourceNode && enableRename);
    m_renameResourceFile->setVisible(isResourceNode && enableRename);
    m_removeResourceFile->setEnabled(isResourceNode && enableRemove);
    m_removeResourceFile->setVisible(isResourceNode && enableRemove);

    m_openInEditor->setEnabled(isResourceNode);
    m_openInEditor->setVisible(isResourceNode);

    const bool isResourceFolder = dynamic_cast<const ResourceFolderNode *>(node);
    m_removePrefix->setEnabled(isResourceFolder);
    m_removePrefix->setVisible(isResourceFolder);

    m_renamePrefix->setEnabled(isResourceFolder);
    m_renamePrefix->setVisible(isResourceFolder);

    m_removeNonExisting->setEnabled(isResourceNode);
    m_removeNonExisting->setVisible(isResourceNode);

    if (isResourceNode)
        EditorManager::populateOpenWithMenu(m_openWithMenu, node->filePath());
    else
        m_openWithMenu->clear();
    m_openWithMenu->menuAction()->setVisible(!m_openWithMenu->actions().isEmpty());

    const bool isResourceFile = dynamic_cast<const ResourceFileNode *>(node);
    m_copyPath->setEnabled(isResourceFile);
    m_copyPath->setVisible(isResourceFile);
    m_copyUrl->setEnabled(isResourceFile);
    m_copyUrl->setVisible(isResourceFile);
    if (isResourceFile) {
        auto fileNode = dynamic_cast<const ResourceFileNode *>(node);
        QTC_ASSERT(fileNode, return);
        QString qrcPath = fileNode->qrcPath();
        m_copyPath->setParameter(QLatin1String(resourcePrefix) + qrcPath);
        m_copyUrl->setParameter(QLatin1String(urlPrefix) + qrcPath);
    }
}

void ResourceEditorPlugin::extensionsInitialized()
{
    ProjectTree::registerTreeManager([](FolderNode *folder, ProjectTree::ConstructionPhase phase) {
        switch (phase) {
        case ProjectTree::AsyncPhase: {
            QList<FileNode *> toReplace;
            folder->forEachNode([&toReplace](FileNode *fn) {
                if (fn->fileType() == FileType::Resource)
                    toReplace.append(fn);
            }, {}, [](const FolderNode *fn) {
                return dynamic_cast<const ResourceTopLevelNode *>(fn) == nullptr;
            });
            for (FileNode *file : std::as_const(toReplace)) {
                FolderNode *const pn = file->parentFolderNode();
                QTC_ASSERT(pn, continue);
                const FilePath path = file->filePath();
                auto topLevel = std::make_unique<ResourceTopLevelNode>(path, pn->filePath());
                topLevel->setEnabled(file->isEnabled());
                topLevel->setIsGenerated(file->isGenerated());
                pn->replaceSubtree(file, std::move(topLevel));
            }
            break;
        }
        case ProjectTree::FinalPhase: {
            folder->forEachNode({}, [](FolderNode *fn) {
                if (auto topLevel = dynamic_cast<ResourceTopLevelNode *>(fn))
                    topLevel->setupWatcherIfNeeded();
            });
            break;
        }
        }
    });
}

} // ResourceEditor::Internal

#include "resourceeditorplugin.moc"
