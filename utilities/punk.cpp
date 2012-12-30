#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

#include <iostream>

using namespace std;

void printArguments(ArgumentList::ReadCursor reader )
{
    // TODO - this is well-known though
}

void fillHelloMessage(Message *hello)
{
    hello->setType(Message::MethodCallMessage);
    hello->setDestination(string("org.freedesktop.DBus"));
    hello->setInterface(string("org.freedesktop.DBus"));
    hello->setPath(string("/org/freedesktop/DBus"));
    hello->setMethod(string("Hello"));
}

class ReplyPrinter : public ITransceiverClient
{
    // reimplemented from ITransceiverClient
    virtual void messageReceived(Message *m);
};

void ReplyPrinter::messageReceived(Message *m)
{
    cout << "Reply, pretty-printed:\n" << m->argumentList().prettyPrint();
}

int main(int argc, char *argv[])
{

    EpollEventDispatcher dispatcher;

    Transceiver transceiver(&dispatcher);
    ReplyPrinter receiver;
    transceiver.setClient(&receiver);
    {
        Message hello(1);
        fillHelloMessage(&hello);
        transceiver.sendAsync(&hello);
        while (true) {
            dispatcher.poll();
        }
    }

    return 0;
}
