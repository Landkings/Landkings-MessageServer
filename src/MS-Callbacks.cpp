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
    processServerMessage(socket, doc);
}

// *** WEB SERVER CALLBACKS ***

void MessageServer::onWebServerHttpRequest(HttpResponse* response, HttpRequest request, char* data, size_t length, size_t remainingBytes)
{
    const static string httpErrStr("HTTP/1.1 500 Internal Server Error\nContent-Length: 0\nConnection: closed\n\n\n");
    const static string httpOkStr("HTTP/1.1 200 OK\nContent-Length: 0\nConnection: closed\n\n\n");

    Header secretHeader = request.getHeader("secret");
    if (!secretHeader.key || secretHeader.valueLength != _secretMessage.length() ||
        strncmp(secretHeader.value, _secretMessage.data(), secretHeader.valueLength))
        return;

    log("Http request");
    if (!_serverConnected.load())
    {
        response->write(httpErrStr.data(), httpErrStr.length());
        response->end();
        return;
    }
    response->write(httpOkStr.data(), httpOkStr.length());
    response->end();

    Header nickHeader = request.getHeader("nickname");
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
}

// *** CLIENT CALLBACKS ***

void MessageServer::onClientConnection(WebSocket<SERVER>* socket, HttpRequest request)
{
    if (_clientIp.find(socket->getAddress().address) != _clientIp.end())
    {
        socket->close(static_cast<int>(CloseCode::duplicatedConnection));
        return;
    }
    _clientIp.insert(socket->getAddress().address);
    _clientSocket.insert(socket);
    if (!_mapReceived.load())
    {
        socket->close(static_cast<int>(CloseCode::mapNotReceived));
        return;
    }
    log(string("Client connected: ") + "address = " + socket->getAddress().family + socket->getAddress().address +
        " clients = " + to_string(_clientSocket.size()));
    sendMap(socket);
}

void MessageServer::onClientDisconnection(WebSocket<SERVER>* socket, int code, char* message, size_t length)
{
    if (_clientSocket.find(socket) != _clientSocket.end())
    {
        log(string("Client disconnected: code = ") + to_string(code) + " address = " + socket->getAddress().family + socket->getAddress().address);
        _clientSocket.erase(socket);
        _clientIp.erase(socket->getAddress().address);
    }
    else
        log(string("Decline client connection: code = " + to_string(code)));
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
    StringBuffer buffer;
    docBuffer(doc, buffer);
    _loadedMap.assign(buffer.GetString(), buffer.GetLength());
    _mapReceived.store(true);
    log("Map loaded");
}

void MessageServer::processServerLoadObjects(WebSocket<SERVER>* socket, const Document& doc)
{
    log("Objects loaded");
    injectObjectsSending(doc);
}

void MessageServer::processServerUnknown(WebSocket<SERVER>* socket, const Document& doc)
{
    log("Unknown server message");
}

void MessageServer::injectObjectsSending(const Document& doc)
{
    auto sendingInjector = [](Async* async)
    {
        void* data = async->getData();
        StringBuffer* buffer = getFromVoid<StringBuffer*>(data, 0);
        Hub* clientHub = getFromVoid<Hub*>(data + sizeof(StringBuffer*));
        clientHub->Group<SERVER>::broadcast(buffer->GetString(), buffer->GetLength(), TEXT);
        async->close();
        delete buffer;
        free(data);
    };
    StringBuffer* buffer = docBuffer(doc);
    Async* async = new Async(_hub[client]->getLoop());
    void* data = malloc(sizeof(StringBuffer*) + sizeof(Hub*));
    putToVoid(data, buffer, 0);
    putToVoid(data, _hub[client], sizeof(StringBuffer*));
    async->setData(data);
    async->start(sendingInjector);
    async->send();
}
