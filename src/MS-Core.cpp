#include "MS.hpp"

#include <iostream>
#include <iomanip>

using namespace std;
using namespace uWS;


const bool MessageServer::_falseExpected = false;

MessageServer::MessageServer() : _hub(HUBS), _threadTerminated(HUBS), _port(HUBS)
{

}

MessageServer::~MessageServer()
{
    terminate();
}

// *** TERMINATION ***

void MessageServer::terminate()
{
    if (_logThreadTeminated)
        return;
    while (!_started.load())
        customSleep<milli>(10);
    log("Termination");
    vector<atomic<bool>> callbacksStoped(_hub.size());
    for (unsigned i = 0; i < HUBS; ++i)
        callbacksStoped[i] = false;
    for (unsigned i = 0; i < HUBS; ++i)
        terminateHub(i, callbacksStoped.data());
    while (!_logThreadTeminated.load())
        customSleep<milli>(10);
    _log.close();
}

void MessageServer::terminateHub(int i, atomic<bool>* callbacksStoped)
{
    auto eventLoopTermPost = [](Async* async)
    {
        void* asyncData = async->getData();
        Hub* h = getFromVoid<Hub*>(asyncData);
        atomic<bool>* callbacksStoped = getFromVoid<atomic<bool>*>(asyncData, sizeof(Hub*));
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
    void* data = malloc(sizeof(Hub*) + sizeof(atomic<bool>*));
    putToVoid<Hub*>(data, _hub[i]);
    putToVoid<atomic<bool>*>(data, callbacksStoped, sizeof(Hub*));
    async->setData(data);
    async->start(eventLoopTermPost);
    async->send();
}

void MessageServer::sleepHub(int i, atomic<bool>& sleeped, atomic<bool>& wake)
{
    auto eventLoopSleep = [](Async* async)
    {
        void* data = async->getData();
        atomic<bool>* sleeped = getFromVoid<atomic<bool>*>(data);
        atomic<bool>* wake = getFromVoid<atomic<bool>*>(data + sizeof(atomic<bool>*));
        sleeped->store(true);
        while (!wake->load())
            customSleep<micro>(10);
        async->close();
        free(async->data);
    };
    Async* async = new Async(_hub[i]->getLoop());
    void* data = malloc(2 * sizeof(atomic<bool>*));
    putToVoid<atomic<bool>*>(data, &sleeped);
    putToVoid<atomic<bool>*>(data, &wake, sizeof(atomic<bool>*));
    async->setData(data);
    async->start(eventLoopSleep);
    async->send();
}

// *** START ***

void MessageServer::init()
{
    getline(std::ifstream("secret.txt"), _secretMessage);
    _log.open("ws-server.log", ios_base::app);
    _serverConnected.store(false);
    _mapReceived.store(false);
    _started.store(false);
    _logThreadTeminated.store(false);
    for (unsigned i = 0; i < HUBS; ++i)
        _threadTerminated[i].store(false);
    _logCaptured = false;
    _loadedMap = "";
    _logDeq.clear();
    _clientIp.clear();
    _clientSocket.clear();
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
    thread(&MessageServer::clientThreadFunction, this, clientPort).detach();
    customSleep<milli>(500);
    thread(&MessageServer::serverThreadFunction, this, serverPort).detach();
    customSleep<milli>(500);
    thread(&MessageServer::webServerThreadFunction, this, webServerPort).detach();
    customSleep<milli>(500);
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
        while (!_logCaptured.compare_exchange_strong(const_cast<bool&>(_falseExpected), true))
            customSleep<micro>(5);
        printLogDeq();
        _logCaptured.store(false);
        if (_threadTerminated[server].load() && _threadTerminated[webServer].load() && _threadTerminated[client].load())
        {
            lastLog();
            log("Log thread terminated");
            printLogDeq();
            _logThreadTeminated.store(true);
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
    _hub[client]->run();
    _threadTerminated[client].store(true);
    free(_hub[client]->getDefaultGroup<SERVER>().getUserData());
    delete _hub[client];
    log("Client thread terminated");
}

void MessageServer::setGroupData(Group<SERVER>* g, int i)
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
    while (!_logCaptured.compare_exchange_strong(const_cast<bool&>(_falseExpected), true))
        customSleep<micro>(10);
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
    int64_t uptime = (chrono::time_point_cast<chrono::seconds>(chrono::system_clock::now()) - _startPoint).count();
    if (uptime == 0)
        return;
    log(string("Uptime: ") + to_string(uptime) + " seconds");
    log(string("Out traffic: " + to_string(_outTraffic)) + " bytes" + " - " + to_string(_outTraffic / uptime) + " b/s");
}
