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
    typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> TimePoint;
    typedef std::atomic<bool> Flag;
    typedef uWS::Group<uWS::SERVER> UGroup;
    typedef uWS::WebSocket<uWS::SERVER> USocket;
    typedef std::ratio<1, 1> EmptyRatio;

    struct ClientInfo
    {
        ClientInfo(USocket* s) : socket(s), lastTry(std::chrono::system_clock::now()) {}
        USocket* socket;
        TimePoint lastTry;
        int blc;
    };

    enum class InputMessageType
    {
        unknown = -1, loadMap = 'm', loadObjects = 'o'
    };

    enum class OutputMessageType
    {
        unknown = -1, acceptConnection = 'c', newPlayer = 'p'
    };

    enum class ConnectionType
    {
        firstTime, reconnection, replace, blackListCandidat
    };

    enum CloseCode
    {
        mapNotReceived = 4001, replaceConnection, spamConnection, blackList, termination
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

    USocket* _serverSocket;
    std::unordered_map<std::string, ClientInfo> _clientInfo;
    std::unordered_map<std::string, TimePoint> _blackList;

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

    TimePoint _startPoint;
    unsigned long _outTraffic;

    // *** CORE ***
    void logThreadFunction();
    void serverThreadFunction(uint16_t port);
    void webServerThreadFunction(uint16_t port);
    void clientThreadFunction(uint16_t port);
    void setGroupData(UGroup* g, int i);

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

    ConnectionType getConnectionType(std::unordered_map<std::string, ClientInfo>::iterator& itr);
    bool inBlackList(USocket* socket);
    void toBlackList(std::unordered_map<std::string, ClientInfo>::iterator& itr);

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
    static int64_t since(TimePoint& point)
    {
        return std::chrono::duration_cast<std::chrono::duration<int64_t, T>>(std::chrono::system_clock::now() - point).count();
    }
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
