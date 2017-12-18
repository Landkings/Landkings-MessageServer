#include "MS.hpp"

#include <random>

using namespace std;
using namespace uWS;
using namespace boost::property_tree;


// *** SERVER CALLBACKS ***

void MessageServer::onServerConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
{
    log("Attempt connection as server");
    Header h;
    if (_serverConnected.load())
        goto CloseSocket;
    h = request.getHeader("secret");
    if (!h.key || h.valueLength != _secretMessage.length() || strncmp(h.value, _secretMessage.data(), h.valueLength))
        goto CloseSocket;
    _serverSocket = socket;
    sendAcceptConnection();
    _serverConnected.store(true);
    log("Server connected");
    return;
CloseSocket:
    log("Unsuccessful attempt");
    socket->close();
}

void MessageServer::onServerDisconnetion(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length)
{
    if (socket != _serverSocket)
        return;
    log("Server disconneted");
    _serverConnected.store(false);
    _mapReceived.store(false);
    _serverSocket = nullptr;
}

void MessageServer::onServerMessage(uWS::WebSocket<uWS::SERVER>* socket, char* message, size_t length, uWS::OpCode opCode)
{
    ptree pt;
    if (!ptreeFromString(string(message).substr(0, length), pt))
    {
        log("Receive FROM SERVER invalid JSON");
        return;
    }
    processServerMessage(socket, pt);
}

// *** WEB SERVER CALLBACKS ***

void MessageServer::onWebServerHttpRequest(HttpResponse* response, HttpRequest request, char* data, size_t length, size_t remainingBytes)
{
    const static string httpErrStr("HTTP/1.1 500 Internal Server Error");
    const static string httpOkStr("HTTP/1.1 200 OK");
    _ws.store(WWork::request);
    // TODO: check web server
    if (!_serverConnected.load())
    {
        response->write(httpErrStr.data(), httpErrStr.length());
        response->end();
        _ws.store(WWork::nothing);
        return;
    }
    response->write(httpOkStr.data(), httpOkStr.length());
    response->end();
    Header nickHeader = request.getHeader("nickname");
    if (!nickHeader.key) // TODO: del
    {
        log("http request, nick header -");
        _ws.store(WWork::nothing);
        return;
    }
    ptree pt;
    pt.put<string>("messageType", "newPlayer");
    pt.put<string>("nickname", string(nickHeader.value, nickHeader.valueLength));
    pt.put<string>("sourceCode", string(data, length));
    socketSend(_serverSocket, pt);
    _ws.store(WWork::nothing);
}

// *** CLIENT CALLBACKS ***

// TODO: terminate all client groups

void MessageServer::onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
{
    _cs.store(CWork::connection);
    if (_clientIp.find(socket->getAddress().address) != _clientIp.end())
    {
        socket->close(static_cast<int>(CloseCode::duplicatedConnection));
        _cs.store(CWork::nothing);
        return;
    }
    int transferGroupId = rand() % _clientGroup.size();
    if (transferGroupId != 0) // if 0 -> default group
        socket->transfer(_clientGroup[transferGroupId]);
    _clientIp.insert(socket->getAddress().address);
    _clientSocket.insert(socket);
    if (!_mapReceived.load())
    {
        socket->close(static_cast<int>(CloseCode::mapNotReceived));
        _cs.store(CWork::nothing);
        return;
    }
    log(string("Client + : group = ") + to_string(transferGroupId) + " address = " + socket->getAddress().family + socket->getAddress().address);
    sendMap(socket);
    _cs.store(CWork::nothing);
}

void MessageServer::onClientDisconnection(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length)
{
    _cs.store(CWork::disconnection);
    if (_clientSocket.find(socket) != _clientSocket.end())
    {
        log(string("Client - : code = ") + to_string(code) + " address = " + socket->getAddress().family + socket->getAddress().address);
        _clientSocket.erase(socket);
        _clientIp.erase(socket->getAddress().address);
    }
    else
        log(string("Decline client connection : code = " + to_string(code)));
    _cs.store(CWork::nothing);
}

// *** SERVER MESSAGE ***

MessageServer::SInMessageType MessageServer::getServerMessageType(ptree& message) const
{
    string messageType;
    try
    {
        messageType = message.get<string>("messageType");
    }
    catch (...)
    {
        return SInMessageType::unknown;
    }
    if (messageType == "loadMap")
        return SInMessageType::loadMap;
    if (messageType == "loadObjects")
        return SInMessageType::loadObjects;
    return SInMessageType::unknown;
}

void MessageServer::processServerMessage(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    SInMessageType type = getServerMessageType(message);
    switch (type)
    {
        case SInMessageType::loadMap:
            processServerLoadMap(socket, message);
            return;
        case SInMessageType::loadObjects:
            processServerLoadObjects(socket, message);
            return;
        case SInMessageType::unknown:
            processServerUnknown(socket, message);
            return;
    }
}

void MessageServer::processServerLoadMap(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    _ss.store(SWork::loadMap);
    stringFromPtree(message, _loadedMap);
    _mapReceived.store(true);
    log("Map loaded");
    _ss.store(SWork::nothing);
}

void MessageServer::processServerLoadObjects(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    _ss.store(SWork::loadObjects);
    string s;
    stringFromPtree(message, s);
    _loadedObjects = s;
    log("Objects loaded");
    vector<atomic<bool>> broadcasted(_clientGroup.size() - 1);
    for (unsigned i = 1; i < _clientGroup.size(); ++i)
    {
        broadcasted[i].store(false);
        thread([this, &broadcasted](int i)
        {
            _clientGroup[i]->broadcast(_loadedObjects.data(), _loadedObjects.length(), TEXT);
            broadcasted[i].store(true);
        }, i).detach();
    }
    _hub[client]->getDefaultGroup<SERVER>().broadcast(_loadedObjects.data(), _loadedObjects.length(), TEXT);
    for (unsigned i = 1; i < _clientGroup.size(); ++i)
        if (!broadcasted[i - 1].load())
            i = 0;
    _ss.store(SWork::nothing);
}

void MessageServer::processServerUnknown(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    log("Unknown server message");
}
