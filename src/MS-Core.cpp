#include "MS.hpp"

#include <iostream>
#include <iomanip>

using namespace std;
using namespace uWS;


MessageServer::MessageServer() : _hub(HUBS), _loopRunning(HUBS), _threadTerminated(HUBS), _port(HUBS)
{

}

MessageServer::~MessageServer()
{
    terminate();
}

// *** TERMINATION ***

bool MessageServer::terminate()
{
    if (!_started.load())
        return false;
    if (_termination.load())
    {
        while (_termination.load())
            customSleep<milli>(1);
        return true;
    }
    _termination.store(true);
    if (_logThreadTerminated)
    {
        _termination.store(false);
        return true;
    }
    log("Termination");
    vector<Flag> callbacksStoped(HUBS);
    for (unsigned i = 0; i < HUBS; ++i)
        callbacksStoped[i] = false;
    for (unsigned i = 0; i < HUBS; ++i)
        terminateHub(i, callbacksStoped.data());
    while (!_logThreadTerminated.load())
        customSleep<milli>(10);
    _log.close();
    _termination.store(false);
    return true;
}

void MessageServer::terminateHub(int i, Flag* callbacksStoped)
{
    auto eventLoopTermPost = [](Async* async)
    {
        void* asyncData = async->getData();
        Hub* h = getFromVoid<Hub*>(asyncData);
        Flag* callbacksStoped = getFromVoid<Flag*>(asyncData, sizeof(Hub*));
        void* hubData = h->getDefaultGroup<SERVER>().getUserData();
        int i = getFromVoid<int>(hubData + sizeof(MessageServer*));
        callbacksStoped[i].store(true);
        for (int i = 0; i < HUBS; ++i)
            if (!callbacksStoped[i])
                i = -1;
        h->getDefaultGroup<SERVER>().close(static_cast<int>(CloseCode::termination));
        h->getDefaultGroup<SERVER>().terminate();
        async->close();
        free(asyncData);
    };
    Async* async = new Async(_hub[i]->getLoop());
    void* data = malloc(sizeof(Hub*) + sizeof(Flag*));
    putToVoid<Hub*>(data, _hub[i]);
    putToVoid<Flag*>(data, callbacksStoped, sizeof(Hub*));
    async->setData(data);
    async->start(eventLoopTermPost);
    async->send();
}

void MessageServer::sleepHub(int i, Flag& sleeped, Flag& wake)
{
    auto eventLoopSleep = [](Async* async)
    {
        void* data = async->getData();
        Flag* sleeped = getFromVoid<Flag*>(data);
        Flag* wake = getFromVoid<Flag*>(data + sizeof(Flag*));
        sleeped->store(true);
        while (!wake->load())
            customSleep<micro>(10);
        async->close();
        free(async->data);
    };
    Async* async = new Async(_hub[i]->getLoop());
    void* data = malloc(2 * sizeof(Flag*));
    putToVoid<Flag*>(data, &sleeped);
    putToVoid<Flag*>(data, &wake, sizeof(Flag*));
    async->setData(data);
    async->start(eventLoopSleep);
    async->send();
}

// *** START ***

void MessageServer::init()
{
    getline(std::ifstream("secret.txt"), _secretMessage);
    _log.open("message-server.log", ios_base::app);
    _termination = false;
    _serverConnected = false;
    _mapReceived = false;
    _started = false;
    _logThreadTerminated = false;
    _objectsSending = false;
    for (unsigned i = 0; i < HUBS; ++i)
    {
        _threadTerminated[i] = false;
        _loopRunning[i] = false;
    }
    _logCaptured = false;
    _loadedMap = "";
    _logDeq.clear();
    _clientInfo.clear();
    _outTraffic = 0;
}

void MessageServer::start(uint16_t serverPort, uint16_t webServerPort, uint16_t clientPort)
{
    _startPoint = chrono::time_point_cast<chrono::seconds>(chrono::system_clock::now());
    init();
    _port[client] = clientPort;
    _port[server] = serverPort;
    _port[webServer] = webServerPort;
    thread(&MessageServer::logThreadFunction, this).detach();
    customSleep<milli>(100);
    thread(&MessageServer::clientThreadFunction, this, clientPort).detach();
    customSleep<milli>(100);
    thread(&MessageServer::serverThreadFunction, this, serverPort).detach();
    customSleep<milli>(100);
    thread(&MessageServer::webServerThreadFunction, this, webServerPort).detach();
    customSleep<milli>(100);
    for (int i = 0; i < HUBS; ++i)
        if (!_loopRunning[i].load())
            i = -1;
    customSleep<milli>(50);
    _started.store(true);
}

void MessageServer::restart()
{
    terminate();
    start(_port[server], _port[webServer], _port[client]);
}

void MessageServer::logThreadFunction()
{
    log("Log thread running");
    while (true)
    {
        customSleep<milli>(LOG_INTERVAL);
        while (_logCaptured.load()) // cmpxchng crash
            customSleep<micro>(5);
        _logCaptured.store(true);
        printLogDeq();
        _logCaptured.store(false);
        if (_threadTerminated[server].load() && _threadTerminated[webServer].load() && _threadTerminated[client].load())
        {
            lastLog();
            log("Log thread terminated");
            printLogDeq();
            _logThreadTerminated.store(true);
            break;
        }
    }
}

void MessageServer::serverThreadFunction(uint16_t port)
{
    log("Server thread running");
    _hub[server] = new Hub();
    setGroupData(&_hub[server]->getDefaultGroup<SERVER>(), server);
    setServerCallbacks();
    _hub[server]->listen(port);
    _loopRunning[server].store(true);
    _hub[server]->run();
    _threadTerminated[server].store(true);
    free(_hub[server]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[server];
    log("Server thread terminated");
}

void MessageServer::webServerThreadFunction(uint16_t port)
{
    log("Web server thread running");
    _hub[webServer] = new Hub();
    setGroupData(&_hub[webServer]->getDefaultGroup<SERVER>(), webServer);
    setWebServerCallbacks();
    _hub[webServer]->listen(port);
    _loopRunning[webServer].store(true);
    _hub[webServer]->run();
    _threadTerminated[webServer].store(true);
    free(_hub[webServer]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[webServer];
    log("Web server thread terminated");
}

void MessageServer::clientThreadFunction(uint16_t port)
{
    log("Client thread running");
    _hub[client] = new Hub();
    setGroupData(&_hub[client]->getDefaultGroup<SERVER>(), client);
    setClientCallbacks();
    _hub[client]->listen(port);
    _loopRunning[client].store(true);
    _hub[client]->run();
    _threadTerminated[client].store(true);
    free(_hub[client]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[client];
    log("Client thread terminated");
}

void MessageServer::setGroupData(UGroup* g, int i)
{
    void* data = malloc(sizeof(MessageServer*) + sizeof(int));
    putToVoid<MessageServer*>(data, this);
    putToVoid<int>(data, i, sizeof(MessageServer*));
    g->setUserData(data);
}

// *** LOG ***

void MessageServer::log(const string& msg)
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
    while (_logCaptured.load()) // cmpxchng crash
        customSleep<micro>(10);
    _logCaptured.store(true);
    _logDeq.push_back(buffer.str());
    _logCaptured.store(false);
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

void MessageServer::lastLog()
{
    int64_t uptime = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - _startPoint).count();
    if (uptime == 0)
        return;
    log(string("Uptime: ") + to_string(uptime) + " sec");
    log(string("Out traffic: " + to_string(_outTraffic)) + " bytes" + " - " + to_string(_outTraffic / uptime) + " b/s");
}
