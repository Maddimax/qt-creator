// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "designdocument.h"
#include "designdocumentview.h"
#include "documentmanager.h"
#include "qmlitemnode.h"
#include "qmlvisualnode.h"

#ifndef QDS_USE_PROJECTSTORAGE
#  include <metainfo.h>
#endif
#include <model/modelresourcemanagement.h>
#include <nodeinstanceview.h>
#include <nodelistproperty.h>
#include <qmldesignerplugin.h>
#include <qmlobjectnode.h>
#include <svgpasteaction.h>
#include <timelineactions.h>
#include <utils3d.h>
#include <variantproperty.h>
#include <viewmanager.h>
#include <qmldesignerplugin.h>
#include <qmlobjectnode.h>
#include <qmlprojectmanager/qmlprojectconstants.h>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/target.h>

#include <qmljs/qmljsmodelmanagerinterface.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QFileInfo>
#include <QTextDocument>
#include <QUrl>
#include <QDebug>

#include <QApplication>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QClipboard>

enum {
    debug = false
};

namespace QmlDesigner {

using ProjectManagingTracing::category;

/**
  \class QmlDesigner::DesignDocument

  DesignDocument acts as a facade to a model representing a qml document,
  and the different views/widgets accessing it.
  */
DesignDocument::DesignDocument([[maybe_unused]] const QUrl &filePath,
                               ProjectStorageDependencies projectStorageDependencies,
                               ExternalDependenciesInterface &externalDependencies,
                               ModulesStorage &modulesStorage)
    : m_documentLoaded(false)
    , m_currentTarget(nullptr)
    , m_projectStorageDependencies(projectStorageDependencies)
    , m_externalDependencies{externalDependencies}
    , m_modulesStorage{modulesStorage}
{
    NanotraceHR::Tracer tracer{"design document constructor", category()};

#ifdef QDS_USE_PROJECTSTORAGE
    m_documentModel = Model::create(projectStorageDependencies,
                                    "Item",
                                    {Import::createLibraryImport("QtQuick")},
                                    filePath,
                                    std::make_unique<ModelResourceManagement>());
#else
    m_documentModel = Model::create("QtQuick.Item",
                                    1,
                                    0,
                                    nullptr,
                                    std::make_unique<ModelResourceManagement>());
    m_subComponentManager.reset(new SubComponentManager(m_documentModel.get(), externalDependencies));
#endif
    m_rewriterView = std::make_unique<RewriterView>(externalDependencies,
                                                    modulesStorage,
                                                    RewriterView::Amend);

#ifndef QDS_USE_PROJECTSTORAGE
    m_rewriterView->setIsDocumentRewriterView(true);
#endif
}

DesignDocument::~DesignDocument()
{
    NanotraceHR::Tracer tracer{"design document destructor", category()};
}

Model *DesignDocument::currentModel() const
{
    NanotraceHR::Tracer tracer{"design document current model", category()};

    if (m_inFileComponentModel) {
        return m_inFileComponentModel.get();
    }

    return m_documentModel.get();
}

Model *DesignDocument::documentModel() const
{
    NanotraceHR::Tracer tracer{"design document document model", category()};

    return m_documentModel.get();
}

QWidget *DesignDocument::centralWidget() const
{
    NanotraceHR::Tracer tracer{"design document central widget", category()};

    return qobject_cast<QWidget *>(parent());
}

const ViewManager &DesignDocument::viewManager() const
{
    NanotraceHR::Tracer tracer{"design document view manager", category()};

    return QmlDesignerPlugin::instance()->viewManager();
}

ViewManager &DesignDocument::viewManager()
{
    NanotraceHR::Tracer tracer{"design document view manager", category()};

    return QmlDesignerPlugin::instance()->viewManager();
}

static ComponentTextModifier *createComponentTextModifier(TextModifier *originalModifier,
                                                          RewriterView *rewriterView,
                                                          const QString &componentText,
                                                          const ModelNode &componentNode)
{
    bool explicitComponent = componentText.contains(QLatin1String("Component"));

    ModelNode rootModelNode = rewriterView->rootModelNode();

    int componentStartOffset;
    int componentEndOffset;

    int rootStartOffset = rewriterView->nodeOffset(rootModelNode);

    if (explicitComponent) { //the component is explciit we have to find the first definition inside
        componentStartOffset = rewriterView->firstDefinitionInsideOffset(componentNode);
        componentEndOffset = componentStartOffset + rewriterView->firstDefinitionInsideLength(componentNode);
    } else { //the component is implicit
        componentStartOffset = rewriterView->nodeOffset(componentNode);
        componentEndOffset = componentStartOffset + rewriterView->nodeLength(componentNode);
    }

    return new ComponentTextModifier(originalModifier, componentStartOffset, componentEndOffset, rootStartOffset);
}

bool DesignDocument::loadInFileComponent(const ModelNode &componentNode)
{
    NanotraceHR::Tracer tracer{"design document load in file component", category()};

    QString componentText = rewriterView()->extractText({componentNode}).value(componentNode);

    if (componentText.isEmpty())
        return false;

    if (!componentNode.isRootNode()) {
        //change to subcomponent model
        changeToInFileComponentModel(createComponentTextModifier(m_documentTextModifier.get(),
                                                                 rewriterView(),
                                                                 componentText,
                                                                 componentNode));
    }

    return true;
}

const AbstractView *DesignDocument::view() const
{
    NanotraceHR::Tracer tracer{"design document view", category()};

    return viewManager().view();
}

ModelPointer DesignDocument::createInFileComponentModel()
{
    NanotraceHR::Tracer tracer{"design document create in file component model", category()};

#ifdef QDS_USE_PROJECTSTORAGE
    auto model = m_documentModel->createModel("Item", std::make_unique<ModelResourceManagement>());
#else
    auto model = Model::create("QtQuick.Item",
                               1,
                               0,
                               nullptr,
                               std::make_unique<ModelResourceManagement>());
    model->setFileUrl(m_documentModel->fileUrl());
    model->setMetaInfo(m_documentModel->metaInfo());
#endif

    return model;
}

bool DesignDocument::pasteSVG()
{
    NanotraceHR::Tracer tracer{"design document paste SVG", category()};

    SVGPasteAction svgPasteAction;

    if (!svgPasteAction.containsSVG(QApplication::clipboard()->text()))
        return false;

    rewriterView()->executeInTransaction("DesignDocument::paste1", [&]() {
        ModelNode targetNode;

        // If nodes are currently selected make the first node in selection the target
        if (!view()->selectedModelNodes().isEmpty())
            targetNode = view()->firstSelectedModelNode();

        // If target is still invalid make the root node the target
        if (!targetNode.isValid())
            targetNode = view()->rootModelNode();

        // Check if document has studio components import, if not create it
        QmlDesigner::Import import = QmlDesigner::Import::createLibraryImport("QtQuick.Studio.Components", "1.0");
        if (!currentModel()->hasImport(import, true, true)) {
            QmlDesigner::Import studioComponentsImport = QmlDesigner::Import::createLibraryImport("QtQuick.Studio.Components", "1.0");
            try {
                currentModel()->changeImports({studioComponentsImport}, {});
            } catch (const QmlDesigner::Exception &) {
                QTC_ASSERT(false, return);
            }
        }

        svgPasteAction.createQmlObjectNode(targetNode);
    });

    return true;
}

void DesignDocument::moveNodesToPosition(const QList<ModelNode> &nodes, const std::optional<QVector3D> &position)
{
    NanotraceHR::Tracer tracer{"design document move nodes to position", category()};

    if (!nodes.size())
        return;

    QList<ModelNode> movingNodes = nodes;
    DesignDocumentView view{m_externalDependencies, m_modulesStorage};
    currentModel()->attachView(&view);

    ModelNode targetNode; // the node that new nodes should be placed in
    if (!view.selectedModelNodes().isEmpty())
        targetNode = view.firstSelectedModelNode();

    // in case we copy and paste a selection we paste in the parent item
    if ((view.selectedModelNodes().size() == movingNodes.size()) && targetNode.hasParentProperty()) {
        targetNode = targetNode.parentProperty().parentModelNode();
    } else if (view.selectedModelNodes().isEmpty()) {
        // if selection is empty and copied nodes are all 3D nodes, paste them under the active scene
        bool all3DNodes = Utils::allOf(movingNodes, [](const ModelNode &node) {
            return node.metaInfo().isQtQuick3DNode();
        });

        if (all3DNodes) {
            int activeSceneId = Utils3D::active3DSceneId(m_documentModel.get());

            if (activeSceneId != -1) {
                NodeListProperty sceneNodeProperty = QmlVisualNode::findSceneNodeProperty(
                    rootModelNode().view(), activeSceneId);
                targetNode = sceneNodeProperty.parentModelNode();
            }
        }
    }

    if (!targetNode.isValid())
        targetNode = view.rootModelNode();

    for (const ModelNode &node : std::as_const(movingNodes)) {
        for (const ModelNode &node2 : std::as_const(movingNodes)) {
            if (node.isAncestorOf(node2))
                movingNodes.removeAll(node2);
        }
    }

    bool isSingleNode = movingNodes.size() == 1;
    if (isSingleNode) {
        ModelNode singleNode = movingNodes.first();
        if (targetNode.hasParentProperty()
            && singleNode.simplifiedTypeName() == targetNode.simplifiedTypeName()
            && singleNode.variantProperty("width").value() == targetNode.variantProperty("width").value()
            && singleNode.variantProperty("height").value() == targetNode.variantProperty("height").value()) {
            targetNode = targetNode.parentProperty().parentModelNode();
        }
    }

    const PropertyName defaultPropertyName = targetNode.metaInfo().defaultPropertyName();
    if (!targetNode.metaInfo().property(defaultPropertyName).isListProperty())
        qWarning() << Q_FUNC_INFO << "Cannot reparent to" << targetNode;
    NodeListProperty parentProperty = targetNode.nodeListProperty(defaultPropertyName);
    QList<ModelNode> pastedNodeList;

    std::optional<QmlVisualNode> firstVisualNode;
    QVector3D translationVect;
    for (const ModelNode &node : std::as_const(movingNodes)) {
        ModelNode pastedNode(view.insertModel(node));
        pastedNodeList.append(pastedNode);
        parentProperty.reparentHere(pastedNode);

        QmlVisualNode visualNode(pastedNode);
        if (!firstVisualNode && visualNode) {
            firstVisualNode = visualNode;
            translationVect = (position && firstVisualNode) ? *position - firstVisualNode->position()
                                                            : QVector3D();
        }
        visualNode.translate(translationVect);
    }

    view.setSelectedModelNodes(pastedNodeList);
    view.model()->clearMetaInfoCache();
}

bool DesignDocument::inFileComponentModelActive() const
{
    NanotraceHR::Tracer tracer{"design document in file component model active", category()};

    return bool(m_inFileComponentModel);
}

QList<DocumentMessage> DesignDocument::qmlParseWarnings() const
{
    NanotraceHR::Tracer tracer{"design document qml parse warnings", category()};

    return m_rewriterView->warnings();
}

bool DesignDocument::hasQmlParseWarnings() const
{
    NanotraceHR::Tracer tracer{"design document has qml parse warnings", category()};

    return !m_rewriterView->warnings().isEmpty();
}

QList<DocumentMessage> DesignDocument::qmlParseErrors() const
{
    NanotraceHR::Tracer tracer{"design document qml parse errors", category()};

    return m_rewriterView->errors();
}

bool DesignDocument::hasQmlParseErrors() const
{
    NanotraceHR::Tracer tracer{"design document has qml parse errors", category()};

    return !m_rewriterView->errors().isEmpty();
}

QString DesignDocument::displayName() const
{
    NanotraceHR::Tracer tracer{"design document display name", category()};

    return fileName().path();
}

QString DesignDocument::simplfiedDisplayName() const
{
    NanotraceHR::Tracer tracer{"design document simplified display name", category()};

    if (rootModelNode().id().isEmpty())
        return rootModelNode().id();
    else
        return rootModelNode().simplifiedTypeName();
}

void DesignDocument::updateFileName(const Utils::FilePath & /*oldFileName*/, const Utils::FilePath &newFileName)
{
    NanotraceHR::Tracer tracer{"design document update file name", category()};

    if (m_documentModel)
        m_documentModel->setFileUrl(QUrl::fromLocalFile(newFileName.toUrlishString()));

    if (m_inFileComponentModel)
        m_inFileComponentModel->setFileUrl(QUrl::fromLocalFile(newFileName.toUrlishString()));

    emit displayNameChanged(displayName());
}

Utils::FilePath DesignDocument::fileName() const
{
    NanotraceHR::Tracer tracer{"design document file name", category()};

    return editor() ? editor()->document()->filePath() : Utils::FilePath();
}

ProjectExplorer::Target *DesignDocument::currentTarget() const
{
    NanotraceHR::Tracer tracer{"design document current target", category()};

    return m_currentTarget;
}

bool DesignDocument::isDocumentLoaded() const
{
    NanotraceHR::Tracer tracer{"design document is document loaded", category()};

    return m_documentLoaded;
}

void DesignDocument::resetToDocumentModel()
{
    NanotraceHR::Tracer tracer{"design document reset to document model", category()};

    clearUndoRedoStacks();

    m_inFileComponentModel.reset();
}

void DesignDocument::loadDocument(Core::IEditor *editor)
{
    NanotraceHR::Tracer tracer{"design document load document", category()};

    TextEditor::TextDocument * const document
        = editor ? qobject_cast<TextEditor::TextDocument *>(editor->document()) : nullptr;
    QTC_ASSERT(document, return);

    QTextDocument * const text = document->document();
    connect(text, &QTextDocument::undoAvailable, this, &DesignDocument::undoAvailable);
    connect(text, &QTextDocument::redoAvailable, this, &DesignDocument::redoAvailable);
    connect(text, &QTextDocument::modificationChanged, this, &DesignDocument::dirtyStateChanged);

    m_documentTextModifier.reset(new BaseTextEditModifier(document));

    connect(m_documentTextModifier.get(),
            &TextModifier::textChanged,
            this,
            &DesignDocument::updateQrcFiles);

    m_rewriterView->setTextModifier(m_documentTextModifier.get());

    m_inFileComponentTextModifier.reset();

    updateFileName(Utils::FilePath(), fileName());

    updateQrcFiles();

    m_documentLoaded = true;
}

void DesignDocument::changeToDocumentModel()
{
    NanotraceHR::Tracer tracer{"design document change to document model", category()};

    viewManager().detachRewriterView();
    viewManager().detachViewsExceptRewriterAndComponetView();

    clearUndoRedoStacks();

    m_rewriterView->setTextModifier(m_documentTextModifier.get());

    m_inFileComponentModel.reset();
    m_inFileComponentTextModifier.reset();

    viewManager().attachRewriterView();
    viewManager().attachViewsExceptRewriterAndComponetView();
}

bool DesignDocument::isQtForMCUsProject() const
{
    NanotraceHR::Tracer tracer{"design document is Qt for MCUs project", category()};

    if (m_currentTarget && m_currentTarget->buildSystem())
        return m_currentTarget->buildSystem()->additionalData("CustomQtForMCUs").toBool();

    return false;
}

QString DesignDocument::defaultFontFamilyMCU() const
{
    NanotraceHR::Tracer tracer{"design document default font family MCU", category()};

    if (!m_currentTarget || !m_currentTarget->buildSystem())
        return QmlProjectManager::Constants::FALLBACK_MCU_FONT_FAMILY;

    return m_currentTarget->buildSystem()
        ->additionalData(QmlProjectManager::Constants::customDefaultFontFamilyMCU)
        .toString();
}

Utils::FilePath DesignDocument::projectFolder() const
{
    NanotraceHR::Tracer tracer{"design document project folder", category()};

    ProjectExplorer::Project *currentProject = ProjectExplorer::ProjectManager::projectForFile(
        fileName());

    if (currentProject)
        return currentProject->projectDirectory();
    return {};
}

bool DesignDocument::hasProject() const
{
    NanotraceHR::Tracer tracer{"design document has project", category()};

    return !DocumentManager::currentProjectDirPath().isEmpty();
}

void DesignDocument::setModified()
{
    NanotraceHR::Tracer tracer{"design document set modified", category()};

    if (m_documentTextModifier)
        m_documentTextModifier->textDocument()->setModified(true);
}

void DesignDocument::changeToInFileComponentModel(ComponentTextModifier *textModifer)
{
    NanotraceHR::Tracer tracer{"design document change to in file component model", category()};

    m_inFileComponentTextModifier.reset(textModifer);
    viewManager().detachRewriterView();
    viewManager().detachViewsExceptRewriterAndComponetView();

    clearUndoRedoStacks();

    m_inFileComponentModel = createInFileComponentModel();

    m_rewriterView->setTextModifier(m_inFileComponentTextModifier.get());

    viewManager().attachRewriterView();
    viewManager().attachViewsExceptRewriterAndComponetView();
}

void DesignDocument::updateQrcFiles()
{
    NanotraceHR::Tracer tracer{"design document update QRC files", category()};

    ProjectExplorer::Project *currentProject = ProjectExplorer::ProjectManager::projectForFile(
        fileName());

    if (currentProject) {
        const auto srcFiles = currentProject->files(ProjectExplorer::Project::SourceFiles);
        for (const Utils::FilePath &fileName : srcFiles) {
            if (fileName.endsWith(".qrc"))
                QmlJS::ModelManagerInterface::instance()->updateQrcFile(fileName);
        }
    }
}

void DesignDocument::changeToSubComponent(const ModelNode &componentNode)
{
    NanotraceHR::Tracer tracer{"design document change to sub component", category()};

    if (QmlDesignerPlugin::instance()->currentDesignDocument() != this)
        return;

    if (m_inFileComponentModel)
        changeToDocumentModel();

    bool subComponentLoaded = loadInFileComponent(componentNode);

    if (subComponentLoaded)
        attachRewriterToModel();

    QmlDesignerPlugin::instance()->viewManager().pushInFileComponentOnCrumbleBar(componentNode);
    QmlDesignerPlugin::instance()->viewManager().setComponentNode(componentNode);
}

void DesignDocument::changeToMaster()
{
    NanotraceHR::Tracer tracer{"design document change to master", category()};

    if (QmlDesignerPlugin::instance()->currentDesignDocument() != this)
        return;

    if (m_inFileComponentModel)
        changeToDocumentModel();

    QmlDesignerPlugin::instance()->viewManager().pushFileOnCrumbleBar(fileName());
    QmlDesignerPlugin::instance()->viewManager().setComponentNode(rootModelNode());
}

void DesignDocument::attachRewriterToModel()
{
    NanotraceHR::Tracer tracer{"design document attach rewriter to model", category()};

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    Q_ASSERT(m_documentModel);

    viewManager().attachRewriterView();

    Q_ASSERT(m_documentModel);
    QApplication::restoreOverrideCursor();
}

bool DesignDocument::isUndoAvailable() const
{
    NanotraceHR::Tracer tracer{"design document is undo available", category()};

    if (TextEditor::TextDocument * const document = textDocument())
        return document->document()->isUndoAvailable();

    return false;
}

bool DesignDocument::isRedoAvailable() const
{
    NanotraceHR::Tracer tracer{"design document is redo available", category()};

    if (TextEditor::TextDocument * const document = textDocument())
        return document->document()->isRedoAvailable();

    return false;
}

void DesignDocument::clearUndoRedoStacks() const
{
    NanotraceHR::Tracer tracer{"design document clear undo redo stacks", category()};

    if (TextEditor::TextDocument * const document = textDocument())
        document->document()->clearUndoRedoStacks();
}

void DesignDocument::close()
{
    NanotraceHR::Tracer tracer{"design document close", category()};

    m_documentLoaded = false;
    emit designDocumentClosed();
}

#ifndef QDS_USE_PROJECTSTORAGE
void DesignDocument::updateSubcomponentManager()
{
    Q_ASSERT(m_subComponentManager);
    m_subComponentManager->update(QUrl::fromLocalFile(fileName().toUrlishString()),
                                  currentModel()->imports() + currentModel()->possibleImports());
}

void DesignDocument::addSubcomponentManagerImport(const Import &import)
{
    m_subComponentManager->addAndParseImport(import);
}
#endif

void DesignDocument::deleteSelected()
{
    NanotraceHR::Tracer tracer{"design document delete selected", category()};
    if (!currentModel())
        return;

    QStringList lockedNodes;
    for (const ModelNode &modelNode : view()->selectedModelNodes()) {
        for (const ModelNode &node : modelNode.allSubModelNodesAndThisNode()) {
            if (node.isValid() && !node.isRootNode() && node.locked() && !lockedNodes.contains(node.id()))
                lockedNodes.push_back(node.id());
        }
    }

    if (!lockedNodes.empty()) {
        Utils::sort(lockedNodes);
        QString detailedText = QString("<b>" + tr("Locked items:") + "</b><br>");

        for (const auto &id : std::as_const(lockedNodes))
            detailedText.append("- " + id + "<br>");

        detailedText.chop(QString("<br>").size());

        QMessageBox msgBox;
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setWindowTitle(tr("Delete/Cut Item"));
        msgBox.setText(QString(tr("Deleting or cutting this item will modify locked items.") + "<br><br>%1")
                               .arg(detailedText));
        msgBox.setInformativeText(tr("Do you want to continue by removing the item (Delete) or removing it and copying it to the clipboard (Cut)?"));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);

        if (msgBox.exec() == QMessageBox::Cancel)
            return;
    }

    rewriterView()->executeInTransaction("DesignDocument::deleteSelected", [this] {
        const QList<ModelNode> toDelete = view()->selectedModelNodes();
        for (ModelNode node : toDelete) {
            if (node.isValid() && !node.isRootNode() && QmlObjectNode::isValidQmlObjectNode(node))
                QmlObjectNode(node).destroy();
        }
    });
}

void DesignDocument::copySelected()
{
    NanotraceHR::Tracer tracer{"design document copy selected", category()};

    DesignDocumentView view{m_externalDependencies, m_modulesStorage};

    currentModel()->attachView(&view);

    DesignDocumentView::copyModelNodes(view.selectedModelNodes(), m_externalDependencies);
}

void DesignDocument::cutSelected()
{
    NanotraceHR::Tracer tracer{"design document cut selected", category()};

    copySelected();
    deleteSelected();
}

void DesignDocument::duplicateSelected()
{
    NanotraceHR::Tracer tracer{"design document duplicate selected", category()};

    DesignDocumentView view{m_externalDependencies, m_modulesStorage};
    currentModel()->attachView(&view);
    const QList<ModelNode> selectedNodes = view.selectedModelNodes();
    currentModel()->detachView(&view);

    rewriterView()->executeInTransaction("DesignDocument::duplicateSelected", [this, selectedNodes]() {
        moveNodesToPosition(selectedNodes, {});
    });
}

void DesignDocument::paste()
{
    NanotraceHR::Tracer tracer{"design document paste", category()};

    pasteToPosition({});
}

void DesignDocument::pasteToPosition(const std::optional<QVector3D> &position)
{
    NanotraceHR::Tracer tracer{"design document paste to position", category()};

    if (pasteSVG())
        return;

    if (TimelineActions::clipboardContainsKeyframes()) // pasting keyframes is handled in TimelineView
        return;

    auto pasteModel = DesignDocumentView::pasteToModel(m_externalDependencies, m_modulesStorage);

    if (!pasteModel)
        return;

    DesignDocumentView view{m_externalDependencies, m_modulesStorage};
    pasteModel->attachView(&view);
    ModelNode rootNode(view.rootModelNode());

    if (rootNode.type() == "empty")
        return;

    QList<ModelNode> selectedNodes;
    if (rootNode.id() == "__multi__selection__")
        selectedNodes << rootNode.directSubModelNodes();
    else
        selectedNodes << rootNode;

    pasteModel->detachView(&view);

    rewriterView()->executeInTransaction("DesignDocument::pasteToPosition", [this, selectedNodes, position]() {
        moveNodesToPosition(selectedNodes, position);
    });
}

void DesignDocument::selectAll()
{
    NanotraceHR::Tracer tracer{"design document select all", category()};

    if (!currentModel())
        return;

    DesignDocumentView view{m_externalDependencies, m_modulesStorage};
    currentModel()->attachView(&view);

    QList<ModelNode> allNodesExceptRootNode(view.allModelNodes());
    allNodesExceptRootNode.removeOne(view.rootModelNode());
    view.setSelectedModelNodes(allNodesExceptRootNode);
}

RewriterView *DesignDocument::rewriterView() const
{
    NanotraceHR::Tracer tracer{"design document rewriter view", category()};

    return m_rewriterView.get();
}

#ifndef QDS_USE_PROJECTSTORAGE
static void removeUnusedImports(RewriterView *rewriter)
{
    // Remove any import statements for asset based nodes (composed effect or imported3d)
    // if there is no nodes using them in the scene.
    QTC_ASSERT(rewriter && rewriter->model(), return);

    GeneratedComponentUtils compUtils{rewriter->externalDependencies()};

    const QString effectPrefix = compUtils.composedEffectsTypePrefix();
    const QString imported3dPrefix = compUtils.import3dTypePrefix();
    const Utils::FilePaths qmlFiles = compUtils.imported3dComponents();
    QHash<QString, QString> m_imported3dTypeMap;
    for (const Utils::FilePath &qmlFile : qmlFiles) {
        QString importName = compUtils.getImported3dImportName(qmlFile);
        QString type = qmlFile.baseName();
        m_imported3dTypeMap.insert(importName, type);
    }

    const Imports &imports = rewriter->model()->imports();
    QHash<QString, Import> assetImports;
    for (const Import &import : imports) {
        if (import.url().startsWith(effectPrefix)) {
            QString type = import.url().split('.').last();
            assetImports.insert(type, import);
        } else if (import.url().startsWith(imported3dPrefix)) {
            assetImports.insert(m_imported3dTypeMap[import.url()], import);
        }
    }

    const QList<ModelNode> allNodes = rewriter->allModelNodes();
    for (const ModelNode &node : allNodes) {
        if (QmlItemNode(node).isEffectItem()
            || (node.isComponent() && node.metaInfo().isQtQuick3DNode())) {
            assetImports.remove(node.simplifiedTypeName());
        }
    }

    if (!assetImports.isEmpty()) {
        Imports removeImports;
        for (const Import &import : assetImports)
            removeImports.append(import);
        rewriter->model()->changeImports({}, removeImports);
    }

    rewriter->forceAmend();
}
#endif

void DesignDocument::setEditor(Core::IEditor *editor)
{
    NanotraceHR::Tracer tracer{"design document set editor", category()};

    m_textEditor = editor;
    // if the user closed the file explicit we do not want to do anything with it anymore

    connect(Core::EditorManager::instance(), &Core::EditorManager::aboutToSave,
            this, [this](Core::IDocument *document) {
        if (m_textEditor && m_textEditor->document() == document) {
            if (m_documentModel && m_documentModel->rewriterView()) {

#ifdef QDS_USE_PROJECTSTORAGE
                // TODO: ProjectStorage should handle this via Model somehow (QDS-14519)
#else
                removeUnusedImports(rewriterView());
#endif
                m_documentModel->rewriterView()->writeAuxiliaryData();
            }
        }
    });

    connect(Core::EditorManager::instance(), &Core::EditorManager::editorAboutToClose,
            this, [this](Core::IEditor *editor) {
        if (m_textEditor.data() == editor)
            m_textEditor.clear();
    });

    connect(editor->document(), &Core::IDocument::filePathChanged, this, &DesignDocument::updateFileName);

    updateActiveTarget();
    updateActiveTarget();
}

Core::IEditor *DesignDocument::editor() const
{
    NanotraceHR::Tracer tracer{"design document editor", category()};

    return m_textEditor.data();
}

TextEditor::TextDocument *DesignDocument::textDocument() const
{
    return m_textEditor ? qobject_cast<TextEditor::TextDocument *>(m_textEditor->document())
                        : nullptr;
}

ModelNode DesignDocument::rootModelNode() const
{
    NanotraceHR::Tracer tracer{"design document root model node", category()};

    return rewriterView()->rootModelNode();
}

void DesignDocument::undo()
{
    NanotraceHR::Tracer tracer{"design document undo", category()};

    if (rewriterView() && !rewriterView()->modificationGroupActive()) {
        TextEditor::undoIn(editor());
        rewriterView()->forceAmend();
    }

    viewManager().resetPropertyEditorView();
}

void DesignDocument::redo()
{
    NanotraceHR::Tracer tracer{"design document redo", category()};

    if (rewriterView() && !rewriterView()->modificationGroupActive()) {
        TextEditor::redoIn(editor());
        rewriterView()->forceAmend();
    }

    viewManager().resetPropertyEditorView();
}

static ProjectExplorer::Target *getActiveTarget(DesignDocument *designDocument)
{
    auto currentProject = ProjectExplorer::ProjectManager::projectForFile(designDocument->fileName());

    if (!currentProject)
        currentProject = ProjectExplorer::ProjectTree::currentProject();

    if (!currentProject)
        return nullptr;

    QObject::connect(ProjectExplorer::ProjectTree::instance(),
                     &ProjectExplorer::ProjectTree::currentProjectChanged,
                     designDocument,
                     &DesignDocument::updateActiveTarget,
                     Qt::UniqueConnection);

    QObject::connect(currentProject,
                     &ProjectExplorer::Project::activeTargetChanged,
                     designDocument,
                     &DesignDocument::updateActiveTarget,
                     Qt::UniqueConnection);

    auto target = currentProject->activeTarget();

    if (!target || !target->kit()->isValid())
        return nullptr;

    QObject::connect(target,
                     &ProjectExplorer::Target::kitChanged,
                     designDocument,
                     &DesignDocument::updateActiveTarget,
                     Qt::UniqueConnection);

    return target;
}

void DesignDocument::updateActiveTarget()
{
    NanotraceHR::Tracer tracer{"design document update active target", category()};

    m_currentTarget = getActiveTarget(this);
    viewManager().setNodeInstanceViewTarget(m_currentTarget);
}

void DesignDocument::contextHelp(const Core::IContext::HelpCallback &callback) const
{
    NanotraceHR::Tracer tracer{"design document context help", category()};

    if (auto v = view())
        QmlDesignerPlugin::contextHelp(callback, v->contextHelpId());
    else
        callback({});
}

} // namespace QmlDesigner

#ifdef WITH_TESTS

#include <coreplugin/editormanager/ieditorfactory.h>
#include <qmljseditor/qmljseditorconstants.h>
#include <texteditor/texteditor.h>
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

namespace QmlDesigner {

// The design document reached the text through the widget editor: it built
// its modifier on it, followed its undo and modification signals, and read
// undo availability from it. Everything it needs is the document's, so a
// design document stands on whichever view shows the file.
class DesignDocumentTest final : public QObject
{
    Q_OBJECT

private slots:
    void testADesignDocumentStandsOnEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testADesignDocumentStandsOnEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("qmldesigner-design-document");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Form.ui.qml");
        QVERIFY(file.writeFileContents("import QtQuick\nItem {\n    width: 10\n}\n"));

        // The designer's own factory, by id: the file's default editor is a
        // question of preferences, and this is about the editor behind a
        // design document.
        const Utils::Id designerId(QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID);
        auto * const factory = dynamic_cast<TextEditor::TextEditorFactory *>(
            Core::IEditorFactory::editorFactoryForId(designerId));
        QVERIFY2(factory, "the designer's editor factory is not a text editor factory");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file, designerId);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // A design document for it, the way the plugin makes one when Design
        // mode is entered; taken back before the editor closes.
        DocumentManager &manager = QmlDesignerPlugin::instance()->documentManager();
        manager.setCurrentDesignDocument(editor);
        const QScopeGuard forget([&manager, editor] {
            manager.setCurrentDesignDocument(nullptr);
            manager.removeEditors({editor});
        });
        DesignDocument * const designDocument = manager.currentDesignDocument();
        QVERIFY2(designDocument, "the document manager made no design document");
        QCOMPARE(designDocument->editor(), editor);

        QSignalSpy undoTold(designDocument, &DesignDocument::undoAvailable);
        QSignalSpy dirtyTold(designDocument, &DesignDocument::dirtyStateChanged);
        designDocument->loadDocument(editor);
        QVERIFY2(designDocument->isDocumentLoaded(), "loading from this editor did not take");
        QVERIFY(!designDocument->isUndoAvailable());
        QVERIFY(!designDocument->isRedoAvailable());

        // An edit to the text, and the design document knows about it: undo
        // is available and it says so, and the document is dirty and it says
        // so.
        QTextCursor cursor(document->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText("// edited\n");
        QVERIFY(designDocument->isUndoAvailable());
        QVERIFY2(!undoTold.isEmpty() && undoTold.last().first().toBool(),
                 "the design document did not say undo became available");
        QVERIFY2(!dirtyTold.isEmpty() && dirtyTold.last().first().toBool(),
                 "the design document did not say the text became dirty");

        // Undo and redo through the editor, which is what DesignDocument::undo()
        // does before it tells the rewriter - the rewriter is not attached
        // here, so its half is not driven.
        TextEditor::undoIn(editor);
        QVERIFY(!document->plainText().contains("edited"));
        QVERIFY(!designDocument->isUndoAvailable());
        QVERIFY(designDocument->isRedoAvailable());
        TextEditor::redoIn(editor);
        QVERIFY(document->plainText().contains("edited"));
        QVERIFY(designDocument->isUndoAvailable());
    }
};

QObject *createDesignDocumentTest()
{
    return new DesignDocumentTest;
}

} // namespace QmlDesigner

#include "designdocument.moc"

#endif // WITH_TESTS
