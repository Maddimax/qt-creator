// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "openeditorsview.h"

#include "editormanager.h"
#include "opendocumentslist.h"
#include "../actionmanager/command.h"
#include "../coreplugintr.h"
#include "../inavigationwidgetfactory.h"

#include <utils/qtcassert.h>

#include <QMenu>


using namespace Utils;

namespace Core::Internal {

// OpenEditorsViewFactory

class OpenEditorsViewFactory final : public INavigationWidgetFactory
{
public:
    OpenEditorsViewFactory()
    {
        setId("Open Documents");
        setDisplayName(Tr::tr("Open Documents"));
        setActivationSequence(QKeySequence(useMacShortcuts ? Tr::tr("Meta+O") : Tr::tr("Alt+O")));
        setPriority(200);
    }

    NavigationView createWidget() final
    {
        // The Qt Quick view is the sidebar. Everything the tree view did it
        // does: the rows with their icon, version control colour and tool
        // tip, single-click and Return to open, Delete, Backspace and the
        // middle button to close, the right-click menu from the same
        // EditorManager call, the drag that carries the file, walking with
        // the arrows without opening anything, and the row following
        // whichever document is open.
        auto * const list = new OpenDocumentsList;
        QWidget * const view = createQmlView(
            QUrl("qrc:/qt/qml/QtCreator/Core/OpenDocumentsView.qml"), list);
        QTC_ASSERT(view, delete list; return {});
        return {view, {}};
    }
};

void createOpenEditorsViewFactory()
{
    static OpenEditorsViewFactory theOpenEditorsViewFactory;
}

} // Core::Internal
