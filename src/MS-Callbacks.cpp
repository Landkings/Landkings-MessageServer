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
    InputMessageType type = getServerMessageType(*message);
    switch (type)
    {
        case InputMessageType::loadMap:
            processServerLoadMap(message + 1, length - 1);
            return;
        case InputMessageType::loadObjects:
            processServerLoadObjects(message + 1, length - 1);
            return;
        case InputMessageType::unknown:
            return;
    }
}

MessageServer::InputMessageType MessageServer::getServerMessageType(char firstChar) const
{
    switch (firstChar)
    {
        case 'm':
            return InputMessageType::loadMap;
        case 'o':
            return InputMessageType::loadObjects;
        default:
            return InputMessageType::unknown;
    }
}

void MessageServer::processServerLoadMap(const char* message, size_t length)
{
    _loadedMap.assign(message, length);
    _mapReceived.store(true);
    log("Map loaded");
}

void MessageServer::processServerLoadObjects(const char* message, size_t length)
{
    log("Objects loaded");
    injectObjectsSending(message, length);
}

void MessageServer::injectObjectsSending(const char* message, size_t length)
{
    auto sendingInjector = [](Async* async)
    {
        void* data = async->getData();
        MessageServer* mServer = getFromVoid<MessageServer*>(data);
        size_t length = getFromVoid<size_t>(data, sizeof(MessageServer*));
        char* message = static_cast<char*>(data + sizeof(MessageServer*) + sizeof(size_t));
        mServer->_hub[client]->getDefaultGroup<SERVER>().broadcast(message, length, TEXT);
        async->close();
        free(data);
    };
    Async* async = new Async(_hub[client]->getLoop());
    void* data = malloc(sizeof(MessageServer*) + sizeof(size_t) + length * sizeof(char));
    putToVoid(data, this);
    putToVoid(data, length, sizeof(MessageServer*));
    memcpy(data + sizeof(MessageServer*) + sizeof(size_t), message, length * sizeof(char));
    async->setData(data);
    async->start(sendingInjector);
    async->send();
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

    string buffer;
    setMessageType(OutputMessageType::newPlayer, buffer);
    Header nickHeader = request.getHeader("nickname");
    buffer.append(nickHeader.value, nickHeader.valueLength);
    buffer += '\n';
    buffer.append(data, length);
    socketSend(_serverSocket, buffer);
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
