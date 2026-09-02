// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cpprefactoringchanges.h"

#include <QtTaskTree/QSingleTaskTreeRunner>

#include <QPointer>
#include <QString>
#include <QTextCursor>
#include <QTimer>

namespace Core { class IEditor; }

namespace CppEditor {
class CppEditorDocument;
class CppEditorWidget;

namespace Internal {
class FunctionDeclDefLink;

class FunctionDeclDefLinkFinder : public QObject
{
    Q_OBJECT
public:
    FunctionDeclDefLinkFinder(QObject *parent = nullptr);

    void startFindLinkAt(QTextCursor cursor,
                    const CPlusPlus::Document::Ptr &doc,
                    const CPlusPlus::Snapshot &snapshot);

    QTextCursor scannedSelection() const;

signals:
    void foundLink(std::shared_ptr<FunctionDeclDefLink> link);

private:
    QTextCursor m_scannedSelection;
    QTextCursor m_nameSelection;
    QtTaskTree::QSingleTaskTreeRunner m_taskTreeRunner;
};

// Watching for a function whose declaration and definition have drifted
// apart, offering to bring the other one along, and doing it when the reader
// says so. One per view, because it follows a caret; a widget builds its own
// and the plugin builds one for every other view.
class CppDeclDefLinkController : public QObject
{
    Q_OBJECT

public:
    explicit CppDeclDefLinkController(CppEditorWidget *widget);
    explicit CppDeclDefLinkController(Core::IEditor *editor);

    std::shared_ptr<FunctionDeclDefLink> link() const { return m_link; }

    // The caret moved or the text changed: look again, after a moment.
    void scheduleUpdate();
    // Look now.
    void updateNow();
    void apply(bool jumpToMatch);
    void abort();

    // The view this belongs to. Found rather than held for a widget, which is
    // built before the editor that shows it.
    Core::IEditor *editor() const;

private:
    void onFound(std::shared_ptr<FunctionDeclDefLink> link);

    CppEditorWidget * const m_widget = nullptr;
    const QPointer<Core::IEditor> m_editor;
    FunctionDeclDefLinkFinder * const m_finder;
    std::shared_ptr<FunctionDeclDefLink> m_link;
    QTimer m_updateTimer;
};

// The one \a editor has, wherever it keeps it: a widget owns its controller,
// and a view that is not one has it parented to the editor.
CppDeclDefLinkController *declDefLinkControllerFor(Core::IEditor *editor);

// Apply what the link in \a editor offers, which is what its marker does.
void applyDeclDefLinkChangesIn(Core::IEditor *editor, bool jumpToMatch);

#ifdef WITH_TESTS
QObject *createDeclDefLinkTest();
#endif

class FunctionDeclDefLink
{
    Q_DISABLE_COPY(FunctionDeclDefLink)
    FunctionDeclDefLink() = default;
public:
    bool isValid() const;
    bool isMarkerVisible() const;

    // The editor rather than the widget: what these need is the document to
    // change and somewhere to offer the marker, and any view has both.
    void apply(Core::IEditor *editor, bool jumpToMatch);
    void hideMarker(Core::IEditor *editor);
    void showMarker(Core::IEditor *editor);
    Utils::ChangeSet changes(const CPlusPlus::Snapshot &snapshot, int targetOffset = -1);

    QTextCursor linkSelection;

    // stored to allow aborting when the name is changed
    QTextCursor nameSelection;
    QString nameInitial;

    // The 'source' prefix denotes information about the original state
    // of the function before the user did any edits.
    CPlusPlus::Document::Ptr sourceDocument;
    CPlusPlus::Function *sourceFunction = nullptr;
    CPlusPlus::DeclarationAST *sourceDeclaration = nullptr;
    CPlusPlus::FunctionDeclaratorAST *sourceFunctionDeclarator = nullptr;

    // The 'target' prefix denotes information about the remote declaration matching
    // the 'source' declaration, where we will try to apply the user changes.
    // 1-based line and column
    int targetLine = 0;
    int targetColumn = 0;
    QString targetInitial;

    CppRefactoringFileConstPtr targetFile;
    CPlusPlus::Function *targetFunction = nullptr;
    CPlusPlus::DeclarationAST *targetDeclaration = nullptr;
    CPlusPlus::FunctionDeclaratorAST *targetFunctionDeclarator = nullptr;

private:
    QString normalizedInitialName() const;

    bool hasMarker = false;

    friend class FunctionDeclDefLinkFinder;
};

} // namespace Internal
} // namespace CppEditor
