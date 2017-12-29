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
    if (!_objectsSending.load())
    {
        _objectsSending.store(true);
        injectObjectsSending(message, length);
        _objectsSending.store(false);
    }
    else
        log("Last objects pack not sent yet");
}

void MessageServer::injectObjectsSending(const char* message, size_t length)
{
    static size_t curMessageLength = 1000;
    static void* data = malloc(sizeof(MessageServer*) + sizeof(size_t) + curMessageLength * sizeof(char));
    //**********
    auto sendingInjector = [](Async* async)
    {
        void* data = async->getData();
        MessageServer* mServer = getFromVoid<MessageServer*>(data);
        size_t length = getFromVoid<size_t>(data, sizeof(MessageServer*));
        char* objectsMsg = static_cast<char*>(data + sizeof(MessageServer*) + sizeof(size_t));
        mServer->_hub[client]->getDefaultGroup<SERVER>().broadcast(objectsMsg, length, TEXT);
        mServer->log(string("Broadcasted: \n") + string(objectsMsg, length));
        mServer->_objectsSending.store(false);
        async->close();
    };
    //**********
    if (length > curMessageLength)
    {
        curMessageLength = length * 1.5;
        data = realloc(data, curMessageLength);
    }
    Async* async = new Async(_hub[client]->getLoop());
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
    bool socketReplace = false;
    unordered_map<string, WebSocket<SERVER>*>::iterator itr = _clientInfo.find(socket->getAddress().address);
    if (itr != _clientInfo.end())
    {
        itr->second->close(duplicatedConnection);
        socketReplace = true;
    }
    _clientInfo.insert(pair(socket->getAddress().address, socket));
    if (!_mapReceived.load())
    {
        socket->close(mapNotReceived);
        return;
    }
    if (!socketReplace)
        log(string("Client connected: ") + "IP = " + socket->getAddress().address +
            " clients = " + to_string(_clientInfo.size()));
    sendMap(socket);
}

void MessageServer::onClientDisconnection(WebSocket<SERVER>* socket, int code, char* message, size_t length)
{
    const char* addr = socket->getAddress().address;
    if (code == duplicatedConnection)
        log(string("Socket replaced: ") + "IP = " + addr);
    else
        log(string("Client disconnected: code = ") + to_string(code) + " IP = " + addr +
            " clients = " + to_string(_clientInfo.size()));
    _clientInfo.erase(addr);
}
