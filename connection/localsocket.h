/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef LOCALSOCKET_H
#define LOCALSOCKET_H

#include "iconnection.h"

#include <map>
#include <string>

class IConnectionListener;
struct SessionBusInfo;

class LocalSocket : public IConnection
{
public:
    // Connect to local socket at socketFilePath
    LocalSocket(const std::string &socketFilePath);
    // Use an already open file descriptor
    LocalSocket(int fd);

    ~LocalSocket();

    // pure virtuals from IConnection
    int write(chunk data) override;
    int availableBytesForReading() override;
    chunk read(byte *buffer, int maxSize) override;
    void close() override;
    bool isOpen() override;
    int fileDescriptor() const override;
    void notifyRead() override;
    // end IConnection

private:
    friend class IEventLoop;
    friend class IConnectionListener;

    LocalSocket(); // not implemented
    LocalSocket(const LocalSocket &); // not implemented, disable copying
    LocalSocket &operator=(const LocalSocket &); // dito

    int m_fd;
};

#endif // LOCALSOCKET_H
