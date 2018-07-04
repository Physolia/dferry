/*
   Copyright (C) 2017 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "server.h"

#include "connectaddress.h"
#include "connection.h"
#include "icompletionlistener.h"
#include "inewconnectionlistener.h"
#include "iserver.h"
#include "itransport.h"

#include <cassert>

#include <iostream>

class ServerPrivate : public ICompletionListener
{
public:
    void handleCompletion(void *transportServer) override;

    ConnectAddress listenAddress;
    ConnectAddress concreteAddress;
    Server *server;
    INewConnectionListener *newConnectionListener;
    IServer *transportServer;
};

Server::Server(EventDispatcher *dispatcher, const ConnectAddress &listenAddress)
   : d(new ServerPrivate)
{
#if 0
    if (ca.bus() == ConnectAddress::Bus::None || ca.socketType() == ConnectAddress::AddressType::None ||
        ca.role() == ConnectAddress::Role::None ||
        (ca.role() != ConnectAddress::Role::Server && ca.isServerOnly())) {
        cerr << "\nConnection: connection constructor Exit A\n\n";
        return;
    }
#endif
    d->listenAddress = listenAddress;
    d->server = this;
    d->newConnectionListener = nullptr;
    d->transportServer = IServer::create(listenAddress, &d->concreteAddress);
    if (d->transportServer) {
        d->transportServer->setEventDispatcher(dispatcher);
        d->transportServer->setNewConnectionListener(d);
    }
}

Server::~Server()
{
    delete d->transportServer;

    delete d;
    d = nullptr;
}

void Server::setNewConnectionListener(INewConnectionListener *listener)
{
    d->newConnectionListener = listener;
}

INewConnectionListener *Server::newConnectionListener() const
{
    return d->newConnectionListener;
}

Connection *Server::takeNextClient()
{
    // TODO proper error handling / propagation
    if (!d->transportServer) {
        return nullptr;
    }
    ITransport *newTransport = d->transportServer->takeNextClient();
    if (!newTransport) {
        return nullptr;
    }
    newTransport->setEventDispatcher(d->transportServer->eventDispatcher());
    return new Connection(newTransport, d->concreteAddress);
}

bool Server::isListening() const
{
    return d->transportServer ? d->transportServer->isListening() : false;
}

ConnectAddress Server::listenAddress() const
{
    return d->listenAddress;
}

ConnectAddress Server::concreteAddress() const
{
    return d->concreteAddress;
}

void ServerPrivate::handleCompletion(void *task)
{
    assert(task == transportServer);
    (void) task;
    if (newConnectionListener) {
        newConnectionListener->handleNewConnection(server);
    }
}
