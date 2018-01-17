#include "MS.hpp"

#include <random>
#include <string_view>

#include <curlpp/cURLpp.hpp>

using namespace std;
using namespace uWS;


// *** SERVER CALLBACKS ***

void MessageServer::onGameConnection(USocket* socket, HttpRequest request)
{
    Header h;
    if (_serverConnected.load())
        goto CloseSocket;
    h = request.getHeader("secret");
    if (!h.key || h.valueLength != _secretMessage.length() || strncmp(h.value, _secretMessage.data(), h.valueLength))
        goto CloseSocket;
    _gameSocket = socket;
    sendAcceptConnection();
    _serverConnected.store(true);
    log("Server connected");
    return;
CloseSocket:
    socket->close();
}

void MessageServer::onGameDisconnetion(USocket* socket, int code, char* message, size_t length)
{
    if (socket != _gameSocket)
        return;
    log(string("Server disconneted:") + " code = " + to_string(code));
    _serverConnected.store(false);
    _mapReceived.store(false);
}

void MessageServer::onGameMessage(USocket* socket, char* message, size_t length, OpCode opCode)
{
    MessageProcessor prc = GAME_MESSAGE_PROCESSOR[static_cast<unsigned char>(message[0])];
    if (prc)
        (this->*prc)(message + 1, length - 1);
}

void MessageServer::processGameMap(char* message, size_t length)
{
    _loadedMap.assign(message, length);
    _mapReceived.store(true);
    log("Map loaded");
}

void MessageServer::processGameObjects(char* message, size_t length)
{
    static unsigned long objectsCounter = 0;
    if (objectsCounter % 1000 == 0)
    {
        log("Objects loaded");
        log(string("Broadcasted:\n") + string(message, length));
    }
    ++objectsCounter;
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
    auto sendingFunc = [](Async* async)
    {
        void* data = async->getData();
        MessageServer* mServer = voidGet<MessageServer*>(data);
        size_t length = voidGet<size_t>(data, sizeof(MessageServer*));
        char* objectsMsg = static_cast<char*>(data + preMessageDataSize);
        mServer->_hub[client]->getDefaultGroup<SERVER>().broadcast(objectsMsg, length, TEXT);
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
    voidPut(data, this);
    voidPut(data, length, sizeof(MessageServer*));
    memcpy(data + sizeof(MessageServer*) + sizeof(size_t), message, length * sizeof(char));
    async->setData(data);
    async->start(sendingFunc);
    async->send();
}

// *** WEB SERVER CALLBACKS ***

void MessageServer::onWebHttpRequest(HttpResponse* response, HttpRequest request, char* data, size_t length, size_t remainingBytes)
{
    // [secret][messageType][message]
    static const string httpErrStr("HTTP/1.1 500 Internal Server Error\nContent-Length: 0\nConnection: closed\n\n\n");
    static const string httpOkStr("HTTP/1.1 200 OK\nContent-Length: 0\nConnection: closed\n\n\n");
    if (length < _secretMessage.length() || strncmp(_secretMessage.data(), data, _secretMessage.length()))
        return;
    MessageProcessor prc = WEB_MESSAGE_PROCESSOR[static_cast<unsigned char>(data[_secretMessage.length()])];
    if (prc)
        (this->*prc)(data + _secretMessage.length() + 1, length - _secretMessage.length() - 1);
    response->write(httpOkStr.data(), httpOkStr.length());
    response->end();
}

void MessageServer::processWebClientLogin(char* data, size_t length)
{
    // l[nick]>[sessid]
    log(string("Client login:"));
    string_view dataView(data, length);
    size_t nlIdx = dataView.find_first_of('>');
    string nick(dataView.substr(0, nlIdx));
    string sessid(dataView.substr(nlIdx + 1));
    auto itr = _clientInfoNick.find(nick);
    if (itr == _clientInfoNick.end())
    {
        ClientInfo* clientInfo = new ClientInfo(nick, sessid);
        _clientInfoNick[nick] = clientInfo;
        _clientInfoSessid[sessid] = clientInfo;
        return;
    }
    ClientInfo* clientInfo = itr->second;
    _clientInfoSessid.erase(clientInfo->sessid);
    _clientInfoSessid.insert(pair(sessid, clientInfo));
    clientInfo->sessid = sessid;
    clientInfo->frqConnectionCounter = 0;
}

void MessageServer::processWebClientExit(char* data, size_t length)
{
    // e[nick]
    log(string("Client exit:"));
    ClientInfo* clientInfo = _clientInfoNick[string(data, length)];
    _clientInfoSessid.erase(clientInfo->sessid);
}

void MessageServer::processWebAddPlayer(char* data, size_t length)
{
    // p[nick]>[code]
    log(string("New character:"));
    socketSend(_gameSocket, data - 1, length + 1);
}

// *** CLIENT CALLBACKS ***


void MessageServer::onClientConnection(USocket* socket, HttpRequest request)
{
    // TODO: IP block
    /* TODO: uncomment
    if (!_mapReceived.load())
    {
        socket->close(mapNotReceived);
        return;
    }
    */
    Header secWsProtocol = request.getHeader("sec-websocket-protocol");
    string sessid(secWsProtocol.value, secWsProtocol.valueLength);
    auto itr = _clientInfoSessid.find(sessid);
    if (itr == _clientInfoSessid.end())
    {
        socket->close(invalidSessid);
        return;
    }
    ClientInfo* clientInfo = itr->second;
    if (clientInfo->socket)
    {
        clientInfo->socket->close(replaceSocket);
        _clientInfoSocket.erase(clientInfo->socket);
    }
    clientInfo->socket = socket;
    _clientInfoSocket.insert(pair(socket, clientInfo));
    if (connectionSpammer(clientInfo))
        return;
    log(string("Client connected:") + " IP = " + socket->getAddress().address + " | nick = " + clientInfo->nick +
        " | clients = " + to_string(_clientInfoSocket.size()));
    sendMap(socket);
}

void MessageServer::onClientDisconnection(USocket* socket, int code, char* message, size_t length)
{
    auto itr  = _clientInfoSocket.find(socket);
    string logMessage("Client disconnected:");
    if (itr != _clientInfoSocket.end())
    {
        itr->second->socket = nullptr;
        logMessage += string(" nick = ") + itr->second->nick + " | sessid = " + itr->second->sessid;
        _clientInfoSocket.erase(itr);
    }
    logMessage += string(" | code = ") + to_string(code);
    log(logMessage);
}

void MessageServer::onClientMessage(USocket* socket, char* message, size_t length, OpCode opCode)
{
    auto itr = _clientInfoSocket.find(socket);
    if (messageSpammer(itr->second) || !length)
        return;
    ClientMessageProcessor prc = CLIENT_MESSAGE_PROCESSOR[static_cast<unsigned char>(message[0])];
    if (prc)
        (this->*prc)(socket, message + 1, length - 1);
}

void MessageServer::processClientFollow(USocket* socket, char* data, size_t length)
{
    // TODO:
    // [c][nick] - character
    // [p][x>y] - position
}

void MessageServer::processClientPosition(USocket* socket, char* data, size_t length)
{
    // TODO:
}

void MessageServer::sendBlockRequest(ClientInfo* clientInfo)
{
    // TODO:
}
