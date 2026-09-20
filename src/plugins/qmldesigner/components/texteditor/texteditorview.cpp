// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "texteditorview.h"

#include <assetslibrarywidget.h>
#include <customnotifications.h>
#include <designdocument.h>
#include <designeractionmanager.h>
#include <designersettings.h>
#include <itemlibraryentry.h>
#include <model.h>
#include <modelnode.h>
#include <nodeabstractproperty.h>
#include <qmldesignerconstants.h>
#include <qmldesignerplugin.h>
#include <qmlitemnode.h>
#include <qmlstate.h>
#include <rewriterview.h>
#include <texteditorstatusbar.h>
#include <texteditorview.h>

#include <coreplugin/actionmanager/command.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/icore.h>

#include <qmljseditor/qmljseditordocument.h>

#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/qmljsreformatter.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/changeset.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>
#include <utils/uniqueobjectptr.h>

#include <QDebug>
#include <QEvent>
#include <QPair>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <memory>
#include <vector>

using namespace Core;

namespace QmlDesigner {

class TextEditorStatusBar;

static QTextDocument *textOf(Core::IEditor *editor)
{
    auto * const document = editor ? qobject_cast<TextEditor::TextDocument *>(editor->document())
                                   : nullptr;
    return document ? document->document() : nullptr;
}

class TextEditorWidget : public QWidget
{
public:
    TextEditorWidget(TextEditorView *textEditorView);

    void setEditor(Utils::UniqueObjectLatePtr<Core::IEditor> editor);

    Core::IEditor *editor() const { return m_editor.get(); }

    void contextHelp(const Core::IContext::HelpCallback &callback) const;
    void jumpTextCursorToSelectedModelNode();
    void gotoCursorPosition(int line, int column);

    void setStatusText(const QString &text);
    void clearStatusBar();

    int currentLine() const;

    void setBlockCursorSelectionSynchronisation(bool b);
    void jumpToModelNode(const ModelNode &modelNode);
    void highlightToModelNode(const ModelNode &modelNode);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *dragEnterEvent) override;
    void dragMoveEvent(QDragMoveEvent *dragMoveEvent) override;
    void dropEvent(QDropEvent *dropEvent) override;

private:
    void updateSelectionByCursorPosition();
    void gotoPosition(int position);
    // A point of this widget's, in the coordinates of the view inside it.
    QPoint mapToEditor(const QPoint &point) const;

    Utils::UniqueObjectLatePtr<Core::IEditor> m_editor;
    QPointer<TextEditorView> m_textEditorView;
    QTimer m_updateSelectionTimer;
    TextEditorStatusBar *m_statusBar = nullptr;
    Core::FindToolBarPlaceHolder *m_findToolBar = nullptr;
    QVBoxLayout *m_layout = nullptr;
    bool m_blockCursorSelectionSynchronisation = false;
    bool m_blockRoundTrip = false;
    ItemLibraryEntry m_draggedEntry;
};

TextEditorWidget::TextEditorWidget(TextEditorView *textEditorView)
    : m_textEditorView(textEditorView)
    , m_statusBar(new TextEditorStatusBar(this))
    , m_findToolBar(new Core::FindToolBarPlaceHolder(this))
    , m_layout(new QVBoxLayout(this))
{
    setAcceptDrops(true);

    m_statusBar->hide();

    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(m_statusBar);
    m_layout->addWidget(m_findToolBar);

    m_updateSelectionTimer.setSingleShot(true);
    m_updateSelectionTimer.setInterval(200);

    connect(&m_updateSelectionTimer,
            &QTimer::timeout,
            this,
            &TextEditorWidget::updateSelectionByCursorPosition);
}

void TextEditorWidget::setEditor(Utils::UniqueObjectLatePtr<Core::IEditor> editor)
{
    std::swap(m_editor, editor);

    if (m_editor) {
        QWidget * const view = m_editor->widget();
        m_layout->insertWidget(0, view);

        setFocusProxy(view);

        TextEditor::whenCursorMoved(m_editor.get(), this, [this] {
            // Cursor position is changed by rewriter
            if (!m_blockCursorSelectionSynchronisation)
                m_updateSelectionTimer.start();
        });

        view->installEventFilter(this);
    }
}

QPoint TextEditorWidget::mapToEditor(const QPoint &point) const
{
    return m_editor ? m_editor->widget()->mapFrom(this, point) : point;
}

void TextEditorWidget::gotoPosition(int position)
{
    QTextDocument * const text = textOf(m_editor.get());
    QTC_ASSERT(text, return);

    // A one-based line and a zero-based column, which is what gotoLine() takes.
    int line = 0;
    int column = 0;
    Utils::Text::convertPosition(text, position, &line, &column);
    m_editor->gotoLine(line, column);
}

void TextEditorWidget::contextHelp(const Core::IContext::HelpCallback &callback) const
{
    if (m_textEditorView)
        QmlDesignerPlugin::contextHelp(callback, m_textEditorView->contextHelpId());
    else
        callback({});
}

void TextEditorWidget::updateSelectionByCursorPosition()
{
    if (!m_textEditorView->model())
        return;

    const int cursorPosition = TextEditor::textCursorOf(m_editor.get()).position();
    RewriterView *rewriterView = m_textEditorView->model()->rewriterView();

    m_blockRoundTrip = true;
    if (rewriterView) {
        ModelNode modelNode = rewriterView->nodeAtTextCursorPosition(cursorPosition);
        if (modelNode.isValid() && !m_textEditorView->isSelectedModelNode(modelNode))
            m_textEditorView->setSelectedModelNode(modelNode);
    }
    m_blockRoundTrip = false;
}

void TextEditorWidget::jumpToModelNode(const ModelNode &modelNode)
{
    RewriterView *rewriterView = m_textEditorView->model()->rewriterView();

    m_blockCursorSelectionSynchronisation = true;
    const int nodeOffset = rewriterView->nodeOffset(modelNode);
    if (nodeOffset > 0) {
        gotoPosition(nodeOffset);
        highlightToModelNode(modelNode);
    }
    m_blockCursorSelectionSynchronisation = false;
}

void TextEditorWidget::highlightToModelNode(const ModelNode &modelNode)
{
    RewriterView *rewriterView = m_textEditorView->model()->rewriterView();
    const int nodeOffset = rewriterView->nodeOffset(modelNode);
    if (nodeOffset > 0)
        TextEditor::highlightScopeAtIn(m_editor.get(), nodeOffset);
}

void TextEditorWidget::jumpTextCursorToSelectedModelNode()
{
    if (m_blockRoundTrip)
        return;

    ModelNode selectedNode;

    if (hasFocus())
        return;

    if (m_editor && TextEditor::hasFocusIn(m_editor.get()))
        return;

    if (!m_textEditorView->selectedModelNodes().isEmpty())
        selectedNode = m_textEditorView->selectedModelNodes().constFirst();

    if (selectedNode.isValid()) {
        QmlModelState currentState = m_textEditorView->currentStateNode();
        if (currentState.isBaseState()) {
            jumpToModelNode(selectedNode);
        } else {
            if (currentState.affectsModelNode(selectedNode)) {
                // FIXME: QMLDESIGNER_MERGE
                QTC_CHECK(false);
                // auto propertyChanges = currentState.propertyChanges(selectedNode);
                // jumpToModelNode(propertyChanges.modelNode());
            } else {
                jumpToModelNode(currentState.modelNode());
            }
        }
    }
    m_updateSelectionTimer.stop();
}

void TextEditorWidget::gotoCursorPosition(int line, int column)
{
    if (m_editor) {
        m_editor->gotoLine(line, column);
        TextEditor::setFocusIn(m_editor.get());
    }
}

void TextEditorWidget::setStatusText(const QString &text)
{
    m_statusBar->setText(text);
    m_statusBar->setVisible(!text.isEmpty());
}

void TextEditorWidget::clearStatusBar()
{
    m_statusBar->clearText();
    m_statusBar->hide();
}

int TextEditorWidget::currentLine() const
{
    if (m_editor)
        return TextEditor::lineColumnOf(m_editor.get()).line;
    return -1;
}

void TextEditorWidget::setBlockCursorSelectionSynchronisation(bool b)
{
    m_blockCursorSelectionSynchronisation = b;
}

bool TextEditorWidget::eventFilter(QObject *, QEvent *event)
{
    //do not call the eventfilter when the editor is gone
    if (!m_editor)
        return false;

    static std::vector<int> overrideKeys = { Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Insert,
                                             Qt::Key_Escape };

    static std::vector<QKeySequence> overrideSequences = {QKeySequence(Qt::CTRL | Qt::ALT),
                                                          QKeySequence(Qt::Key_Left | Qt::CTRL),
                                                          QKeySequence(Qt::Key_Right | Qt::CTRL),
                                                          QKeySequence(Qt::Key_Up | Qt::CTRL),
                                                          QKeySequence(Qt::Key_Down | Qt::CTRL)};
    if (event->type() == QEvent::ShortcutOverride) {
        auto keyEvent = static_cast<QKeyEvent *>(event);

        if (std::find(overrideKeys.begin(), overrideKeys.end(), keyEvent->key()) != overrideKeys.end()) {
            if (keyEvent->key() == Qt::Key_Escape)
                m_findToolBar->hide();

            keyEvent->accept();
            return true;
        }

        static const Qt::KeyboardModifiers relevantModifiers = Qt::ShiftModifier
                                                             | Qt::ControlModifier
                                                             | Qt::AltModifier
                                                             | Qt::MetaModifier;

        const QKeySequence keySqeuence(keyEvent->key() | (keyEvent->modifiers() & relevantModifiers));
        for (const QKeySequence &overrideSequence : overrideSequences) {
            if (keySqeuence.matches(overrideSequence)) {
                keyEvent->accept();
                return true;
            }
        }
    } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        TextEditor::clearScopeHighlightIn(m_editor.get());
    }
    return false;
}

void TextEditorWidget::dragEnterEvent(QDragEnterEvent *dragEnterEvent)
{
    const DesignerActionManager &actionManager
        = QmlDesignerPlugin::instance()->viewManager().designerActionManager();
    if (actionManager.externalDragHasSupportedAssets(dragEnterEvent->mimeData()))
        dragEnterEvent->acceptProposedAction();

    if (dragEnterEvent->mimeData()->hasFormat(Constants::MIME_TYPE_ITEM_LIBRARY_INFO)
        || dragEnterEvent->mimeData()->hasFormat(Constants::MIME_TYPE_ASSETS)) {
        QByteArray data = dragEnterEvent->mimeData()->data(Constants::MIME_TYPE_ITEM_LIBRARY_INFO);
        if (!data.isEmpty()) {
            QDataStream stream(data);
            stream >> m_draggedEntry;
        }
        dragEnterEvent->acceptProposedAction();
    }
}

void TextEditorWidget::dragMoveEvent(QDragMoveEvent *dragMoveEvent)
{
    const int cursorPosition = TextEditor::positionAtIn(
        m_editor.get(), mapToEditor(dragMoveEvent->position().toPoint()));
    RewriterView *rewriterView = m_textEditorView->model()->rewriterView();

    QTC_ASSERT(rewriterView, return );
    ModelNode modelNode = rewriterView->nodeAtTextCursorPosition(cursorPosition);

    if (!modelNode.isValid())
        return;
    highlightToModelNode(modelNode);
}

void TextEditorWidget::dropEvent(QDropEvent *dropEvent)
{
    const int cursorPosition = TextEditor::positionAtIn(
        m_editor.get(), mapToEditor(dropEvent->position().toPoint()));
    RewriterView *rewriterView = m_textEditorView->model()->rewriterView();

    QTC_ASSERT(rewriterView, return);
    ModelNode modelNode = rewriterView->nodeAtTextCursorPosition(cursorPosition);

    if (!modelNode.isValid())
        return;

    auto targetProperty = modelNode.defaultNodeAbstractProperty();

    if (dropEvent->mimeData()->hasFormat(Constants::MIME_TYPE_ITEM_LIBRARY_INFO)) {
        if (!m_draggedEntry.name().isEmpty()) {
            m_textEditorView->executeInTransaction("TextEditorWidget::dropEventItem", [&] {
                auto newQmlObjectNode = QmlItemNode::createQmlObjectNode(m_textEditorView,
                                                                         m_draggedEntry,
                                                                         QPointF(),
                                                                         targetProperty,
                                                                         false);
            });
        }
    } else if (dropEvent->mimeData()->hasFormat(Constants::MIME_TYPE_ASSETS)) {
        const QStringList assetsPaths
            = QString::fromUtf8(dropEvent->mimeData()->data(Constants::MIME_TYPE_ASSETS)).split(',');

        QTC_ASSERT(!assetsPaths.isEmpty(), return);
        const ModelNode targetNode = targetProperty.parentModelNode();
        QList<ModelNode> addedNodes;

        for (const QString &assetPath : assetsPaths) {
            ModelNode newModelNode;
            const auto assetTypeAndData = AssetsLibraryWidget::getAssetTypeAndData(assetPath);
            const QString assetType = assetTypeAndData.first;
            const QString assetData = QString::fromUtf8(assetTypeAndData.second);
            bool moveNodesAfter = true; // Appending to parent is the default in text editor
            if (assetType == Constants::MIME_TYPE_ASSET_IMAGE) {
                newModelNode = ModelNodeOperations::handleItemLibraryImageDrop(assetPath,
                                                                               targetProperty,
                                                                               targetNode,
                                                                               moveNodesAfter);
            } else if (assetType == Constants::MIME_TYPE_ASSET_FONT) {
                newModelNode = ModelNodeOperations::handleItemLibraryFontDrop(
                    assetData, // assetData is fontFamily
                    targetProperty,
                    targetNode);
            } else if (assetType == Constants::MIME_TYPE_ASSET_SHADER) {
                newModelNode = ModelNodeOperations::handleItemLibraryShaderDrop(assetPath,
                                                                                assetData == "f",
                                                                                targetProperty,
                                                                                targetNode,
                                                                                moveNodesAfter);
            } else if (assetType == Constants::MIME_TYPE_ASSET_SOUND) {
                newModelNode = ModelNodeOperations::handleItemLibrarySoundDrop(assetPath,
                                                                               targetProperty,
                                                                               targetNode);
            } else if (assetType == Constants::MIME_TYPE_ASSET_TEXTURE3D) {
                newModelNode = ModelNodeOperations::handleItemLibraryTexture3dDrop(assetPath,
                                                                                   targetNode,
                                                                                   moveNodesAfter);
            } else if (assetType == Constants::MIME_TYPE_ASSET_EFFECT) {
                newModelNode = ModelNodeOperations::handleItemLibraryEffectDrop(assetPath,
                                                                                targetNode);
            }

            if (newModelNode.isValid())
                addedNodes.append(newModelNode);
        }

        if (!addedNodes.isEmpty())
            m_textEditorView->setSelectedModelNodes(addedNodes);
    } else {
        const DesignerActionManager &actionManager
            = QmlDesignerPlugin::instance()->viewManager().designerActionManager();
        actionManager.handleExternalAssetsDrop(dropEvent->mimeData());
    }
    m_textEditorView->model()->endDrag();
    TextEditor::clearScopeHighlightIn(m_editor.get());
}


// TextEditorView

TextEditorView::TextEditorView(ExternalDependenciesInterface &externalDependencies)
    : AbstractView{externalDependencies}
    , m_widget(new TextEditorWidget(this))
{
}

TextEditorView::~TextEditorView()
{
    // m_textEditorContext is responsible for deleting the widget
}

void TextEditorView::modelAttached(Model *model)
{
    Q_ASSERT(model);
    m_widget->clearStatusBar();

    AbstractView::modelAttached(model);

    createTextEditor();
}

void TextEditorView::modelAboutToBeDetached(Model *model)
{
    AbstractView::modelAboutToBeDetached(model);

    if (m_widget)
        m_widget->setEditor(nullptr);
    disconnect(m_designDocumentConnection);
}

WidgetInfo TextEditorView::widgetInfo()
{
    return createWidgetInfo(m_widget,
                            "TextEditor",
                            WidgetInfo::CentralPane,
                            tr("Code"),
                            tr("Code view"),
                            DesignerWidgetFlags::IgnoreErrors);
}

Core::IEditor *TextEditorView::editor()
{
    return m_widget->editor();
}

void TextEditorView::selectedNodesChanged(const QList<ModelNode> &/*selectedNodeList*/,
                                          const QList<ModelNode> &/*lastSelectedNodeList*/)
{
    if (!m_errorState)
        m_widget->jumpTextCursorToSelectedModelNode();
}

void TextEditorView::customNotification(const AbstractView * /*view*/, const QString &identifier, const QList<ModelNode> &/*nodeList*/, const QList<QVariant> &/*data*/)
{
    if (identifier == StartRewriterApply)
        m_widget->setBlockCursorSelectionSynchronisation(true);
    else if (identifier == EndRewriterApply)
        m_widget->setBlockCursorSelectionSynchronisation(false);
}

void TextEditorView::documentMessagesChanged(const QList<DocumentMessage> &errors, const QList<DocumentMessage> &)
{
    if (errors.isEmpty()) {
        m_widget->clearStatusBar();
        m_errorState = false;
    } else {
        const DocumentMessage &error = errors.constFirst();
        m_widget->setStatusText(QString("%1 (Line: %2)").arg(error.description()).arg(error.line()));
        m_errorState = true;
    }
}

void TextEditorView::gotoCursorPosition(int line, int column)
{
    if (m_widget)
        m_widget->gotoCursorPosition(line, column);
}

void TextEditorView::reformatFile()
{
    QTC_ASSERT(!m_widget.isNull(), return);

    auto document =
            qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(Core::EditorManager::currentDocument());

    // Reformat document if we have a .ui.qml file
    if (document && document->filePath().toUrlishString().endsWith(".ui.qml")
                 && designerSettings().reformatUiQmlFiles()) {

        QmlJS::Document::Ptr currentDocument(document->semanticInfo().document);
        QmlJS::Snapshot snapshot = QmlJS::ModelManagerInterface::instance()->snapshot();

        if (document->isSemanticInfoOutdated()) {
            QmlJS::Document::MutablePtr latestDocument;

            const Utils::FilePath fileName = document->filePath();
            latestDocument = snapshot.documentFromSource(QString::fromUtf8(document->contents()),
                                                         fileName,
                                                         QmlJS::ModelManagerInterface::guessLanguageOfFile(fileName));
            latestDocument->parseQml();
            snapshot.insert(latestDocument);

            currentDocument = latestDocument;
        }

        if (!currentDocument->isParsedCorrectly())
            return;

        const QString &newText = QmlJS::reformat(currentDocument);
        if (currentDocument->source() == newText)
            return;

        const bool hasEditor = m_widget->editor();
        if (!hasEditor)
            createTextEditor();

        QTextCursor tc = TextEditor::textCursorOf(m_widget->editor());
        const int pos = tc.position();

        Utils::ChangeSet changeSet;
        changeSet.replace(0, document->plainText().size(), newText);

        tc.beginEditBlock();
        changeSet.apply(&tc);
        tc.setPosition(pos);
        tc.endEditBlock();

        TextEditor::setTextCursorOf(m_widget->editor(), tc);

        if (!hasEditor)
            m_widget->setEditor(nullptr);
    }
}

void TextEditorView::jumpToModelNode(const ModelNode &modelNode)
{
    m_widget->jumpToModelNode(modelNode);

    m_widget->window()->windowHandle()->requestActivate();
    TextEditor::setFocusIn(m_widget->editor());
    TextEditor::clearScopeHighlightIn(m_widget->editor());
}

void TextEditorView::createTextEditor()
{
    DesignDocument *designDocument = QmlDesignerPlugin::instance()->currentDesignDocument();
    Core::IEditor * const source = designDocument->editor();
    QTC_ASSERT(source, return);
    auto editor = Utils::UniqueObjectLatePtr<Core::IEditor>(source->duplicate());
    QTC_ASSERT(editor, return);
    static constexpr char qmlTextEditorContextId[] = "QmlDesigner::TextEditor";
    IContext::attach(editor->widget(),
                     Context(qmlTextEditorContextId, Constants::qtQuickToolsMenuContextId),
                     [this](const IContext::HelpCallback &callback) {
                         m_widget->contextHelp(callback);
                     });
    m_widget->setEditor(std::move(editor));

    disconnect(m_designDocumentConnection);
    m_designDocumentConnection = connect(designDocument,
                                         &DesignDocument::designDocumentClosed,
                                         m_widget,
                                         [this] { m_widget->setEditor(nullptr); });
}

} // namespace QmlDesigner

#ifdef WITH_TESTS

#include <coreplugin/editormanager/ieditorfactory.h>
#include <qmljseditor/qmljseditorconstants.h>
#include <utils/temporarydirectory.h>

#include <QKeyEvent>
#include <QScopeGuard>
#include <QTest>

namespace QmlDesigner {

class TextEditorViewTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheCodeViewStandsOnEitherView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheCodeViewStandsOnEitherView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("qmldesigner-code-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Form.ui.qml");
        QVERIFY(file.writeFileContents("import QtQuick\n"
                                       "Item {\n"
                                       "    Rectangle {\n"
                                       "        id: box\n"
                                       "    }\n"
                                       "}\n"));

        const Utils::Id designerId(QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID);
        auto * const factory = dynamic_cast<TextEditor::TextEditorFactory *>(
            Core::IEditorFactory::editorFactoryForId(designerId));
        QVERIFY2(factory, "the designer's editor factory is not a text editor factory");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        // Opening a designer editor enters Design mode, which loads the design
        // document and attaches the views - the Code view among them.
        Core::IEditor * const editor = Core::EditorManager::openEditor(file, designerId);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        TextEditorView * const view = QmlDesignerPlugin::instance()->viewManager().textEditorView();
        QVERIFY(view);
        QVERIFY2(view->model(), "Design mode attached no model to the Code view");

        // Its own editor over the same document: a duplicate, in the view the
        // file was opened in.
        Core::IEditor * const code = view->editor();
        QVERIFY2(code, "the Code view has no editor of its own");
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(code) == nullptr, quick);
        QVERIFY2(code != editor, "the Code view took the editor rather than a duplicate");
        QCOMPARE(code->document(), editor->document());

        // Where the Code view is sent is where its caret goes.
        view->gotoCursorPosition(4, 0);
        QCOMPARE(TextEditor::lineColumnOf(code).line, 4);
        view->gotoCursorPosition(2, 0);
        QCOMPARE(TextEditor::lineColumnOf(code).line, 2);

        // The keys the Code view takes for the text rather than letting them
        // fire a designer shortcut. Its event filter is on the editor's
        // widget, whichever view that widget belongs to, and a
        // ShortcutOverride accepted there is a key that reaches the text.
        const auto claims = [code](int key, Qt::KeyboardModifiers modifiers) {
            QKeyEvent ask(QEvent::ShortcutOverride, key, modifiers);
            ask.setAccepted(false);
            QCoreApplication::sendEvent(code->widget(), &ask);
            return ask.isAccepted();
        };
        QVERIFY2(claims(Qt::Key_Delete, Qt::NoModifier),
                 "Delete would fire a designer shortcut instead of reaching the text");
        QVERIFY2(claims(Qt::Key_Backspace, Qt::NoModifier),
                 "Backspace would fire a designer shortcut instead of reaching the text");
        QVERIFY2(claims(Qt::Key_Left, Qt::ControlModifier),
                 "Ctrl+Left would fire a designer shortcut instead of reaching the text");

    }
    // The switch itself. No row for the widget view: this is about what the
    // designer's factory does when nobody has flipped it, which is what a
    // reader opening a .ui.qml gets.
    void testADesignerEditorIsQuickByDefault()
    {
        Utils::TemporaryDirectory dir("qmldesigner-default-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Form.ui.qml");
        QVERIFY(file.writeFileContents("import QtQuick\nItem {\n    width: 10\n}\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(
            file, QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the designer's factory still opens a widget editor");

        // And the Code view beside it, which duplicates that editor.
        TextEditorView * const view = QmlDesignerPlugin::instance()->viewManager().textEditorView();
        QVERIFY(view);
        QVERIFY2(view->editor(), "the Code view has no editor of its own");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(view->editor()),
                 "the Code view still shows a widget editor");
    }
};

QObject *createTextEditorViewTest()
{
    return new TextEditorViewTest;
}

} // namespace QmlDesigner

#include "texteditorview.moc"

#endif // WITH_TESTS
