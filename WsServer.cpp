#include "WsServer.h"

#include <thread>
#include <iostream>
#include <functional>
#include <chrono>

#include <boost/property_tree/json_parser.hpp>

using namespace std;
using namespace uWS;
using namespace boost::property_tree;


constexpr int CC_MAP_NOT_RECEIVED = 4001;
constexpr int CC_DUPLICATED_CONNECTION = 4002;
constexpr int CC_TERMINATION = 4003;


WsServer::WsServer()
{

}

WsServer::~WsServer()
{
    terminate();
}

void WsServer::terminate()
{
    if (_logThreadTeminated)
        return;
    while (!_started.load())
        this_thread::sleep_for(chrono::nanoseconds(1));
    log("Termination");
    terminateHub(_clientHub);
    terminateHub(_serverHub);
    terminateHub(_webServerHub);
    while (!_logThreadTeminated.load()) // log terminated when client and server terminated
        this_thread::sleep_for(chrono::milliseconds(10));
    delete _clientHub;
    delete _serverHub;
    delete _webServerHub;
    _log.close();
}

void WsServer::terminateHub(Hub* hub)
{
    auto termCb = [](Async* async) -> void
    {
        Hub* hub = static_cast<Hub*>(async->getData());
        hub->Group<SERVER>::close(CC_TERMINATION);
        hub->Group<SERVER>::terminate();
        async->close();
    };
    Async* termAsync = new Async(hub->getLoop());
    termAsync->setData(static_cast<void*>(hub));
    termAsync->start(termCb);
    termAsync->send();
}

// *** START ***

void WsServer::init()
{
    getline(std::ifstream("secret.txt"), _secretMessage);
    _log.open("ws-server.log", ios_base::app);
    _serverHub = nullptr;
    _clientHub = nullptr;
    _clientHubReady.store(false);
    _serverHubReady.store(false);
    _webServerHubReady.store(false);
    _serverSocket = nullptr;
    _serverConnected.store(false);
    _mapReceived.store(false);
    _started.store(false);
    _logThreadTeminated.store(false);
    _serverThreadTerminated.store(false);
    _webServerThreadTerminated.store(false);
    _clientThreadTerminated.store(false);
    _logMutex.unlock();
    _loadedMap = "";
    _loadedObjects = "";
    _logDeq.clear();
    _clientIp.clear();
    _clientSocket.clear();
}

void WsServer::start(uint16_t clientPort, uint16_t serverPort, uint16_t webServerPort)
{
    _clientPort = clientPort;
    _serverPort = serverPort;
    _webServerPort = webServerPort;
    init();
    thread(&WsServer::logThreadFunction, this).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread(&WsServer::serverThreadFunction, this, _serverPort).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread(&WsServer::webServerThreadFunction, this, _webServerPort).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread(&WsServer::clientThreadFunction, this, _clientPort).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    _started.store(true);
}

void WsServer::restart()
{
    terminate();
    start(_clientPort, _serverPort, _webServerPort);
}

void WsServer::logThreadFunction()
{
    log("Log thread running");
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
        while (!_logMutex.try_lock())
            this_thread::sleep_for(chrono::nanoseconds(1));
        printLogDeq();
        _logMutex.unlock();
        if (_clientThreadTerminated.load() && _serverThreadTerminated.load() && _webServerThreadTerminated.load())
        {
            _logThreadTeminated.store(true);
            log("Log thread terminated");
            printLogDeq();
            break;
        }
    }
}

void WsServer::serverThreadFunction(uint16_t port)
{
    log("Server thread running");
    _serverHub = new Hub();
    setServerCallbacks();
    _serverHub->listen(port);
    _serverHubReady.store(true);
    _serverHub->run();
    _serverThreadTerminated.store(true);
    log("Server thread terminated");
}

void WsServer::webServerThreadFunction(uint16_t port)
{
    log("Web server thread running");
    _webServerHub = new Hub();
    setWebServerCallbacks();
    _webServerHub->listen(port);
    _webServerHubReady.store(true);
    _webServerHub->run();
    _webServerThreadTerminated.store(true);
    log("Web server thread terminated");
}

void WsServer::clientThreadFunction(uint16_t port)
{
    log("Client thread running");
    _clientHub = new Hub();
    setClientCallbacks();
    _clientHub->listen(port, nullptr);
    _clientHubReady.store(true);
    _clientHub->run();
    _clientThreadTerminated.store(true);
    log("Client thread terminated");
}

void WsServer::setServerCallbacks()
{
    auto connectionHandler = bind(&WsServer::onServerConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&WsServer::onServerDisconnetion, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    auto messageHandler = bind(&WsServer::onServerMessage, this,
                                placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _serverHub->onConnection(connectionHandler);
    _serverHub->onDisconnection(disconnectionHandler);
    _serverHub->onMessage(messageHandler);
}

void WsServer::setWebServerCallbacks()
{
    auto requestHandler = bind(&WsServer::onWebServerHttpRequest, this,
                               placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4, placeholders::_5);
    _webServerHub->onHttpRequest(requestHandler);
}

void WsServer::setClientCallbacks()
{
    auto connectionHandler = bind(&WsServer::onClientConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&WsServer::onClientDisconnection, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _clientHub->onConnection(connectionHandler);
    _clientHub->onDisconnection(disconnectionHandler);
}

// *** SERVER CALLBACKS ***

void WsServer::onServerConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
{
    log("Attempt connection as server");
    Header h;
    if (_serverConnected.load())
        goto CloseSocket;
    h = request.getHeader("secret");
    if (!h.key || h.valueLength != _secretMessage.length() || strncmp(h.value, _secretMessage.data(), h.valueLength))
        goto CloseSocket;
    _serverConnected.store(true);
    _serverSocket = socket;
    sendAcceptConnection();
    log("Server connected");
    return;
CloseSocket:
    log("Unsuccessful attempt");
    socket->close();
}

void WsServer::onServerDisconnetion(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length)
{
    if (socket != _serverSocket)
        return;
    log("Server disconneted");
    _serverConnected.store(false);
    _mapReceived.store(false);
    _serverSocket = nullptr;
}

void WsServer::onServerMessage(uWS::WebSocket<uWS::SERVER>* socket, char* message, size_t length, uWS::OpCode opCode)
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

void WsServer::onWebServerHttpRequest(HttpResponse* response, HttpRequest request, char* data, size_t length, size_t remainingBytes)
{
    // TODO: check web server
    Header codeHeader = request.getHeader("sourcecode");
    Header nickHeader = request.getHeader("nickname");
    if (!codeHeader.key || !nickHeader.key)
    {
        if (_serverHubReady.load())
            _serverSocket->send("http request");
        return;
        // TODO: something
    }
    ptree pt;
    pt.put<string>("messageType", "newPlayer");
    pt.put<string>("sourceCode", string(codeHeader.value, codeHeader.valueLength));
    pt.put<string>("nickname", string(nickHeader.value, nickHeader.valueLength));
    socketSend(_serverSocket, pt);
}

// *** CLIENT CALLBACKS ***

void WsServer::onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
{
    if (_clientIp.find(socket->getAddress().address) != _clientIp.end()) // TODO: may be exist better way?
    {
        socket->close(CC_DUPLICATED_CONNECTION);
        return;
    }
    _clientIp.insert(socket->getAddress().address);
    _clientSocket.insert(socket);
    if (!_mapReceived.load())
    {
        socket->close(CC_MAP_NOT_RECEIVED);
        return;
    }
    log(string("Client connected: ") + socket->getAddress().family + socket->getAddress().address);
    sendMap(socket);
}

void WsServer::onClientDisconnection(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length)
{
    log(string("Client disconnected: ") + socket->getAddress().family + socket->getAddress().address + ' ' +
        "with code: " + to_string(code));
    if (_clientSocket.find(socket) != _clientSocket.end())
    {
        _clientSocket.erase(socket);
        _clientIp.erase(socket->getAddress().address);
    }
}

// *** LOG ***

void WsServer::log(string msg)
{
    stringstream buffer;
    time_t t = time(nullptr);
    tm* curTime = localtime(&t);
    buffer << '('
           << setfill('0') << setw(2) << curTime->tm_mday << 'd' << ' '
           << setfill('0') << setw(2) << curTime->tm_hour << ':'
           << setfill('0') << setw(2) << curTime->tm_min << ':'
           << setfill('0') << setw(2) << curTime->tm_sec
           << ')';
    buffer << ' ' << msg << '\n';
    while (!_logMutex.try_lock())
        this_thread::sleep_for(chrono::nanoseconds(1));
    _logDeq.push_back(buffer.str());
    _logMutex.unlock();
}

void WsServer::printLogDeq()
{
    while (!_logDeq.empty())
    {
        cout << _logDeq[0];
        _log << _logDeq[0];
        _logDeq.pop_front();
    }
    cout.flush();
    _log.flush();
}

// *** SERVER MESSAGE ***

void WsServer::processServerMessage(uWS::WebSocket<SERVER>* socket, ptree& message)
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

void WsServer::processServerLoadMap(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    stringFromPtree(message, _loadedMap);
    _mapReceived.store(true);
    log("Map loaded");
}

void WsServer::processServerLoadObjects(uWS::WebSocket<SERVER>* socket, ptree& message)
{   
    string s;
    stringFromPtree(message, s);
    _loadedObjects = s;
    log("Objects loaded");
    if (_clientHubReady.load())
        _clientHub->Group<SERVER>::broadcast(_loadedObjects.data(), _loadedObjects.length(), TEXT); // thread safe
    // TODO: may be parallel broadcast ?
}

void WsServer::processServerUnknown(uWS::WebSocket<SERVER>* socket, ptree& message)
{

}

WsServer::SInMessageType WsServer::getServerMessageType(ptree& message) const
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

// ***************************************

void WsServer::sendAcceptConnection()
{
    socketSend(_serverSocket, "{\"messageType\" : \"acceptConnection\"}");
}

void WsServer::sendMap(uWS::WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedMap);
}

void WsServer::sendObjects(uWS::WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedObjects);
}

bool WsServer::ptreeFromString(const string& s, ptree& output) const
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

void WsServer::stringFromPtree(const ptree& pt, string& output) const
{
    stringstream ss;
    json_parser::write_json(ss, pt);
    output = ss.str();
}

void WsServer::socketSend(uWS::WebSocket<SERVER>* socket, const string& message)
{
    socket->send(message.data(), message.length(), TEXT);
}

void WsServer::socketSend(uWS::WebSocket<SERVER>* socket, const ptree& message)
{
    string s;
    stringFromPtree(message, s);
    socket->send(s.data(), s.length(), TEXT);
}
