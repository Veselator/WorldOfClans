// Log.h - single logging sink for the whole engine.
#pragma once

#include "Singleton.h"
#include "Types.h"
#include <sstream>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace woc
{
    enum class LogLevel { Trace, Info, Warning, Error };

    class Logger final : public Singleton<Logger>
    {
        friend class Singleton<Logger>;
    public:
        void Open(const std::string& filePath);
        void Write(LogLevel level, const std::string& message);
        void SetMinimumLevel(LogLevel level) { m_minimum = level; }

    private:
        Logger() = default;
        ~Logger();

        std::ofstream m_file;
        std::mutex m_mutex;
        LogLevel m_minimum = LogLevel::Trace;
    };

    namespace detail
    {
        template <typename... Args>
        std::string Format(Args&&... args)
        {
            std::ostringstream os;
            (os << ... << args);
            return os.str();
        }
    }
}

#define WOC_LOG_TRACE(...) ::woc::Logger::Get().Write(::woc::LogLevel::Trace,   ::woc::detail::Format(__VA_ARGS__))
#define WOC_LOG_INFO(...)  ::woc::Logger::Get().Write(::woc::LogLevel::Info,    ::woc::detail::Format(__VA_ARGS__))
#define WOC_LOG_WARN(...)  ::woc::Logger::Get().Write(::woc::LogLevel::Warning, ::woc::detail::Format(__VA_ARGS__))
#define WOC_LOG_ERROR(...) ::woc::Logger::Get().Write(::woc::LogLevel::Error,   ::woc::detail::Format(__VA_ARGS__))
