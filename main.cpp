#include <iostream>
#include <thread>

#include <boost/program_options.hpp>

#include "WsServer.h"

using namespace std;
using namespace boost::program_options;


int main(int argc, char** argv)
{
    options_description desc;
    int clientPort, serverPort, time;
    desc.add_options()
            ("help,h", "Print options list")
            ("cp,c", value<int>(&clientPort)->default_value(19999), "Port for clients")
            ("sp,s", value<int>(&serverPort)->default_value(19998), "Port for game server")
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
    clientPort = vm["cp"].as<int>();
    serverPort = vm["sp"].as<int>();
    time = vm["time"].as<int>();
    if (clientPort < 1024 || clientPort > 65535 || clientPort < 1024 || clientPort > 65535)
    {
        cout << "1024 <= port <= 65535" << endl;
        return 1;
    }
    if (clientPort == serverPort)
    {
        cout << "Server port == Client port" << endl;
        return 1;
    }
    {
        uWS::Hub testClientPortHub;
        if (!testClientPortHub.listen(clientPort))
        {
            cout << "Can't listen client port " << clientPort << endl;
            return 1;
        }
        uWS::Hub testServerPortHub;
        if (!testServerPortHub.listen(serverPort))
        {
            cout << "Can't listen server port " << serverPort << endl;
            return 1;
        }
        testClientPortHub.getDefaultGroup<uWS::SERVER>().terminate();
        testServerPortHub.getDefaultGroup<uWS::SERVER>().terminate();
    }
    //*********************
    WsServer wsServer;
    wsServer.start(clientPort, serverPort);
    if (time > 0)
        this_thread::sleep_for(chrono::seconds(time));
    else
        while (true)
            this_thread::sleep_for(chrono::seconds(5));
    wsServer.terminate();
    cout << "Terminated normally" << endl;
    return 0;
}
