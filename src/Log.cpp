#include "Log.hpp"

#include <thread>
#include <cstring>
#include <iostream>

using namespace std;


Log::Log() : _stream(cout), _curBlockPos(0u)
{
    _acquired.store(false);
    _block.resize(10000000u);
}

Log::Log(const string& fname, bool& opened) : _fileStream(fname, ios_base::app), _stream(_fileStream)
{
    if (!_fileStream.good())
    {
        opened = false;
        return;
    }
    _acquired.store(false);
    _block.resize(10000000u);
    opened = true;
}

void Log::write(const string& message)
{
    acquire();
    putTime();
    memcpy(&_block[_curBlockPos], message.data(), message.length() * sizeof(char));
    _block[_curBlockPos + message.length()] = '\n';
    _curBlockPos += (message.length() + 1) * sizeof(char);
    release();
}

void Log::write(const string_view& message)
{
    acquire();
    putTime();
    memcpy(&_block[_curBlockPos], message.data(), message.length() * sizeof(char));
    _block[_curBlockPos + message.length()] = '\n';
    _curBlockPos += (message.length() + 1) * sizeof(char);
    release();
}

void Log::write(const char* message)
{
    acquire();
    size_t length = strlen(message);
    putTime();
    memcpy(&_block[_curBlockPos], message, length * sizeof(char));
    _block[_curBlockPos + length] = '\n';
    _curBlockPos += (length + 1) * sizeof(char);
    release();
}

void Log::flush()
{
    acquire();
    string_view blockView(_block.data(), _curBlockPos);
    _stream << blockView;
    _stream.flush();
    _curBlockPos = 0;
    release();
}

void Log::acquire()
{
    while (true)
    {
        bool cur = false;
        if (_acquired.compare_exchange_strong(cur, true)) // mem_ord_acquire
            return;
        std::this_thread::sleep_for(std::chrono::nanoseconds(5));
    }
}

void Log::release()
{
    _acquired.store(false); // mem_ord_release
}

void Log::putTime()
{
    time_t t = time(nullptr);
    tm* curTime = localtime(&t);
    _block[_curBlockPos] = '(';
    _block[_curBlockPos + 1] = '0' + curTime->tm_mday / 10;
    _block[_curBlockPos + 2] = '0' + curTime->tm_mday % 10;
    _block[_curBlockPos + 3] = 'd';
    _block[_curBlockPos + 4] = ' ';
    _block[_curBlockPos + 5] = '0' + curTime->tm_hour / 10;
    _block[_curBlockPos + 6] = '0' + curTime->tm_hour % 10;
    _block[_curBlockPos + 7] = ':';
    _block[_curBlockPos + 8] = '0' + curTime->tm_min / 10;
    _block[_curBlockPos + 9] = '0' + curTime->tm_min % 10;
    _block[_curBlockPos + 10] = ':';
    _block[_curBlockPos + 11] = '0' + curTime->tm_sec / 10;
    _block[_curBlockPos + 12] = '0' + curTime->tm_sec % 10;
    _block[_curBlockPos + 13] = ')';
    _block[_curBlockPos + 14] = ' ';
    _curBlockPos += TIME_FORMAT_SIZE;
}
