// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>
#include <QStringList>
#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QObject;
class QPushButton;
QT_END_NAMESPACE

namespace Core::Internal {

class SessionSettings;
class SessionNameSettings;

// What is wrong with \a name as a session name, or nothing when it is a good
// one. A session is a file, so a name that cannot be one is refused.
QString sessionNameIssue(const QString &name, const QStringList &existing);

// \a base with " (2)", " (3)"... until one is not taken.
QString unusedSessionName(const QString &base, const QStringList &existing);
#ifdef WITH_TESTS
class SessionDialogTest;
QObject *createSessionDialogTest();
#endif

class SessionDialog : public QDialog
{
public:
    SessionDialog();
    ~SessionDialog() override;

    void setAutoLoadSession(bool);
    bool autoLoadSession() const;

private:
    void updateActions();
    QString currentSession() const;
    QStringList selectedSessions() const;
    void switchToCurrentSession();
    void selectSession(const QString &sessionName);

    const std::unique_ptr<SessionSettings> m_settings;

#ifdef WITH_TESTS
    friend class SessionDialogTest;
#endif
};

class SessionNameInputDialog : public QDialog
{
public:
    SessionNameInputDialog();
    ~SessionNameInputDialog() override;

    void setActionText(const QString &actionText, const QString &openActionText);
    void setValue(const QString &value);
    QString value() const;
    bool isSwitchToRequested() const;

private:
    const std::unique_ptr<SessionNameSettings> m_settings;
    QPushButton *m_switchToButton = nullptr;
    QPushButton *m_okButton = nullptr;
    bool m_usedSwitchTo = false;
};

} // namespace Core::Internal
