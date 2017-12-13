#include <iostream>
#include <thread>

#include <boost/program_options.hpp>

#include "WsServer.h"

using namespace std;
using namespace boost::program_options;


// amend test

int main(int argc, char** argv)
{
    options_description desc;
    int clientPort, serverPort;
    desc.add_options()
            ("help,h", "Print options list")
            ("cp,c", value<int>(&clientPort)->default_value(19999), "Port for clients")
            ("sp,s", value<int>(&serverPort)->default_value(19998), "Port for game server");
    variables_map vm;
    try
    {
        store(parse_command_line(argc, argv, desc), vm);
    }
    catch (exception e)
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
    if (clientPort < 1024 || clientPort > 65535 || clientPort < 1024 || clientPort > 65535)
    {
        cout << "1024 <= port <= 65535" << endl;
        return 1;
    }
    /*
    uWS::Hub testPortHub;
    if (!testPortHub.listen(clientPort))
    {
        cout << "Can't listen client port " << clientPort << endl;
        return 1;
    }
    if (!testPortHub.listen(serverPort))
    {
        cout << "Can't listen server port " << serverPort << endl;
        return 1;
    }
    testPortHub.getDefaultGroup<uWS::SERVER>().terminate();
    */
    //***
    WsServer wsServer;
    wsServer.start(clientPort, serverPort);
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
