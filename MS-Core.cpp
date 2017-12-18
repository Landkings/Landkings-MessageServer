#include "MS.hpp"

#include <iostream>
#include <iomanip>

using namespace std;
using namespace uWS;
using namespace boost::property_tree;


const int MessageServer::FREE_THREADS = std::thread::hardware_concurrency() - 5 <= 0 ? 0 : std::thread::hardware_concurrency() - 5;

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
        customSleep<nano>(1);
    log("Termination");
    vector<atomic<bool>> callbacksStoped(_hub.size());
    for (unsigned i = 0; i < HUBS; ++i)
        callbacksStoped[i] = false;
    for (unsigned i = 0; i < HUBS; ++i)
        terminateHub(i, callbacksStoped.data());
    while (!_logThreadTeminated.load())
        customSleep<nano>(1);
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
        MessageServer* _this = getFromVoid<MessageServer*>(hubData);
        int i = getFromVoid<int>(hubData + sizeof(MessageServer*));
        callbacksStoped[i].store(true);
        for (int i = 0; i < HUBS; ++i)
            if (!callbacksStoped[i])
                i = -1;
        h->getDefaultGroup<SERVER>().close(static_cast<int>(CloseCode::termination));
        h->getDefaultGroup<SERVER>().terminate();
        if (i == client)
        {
            for (unsigned j = 1; j < _this->_clientGroup.size(); ++j)
            {
                _this->_clientGroup[j]->close(static_cast<int>(CloseCode::termination));
                _this->_clientGroup[j]->terminate();
            }
        }
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
    _logMutex.unlock();
    _loadedMap = "";
    _loadedObjects = "";
    _logDeq.clear();
    _clientIp.clear();
    _clientSocket.clear();
    _clientGroup.clear();
    _ss = SWork::nothing;
    _ws = WWork::nothing;
    _cs = CWork::nothing;
}

void MessageServer::start(uint16_t serverPort, uint16_t webServerPort, uint16_t clientPort)
{
    _port[client] = clientPort;
    _port[server] = serverPort;
    _port[webServer] = webServerPort;
    init();
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
        customSleep<milli>(400);
        while (!_logMutex.try_lock())
            customSleep<nano>(1);
        printLogDeq();
        _logMutex.unlock();
        if (_threadTerminated[server].load() && _threadTerminated[webServer].load() && _threadTerminated[client].load())
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
    _hub[server] = new Hub();
    setGroupData(&_hub[server]->getDefaultGroup<SERVER>(), server);
    setServerCallbacks();
    _hub[server]->listen(port);
    _hub[server]->run();
    _threadTerminated[server].store(true);
    delete _hub[server]->getDefaultGroup<SERVER>().getUserData();
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
    delete _hub[webServer]->getDefaultGroup<SERVER>().getUserData();
    delete _hub[webServer];
    log("Web server thread terminated");
}

void MessageServer::clientThreadFunction(uint16_t port)
{
    log("Client thread running");
    _hub[client] = new Hub();
    _clientGroup.push_back(&_hub[client]->getDefaultGroup<SERVER>());
    setGroupData(_clientGroup[0], client);
    for (int i = 0; i < FREE_THREADS; ++i)
    {
        _clientGroup.push_back(_hub[client]->createGroup<SERVER>());
        setGroupData(_clientGroup[i + 1], client);
    }
    setClientCallbacks();
    _hub[client]->listen(port);
    _hub[client]->run();
    _threadTerminated[client].store(true);
    delete _hub[client]->getDefaultGroup<SERVER>().getUserData();
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

template<class T>
void MessageServer::putToVoid(void* base, T val, int offset)
{
    *static_cast<T*>(base + offset) = val;
}

template<class T>
T MessageServer::getFromVoid(void* base, int offset)
{
    return *static_cast<T*>(base + offset);
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
        customSleep<nano>(1);
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
