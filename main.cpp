#include <iostream>
#include <thread>

#include <unistd.h>

#include <boost/program_options.hpp>

#include "MS.hpp"

using namespace std;
using namespace boost::program_options;


MessageServer* mServerPtr;

void sigIntHandler(int)
{
    while (!mServerPtr->terminate())
        this_thread::sleep_for(chrono::milliseconds(10));
    cout << "Terminated normally" << endl;
    exit(0);
}

void setSigIntHandler()
{
    struct sigaction* sigAction = new struct sigaction;
    sigAction->sa_handler = &sigIntHandler;
    sigAction->sa_flags = 0;
    sigaction(SIGINT, sigAction, NULL);
}

int main(int argc, char** argv)
{

    srand(clock());
    options_description desc;
    vector<int> usePort(3);
    int time;
    desc.add_options()
            ("help,h", "Print options list")
            ("sp,s", value<int>(&usePort[0])->default_value(19998), "Port for game server")
            ("wp,w", value<int>(&usePort[1])->default_value(19997), "Port for web server")
            ("cp,c", value<int>(&usePort[2])->default_value(19999), "Port for clients")
            ("time,t", value<int>(&time)->default_value(-1), "Time in seconds through server terminated\n"
                                                             "val < 0 -> never");
    variables_map vm;
    try
    {
        store(parse_command_line(argc, argv, desc), vm);
    }
    catch (...)
    {
        cout << desc << endl;
        return 1;
    }
    vm.notify();
    if (vm.count("help"))
    {
        cout << desc << endl;
        return 0;
    }
    for (unsigned i = 0; i < usePort.size(); ++i)
    {
        if (usePort[i] < 1024 || usePort[i] > 65535)
        {
            cout << "1024 <= port <= 65535" << endl;
            return 1;
        }
        uWS::Hub testPortHub;
        if (!testPortHub.listen(usePort[i]))
        {
            cout << "Can't listen port " << usePort[i] << endl;
            return 1;
        }
        testPortHub.uWS::Group<uWS::SERVER>::terminate();
        for (unsigned j = i + 1; j < usePort.size(); ++j)
            if (usePort[i] == usePort[j])
            {
                cout << "Ports must be different" << endl;
                return 1;
            }
    }
    //*********************
    MessageServer mServer;
    mServerPtr = &mServer;
    setSigIntHandler();

    mServer.start(usePort[0], usePort[1], usePort[2]);
    if (time >= 0)
        this_thread::sleep_for(chrono::seconds(time));
    else
        while (true)
            this_thread::sleep_for(chrono::seconds(5));
    mServer.terminate();
    cout << "Terminated normally" << endl;
    return 0;
}
