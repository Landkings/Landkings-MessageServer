#include "MS.hpp"

#include <boost/property_tree/json_parser.hpp>

using namespace std;
using namespace uWS;
using namespace boost::property_tree;


void MessageServer::sendAcceptConnection()
{
    socketSend(_serverSocket, "{\"messageType\" : \"acceptConnection\"}");
}

void MessageServer::sendMap(WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedMap);
}

void MessageServer::sendObjects(WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedObjects);
}

void MessageServer::socketSend(WebSocket<SERVER>* socket, const string& message)
{
    socket->send(message.data(), message.length(), TEXT);
}

void MessageServer::socketSend(WebSocket<SERVER>* socket, const ptree& message)
{
    string s;
    stringFromPtree(message, s);
    socket->send(s.data(), s.length(), TEXT);
}

bool MessageServer::ptreeFromString(const string& s, ptree& output)
{
    stringstream ss;
    ss << s;
    try
    {
        json_parser::read_json(ss, output);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

void MessageServer::stringFromPtree(const ptree& pt, string& output)
{
    stringstream ss;
    json_parser::write_json(ss, pt);
    output = ss.str();
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
    for (unsigned i = 0; i < _clientGroup.size(); ++i)
    {
        _clientGroup[i]->onConnection(connectionHandler);
        _clientGroup[i]->onDisconnection(disconnectionHandler);
    }
}
