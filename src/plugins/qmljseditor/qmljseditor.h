// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljseditor_global.h"

#include <qmljs/parser/qmljsast_p.h>
#include <qmljstools/qmljssemanticinfo.h>
#include <texteditor/texteditor.h>
#include <texteditor/quickfix.h>
#include <utils/uncommentselection.h>

#include <QModelIndex>
#include <QTimer>

namespace Utils { class TreeViewComboBox; }

namespace QmlJS { class ModelManagerInterface; }

namespace QmlJSEditor {

class QmlJSEditorDocument;
class QuickToolBar;
class FindReferences;

// Where the name under the cursor comes from - an import, a property, an id or
// a type. Free rather than a widget virtual, so that a view which is not a
// TextEditorWidget can follow it too; qbs files reuse it.
QMLJSEDITOR_EXPORT void findQmlJSLinkAt(TextEditor::TextDocument *document,
                                        const QTextCursor &cursor,
                                        const Utils::LinkHandler &processLinkCallback,
                                        bool resolveTarget,
                                        bool inNextSplit);

class QMLJSEDITOR_EXPORT QdsSettings : public QObject
{
    Q_OBJECT

public:
    QdsSettings();

    void setQdsSettingVisible(bool visible);
    Utils::FilePath qdsCommand();

signals:
    void changed();
};

QMLJSEDITOR_EXPORT QdsSettings &qdsSettings();

class QMLJSEDITOR_EXPORT QmlJSEditorWidget : public TextEditor::TextEditorWidget
{
    Q_OBJECT

public:
    QmlJSEditorWidget();

    void finalizeInitialization() override;
    void restoreState(const QByteArray &state) override;

    QmlJSEditorDocument *qmlJsEditorDocument() const;

    QModelIndex outlineModelIndex();
    void updateOutlineIndexNow();

    void findUsages() override;
    void renameSymbolUnderCursor() override;
    void showContextPane();

signals:
    void selectedElementsChanged(QList<QmlJS::AST::UiObjectMember*> offsets,
                                 const QString &wordAtCursor);
private:

    void jumpToOutlineElement(int index);
    void updateContextPane();
    void showTextMarker();


    void semanticInfoUpdated(const QmlJSTools::SemanticInfo &semanticInfo);

    void foldAuxiliaryData();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool event(QEvent *e) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void createToolBar();
    void updateOutline(QWidget *newOutline);
    QString foldReplacementText(const QTextBlock &block) const override;

private:
    void setSelectedElements();

    bool hideContextPane();

    QTimer m_updateOutlineIndexTimer;
    QTimer m_contextPaneTimer;
    Utils::TreeViewComboBox *m_outlineCombo = nullptr;
    QModelIndex m_outlineModelIndex;

    QuickToolBar *m_contextPane = nullptr;
    int m_oldCursorPosition = -1;

    FindReferences *m_findReferences;
};


class QMLJSEDITOR_EXPORT QmlJSEditorFactory : public TextEditor::TextEditorFactory
{
public:
    QmlJSEditorFactory();
    QmlJSEditorFactory(Utils::Id id);

    static void decorateEditor(TextEditor::TextEditorWidget *editor);
};

} // namespace QmlJSEditor
