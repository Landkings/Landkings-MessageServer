#include "MessageServer.h"

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


MessageServer::MessageServer()
{

}

MessageServer::~MessageServer()
{
    terminate();
}

void MessageServer::terminate()
{
    if (_logThreadTeminated)
        return;
    while (!_started.load())
        this_thread::sleep_for(chrono::nanoseconds(1));
    log("Termination");
    terminateHub(_webServerHub, _webServerThreadTerminated); //  termination order is important
    terminateHub(_serverHub, _serverThreadTerminated);
    terminateHub(_clientHub, _clientThreadTerminated);
    while (!_logThreadTeminated.load())
        this_thread::sleep_for(chrono::milliseconds(10));
    _log.close();
}

void MessageServer::terminateHub(Hub* hub, const atomic<bool>& confirmer)
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
    while (!confirmer.load())
        this_thread::sleep_for(chrono::milliseconds(10));
    delete hub;
}

// *** START ***

void MessageServer::init()
{
    getline(std::ifstream("secret.txt"), _secretMessage);
    _log.open("ws-server.log", ios_base::app);
    _serverHub = nullptr;
    _clientHub = nullptr;
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

void MessageServer::start(uint16_t serverPort, uint16_t webServerPort, uint16_t clientPort)
{
    _clientPort = clientPort;
    _serverPort = serverPort;
    _webServerPort = webServerPort;
    init();
    thread(&MessageServer::logThreadFunction, this).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread(&MessageServer::clientThreadFunction, this, _clientPort).detach();
    this_thread::sleep_for(chrono::milliseconds(500));
    thread(&MessageServer::serverThreadFunction, this, _serverPort).detach();
    this_thread::sleep_for(chrono::milliseconds(500));
    thread(&MessageServer::webServerThreadFunction, this, _webServerPort).detach();
    this_thread::sleep_for(chrono::milliseconds(500));
    _started.store(true);
}

void MessageServer::restart()
{
    terminate();
    start(_clientPort, _serverPort, _webServerPort);
}

void MessageServer::logThreadFunction()
{
    log("Log thread running");
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(400));
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

void MessageServer::serverThreadFunction(uint16_t port)
{
    log("Server thread running");
    _serverHub = new Hub();
    setServerCallbacks();
    _serverHub->listen(port);
    _serverHub->run();
    _serverThreadTerminated.store(true);
    log("Server thread terminated");
}

void MessageServer::webServerThreadFunction(uint16_t port)
{
    log("Web server thread running");
    _webServerHub = new Hub();
    setWebServerCallbacks();
    _webServerHub->listen(port);
    _webServerHub->run();
    _webServerThreadTerminated.store(true);
    log("Web server thread terminated");
}

void MessageServer::clientThreadFunction(uint16_t port)
{
    log("Client thread running");
    _clientHub = new Hub();
    setClientCallbacks();
    _clientHub->listen(port);
    _clientHub->run();
    _clientThreadTerminated.store(true);
    log("Client thread terminated");
}

void MessageServer::setServerCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onServerConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onServerDisconnetion, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    auto messageHandler = bind(&MessageServer::onServerMessage, this,
                                placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _serverHub->onConnection(connectionHandler);
    _serverHub->onDisconnection(disconnectionHandler);
    _serverHub->onMessage(messageHandler);
}

void MessageServer::setWebServerCallbacks()
{
    auto requestHandler = bind(&MessageServer::onWebServerHttpRequest, this,
                               placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4, placeholders::_5);
    _webServerHub->onHttpRequest(requestHandler);
}

void MessageServer::setClientCallbacks()
{
    auto connectionHandler = bind(&MessageServer::onClientConnection, this,
                                  placeholders::_1, placeholders::_2);
    auto disconnectionHandler = bind(&MessageServer::onClientDisconnection, this,
                                     placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
    _clientHub->onConnection(connectionHandler);
    _clientHub->onDisconnection(disconnectionHandler);
}

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
    _serverConnected.store(true);
    _serverSocket = socket;
    sendAcceptConnection();
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
    // TODO: check web server
    if (!_serverConnected.load())
    {
        response->write(httpErrStr.data(), httpErrStr.length());
        response->end();
        return;
    }
    response->write(httpOkStr.data(), httpOkStr.length());
    response->end();
    Header nickHeader = request.getHeader("nickname");
    if (!nickHeader.key) // TODO: del
    {
        log("http request, nick header -");
        return;
    }
    ptree pt;
    pt.put<string>("messageType", "newPlayer");
    pt.put<string>("nickname", string(nickHeader.value, nickHeader.valueLength));
    pt.put<string>("sourceCode", string(data, length));
    socketSend(_serverSocket, pt);
}

// *** CLIENT CALLBACKS ***

void MessageServer::onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
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

void MessageServer::onClientDisconnection(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length)
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

void MessageServer::log(string msg)
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

void MessageServer::printLogDeq()
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
    stringFromPtree(message, _loadedMap);
    _mapReceived.store(true);
    log("Map loaded");
}

void MessageServer::processServerLoadObjects(uWS::WebSocket<SERVER>* socket, ptree& message)
{   
    string s;
    stringFromPtree(message, s);
    _loadedObjects = s;
    log("Objects loaded");
    _clientHub->Group<SERVER>::broadcast(_loadedObjects.data(), _loadedObjects.length(), TEXT); // thread safe
    // TODO: may be parallel broadcast ?
}

void MessageServer::processServerUnknown(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    log("Unknown server message");
}

// ***************************************

void MessageServer::sendAcceptConnection()
{
    socketSend(_serverSocket, "{\"messageType\" : \"acceptConnection\"}");
}

void MessageServer::sendMap(uWS::WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedMap);
}

void MessageServer::sendObjects(uWS::WebSocket<SERVER>* socket)
{
    socketSend(socket, _loadedObjects);
}

bool MessageServer::ptreeFromString(const string& s, ptree& output) const
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

void MessageServer::stringFromPtree(const ptree& pt, string& output) const
{
    stringstream ss;
    json_parser::write_json(ss, pt);
    output = ss.str();
}

void MessageServer::socketSend(uWS::WebSocket<SERVER>* socket, const string& message)
{
    socket->send(message.data(), message.length(), TEXT);
}

void MessageServer::socketSend(uWS::WebSocket<SERVER>* socket, const ptree& message)
{
    string s;
    stringFromPtree(message, s);
    socket->send(s.data(), s.length(), TEXT);
}
