// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include <abstractview.h>
#include <qmldesignercomponents_global.h>


namespace Core { class IEditor; }

namespace QmlDesigner {

class TextEditorWidget;

class QMLDESIGNERCOMPONENTS_EXPORT TextEditorView : public AbstractView
{
    Q_OBJECT

public:
    TextEditorView(ExternalDependenciesInterface &externalDependencies);
    ~TextEditorView() override;

    // AbstractView
    void modelAttached(Model *model) override;
    void modelAboutToBeDetached(Model *model) override;

    void selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                              const QList<ModelNode> &lastSelectedNodeList) override;
    void customNotification(const AbstractView *view, const QString &identifier, const QList<ModelNode> &nodeList, const QList<QVariant> &data) override;
    void documentMessagesChanged(const QList<DocumentMessage> &errors, const QList<DocumentMessage> &warnings) override;

    // TextEditorView
    bool hasWidget() const override { return true; }
    WidgetInfo widgetInfo() override;

    // The editor this view shows: a duplicate of the design document's, in
    // whichever view that one opened in.
    Core::IEditor *editor();

    void gotoCursorPosition(int line, int column);

    void reformatFile();

    void jumpToModelNode(const ModelNode &modelNode);

private:
    void createTextEditor();

    QPointer<TextEditorWidget> m_widget;
    QMetaObject::Connection m_designDocumentConnection;
    bool m_errorState = false;
};

#ifdef WITH_TESTS
QObject *createTextEditorViewTest();
#endif

} // namespace QmlDesigner
