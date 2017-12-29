#pragma once
#include <fstream>
#include <mutex>
#include <atomic>
#include <deque>
#include <unordered_map>
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
    bool terminate();
private:
    typedef std::atomic<bool> Flag;

    enum class InputMessageType
    {
        unknown = -1, loadMap = 'm', loadObjects = 'o'
    };

    enum class OutputMessageType
    {
        unknown = -1, acceptConnection = 'c', newPlayer = 'p'
    };

    enum CloseCode
    {
        mapNotReceived = 4001, duplicatedConnection, termination
    };

    enum HubID
    {
        server = 0, webServer, client
    };

    static constexpr int HUBS = 3;
    static constexpr int LOG_INTERVAL = 50; // ms

    //********************************************************

    std::vector<uWS::Hub*> _hub;
    std::vector<Flag> _loopRunning;
    std::vector<Flag> _threadTerminated;
    std::vector<uint16_t> _port;

    uWS::WebSocket<uWS::SERVER>* _serverSocket;
    std::unordered_map<std::string, uWS::WebSocket<uWS::SERVER>*> _clientInfo;
    Flag _serverConnected;
    Flag _mapReceived;
    std::string _secretMessage;
    std::string _loadedMap;

    std::ofstream _log;
    std::deque<std::string> _logDeq;
    Flag _logCaptured;

    Flag _started;
    Flag _termination;
    Flag _logThreadTerminated;

    Flag _objectsSending;

    std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> _startPoint;
    unsigned long _outTraffic;

    // *** CORE ***
    void logThreadFunction();
    void serverThreadFunction(uint16_t port);
    void webServerThreadFunction(uint16_t port);
    void clientThreadFunction(uint16_t port);
    void setGroupData(uWS::Group<uWS::SERVER>* g, int i);

    void sleepHub(int i, Flag& sleeped, Flag& wake);
    void terminateHub(int i, Flag* callbacksStoped);
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

    InputMessageType getServerMessageType(char firstChar) const;
    void processServerLoadMap(const char* message, size_t length);
    void processServerLoadObjects(const char* message, size_t length);

    // *** FUNCTIONS ***
    void setMessageType(OutputMessageType type, std::string& buffer);
    void injectObjectsSending(const char* message, size_t length);
    void sendAcceptConnection();
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const std::string& message);
    void socketSend(uWS::WebSocket<uWS::SERVER>* socket, const rapidjson::Document& doc);
    void sendMap(uWS::WebSocket<uWS::SERVER>* socket);
    void sendObjects(uWS::WebSocket<uWS::SERVER>* socket);
    static void docBuffer(const rapidjson::Document& doc, rapidjson::StringBuffer& buffer);
    static rapidjson::StringBuffer* docBuffer(const rapidjson::Document& doc);
    void lastLog();

    // ********************
    template<typename T>
    static void customSleep(unsigned val)
    {
        std::this_thread::sleep_for(std::chrono::duration<int64_t, T>(val));
    }
    template<class T>
    static void putToVoid(void* base, T val, int offset = 0)
    {
        *static_cast<T*>(base + offset) = val;
    }
    template<class T>
    static T getFromVoid(void* base, int offset = 0)
    {
        return *static_cast<T*>(base + offset);
    }

};
