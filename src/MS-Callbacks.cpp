#include "MS.hpp"

#include <random>

using namespace std;
using namespace uWS;


// *** SERVER CALLBACKS ***

void MessageServer::onServerConnection(USocket* socket, HttpRequest request)
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

void MessageServer::onServerDisconnetion(USocket* socket, int code, char* message, size_t length)
{
    if (socket != _serverSocket)
        return;
    log(string("Server disconneted:") + " code = " + to_string(code));
    _serverConnected.store(false);
    _mapReceived.store(false);
}

void MessageServer::onServerMessage(USocket* socket, char* message, size_t length, OpCode opCode)
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
    static unsigned long objectsCounter = 0;
    if (++objectsCounter % 1000 == 0 || objectsCounter == 0)
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
    constexpr size_t preMessageDataSize = sizeof(MessageServer*) + sizeof(size_t);
    static size_t curMessageLength = 1024;
    static void* data = malloc(preMessageDataSize + curMessageLength * sizeof(char));
    //**********
    auto sendingInjector = [](Async* async)
    {
        void* data = async->getData();
        MessageServer* mServer = getFromVoid<MessageServer*>(data);
        size_t length = getFromVoid<size_t>(data, sizeof(MessageServer*));
        char* objectsMsg = static_cast<char*>(data + preMessageDataSize);
        mServer->_hub[client]->getDefaultGroup<SERVER>().broadcast(objectsMsg, length, TEXT);
        mServer->log(string("Broadcasted: \n") + string(objectsMsg, length));
        mServer->_objectsSending.store(false);
        async->close();
    };
    //**********
    if (length > curMessageLength)
    {
        curMessageLength <<= length / curMessageLength;
        data = realloc(data, preMessageDataSize + curMessageLength * sizeof(char));
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

    static constexpr size_t expectedMaxBufferSize = 8192;
    string buffer;
    buffer.reserve(expectedMaxBufferSize);
    setMessageType(OutputMessageType::newPlayer, buffer);
    Header nickHeader = request.getHeader("nickname");
    buffer.append(nickHeader.value, nickHeader.valueLength);
    buffer += '\n';
    buffer.append(data, length);
    socketSend(_serverSocket, buffer);
}

// *** CLIENT CALLBACKS ***

void MessageServer::onClientConnection(USocket* socket, HttpRequest request)
{
    // TODO: may be clear _clientInfo?
    if (blackListMember(socket))
    {
        socket->close(blackList);
        return;
    }
    const char* addr = socket->getAddress().address;
    unordered_map<string, ClientInfo>::iterator itr = _clientInfo.find(socket->getAddress().address);
    ConnectionType conType = getConnectionType(itr);
    switch (conType)
    {
        case ConnectionType::firstTime:
            _clientInfo.insert(pair(addr, socket));
            break;
        case ConnectionType::reconnection:
            itr->second.socket = socket;
            itr->second.lastTry = chrono::system_clock::now();
            break;
        case ConnectionType::replace:
            if (itr->second.socket != nullptr)
                itr->second.socket->close(replaceSocket);
            //itr->second.socket->close(replaceSocket);
            itr->second.socket = socket;
            itr->second.lastTry = chrono::system_clock::now();
            break;
        case ConnectionType::blackListCandidat:
            if (itr->second.socket != nullptr)
                itr->second.socket->close(replaceSocket);
            itr->second.socket = socket;
            itr->second.lastTry = chrono::system_clock::now();
            if (++itr->second.blackListBehavior == 5)
            {
                toBlackList(itr);
                return;
            }
    }
    if (conType != ConnectionType::blackListCandidat && conType != ConnectionType::firstTime)
        itr->second.blackListBehavior = 0;
    if (!_mapReceived.load())
    {
        socket->close(mapNotReceived);
        return;
    }
    log(string("Client cnnt:") + " IP = " + addr +
        " contype = " + to_string(static_cast<int>(conType)) +
        " clients = " + to_string(_clientInfo.size()));
    sendMap(socket);
}

void MessageServer::onClientDisconnection(USocket* socket, int code, char* message, size_t length)
{
    const char* addr = socket->getAddress().address;
    unordered_map<string, ClientInfo>::iterator itr = _clientInfo.find(socket->getAddress().address);
    if (itr != _clientInfo.end())
        itr->second.socket = nullptr;
    switch (code)
    {
        case replaceSocket:
            log(string("Socket rplc: ") + "IP = " + addr);
            break;
        case blackList:
            log(string("Blist member dsc: ") + "IP = " + addr);
            break;
        default:
            log(string("Client dsc: code = ") + to_string(code) + " IP = " + addr +
                " clients = " + to_string(_clientInfo.size()));
            break;
    }
}

MessageServer::ConnectionType MessageServer::getConnectionType(unordered_map<string, ClientInfo>::iterator& itr)
{
    if (itr != _clientInfo.end())
    {
        if (since<deci>(itr->second.lastTry) < 100)
            return ConnectionType::blackListCandidat;
        if (itr->second.socket != nullptr)
            return ConnectionType::replace;
        return ConnectionType::reconnection;
    }
    return ConnectionType::firstTime;
}

void MessageServer::toBlackList(unordered_map<string, ClientInfo>::iterator& itr)
{
    itr->second.socket->close(blackList);
    _blackList.insert(pair<string, TimePoint>(itr->first, itr->second.lastTry));
    _clientInfo.erase(itr);
}

bool MessageServer::blackListMember(USocket* socket)
{
    unordered_map<string, TimePoint>::iterator itr = _blackList.find(socket->getAddress().address);
    if (itr != _blackList.end())
    {
        if (since<deci>(itr->second) < 6000)
            return true;
        _blackList.erase(itr);
    }
    return false;
}
