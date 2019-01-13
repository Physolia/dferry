/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#include "localserver.h"

#include "icompletionlistener.h"
#include "localsocket.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

LocalServer::LocalServer(const std::string &socketFilePath)
   : m_listenFd(-1)
{
    const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    // don't let forks inherit the file descriptor - just in case
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un addr;
    addr.sun_family = PF_UNIX;
    bool ok = socketFilePath.length() + 1 <= sizeof(addr.sun_path);
    if (ok) {
        memcpy(addr.sun_path, socketFilePath.c_str(), socketFilePath.length() + 1);
    }

    if (!socketFilePath.empty() && socketFilePath[0] != '\0') {
        // not a so-called abstract socket (weird but useful Linux specialty)
        unlink(socketFilePath.c_str());
    }
    ok = ok && (bind(fd, (struct sockaddr *)&addr, sizeof(sa_family_t) + socketFilePath.length()) == 0);
    ok = ok && (::listen(fd, /* max queued incoming connections */ 64) == 0);

    if (ok) {
        m_listenFd = fd;
    } else {
        ::close(fd);
    }
}

LocalServer::~LocalServer()
{
    close();
}

IO::Status LocalServer::handleIoReady(IO::RW rw)
{
    if (rw != IO::RW::Read) {
        assert(false);
        return IO::Status::InternalError;
    }
    if (m_listenFd < 0) {
        return IO::Status::LocalClosed;
    }
    int connFd = -1;
    while (true) {
        connFd = accept(m_listenFd, nullptr, nullptr);
        if (connFd >= 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        // After listen() succeeded, the only possible errors are invalid parameters (we don't do that,
        // right?), EINTR, out of resource errors (which can be temporary), or aborted connection
        // attempt. Just give up on this connection attempt and stay in listening state.
        return IO::Status::OK;
    }
    fcntl(connFd, F_SETFD, FD_CLOEXEC);

    m_incomingConnections.push_back(new LocalSocket(connFd));
    if (m_newConnectionListener) {
        m_newConnectionListener->handleCompletion(this);
    }
    return IO::Status::OK;
}

bool LocalServer::isListening() const
{
    return m_listenFd >= 0;
}

void LocalServer::platformClose()
{
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
}

FileDescriptor LocalServer::fileDescriptor() const
{
    return m_listenFd;
}
