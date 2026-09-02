// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppoutlinemodel.h"

#include <QObject>

QT_BEGIN_NAMESPACE
class QAction;
class QSortFilterProxyModel;
class QTimer;
QT_END_NAMESPACE

namespace Core { class IEditor; }
namespace Utils { class TreeViewComboBox; }

namespace CppEditor {
class CppEditorDocument;
class CppEditorWidget;

namespace Internal {

class CppEditorOutline : public QObject
{
    Q_OBJECT

public:
    // The editor rather than the widget: what this needs from the view is
    // where the caret is and how to move it, which every view answers. The
    // combo it fills is still a widget - drawing it in a form is what is left.
    explicit CppEditorOutline(Core::IEditor *editor, CppEditorDocument *document);
    // For the widget, which is built before the editor that shows it - so the
    // editor is found when it is needed rather than held.
    explicit CppEditorOutline(CppEditorWidget *widget);

    QWidget *widget() const; // Must be deleted by client.

public slots:
    void updateIndex();

private:
    void build();
    void updateNow();
    void updateIndexNow();
    void updateToolTip();
    void gotoSymbolInEditor();

    CppEditorOutline();

    bool isSorted() const;

    OutlineModel *m_model = nullptr; // Not owned

    Core::IEditor *editor() const;
    CppEditorDocument *document() const;

    CppEditorWidget * const m_widget = nullptr;
    Core::IEditor * const m_editor = nullptr;
    CppEditorDocument * const m_document = nullptr;

    Utils::TreeViewComboBox *m_combo = nullptr; // Not owned
    QSortFilterProxyModel *m_proxyModel = nullptr;
    QAction *m_sortAction = nullptr;
    QTimer *m_updateIndexTimer = nullptr;
};

} // namespace Internal
} // namespace CppEditor
