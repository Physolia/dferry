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

#include "argumentlist.h"
#include "connectioninfo.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "transceiver.h"

#include "../testutil.h"

#include <atomic>
#include <iostream>
#include <thread>

static const char *echoPath = "/echo";
// make the name "fairly unique" because the interface name is our only protection against replying
// to the wrong message
static const char *echoInterface = "org.example_fb39a8dbd0aa66d2.echo";
static const char *echoMethod = "echo";

static const char *pingPayload = "-> J. Random PING";
static const char *pongPayload = "<- J. Random Pong";

class PingResponder : public IMessageReceiver
{
public:
    Transceiver *m_transceiver;

    void spontaneousMessageReceived(Message ping) override
    {
        if (ping.interface() != echoInterface) {
            // This is not the ping... it is probably still something from connection setup.
            // We can possibly receive many things here that we were not expecting.
            return;
        }
        {
            ArgumentList args = ping.argumentList();
            ArgumentList::Reader reader(args);
            cstring payload = reader.readString();
            TEST(!reader.error().isError());
            TEST(reader.isFinished());
            std::cout << "we have ping with payload: " << reinterpret_cast<char *>(payload.begin) << std::endl;
        }

        {
            Message pong = Message::createReplyTo(ping);
            ArgumentList args;
            ArgumentList::Writer writer(&args);
            writer.writeString(pongPayload);
            writer.finish();
            pong.setArgumentList(args);

            Error replyError = m_transceiver->sendNoReply(std::move(pong));
            TEST(!replyError.isError());

            //m_transceiver->eventDispatcher()->poll();
            m_transceiver->eventDispatcher()->interrupt();
        }
    }
};

static void pongThreadRun(Transceiver::CommRef primary)
{
    // open a Transceiver "slaved" to the other Transceiver - it has its own event loop, but uses
    // the same connection as the other Transceiver
    std::cout << " Other thread starting!\n";
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, std::move(primary));

    PingResponder responder;
    responder.m_transceiver = &trans;

    trans.setSpontaneousMessageReceiver(&responder);

    while (eventDispatcher.poll()) {
        // receive ping message
        // send pong message
    }
}

class PongReceiver : public IMessageReceiver
{
public:
    void pendingReplyFinished(PendingReply *pongReply) override
    {
        Message pong = pongReply->takeReply();

        ArgumentList args = pong.argumentList();
        ArgumentList::Reader reader(args);
        cstring payload = reader.readString();
        TEST(!reader.error().isError());
        TEST(reader.isFinished());
        // TEST(payload == pongPayload);
    }
};

static void testPingPong()
{
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, ConnectionInfo::Bus::Session);

    std::thread pongThread(pongThreadRun, trans.createCommRef());

    // send ping message to other thread
    Message ping = Message::createCall(echoPath, echoInterface, echoMethod);
    ArgumentList args;
    ArgumentList::Writer writer(&args);
    writer.writeString(pingPayload);
    writer.finish();
    ping.setArgumentList(args);

    // finish creating the connection
    while (trans.uniqueName().empty()) {
        std::cout << ".";
        eventDispatcher.poll();
    }

    std::cout << "we have connection! " << trans.uniqueName() << "\n";

    ping.setDestination(trans.uniqueName());
    PendingReply pongReply = trans.send(std::move(ping));

    PongReceiver pongReceiver;
    pongReply.setReceiver(&pongReceiver);

    while (!pongReply.isFinished()) {
        eventDispatcher.poll();
    }
    TEST(pongReply.hasNonErrorReply());

    std::cout << "we have pong!\n";

    pongThread.join();
}

class TimeoutReceiver : public IMessageReceiver
{
public:
    void pendingReplyFinished(PendingReply *reply) override
    {
        TEST(reply->isFinished());
        TEST(!reply->hasNonErrorReply());
        TEST(reply->error().code() == Error::Timeout);
        std::cout << "We HAVE timed out.\n";
    }
};

static void timeoutThreadRun(Transceiver::CommRef primary, std::atomic<bool> *done)
{
    // open a Transceiver "slaved" to the other Transceiver - it has its own event loop, but uses
    // the same connection as the other Transceiver
    std::cout << " Other thread starting!\n";
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, std::move(primary));
    while (!trans.uniqueName().length()) {
        eventDispatcher.poll();
    }

    Message notRepliedTo = Message::createCall(echoPath, echoInterface, echoMethod);

    notRepliedTo.setDestination(trans.uniqueName());
    PendingReply neverGonnaReply = trans.send(std::move(notRepliedTo), 200);
    TimeoutReceiver timeoutReceiver;
    neverGonnaReply.setReceiver(&timeoutReceiver);

    while (!neverGonnaReply.isFinished()) {
        eventDispatcher.poll();
    }
    *done = true;
}

static void testThreadedTimeout()
{
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, ConnectionInfo::Bus::Session);

    std::atomic<bool> done(false);
    std::thread timeoutThread(timeoutThreadRun, trans.createCommRef(), &done);

    while (!done) {
        eventDispatcher.poll();
    }

    timeoutThread.join();
}


// more things to test:
// - (do we want to do this, and if so here??) blocking on a reply through other thread's connection
// - ping-pong with several messages queued - every message should arrive exactly once and messages
//   should arrive in sending order (can use serials for that as simplificitaion)

int main(int argc, char *argv[])
{
    testPingPong();
    testThreadedTimeout();
    std::cout << "Passed!\n";
}
