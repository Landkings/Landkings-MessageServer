#pragma once
#include <fstream>
#include <mutex>
#include <atomic>
#include <deque>
#include <unordered_set>
#include <thread>
#include <chrono>

#include <uWS/uWS.h>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

class MessageServer
{
public:
    MessageServer();
    ~MessageServer();
    void start(uint16_t serverPort, uint16_t webServerPort, uint16_t clientPort);
    void terminate();
private:
    enum class CloseCode
    {
        mapNotReceived = 4001, duplicatedConnection, termination
    };
    enum class SInMessageType
    {
        unknown = -1, loadMap, loadObjects
    };
    enum HubID
    {
        server = 0, webServer, client
    };

    static constexpr int HUBS = 3;
    static constexpr int LOG_INTERVAL = 50; // ms

    //********************************************************

    std::vector<uWS::Hub*> _hub;
    std::vector<std::atomic<bool>> _threadTerminated;
    std::vector<uint16_t> _port;

    uWS::WebSocket<uWS::SERVER>* _serverSocket;
    std::unordered_set<uWS::WebSocket<uWS::SERVER>*> _clientSocket;
    std::unordered_set<std::string> _clientIp;
    std::atomic<bool> _serverConnected;
    std::atomic<bool> _mapReceived;
    std::string _secretMessage;
    std::string _loadedMap;
    std::mutex _clientMutex;

    std::ofstream _log;
    std::deque<std::string> _logDeq;
    std::atomic<bool> _logCaptured;
    static bool _falseExpected;

    std::atomic<bool> _started;
    std::atomic<bool> _logThreadTeminated;

    std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> _startPoint;
    unsigned long _outTraffic;

    // *** CORE ***
    void logThreadFunction();
    void serverThreadFunction(uint16_t port);
    void webServerThreadFunction(uint16_t port);
    void clientThreadFunction(uint16_t port);
    void setGroupData(uWS::Group<uWS::SERVER>* g, int i);
    template<class T>
    static void putToVoid(void* base, T val, int offset = 0);
    template<class T>
    static T getFromVoid(void* base, int offset = 0);

    void sleepHub(int i, std::atomic<bool>& sleeped, std::atomic<bool>& wake);
    void terminateHub(int i, std::atomic<bool>* callbacksStoped);
    void init();
    void restart();

    void log(const std::string& msg);
    void printLogDeq();

    // *** CALLBACKS ***
    void onServerConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request);
    void onServerDisconnetion(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length);
    void onServerMessage(uWS::WebSocket<uWS::SERVER>* socket, char* message, size_t length, uWS::OpCode opCode);

    void onWebServerHttpRequest(uWS::HttpResponse* response, uWS::HttpRequest request,
                                char *data, size_t length, size_t remainingBytes);

    void onClientConnection(uWS::WebSocket<uWS::SERVER>* socket, uWS::HttpRequest request);
    void onClientDisconnection(uWS::WebSocket<uWS::SERVER>* socket, int code, char* message, size_t length);

    void setServerCallbacks();
    void setWebServerCallbacks();
    void setClientCallbacks();

    SInMessageType getServerMessageType(const rapidjson::Document& doc) const;
    void processServerMessage(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);
    void processServerLoadMap(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);
    void processServerLoadObjects(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);
    void processServerUnknown(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);

    // *** FUNCTIONS ***
    void sendAcceptConnection();
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const std::string& message);
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);
    void sendMap(uWS::WebSocket<uWS::SERVER>* socket);
    void sendObjects(uWS::WebSocket<uWS::SERVER>* socket);
    static void docBuffer(const rapidjson::Document& doc, rapidjson::StringBuffer& buffer);
    void lastLog();
    template<typename T>
    static void customSleep(unsigned val)
    {
        std::this_thread::sleep_for(std::chrono::duration<int64_t, T>(val));
    }
};
