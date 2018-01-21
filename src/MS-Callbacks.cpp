#include "MS.hpp"

#include <random>
#include <string_view>
/*
 * TODO: uncomment
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
*/

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
    _log.write("Game server connected");
    return;
CloseSocket:
    socket->close();
}

void MessageServer::onGameDisconnetion(USocket* socket, int code, char* message, size_t length)
{
    if (socket != _gameSocket)
        return;
    _log.write(string("Server disconneted:") + " code = " + to_string(code));
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
    _log.write("Map loaded");
}

void MessageServer::processGameObjects(char* message, size_t length)
{
    static unsigned long objectsCounter = 0;
    if (++objectsCounter % 1000 == 1)
    {
        _log.write("Objects loaded");
        _log.write(string("Broadcasted:\n") + string(message, length));
    }
    if (cmpxchng(_objectsSending))
    {
        injectObjectsSending(message, length);
        _objectsSending.store(false);
    }
    else
        _log.write("Last objects pack not sent yet");
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
    static const string errResponse("HTTP/1.1 500 Internal Server Error\nContent-Length: 0\nConnection: closed\n\n");
    static const string okResponse("HTTP/1.1 200 OK\nContent-Length: 0\nConnection: closed\n\n");
    _log.write(string("Http req data:") + " " + string(data, length));
    if (length < _secretMessage.length() || strncmp(_secretMessage.data(), data, _secretMessage.length()))
        return;
    MessageProcessor prc = WEB_MESSAGE_PROCESSOR[static_cast<unsigned char>(data[_secretMessage.length()])];
    if (prc)
        (this->*prc)(data + _secretMessage.length() + 1, length - _secretMessage.length() - 1);
    response->write(okResponse.data(), okResponse.length());
    response->end();
}

void MessageServer::processWebClientLogin(char* data, size_t length)
{
    // l[nick]>[sessid]
    string_view dataView(data, length);
    size_t nlIdx = dataView.find_first_of('>');
    string nick(dataView.substr(0, nlIdx));
    string sessid(dataView.substr(nlIdx + 1));
    _log.write(string("Client login:") + " nick = " + nick + " | sessid = " + sessid);
    acquireFlag<nano>(_clientInfoAcquired, 1);
    auto itr = _clientInfoNick.find(nick);
    if (itr == _clientInfoNick.end())
    {
        ClientInfo* clientInfo = new ClientInfo(nick, sessid);
        _clientInfoNick[nick] = clientInfo;
        _clientInfoSessid[sessid] = clientInfo;
        releaseFlag(_clientInfoAcquired);
        return;
    }
    ClientInfo* clientInfo = itr->second;
    _clientInfoSessid.erase(clientInfo->sessid);
    _clientInfoSessid.insert(pair(sessid, clientInfo));
    clientInfo->sessid = sessid;
    clientInfo->frqConnectionCounter = 0;
    releaseFlag(_clientInfoAcquired);
}

void MessageServer::processWebClientLogout(char* data, size_t length)
{
    // e[nick]
    acquireFlag<nano>(_clientInfoAcquired, 1);
    ClientInfo* clientInfo = _clientInfoNick[string(data, length)];
    if (clientInfo)
    {
        _log.write(string("Client logout:") + " nick = " + clientInfo->nick);
        _clientInfoSessid.erase(clientInfo->sessid);
    }
    releaseFlag(_clientInfoAcquired);
}

void MessageServer::processWebAddPlayer(char* data, size_t length)
{
    // p[nick]>[code]
    string_view dataView(data);
    _log.write(string("New character:") + " nick = " + string(data, dataView.find_first_of('>')));
    socketSend(_gameSocket, data - 1, length + 1);
}

// *** CLIENT CALLBACKS ***


void MessageServer::onClientConnection(USocket* socket, HttpRequest request)
{
    // TODO: IP block
    if (!_mapReceived.load())
    {
        socket->close(mapNotReceived);
        return;
    }
    Header secWsProtocol = request.getHeader("sec-websocket-protocol");
    string sessid(secWsProtocol.value, secWsProtocol.valueLength);
    acquireFlag<nano>(_clientInfoAcquired, 1);
    auto itr = _clientInfoSessid.find(sessid);
    if (itr == _clientInfoSessid.end())
    {
        socket->close(invalidSessid);
        releaseFlag(_clientInfoAcquired);
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
    {
        releaseFlag(_clientInfoAcquired);
        return;
    }
    _log.write(string("Client connected:") + " IP = " + socket->getAddress().address + " | nick = " + clientInfo->nick +
        " | clients = " + to_string(_clientInfoSocket.size()));
    releaseFlag(_clientInfoAcquired);
    sendMap(socket);
}

void MessageServer::onClientDisconnection(USocket* socket, int code, char* message, size_t length)
{
    acquireFlag<nano>(_clientInfoAcquired, 1);
    auto itr  = _clientInfoSocket.find(socket);
    string logMessage("Client disconnected:");
    if (itr != _clientInfoSocket.end())
    {
        itr->second->socket = nullptr;
        logMessage += string(" nick = ") + itr->second->nick + " | sessid = " + itr->second->sessid;
        _clientInfoSocket.erase(itr);
    }
    releaseFlag(_clientInfoAcquired);
    logMessage += string(" | code = ") + to_string(code);
    _log.write(logMessage);
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
    // f[nick] - follow
}

void MessageServer::processClientPosition(USocket* socket, char* data, size_t length)
{
    // TODO:
    // p[x]>[y] - position
}

void MessageServer::sendBanRequest(ClientInfo* clientInfo, unsigned time)
{
    //using namespace curlpp::options;
    return;
    // TODO: uncomment
    /*
    curlpp::Cleanup cleaner;
    curlpp::Easy request;
    request.setOpt(new Url("TODO: ban URL"));
    request.setOpt(new PostFields(_secretMessage + clientInfo->nick + ">" + to_string(time)));
    request.perform();
    */
}
