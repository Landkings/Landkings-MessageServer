#include "MS.hpp"

#include <iostream>
#include <iomanip>

using namespace std;
using namespace uWS;


MessageServer::MessageServer() : GAME_MESSAGE_PROCESSOR{}, WEB_MESSAGE_PROCESSOR{}, CLIENT_MESSAGE_PROCESSOR{},
    _hub(HUBS), _loopRunning(HUBS), _threadTerminated(HUBS), _port(HUBS)
{
    // need array initialization like that {1, 2, [10] = 3, 4}
    const_cast<MessageProcessor&>(GAME_MESSAGE_PROCESSOR['o']) = &MessageServer::processGameObjects;
    const_cast<MessageProcessor&>(GAME_MESSAGE_PROCESSOR['m']) = &MessageServer::processGameMap;
    const_cast<MessageProcessor&>(WEB_MESSAGE_PROCESSOR['l']) = &MessageServer::processWebClientLogin;
    const_cast<MessageProcessor&>(WEB_MESSAGE_PROCESSOR['e']) = &MessageServer::processWebClientExit;
    const_cast<MessageProcessor&>(WEB_MESSAGE_PROCESSOR['p']) = &MessageServer::processWebAddPlayer;
    const_cast<ClientMessageProcessor&>(CLIENT_MESSAGE_PROCESSOR['f']) = &MessageServer::processClientFollow;
    const_cast<ClientMessageProcessor&>(CLIENT_MESSAGE_PROCESSOR['p']) = &MessageServer::processClientPosition;
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
    if (_logThreadTerminated.load())
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
    _termination.store(false);
    return true;
}

void MessageServer::terminateHub(int i, Flag* callbacksStoped)
{
    auto termFunc = [](Async* async)
    {
        void* asyncData = async->getData();
        Hub* h = voidGet<Hub*>(asyncData);
        Flag* callbacksStoped = voidGet<Flag*>(asyncData, sizeof(Hub*));
        void* hubData = h->getDefaultGroup<SERVER>().getUserData();
        int i = voidGet<int>(hubData + sizeof(MessageServer*));
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
    voidPut<Hub*>(data, _hub[i]);
    voidPut<Flag*>(data, callbacksStoped, sizeof(Hub*));
    async->setData(data);
    async->start(termFunc);
    async->send();
}

void MessageServer::sleepHub(int i, Flag& sleeped, Flag& wake)
{
    auto sleepFunc = [](Async* async)
    {
        void* data = async->getData();
        Flag* sleeped = voidGet<Flag*>(data);
        Flag* wake = voidGet<Flag*>(data + sizeof(Flag*));
        sleeped->store(true);
        while (!wake->load())
            customSleep<micro>(10);
        async->close();
        free(async->data);
    };
    Async* async = new Async(_hub[i]->getLoop());
    void* data = malloc(2 * sizeof(Flag*));
    voidPut<Flag*>(data, &sleeped);
    voidPut<Flag*>(data, &wake, sizeof(Flag*));
    async->setData(data);
    async->start(sleepFunc);
    async->send();
}

// *** START ***

void MessageServer::init()
{
    getline(std::ifstream("secret.txt"), _secretMessage);
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
    _loadedMap = "";
    _outTraffic = 0;
}

void MessageServer::start(uint16_t gamePort, uint16_t webPort, uint16_t clientPort)
{
    init();
    _startPoint = chrono::time_point_cast<chrono::seconds>(chrono::system_clock::now());
    log("Start");
    _port[client] = clientPort;
    _port[game] = gamePort;
    _port[web] = webPort;
    thread(&MessageServer::logThreadFunction, this).detach();
    customSleep<milli>(100);
    thread(&MessageServer::clientThreadFunction, this, clientPort).detach();
    customSleep<milli>(100);
    thread(&MessageServer::gameThreadFunction, this, gamePort).detach();
    customSleep<milli>(100);
    thread(&MessageServer::webThreadFunction, this, webPort).detach();
    customSleep<milli>(100);
    for (int i = 0; i < HUBS; ++i)
        if (!_loopRunning[i].load())
            i = -1;
    customSleep<milli>(50);
    log("Started");
    _started.store(true);
}

void MessageServer::restart()
{
    terminate();
    start(_port[game], _port[web], _port[client]);
}

void MessageServer::logThreadFunction()
{
    log("Log thread running");
    while (true)
    {
        customSleep<milli>(LOG_INTERVAL);
        _log.flush();
        if (_threadTerminated[game].load() && _threadTerminated[web].load() && _threadTerminated[client].load())
        {
            lastLog();
            log("Log thread terminated");
            log("Terminated");
            _log.flush();
            _logThreadTerminated.store(true);
            break;
        }
    }
}

void MessageServer::gameThreadFunction(uint16_t port)
{
    log("Server thread running");
    _hub[game] = new Hub();
    setGroupData(&_hub[game]->getDefaultGroup<SERVER>(), game);
    setGameCallbacks();
    _hub[game]->listen(port);
    _loopRunning[game].store(true);
    _hub[game]->run();
    free(_hub[game]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[game];
    log("Server thread terminated");
    _threadTerminated[game].store(true);
}

void MessageServer::webThreadFunction(uint16_t port)
{
    log("Web server thread running");
    _hub[web] = new Hub();
    setGroupData(&_hub[web]->getDefaultGroup<SERVER>(), web);
    setWebCallbacks();
    _hub[web]->listen(port);
    _loopRunning[web].store(true);
    _hub[web]->run();
    free(_hub[web]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[web];
    log("Web server thread terminated");
    _threadTerminated[web].store(true);
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
    free(_hub[client]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[client];
    log("Client thread terminated");
    _threadTerminated[client].store(true);
}

void MessageServer::setGroupData(UGroup* g, int i)
{
    void* data = malloc(sizeof(MessageServer*) + sizeof(int));
    voidPut<MessageServer*>(data, this);
    voidPut<int>(data, i, sizeof(MessageServer*));
    g->setUserData(data);
}

// *** LOG ***

void MessageServer::log(const string& msg)
{
    _log.write(msg);
}

void MessageServer::log(const char* msg)
{
    _log.write(msg);
}

void MessageServer::lastLog()
{
    int64_t uptime = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - _startPoint).count();
    if (uptime == 0)
        return;
    log(string("Uptime: ") + to_string(uptime) + " sec");
    log(string("Out traffic: " + to_string(_outTraffic)) + " bytes" + " - " + to_string(_outTraffic / uptime) + " b/s");
}
