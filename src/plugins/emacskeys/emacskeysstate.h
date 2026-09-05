// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <QPointer>

namespace Core { class IEditor; }

namespace EmacsKeys::Internal {

enum EmacsKeysAction {
    KeysAction3rdParty,
    KeysActionKillWord,
    KeysActionKillLine,
    KeysActionOther,
};

class EmacsKeysState : public QObject
{
public:
    EmacsKeysState(Core::IEditor *editor);
    ~EmacsKeysState() override;
    void setLastAction(EmacsKeysAction action);
    void beginOwnAction() { m_ignore3rdParty = true; }
    void endOwnAction(EmacsKeysAction action) {
        m_ignore3rdParty = false;
        m_lastAction = action;
    }
    EmacsKeysAction lastAction() const { return m_lastAction; }

    int mark() const { return m_mark; }
    void setMark(int mark) { m_mark = mark; }

private:
    void cursorPositionChanged();
    void textChanged();
    void selectionChanged();

    bool m_ignore3rdParty;
    int m_mark;
    EmacsKeysAction m_lastAction;
    // The view the reader is in, not a widget: these keys work in the Qt
    // Quick editor too, and a QPointer because an editor can go first.
    const QPointer<Core::IEditor> m_editor;
};

} // namespace EmacsKeys::Internal
