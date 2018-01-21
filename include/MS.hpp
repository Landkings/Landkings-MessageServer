#pragma once
#include <fstream>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <array>
#include <unordered_set>
#include <string_view>

#include <uWS/uWS.h>

#include <Log.hpp>


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
    typedef std::ratio<1, 1> One;
    typedef void (MessageServer::* MessageProcessor)(char*, size_t);
    typedef void (MessageServer::* ClientMessageProcessor)(USocket*, char*, size_t);

    struct ClientInfo
    {
        ClientInfo(const std::string_view& nick_, const std::string_view& sessid_) :
            nick(nick_), sessid(sessid_), socket(nullptr),
            lastConnection(std::chrono::system_clock::now() - std::chrono::nanoseconds(10000000000)),
            lastMessage(lastConnection), frqConnectionCounter(0u), frqMessageCounter(0u)
        {

        }
        std::string nick;
        std::string sessid;
        USocket* socket;
        TimePoint lastConnection;
        TimePoint lastMessage;
        unsigned frqConnectionCounter;
        unsigned frqMessageCounter;
    };

    /*
    struct ClientCamera
    {
        enum CameraType
        {
            follow, position
        };
        CameraType type;
        union Camera
        {
            std::string followNick;
            struct Pos
            {
                int x;
                int y;
            } pos;
        } cam;
    };
    */

    enum CloseCode
    {
        replaceSocket = 4001, invalidSessid, banRequestSended, mapNotReceived = 4100, termination
    };

    enum HubID
    {
        game = 0, web, client
    };


    static const int HUBS = 3;

    std::array<const MessageProcessor, UCHAR_MAX + 1> GAME_MESSAGE_PROCESSOR;
    std::array<const MessageProcessor, UCHAR_MAX + 1> WEB_MESSAGE_PROCESSOR;
    std::array<const ClientMessageProcessor, UCHAR_MAX + 1> CLIENT_MESSAGE_PROCESSOR;

    static const unsigned LOG_INTERVAL = 100; // ms

    //********************************************************


    std::vector<uWS::Hub*> _hub;
    std::vector<Flag> _loopRunning;
    std::vector<Flag> _threadTerminated;
    std::vector<uint16_t> _port;
    USocket* _gameSocket;

    Flag _clientInfoAcquired;
    std::unordered_map<USocket*, ClientInfo*> _clientInfoSocket; // 3 ptrs - 1 struct
    std::unordered_map<std::string, ClientInfo*> _clientInfoSessid;
    std::unordered_map<std::string, ClientInfo*> _clientInfoNick;

    //std::unordered_map<std::string, ClientCamera> _clientCamera;

    Flag _serverConnected;
    Flag _mapReceived;
    std::string _secretMessage;
    std::string _loadedMap;

    Log _log;

    Flag _started;
    Flag _termination;
    Flag _logThreadTerminated;

    Flag _objectsSending;

    TimePoint _startPoint;
    unsigned long _outTraffic;

    // *** CORE ***
    void logThreadFunction();
    void gameThreadFunction(uint16_t port);
    void webThreadFunction(uint16_t port);
    void clientThreadFunction(uint16_t port);
    void setGroupData(UGroup* g, int i);

    void sleepHub(int i, Flag& sleeped, Flag& wake);
    void terminateHub(int i, Flag* callbacksStoped);
    void init();
    void restart();

    // *** CALLBACKS ***
    void onGameConnection(USocket* socket, uWS::HttpRequest request);
    void onGameDisconnetion(USocket* socket, int code, char* message, size_t length);
    void onGameMessage(USocket* socket, char* message, size_t length, uWS::OpCode opCode);
    void processGameMap(char* message, size_t length);
    void processGameObjects(char* message, size_t length);

    void onWebHttpRequest(uWS::HttpResponse* response, uWS::HttpRequest request, char *data, size_t length, size_t remainingBytes);
    void processWebClientLogin(char* data, size_t length);
    void processWebClientLogout(char* data, size_t length);
    void processWebAddPlayer(char* data, size_t length);

    void onClientConnection(USocket* socket, uWS::HttpRequest request);
    void onClientDisconnection(USocket* socket, int code, char* message, size_t length);
    void onClientMessage(USocket* socket, char* message, size_t length, uWS::OpCode opCode);
    void processClientFollow(USocket* socket, char* data, size_t length);
    void processClientPosition(USocket* socket, char* data, size_t length);

    bool messageSpammer(ClientInfo* clientInfo);
    bool connectionSpammer(ClientInfo* clientInfo);
    void eraseClientInfo(ClientInfo* clientInfo);
    void sendBanRequest(ClientInfo* clientInfo, unsigned time);

    // *** FUNCTIONS ***
    void injectObjectsSending(const char* message, size_t length);
    void sendAcceptConnection();
    void socketSend(USocket* socket, const std::string& message);
    void socketSend(USocket* socket, const char* message, size_t length);
    void sendMap(USocket* socket);
    void sendObjects(USocket* socket);
    void lastLog();
    void setGameCallbacks();
    void setWebCallbacks();
    void setClientCallbacks();
    // ********************
    static inline void releaseFlag(Flag& flag)
    {
        flag.store(false); // mem_ord_release
    }
    template<typename T>
    static void acquireFlag(Flag& flag, unsigned interval)
    {
        while (true)
        {
            bool cur = false;
            if (flag.compare_exchange_strong(cur, true)) // mem_ord_acquire
                break;
            customSleep<T>(interval);
        }
    }
    static bool cmpxchng(Flag& flag, bool val = true)
    {
        bool cur;
        return flag.compare_exchange_strong(cur, val);
    }
    template<typename T>
    inline static int64_t since(TimePoint& point, const TimePoint& cur = std::chrono::system_clock::now())
    {
        return std::chrono::duration_cast<std::chrono::duration<int64_t, T>>(cur - point).count();
    }
    template<typename T>
    inline static void customSleep(unsigned val)
    {
        std::this_thread::sleep_for(std::chrono::duration<int64_t, T>(val));
    }
    template<class T>
    inline static void voidPut(void* base, T val, int offset = 0)
    {
        *static_cast<T*>(base + offset) = val;
    }
    template<class T>
    inline static T voidGet(void* base, int offset = 0)
    {
        return *static_cast<T*>(base + offset);
    }
};
