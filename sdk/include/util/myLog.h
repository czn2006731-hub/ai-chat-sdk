#pragma once
#include <mutex>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

namespace m_log{
class Logger{
    public:
        static void initLogger(const std::string& loggerName, const std::string& loggerFile, spdlog::level::level_enum logLevel = spdlog::level::info);
        static std::shared_ptr<spdlog::logger> getLogger();
    private:
        Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
    private:
        static std::shared_ptr<spdlog::logger> _logger;
        static std::mutex _mutex;
};


// fmt
// #include <format>
// string s = std::format("Hello, {}", "world")
// 09:04:03 [aiChatServer][info   ][/home/bit/will/AIModelAcess/ai-model-acess/sdk/src/DataManager.cpp:15  ] Database opened successfully: chat.db
// DBG("Database opened successfully: {}:{}", dbName, dbType);

#define TRACE(format, ...) m_log::Logger::getLogger()->trace(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)
#define DBG(format, ...) m_log::Logger::getLogger()->debug(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...) m_log::Logger::getLogger()->info(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)
#define WARN(format, ...) m_log::Logger::getLogger()->warn(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)
#define ERR(format, ...) m_log::Logger::getLogger()->error(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)
#define CRIT(format, ...) m_log::Logger::getLogger()->critical(std::string("[{:>10s}:{:<4d}]")+format, __FILE__, __LINE__, ##__VA_ARGS__)

} // end m_log