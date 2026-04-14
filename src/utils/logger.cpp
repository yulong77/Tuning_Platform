#include "acceltool/utils/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace acceltool
{
    static LogLevel g_level = LogLevel::Info;
    static bool g_console = true;
    static bool g_fileEnabled = false;

    static std::ofstream g_file;

    static std::string timestamp()
    {
        using clock = std::chrono::system_clock;

        auto now = clock::now();
        auto t = clock::to_time_t(now);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    static const char* levelName(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            default: return "";
        }
    }

    static void write(LogLevel level, const std::string& msg)
    {
        if (static_cast<int>(level) < static_cast<int>(g_level))
            return;

        std::string line =
            "[" + timestamp() + "] [" +
            levelName(level) + "] " + msg;

        if (g_console)
        {
            if (level == LogLevel::Error)
                std::cerr << line << '\n';
            else
                std::cout << line << '\n';
        }

        if (g_fileEnabled && g_file.is_open())
        {
            g_file << line << '\n';
        }
    }

    void initLogger(LogLevel level, bool enableConsole, bool enableFile, const std::string& filePath)
    {
        g_level = level;
        g_console = enableConsole;
        g_fileEnabled = enableFile;

        if (enableFile && !filePath.empty())
        {
            g_file.open(filePath, std::ios::out | std::ios::app);
        }
    }

    void shutdownLogger()
    {
        if (g_file.is_open())
        {
            g_file.close();
        }
    }

    void logDebug(const std::string& msg)
    {
        write(LogLevel::Debug, msg);
    }

    void logInfo(const std::string& msg)
    {
        write(LogLevel::Info, msg);
    }

    void logWarn(const std::string& msg)
    {
        write(LogLevel::Warn, msg);
    }

    void logError(const std::string& msg)
    {
        write(LogLevel::Error, msg);
    }
}