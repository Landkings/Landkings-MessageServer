#pragma once
#include <string>
#include <fstream>
#include <atomic>
#include <deque>
#include <ostream>

class Log
{
public:
    Log();
    Log(const std::string&, bool& opened);
    ~Log() = default;
    Log(const Log&) = delete;
    Log(Log&&) = delete;
    void write(const std::string&);
    void write(const char*);
    void flush();
private:
    std::ofstream _fileStream;
    std::basic_ostream<char>& _stream;
    std::atomic<bool> _acquired;
    std::string _block;
    size_t _curBlockPos;
    // ***
    static constexpr unsigned TIME_FORMAT_SIZE = 15; // (00d 00:00:00)[space]
    void putTime();
    void acquire();
    void release();
};
