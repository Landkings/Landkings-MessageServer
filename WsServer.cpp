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


WsServer::WsServer() : _serverHub(nullptr), _clientHub(nullptr),
    _serverConnected(false), _mapReceived(false)
{
    getline(std::ifstream("secret.txt"), _secretMessage);
}

WsServer::~WsServer()
{
    terminate();
}

void WsServer::terminate()
{
    terminateHub(_serverHub);
    _serverHub = nullptr;
    terminateHub(_clientHub);
    _clientHub = nullptr;
    if (_log.is_open())
        _log.close();
    _serverConnected.store(false);
    _mapReceived.store(false);
}

void WsServer::terminateHub(Hub *hub)
{
    if (hub != nullptr)
    {
        hub->getDefaultGroup<SERVER>().close(1000);
        hub->getDefaultGroup<SERVER>().terminate();
        delete hub;
    }
}

// *** START ***

void WsServer::start(uint16_t clientPort, uint16_t serverPort)
{
    thread([this]() // log thread
    {
        _log.open("ws-server.log", ios_base::app);
        log("Log thread running");
        while (_log.is_open())
        {
            this_thread::sleep_for(chrono::seconds(1));
            while (!_logMutex.try_lock())
                this_thread::sleep_for(chrono::nanoseconds(1));
            while (!_logDeq.empty())
            {
                cout << _logDeq[0];
                _log << _logDeq[0];
                _logDeq.pop_front();
            }
            _logMutex.unlock();
            cout.flush();
            _log.flush();
        }
    }).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread([this](uint16_t port) // server thread
    {
        log("Server thread running");
        _serverHub = new Hub();
        setServerCallbacks();
        _serverHub->listen(port);
        _serverHub->run();
    }, serverPort).detach();
    this_thread::sleep_for(chrono::milliseconds(10));
    thread([this](uint16_t port) // client thread
    {
        _clientHub = new Hub();
        setClientCallbacks();
        _clientHub->listen(port, nullptr);
        this_thread::sleep_for(chrono::seconds(2)); // for server connection
        log("Client thread running");
        _clientHub->run();
    }, clientPort).detach();
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
    if (!h.key || strncmp(h.value, _secretMessage.data(),
                          std::min(h.valueLength, static_cast<unsigned int>(_secretMessage.length()))))
        goto CloseSocket;
    _serverConnected.store(true);
    socketSend(socket, "");
    log("Server connected");
    _serverSocket = socket;
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

// *** CLIENT CALLBACKS ***

void WsServer::onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request)
{
    if (_ipSet.find(socket->getAddress().address) != _ipSet.end()) // TODO: may be exist better way?
    {
        socket->close(CC_DUPLICATED_CONNECTION);
        return;
    }
    _ipSet.insert(socket->getAddress().address);
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
    _ipSet.erase(socket->getAddress().address);
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

// *** SERVER MESSAGE ***

void WsServer::processServerMessage(uWS::WebSocket<SERVER>* socket, ptree& message)
{
    SMessageType type = getServerMessageType(message);
    switch (type)
    {
        case SMessageType::loadMap:
            processServerLoadMap(socket, message);
            return;
        case SMessageType::loadObjects:
            processServerLoadObjects(socket, message);
            return;
        case SMessageType::unknown:
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
    _clientHub->Group<SERVER>::broadcast(_loadedObjects.data(), _loadedObjects.length(), TEXT);
}

void WsServer::processServerUnknown(uWS::WebSocket<SERVER>* socket, ptree& message)
{

}

WsServer::SMessageType WsServer::getServerMessageType(ptree& message) const
{
    string messageType;
    try
    {
        messageType = message.get<string>("messageType");
    }
    catch (...)
    {
        return SMessageType::unknown;
    }
    if (messageType == "loadMap")
        return SMessageType::loadMap;
    if (messageType == "loadObjects")
        return SMessageType::loadObjects;
    return SMessageType::unknown;
}

// ***************************************

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

void WsServer::socketSend(uWS::WebSocket<SERVER>* socket, string message)
{
    socket->send(message.data(), message.length(), TEXT);
}
