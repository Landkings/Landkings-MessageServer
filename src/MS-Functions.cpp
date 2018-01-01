#include "MS.hpp"

using namespace std;
using namespace uWS;


void MessageServer::setMessageType(OutputMessageType type, std::string& buffer)
{
    buffer.push_back(static_cast<char>(type));
}

void MessageServer::sendAcceptConnection()
{
    socketSend(_serverSocket, "c");
}

void MessageServer::sendMap(WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedMap);
}

void MessageServer::socketSend(WebSocket<SERVER>* socket, const string& message)
{
    _outTraffic += message.length();
    socket->send(message.data(), message.length(), TEXT);
}

void MessageServer::setServerCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onServerConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onServerDisconnetion, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    auto messageHandler = bind(&MessageServer::onServerMessage, this,
                                placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _hub[server]->onConnection(connectionHandler);
    _hub[server]->onDisconnection(disconnectionHandler);
    _hub[server]->onMessage(messageHandler);
}

void MessageServer::setWebServerCallbacks()
{
    auto requestHandler = bind(&MessageServer::onWebServerHttpRequest, this,
                               placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4, placeholders::_5);
    _hub[webServer]->onHttpRequest(requestHandler);
}

void MessageServer::setClientCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onClientConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onClientDisconnection, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _hub[client]->onConnection(connectionHandler);
    _hub[client]->onDisconnection(disconnectionHandler);
}
