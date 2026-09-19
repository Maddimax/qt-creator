// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "scxmleditor.h"

#include "mainwidget.h"
#include "scxmleditorconstants.h"
#include "scxmleditordocument.h"
#include "scxmleditortr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/designmode.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/editortoolbar.h>
#include <coreplugin/icontext.h>
#include <coreplugin/idocument.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/outputpane.h>

#include <texteditor/texteditor.h>

#include <utils/fsengine/fileiconprovider.h>
#include <utils/icon.h>
#include <utils/infobar.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#include <QGuiApplication>
#include <QStackedWidget>
#include <QToolBar>
#include <QUndoGroup>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <utils/algorithm.h>
#include <utils/temporarydirectory.h>

#include <QKeyEvent>
#include <QScopeGuard>
#include <QTest>
#endif

using namespace Core;
using namespace ScxmlEditor::Common;
using namespace ScxmlEditor::PluginInterface;
using namespace Utils;

namespace ScxmlEditor::Internal {

class ScxmlEditorStack final : public QStackedWidget
{
public:
    ScxmlEditorStack() { setObjectName("ScxmlEditorStack"); }

    void add(IEditor *editor, QWidget *widget)
    {
        connect(Core::ModeManager::instance(), &Core::ModeManager::currentModeAboutToChange,
                this, &ScxmlEditorStack::modeAboutToChange);

        m_editors.append(editor);
        addWidget(widget);
        connect(editor, &QObject::destroyed,
                this, &ScxmlEditorStack::removeScxmlTextEditor);
    }

    QWidget *widgetForEditor(IEditor *xmlEditor)
    {
        const int i = m_editors.indexOf(xmlEditor);
        QTC_ASSERT(i >= 0, return nullptr);

        return widget(i);
    }

    void removeScxmlTextEditor(QObject *xmlEditor)
    {
        const int i = m_editors.indexOf(xmlEditor);
        QTC_ASSERT(i >= 0, return);

        QWidget *widget = this->widget(i);
        if (widget) {
            removeWidget(widget);
            widget->deleteLater();
        }
        m_editors.removeAt(i);
    }

    bool setVisibleEditor(Core::IEditor *xmlEditor)
    {
        const int i = m_editors.indexOf(xmlEditor);
        QTC_ASSERT(i >= 0, return false);

        if (i != currentIndex())
            setCurrentIndex(i);

        return true;
    }

private:
    void modeAboutToChange(Utils::Id m)
    {
        // Sync the editor when entering edit mode
        if (m == Core::Constants::MODE_EDIT) {
            for (IEditor *editor : std::as_const(m_editors))
                if (auto document = qobject_cast<ScxmlEditorDocument*>(editor->document()))
                    document->syncXmlFromDesignWidget();
        }
    }

    QList<IEditor *> m_editors;
};

// The read-only text view of a state chart, shown in Edit mode. The chart is
// edited in Design mode, and an info bar on the view says so. Read-only is
// asked of whichever view draws the text, so nothing here names one.
class ScxmlTextEditorFactory : public TextEditor::TextEditorFactory
{
public:
    ScxmlTextEditorFactory()
    {
        setId(ScxmlEditor::Constants::K_SCXML_EDITOR_ID);
        addEditorContext(ScxmlEditor::Constants::C_SCXML_EDITOR);
        setUsesQuickEditor(true);
        setUseGenericHighlighter(true);
        setDuplicatedSupported(false);
        setToolBarVisible(false);
    }

    IEditor *create(ScxmlEditor::Common::MainWidget *designWidget)
    {
        setDocumentCreator([designWidget] { return new ScxmlEditorDocument(designWidget); });
        IEditor * const editor = createEditor();
        if (editor)
            TextEditor::setReadOnlyOf(editor, true);
        return editor;
    }
};

class ScxmlEditorData : public QObject
{
public:
    ScxmlEditorData();
    ~ScxmlEditorData() override;

    void fullInit();
    IEditor *createEditor();

private:
    void updateToolBar();
    QWidget *createModeWidget();
    EditorToolBar *createMainToolBar();

    Context m_contexts;
    QWidget *m_modeWidget = nullptr;
    ScxmlEditorStack *m_widgetStack = nullptr;
    QToolBar *m_widgetToolBar = nullptr;
    EditorToolBar *m_mainToolBar = nullptr;
    QUndoGroup *m_undoGroup = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;

    ScxmlTextEditorFactory *m_xmlEditorFactory = nullptr;
};

ScxmlEditorData::ScxmlEditorData()
{
    m_contexts.add(ScxmlEditor::Constants::C_SCXMLEDITOR);

    QObject::connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
                     this, [this](IEditor *editor) {
        if (editor && editor->document()->id() == Constants::K_SCXML_EDITOR_ID) {
            QWidget *dw = m_widgetStack->widgetForEditor(editor);
            QTC_ASSERT(dw, return );
            m_widgetStack->setVisibleEditor(editor);
            m_mainToolBar->setCurrentEditor(editor);
            updateToolBar();
            auto designWidget = static_cast<MainWidget*>(m_widgetStack->currentWidget());
            if (designWidget)
                designWidget->refresh();
        }
    });

    m_xmlEditorFactory = new ScxmlTextEditorFactory;
}

ScxmlEditorData::~ScxmlEditorData()
{
    if (m_modeWidget) {
        DesignMode::unregisterDesignWidget(m_modeWidget);
        delete m_modeWidget;
        m_modeWidget = nullptr;
    } else if (m_mainToolBar) { // gets automatically deleted with m_modeWidget
        delete m_mainToolBar;
        m_mainToolBar = nullptr;
    }

    delete m_xmlEditorFactory;
}

void ScxmlEditorData::fullInit()
{
    // Create widget-stack, toolbar, mainToolbar and whole design-mode widget
    m_widgetStack = new ScxmlEditorStack;
    m_widgetToolBar = new QToolBar;
    m_mainToolBar = createMainToolBar();
    m_modeWidget = createModeWidget();

    // Create undo/redo group/actions
    m_undoGroup = new QUndoGroup(m_widgetToolBar);
    m_undoAction = m_undoGroup->createUndoAction(m_widgetToolBar);
    m_undoAction->setIcon(Utils::Icons::UNDO_TOOLBAR.icon());
    m_undoAction->setToolTip(Tr::tr("Undo (Ctrl + Z)"));

    m_redoAction = m_undoGroup->createRedoAction(m_widgetToolBar);
    m_redoAction->setIcon(Utils::Icons::REDO_TOOLBAR.icon());
    m_redoAction->setToolTip(Tr::tr("Redo (Ctrl + Y)"));

    ActionManager::registerAction(m_undoAction, Core::Constants::UNDO, m_contexts);
    ActionManager::registerAction(m_redoAction, Core::Constants::REDO, m_contexts);

    Context scxmlContexts = m_contexts;
    scxmlContexts.add(Core::Constants::C_EDITORMANAGER);
    IContext::attach(m_modeWidget, scxmlContexts);

    DesignMode::registerDesignWidget(
        "ScxmlEditor", m_modeWidget, QStringList(Utils::Constants::SCXML_MIMETYPE), m_contexts);
}

IEditor *ScxmlEditorData::createEditor()
{
    auto designWidget = new MainWidget;
    IEditor * const xmlEditor = m_xmlEditorFactory->create(designWidget);

    m_undoGroup->addStack(designWidget->undoStack());
    m_widgetStack->add(xmlEditor, designWidget);
    m_mainToolBar->addEditor(xmlEditor);

    if (xmlEditor) {
        Utils::InfoBarEntry info(Id(Constants::INFO_READ_ONLY),
                                 Tr::tr("This file can only be edited in <b>Design</b> mode."));
        info.addCustomButton(Tr::tr("Switch Mode"),
                             [] { ModeManager::activateMode(Core::Constants::MODE_DESIGN); });
        xmlEditor->document()->infoBar()->addInfo(info);
    }

    return xmlEditor;
}

void ScxmlEditorData::updateToolBar()
{
    auto designWidget = static_cast<MainWidget*>(m_widgetStack->currentWidget());
    if (designWidget && m_widgetToolBar) {
        m_undoGroup->setActiveStack(designWidget->undoStack());
        m_widgetToolBar->clear();
        m_widgetToolBar->addAction(m_undoAction);
        m_widgetToolBar->addAction(m_redoAction);
        m_widgetToolBar->addSeparator();
        m_widgetToolBar->addAction(designWidget->action(ActionCopy));
        m_widgetToolBar->addAction(designWidget->action(ActionCut));
        m_widgetToolBar->addAction(designWidget->action(ActionPaste));
        m_widgetToolBar->addAction(designWidget->action(ActionScreenshot));
        m_widgetToolBar->addAction(designWidget->action(ActionExportToImage));
        m_widgetToolBar->addAction(designWidget->action(ActionFullNamespace));
        m_widgetToolBar->addSeparator();
        m_widgetToolBar->addAction(designWidget->action(ActionZoomIn));
        m_widgetToolBar->addAction(designWidget->action(ActionZoomOut));
        m_widgetToolBar->addAction(designWidget->action(ActionPan));
        m_widgetToolBar->addAction(designWidget->action(ActionFitToView));
        m_widgetToolBar->addSeparator();
        m_widgetToolBar->addWidget(designWidget->toolButton(ToolButtonAdjustment));
        m_widgetToolBar->addWidget(designWidget->toolButton(ToolButtonAlignment));
        m_widgetToolBar->addWidget(designWidget->toolButton(ToolButtonStateColor));
        m_widgetToolBar->addWidget(designWidget->toolButton(ToolButtonFontColor));
        m_widgetToolBar->addWidget(designWidget->toolButton(ToolButtonColorTheme));
        m_widgetToolBar->addSeparator();
        m_widgetToolBar->addAction(designWidget->action(ActionMagnifier));
        m_widgetToolBar->addAction(designWidget->action(ActionNavigator));
        m_widgetToolBar->addSeparator();
        m_widgetToolBar->addAction(designWidget->action(ActionStatistics));
    }
}

EditorToolBar *ScxmlEditorData::createMainToolBar()
{
    auto toolBar = new EditorToolBar;
    toolBar->setToolbarCreationFlags(EditorToolBar::FlagsStandalone);
    toolBar->setNavigationVisible(false);
    toolBar->addCenterToolBar(m_widgetToolBar);

    return toolBar;
}

QWidget *ScxmlEditorData::createModeWidget()
{
    auto widget = new QWidget;

    widget->setObjectName("ScxmlEditorDesignModeWidget");
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_mainToolBar);
    // Avoid mode switch to 'Edit' mode when the application started by
    // 'Run' in 'Design' mode emits output.
    auto splitter = new MiniSplitter(Qt::Vertical);
    splitter->addWidget(m_widgetStack);
    auto outputPane = new OutputPanePlaceHolder(Core::Constants::MODE_DESIGN, splitter);
    outputPane->setObjectName("DesignerOutputPanePlaceHolder");
    splitter->addWidget(outputPane);
    layout->addWidget(splitter);
    widget->setLayout(layout);

    return widget;
}

class ScxmlEditorFactory final : public QObject, public Core::IEditorFactory
{
public:
    ScxmlEditorFactory(QObject *guard)
        : QObject(guard)
    {
        setId(Constants::K_SCXML_EDITOR_ID);
        setDisplayName(Tr::tr("SCXML Editor"));
        addMimeType(Utils::Constants::SCXML_MIMETYPE);

        Utils::FileIconProvider::registerIconOverlayForSuffix(":/projectexplorer/images/fileoverlay_scxml.png", "scxml");

        setEditorCreator([this] {
            if (!m_editorData) {
                m_editorData = new ScxmlEditorData;
                QGuiApplication::setOverrideCursor(Qt::WaitCursor);
                m_editorData->fullInit();
                QGuiApplication::restoreOverrideCursor();
            }
            return m_editorData->createEditor();
        });
    }
    ~ScxmlEditorFactory() final
    {
        delete m_editorData;
    }

private:
    ScxmlEditorData *m_editorData = nullptr;
};

void setupScxmlEditor(QObject *guard)
{
    (void) new ScxmlEditorFactory(guard);
}

#ifdef WITH_TESTS

class TextViewTest final : public QObject
{
    Q_OBJECT

private slots:
    // The text view of a state chart, shown in Edit mode: the chart's XML,
    // read-only, highlighted, drawn by the Qt Quick text editor; one chart
    // beside each view, so not to be split. And reloaded from disk through
    // the chart, which is the document's own doing.
    void testTheXmlViewIsTheQuickEditorAndReloadsThroughTheChart()
    {
        Utils::TemporaryDirectory dir("scxml-xml-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("chart.scxml");
        // Not a raw string: moc takes the "//" in the namespace URL for a
        // comment, loses the rest of the line, and then finds no class in
        // the file.
        const auto chart = [](const QString &state) {
            return QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<scxml xmlns=\"http://www.w3.org/2005/07/scxml\" version=\"1.0\""
                           " initial=\"%1\">\n"
                           "  <state id=\"%1\"/>\n"
                           "</scxml>\n").arg(state).toUtf8();
        };
        QVERIFY(file.writeFileContents(chart("first")));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QCOMPARE(editor->document()->id(), Id(Constants::K_SCXML_EDITOR_ID));

        QVERIFY2(!qobject_cast<TextEditor::BaseTextEditor *>(editor),
                 "the XML view is still the widget editor");
        QVERIFY2(Utils::anyOf(editor->widget()->findChildren<QWidget *>(),
                              [](QWidget *part) { return part->inherits("QQuickWidget"); }),
                 "the XML view is not drawn by a Qt Quick scene");
        QVERIFY2(!editor->duplicateSupported(),
                 "the XML view could be split, and the second view would have no chart");

        auto * const document = qobject_cast<ScxmlEditorDocument *>(editor->document());
        QVERIFY2(document, "the view's document is not the chart's");
        // The text is written from the chart on the way into Edit mode; a
        // chart opens in Design mode.
        QCOMPARE(ModeManager::currentModeId(), Id(Core::Constants::MODE_DESIGN));
        ModeManager::activateMode(Core::Constants::MODE_EDIT);
        QVERIFY2(document->plainText().contains("first"), "the chart's XML never reached the text");
        QVERIFY2(document->syntaxHighlighter(), "the XML is shown unhighlighted");

        // Typed at the view, which must refuse it. Delivered straight to
        // where keys go, so this is about read-only and not about focus.
        const QString before = document->plainText();
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);
        QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QCoreApplication::sendEvent(target, &typed);
        QVERIFY2(document->plainText() == before, "the XML view took a keystroke");

        // Changed on disk and reloaded: through the chart, into the text.
        QVERIFY(file.writeFileContents(chart("second")));
        const Result<> reloaded = document->reload(IDocument::FlagReload, IDocument::TypeContents);
        if (!reloaded)
            QFAIL(qPrintable(reloaded.error()));
        QVERIFY2(document->plainText().contains("second"), "the reload did not reach the text");
        QVERIFY2(!document->plainText().contains("first"), "the reload left the old chart in the text");
    }
};

QObject *createTextViewTest()
{
    return new TextViewTest;
}

#endif // WITH_TESTS

} // ScxmlEditor::Internal

#ifdef WITH_TESTS
#include "scxmleditor.moc"
#endif
