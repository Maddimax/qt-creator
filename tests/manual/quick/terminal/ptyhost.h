// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QByteArray>
#include <QObject>
#include <QSize>

#include <sys/types.h>

class QSocketNotifier;

// Minimal real-shell host: forkpty + QSocketNotifier. Creator uses
// Utils::Process with ptyqt instead; this stands in for it.
class PtyHost : public QObject
{
    Q_OBJECT
public:
    explicit PtyHost(QObject *parent = nullptr);
    ~PtyHost() override;

    bool start(const QSize &gridSize);
    qint64 write(const QByteArray &data);
    void resize(const QSize &gridSize);

    bool echoOff() const; // true while the foreground process hides input
    int resizeCount() const { return m_resizeCount; }
    QSize lastGrid() const { return m_lastGrid; }

signals:
    void dataAvailable(const QByteArray &data);
    void finished();

private:
    void readAvailable();

    int m_masterFd = -1;
    pid_t m_pid = -1;
    QSocketNotifier *m_notifier = nullptr;
    int m_resizeCount = 0;
    QSize m_lastGrid;
};
