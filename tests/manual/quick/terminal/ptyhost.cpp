// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "ptyhost.h"

#include <QSocketNotifier>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <util.h>

PtyHost::PtyHost(QObject *parent)
    : QObject(parent)
{}

PtyHost::~PtyHost()
{
    if (m_pid > 0)
        ::kill(m_pid, SIGHUP);
    if (m_masterFd >= 0)
        ::close(m_masterFd);
}

bool PtyHost::start(const QSize &gridSize)
{
    struct winsize ws = {};
    ws.ws_col = gridSize.width();
    ws.ws_row = gridSize.height();

    m_pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (m_pid < 0)
        return false;

    if (m_pid == 0) {
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("LANG", "en_US.UTF-8", 1);
        ::setenv("LC_ALL", "en_US.UTF-8", 1);
        ::execl("/bin/zsh", "zsh", "-f", "-i", nullptr);
        _exit(1);
    }

    ::fcntl(m_masterFd, F_SETFL, ::fcntl(m_masterFd, F_GETFL) | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PtyHost::readAvailable);
    return true;
}

void PtyHost::readAvailable()
{
    char buffer[65536];
    while (true) {
        const ssize_t n = ::read(m_masterFd, buffer, sizeof buffer);
        if (n > 0) {
            emit dataAvailable(QByteArray(buffer, n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        // n == 0 or hard error: shell exited
        m_notifier->setEnabled(false);
        emit finished();
        return;
    }
}

qint64 PtyHost::write(const QByteArray &data)
{
    const ssize_t n = ::write(m_masterFd, data.constData(), data.size());
    if (n < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    return n;
}

void PtyHost::resize(const QSize &gridSize)
{
    if (m_masterFd < 0)
        return;
    struct winsize ws = {};
    ws.ws_col = gridSize.width();
    ws.ws_row = gridSize.height();
    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
}
