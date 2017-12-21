#include "MS.hpp"

#include <random>

using namespace std;
using namespace uWS;
using namespace rapidjson;


// *** SERVER CALLBACKS ***

void MessageServer::onServerConnection(WebSocket<SERVER>* socket, HttpRequest request)
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

void MessageServer::onServerDisconnetion(WebSocket<SERVER>* socket, int code, char* message, size_t length)
{
    if (socket != _serverSocket)
        return;
    log("Server disconneted: code = " + to_string(code));
    _serverConnected.store(false);
    _mapReceived.store(false);
    _serverSocket = nullptr;
}

void MessageServer::onServerMessage(WebSocket<SERVER>* socket, char* message, size_t length, OpCode opCode)
{
    Document doc;
    doc.Parse(message, length);
    if (!doc.IsObject())
    {
        log("Invalid server JSON");
        return;
    }
    processServerMessage(socket, doc);
}

// *** WEB SERVER CALLBACKS ***

void MessageServer::onWebServerHttpRequest(HttpResponse* response, HttpRequest request, char* data, size_t length, size_t remainingBytes)
{
    const static string httpErrStr("HTTP/1.1 500 Internal Server Error");
    const static string httpOkStr("HTTP/1.1 200 OK");
    _ww.store(WWork::request);
    // TODO: check web server
    /*
    if (!_serverConnected.load())
    {
        response->write(httpErrStr.data(), httpErrStr.length());
        response->end();
        _ww.store(WWork::nothing);
        return;
    }
    response->write(httpOkStr.data(), httpOkStr.length()); // TODO: unknown bug
    response->end();
    */
    Header nickHeader = request.getHeader("nickname");
    if (!nickHeader.key) // TODO: del
    {
        log("http request, nick header -");
        _ww.store(WWork::nothing);
        return;
    }
    Document doc;
    doc.SetObject();
    Document::AllocatorType& allc = doc.GetAllocator();
    Value val(kStringType);
    val.SetString("newPlayer");
    doc.AddMember("messageType", val, allc);
    val.SetString(nickHeader.value, nickHeader.valueLength);
    doc.AddMember("nickname", val, allc);
    val.SetString(data, length);
    doc.AddMember("sourceCode", val, allc);
    socketSend(_serverSocket, doc);
    _ww.store(WWork::nothing);
}

// *** CLIENT CALLBACKS ***

void MessageServer::onClientConnection(WebSocket<SERVER>* socket, HttpRequest request)
{
    _cw.store(CWork::connection);
    if (_clientIp.find(socket->getAddress().address) != _clientIp.end())
    {
        socket->close(static_cast<int>(CloseCode::duplicatedConnection));
        _cw.store(CWork::nothing);
        return;
    }
    int transferGroupId = rand() % _clientGroup.size();
    if (transferGroupId != 0)
        socket->transfer(_clientGroup[transferGroupId]);
    _clientIp.insert(socket->getAddress().address);
    _clientSocket.insert(socket);
    if (!_mapReceived.load())
    {
        socket->close(static_cast<int>(CloseCode::mapNotReceived));
        _cw.store(CWork::nothing);
        return;
    }
    log(string("Client connected: group = ") + to_string(transferGroupId) + " address = " + socket->getAddress().family + socket->getAddress().address);
    sendMap(socket);
    _cw.store(CWork::nothing);
}

void MessageServer::onClientDisconnection(WebSocket<SERVER>* socket, int code, char* message, size_t length)
{
    _cw.store(CWork::disconnection);
    if (_clientSocket.find(socket) != _clientSocket.end())
    {
        log(string("Client disconnected: code = ") + to_string(code) + " address = " + socket->getAddress().family + socket->getAddress().address);
        _clientSocket.erase(socket);
        _clientIp.erase(socket->getAddress().address);
    }
    else
        log(string("Decline client connection: code = " + to_string(code)));
    _cw.store(CWork::nothing);
}

// *** SERVER MESSAGE ***

MessageServer::SInMessageType MessageServer::getServerMessageType(const Document& doc) const
{
    Document::ConstMemberIterator typeIterator = doc.FindMember("messageType");
    if (typeIterator == doc.MemberEnd())
        return SInMessageType::unknown;
    string messageType = typeIterator->value.GetString();
    if (messageType == "loadMap")
        return SInMessageType::loadMap;
    if (messageType == "loadObjects")
        return SInMessageType::loadObjects;
    return SInMessageType::unknown;
}

void MessageServer::processServerMessage(WebSocket<SERVER>* socket, const Document& doc)
{
    SInMessageType type = getServerMessageType(doc);
    switch (type)
    {
        case SInMessageType::loadMap:
            processServerLoadMap(socket, doc);
            return;
        case SInMessageType::loadObjects:
            processServerLoadObjects(socket, doc);
            return;
        case SInMessageType::unknown:
            processServerUnknown(socket, doc);
            return;
    }
}

void MessageServer::processServerLoadMap(WebSocket<SERVER>* socket, const Document& doc)
{
    _sw.store(SWork::loadMap);
    StringBuffer buffer;
    docBuffer(doc, buffer);
    _loadedMap.assign(buffer.GetString(), buffer.GetLength());
    _mapReceived.store(true);
    log("Map loaded");
    _sw.store(SWork::nothing);
}

void MessageServer::processServerLoadObjects(WebSocket<SERVER>* socket, const Document& doc)
{
    _sw.store(SWork::loadObjects);
    StringBuffer buffer;
    docBuffer(doc, buffer);
    _loadedObjects.assign(buffer.GetString(), buffer.GetLength());
    log("Objects loaded");
    vector<atomic<bool>> broadcasted(_clientGroup.size());
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
        if (!broadcasted[i].load())
            i = 0;
    _sw.store(SWork::nothing);
}

void MessageServer::processServerUnknown(WebSocket<SERVER>* socket, const Document& doc)
{
    log("Unknown server message");
}
