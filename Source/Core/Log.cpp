#include "Log.h"

#include <chrono>
#include <cstdio>
#include <iostream>

namespace woc
{
    namespace
    {
        const char* LevelTag(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:   return "TRACE";
            case LogLevel::Info:    return "INFO ";
            case LogLevel::Warning: return "WARN ";
            case LogLevel::Error:   return "ERROR";
            }
            return "?????";
        }

        std::string Timestamp()
        {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
            const std::time_t t = system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
            return buf;
        }
    }

    Logger::~Logger()
    {
        if (m_file.is_open()) m_file.close();
    }

    void Logger::Open(const std::string& filePath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file.open(filePath, std::ios::out | std::ios::trunc);
    }

    void Logger::Write(LogLevel level, const std::string& message)
    {
        if (static_cast<int>(level) < static_cast<int>(m_minimum)) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string line = "[" + Timestamp() + "][" + LevelTag(level) + "] " + message;
        std::cout << line << std::endl;
        if (m_file.is_open())
        {
            // Flush eagerly: when the game dies the tail of the log is the only clue.
            m_file << line << std::endl;
        }
    }
}
