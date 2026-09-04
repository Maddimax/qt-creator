// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"

#include <languageserverprotocol/lsptypes.h>
#include <utils/treemodel.h>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Core { class IEditor; }
namespace TextEditor { class ToolBarOutline; }
namespace Utils { class TreeViewComboBox; }

namespace LanguageClient {
class Client;

class LANGUAGECLIENT_EXPORT LanguageClientOutlineItem
    : public Utils::TypedTreeItem<LanguageClientOutlineItem>
{
public:
    enum ItemDataRoles {
        AnnotationRole = Qt::UserRole + 1,
    };

    LanguageClientOutlineItem() = default;
    LanguageClientOutlineItem(const LanguageServerProtocol::SymbolInformation &info);
    LanguageClientOutlineItem(Client *client, const LanguageServerProtocol::DocumentSymbol &info);

    LanguageServerProtocol::Range range() const { return m_range; }
    LanguageServerProtocol::Range selectionRange() const { return m_selectionRange; }
    LanguageServerProtocol::Position pos() const { return m_range.start(); }
    bool contains(const LanguageServerProtocol::Position &pos) const {
        return m_range.contains(pos);
    }

    bool valid() const { return m_range.isValid(); }

protected:
    // TreeItem interface
    QVariant data(int column, int role) const override;
    Qt::ItemFlags flags(int column) const override;

    QString name() const { return m_name; }
    QString detail() const { return m_detail; }
    int type() const { return m_type; }
    QList<LanguageServerProtocol::SymbolTag> tags() const { return m_tags; }

private:
    QString m_name;
    QString m_detail;
    LanguageServerProtocol::Range m_range;
    LanguageServerProtocol::Range m_selectionRange;
    int m_type = -1;
    QList<LanguageServerProtocol::SymbolTag> m_tags;
};

// The outline for a file a language server knows about, or nullptr where it
// offers none. Parented to \a editor, which is where a view that draws its own
// toolbar looks for one. Its widget() is the combo the widget editor installs.
TextEditor::ToolBarOutline *createToolBarOutline(Client *client, Core::IEditor *editor);
// The combo \a outline fills, for a view that installs a widget rather than
// drawing the outline itself. Whoever installs it takes it over.
QWidget *outlineWidget(TextEditor::ToolBarOutline *outline);

void setupLanguageClientOutline();

} // namespace LanguageClient
