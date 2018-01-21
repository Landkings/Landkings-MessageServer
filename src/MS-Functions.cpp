#include "MS.hpp"

using namespace std;
using namespace uWS;


bool MessageServer::connectionSpammer(ClientInfo* clientInfo)
{
    // TODO: uncomment
    /*
    TimePoint cur = chrono::system_clock::now();
    if (since<One>(clientInfo->lastConnection, cur) < 5)
    {
        if (++clientInfo->frqConnectionCounter == 5)
        {
            sendBanRequest(clientInfo, 60);
            eraseClientInfo(clientInfo);
            clientInfo->socket->close(banRequestSended);
            return true;
        }
    }
    else
        clientInfo->frqConnectionCounter = 0;
    clientInfo->lastConnection = cur;
    */
    return false;
}

bool MessageServer::messageSpammer(ClientInfo* clientInfo)
{
    // TODO: uncomment
    /*
    TimePoint cur = chrono::system_clock::now();
    if (since<deci>(clientInfo->lastMessage, cur) < 15)
    {
        if (++clientInfo->frqMessageCounter == 15)
        {
            sendBanRequest(clientInfo, 10);
            eraseClientInfo(clientInfo);
            clientInfo->socket->close(banRequestSended);
            return true;
        }
    }
    else
        clientInfo->frqMessageCounter = 0;
    clientInfo->lastMessage = cur;
    */
    return false;
}

void MessageServer::eraseClientInfo(ClientInfo* clientInfo)
{
    _clientInfoNick.erase(clientInfo->nick);
    _clientInfoSessid.erase(clientInfo->sessid);
}

void MessageServer::sendAcceptConnection()
{
    socketSend(_gameSocket, "c");
}

void MessageServer::sendMap(USocket* socket)
{
    socketSend(socket, _loadedMap);
}

void MessageServer::socketSend(USocket* socket, const string& message)
{
    _outTraffic += message.length();
    socket->send(message.data(), message.length(), TEXT);
}

void MessageServer::socketSend(USocket* socket, const char* message, size_t length)
{
    _outTraffic += length;
    socket->send(message, length, TEXT);
}

void MessageServer::setGameCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onGameConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onGameDisconnetion, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    auto messageHandler = bind(&MessageServer::onGameMessage, this,
                                placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _hub[game]->onConnection(connectionHandler);
    _hub[game]->onDisconnection(disconnectionHandler);
    _hub[game]->onMessage(messageHandler);
}

void MessageServer::setWebCallbacks()
{
    auto requestHandler = bind(&MessageServer::onWebHttpRequest, this,
                               placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4, placeholders::_5);
    _hub[web]->onHttpRequest(requestHandler);
}

void MessageServer::setClientCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onClientConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onClientDisconnection, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    auto messageHandler = bind(&MessageServer::onClientMessage, this,
                               placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _hub[client]->onConnection(connectionHandler);
    _hub[client]->onDisconnection(disconnectionHandler);
    _hub[client]->onMessage(messageHandler);
}
