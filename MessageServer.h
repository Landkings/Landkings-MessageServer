#pragma once
#include <fstream>
#include <mutex>
#include <atomic>
#include <deque>
#include <unordered_set>

#include <uWS/uWS.h>

#include <boost/property_tree/ptree.hpp>


class MessageServer
{
public:
    MessageServer();
    ~MessageServer();
    void start(uint16_t serverPort, uint16_t webServerPort, uint16_t clientPort);
    void terminate();
private:
    enum class SInMessageType
    {
        unknown = -1, loadMap, loadObjects
    };

    uWS::Hub* _serverHub;
    uWS::Hub* _webServerHub;
    uWS::Hub* _clientHub;
    uWS::WebSocket<uWS::SERVER>* _serverSocket;
    std::unordered_set<uWS::WebSocket<uWS::SERVER>*> _clientSocket;
    std::unordered_set<std::string> _clientIp;
    std::atomic<bool> _serverConnected;
    std::atomic<bool> _mapReceived;
    std::string _secretMessage;

    std::string _loadedMap;
    std::string _loadedObjects;

    std::ofstream _log;
    std::deque<std::string> _logDeq;
    std::mutex _logMutex;

    std::atomic<bool> _started;
    std::atomic<bool> _logThreadTeminated;
    std::atomic<bool> _serverThreadTerminated;
    std::atomic<bool> _webServerThreadTerminated;
    std::atomic<bool> _clientThreadTerminated;

    uint16_t _serverPort;
    uint16_t _webServerPort;
    uint16_t _clientPort;

    void logThreadFunction();
    void serverThreadFunction(uint16_t port);
    void webServerThreadFunction(uint16_t port);
    void clientThreadFunction(uint16_t port);

    void setServerCallbacks();
    void setWebServerCallbacks();
    void setClientCallbacks();

    void onServerConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request);
    void onServerDisconnetion(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length);
    void onServerMessage(uWS::WebSocket<uWS::SERVER>* socket, char* message, size_t length, uWS::OpCode opCode);

    void onWebServerHttpRequest(uWS::HttpResponse* response, uWS::HttpRequest request,
                                char *data, size_t length, size_t remainingBytes);

    void onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request);
    void onClientDisconnection(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length);

    void log(std::string msg);
    void printLogDeq();

    SInMessageType getServerMessageType(boost::property_tree::ptree& message) const;
    void processServerMessage(uWS::WebSocket<uWS::SERVER>* socket, boost::property_tree::ptree& message);
    void processServerLoadMap(uWS::WebSocket<uWS::SERVER>* socket, boost::property_tree::ptree& message);
    void processServerLoadObjects(uWS::WebSocket<uWS::SERVER>* socket, boost::property_tree::ptree& message);
    void processServerUnknown(uWS::WebSocket<uWS::SERVER>* socket, boost::property_tree::ptree& message);

    //*************************
    void sendAcceptConnection();
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const std::string& message);
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const boost::property_tree::ptree& message);
    void sendMap(uWS::WebSocket<uWS::SERVER>* socket);
    void sendObjects(uWS::WebSocket<uWS::SERVER>* socket);
    bool ptreeFromString(const std::string& s, boost::property_tree::ptree& output) const;
    void stringFromPtree(const boost::property_tree::ptree& pt, std::string& output) const;
    void terminateHub(uWS::Hub* hub, int i);
    void init();
    void restart();
};
