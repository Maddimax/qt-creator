// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlpreviewplugin_test.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <utils/algorithm.h>
#include <utils/proxyaction.h>
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QVariant>

typedef QByteArray (*TestFileLoader)(const QString &, bool *);
typedef void (*TestFpsHandler)(quint16[8]);

Q_DECLARE_METATYPE(TestFileLoader)
Q_DECLARE_METATYPE(TestFpsHandler)

namespace QmlPreview {

class QmlPreviewPluginTest : public QObject
{
    Q_OBJECT

private slots:
    void testFileLoaderProperty();
    void testZoomFactorProperty();
    void testFpsHandlerProperty();
    void testTheRunPreviewActionIsOnEitherViewsToolBar_data();
    void testTheRunPreviewActionIsOnEitherViewsToolBar();
};

static ExtensionSystem::IPlugin *getPlugin()
{
    using namespace ExtensionSystem;
    const PluginSpec *spec = PluginManager::specById("qmlpreview");
    return spec ? spec->plugin() : nullptr;
}

void QmlPreviewPluginTest::testFileLoaderProperty()
{
    ExtensionSystem::IPlugin *plugin = getPlugin();
    QVERIFY(plugin);

    QVariant var = plugin->property("fileLoader");
    TestFileLoader loader = qvariant_cast<TestFileLoader>(var);
    bool success = true;
    loader(QString("testzzzztestzzzztest"), &success);
    QVERIFY(!success);
}

void QmlPreviewPluginTest::testZoomFactorProperty()
{
    ExtensionSystem::IPlugin *plugin = getPlugin();
    QVERIFY(plugin);

    QSignalSpy spy(plugin, SIGNAL(zoomFactorChanged(float)));

    QCOMPARE(qvariant_cast<float>(plugin->property("zoomFactor")), -1.0f);
    plugin->setProperty("zoomFactor", 2.0f);
    QCOMPARE(qvariant_cast<float>(plugin->property("zoomFactor")), 2.0f);
    plugin->setProperty("zoomFactor", 1.0f);
    QCOMPARE(qvariant_cast<float>(plugin->property("zoomFactor")), 1.0f);
    QCOMPARE(spy.count(), 2);
}

void QmlPreviewPluginTest::testFpsHandlerProperty()
{
    ExtensionSystem::IPlugin *plugin = getPlugin();
    QVERIFY(plugin);

    QVariant var = plugin->property("fpsHandler");
    TestFpsHandler handler = qvariant_cast<TestFpsHandler>(var);
    QVERIFY(handler);
    quint16 stats[] = { 43, 44, 45, 46, 47, 48, 49, 50 };
    handler(stats);
}

// The run-preview button of a QML file's tool bar was put on the widget
// editor's QToolBar, so a QML file in the Qt Quick editor - which is where it
// opens - had no preview button.
void QmlPreviewPluginTest::testTheRunPreviewActionIsOnEitherViewsToolBar_data()
{
    QTest::addColumn<bool>("quick");
    QTest::newRow("widget") << false;
    QTest::newRow("quick") << true;
}

void QmlPreviewPluginTest::testTheRunPreviewActionIsOnEitherViewsToolBar()
{
    QFETCH(bool, quick);

    Utils::TemporaryDirectory dir("qml-preview-tool-bar-action");
    QVERIFY(dir.isValid());
    const Utils::FilePath file = dir.filePath("Preview.qml");
    QVERIFY(file.writeFileContents("import QtQuick\nItem {}\n"));

    TextEditor::TextEditorFactory * const factory
        = TextEditor::TextEditorFactory::preferredFactoryFor(file);
    QVERIFY2(factory, "no text editor factory claims a QML file");
    const bool wasQuick = factory->usesQuickEditor();
    const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
    factory->setUsesQuickEditor(quick);

    Core::IEditor * const editor = Core::EditorManager::openEditor(file, factory->id());
    QVERIFY2(editor, "the editor manager opened nothing");
    const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
    QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);

    const QList<QAction *> actions = [editor]() -> QList<QAction *> {
        if (auto * const widget = TextEditor::TextEditorWidget::fromEditor(editor))
            return widget->toolBar()->actions();
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        return document ? document->toolBarActions() : QList<QAction *>();
    }();
    Core::Command * const command = Core::ActionManager::command("QmlPreview.RunPreview");
    QVERIFY(command && command->action());
    // A proxy of the command's own action, with the preview icon.
    QAction * const proxy = Utils::findOrDefault(actions, [command](QAction *action) {
        auto * const proxy = qobject_cast<Utils::ProxyAction *>(action);
        return proxy && proxy->action() == command->action();
    });
    QVERIFY2(proxy, "the run-preview action is not on the tool bar in this view");
    // Whose it is: it goes with the editor.
    QCOMPARE(proxy->parent(), editor);
}

QObject *createQmlPreviewPluginTest()
{
    return new QmlPreviewPluginTest;
}

} // namespace QmlPreview

#include "qmlpreviewplugin_test.moc"
