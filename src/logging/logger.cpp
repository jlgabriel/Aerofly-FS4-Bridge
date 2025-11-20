///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @file logger.cpp
 * @brief Implementation of the logging system for Aerofly FS4 Bridge
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "logging/logger.h"

// We'll use spdlog header-only mode for simplicity
#define SPDLOG_HEADER_ONLY
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/msvc_sink.h"  // OutputDebugString sink for Windows

#include <windows.h>
#include <shlobj.h>  // For SHGetFolderPath
#include <ctime>
#include <iomanip>
#include <sstream>

namespace AeroflyBridge {

// Static member initialization
std::shared_ptr<spdlog::logger> Logger::s_logger = nullptr;
bool Logger::s_initialized = false;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Helper Functions
///////////////////////////////////////////////////////////////////////////////////////////////////

std::string Logger::GetLogFilePath() {
    // Get Documents folder
    char documentsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath))) {
        std::string logDir = std::string(documentsPath) + "\\Aerofly FS 4\\logs";

        // Create logs directory if it doesn't exist
        CreateDirectoryA(logDir.c_str(), NULL);

        // Generate filename with current date: bridge_YYYYMMDD.log
        std::time_t now = std::time(nullptr);
        std::tm timeInfo;
        localtime_s(&timeInfo, &now);

        std::ostringstream filename;
        filename << logDir << "\\bridge_"
                 << std::put_time(&timeInfo, "%Y%m%d")
                 << ".log";

        return filename.str();
    }

    // Fallback to current directory
    return "bridge.log";
}

int Logger::GetLogLevelFromEnv() {
    char buffer[32] = {0};
    DWORD result = GetEnvironmentVariableA("AEROFLY_BRIDGE_LOG_LEVEL", buffer, sizeof(buffer));

    if (result > 0) {
        std::string level(buffer);

        // Convert to lowercase for comparison
        for (char& c : level) {
            c = static_cast<char>(tolower(c));
        }

        if (level == "trace")    return SPDLOG_LEVEL_TRACE;
        if (level == "debug")    return SPDLOG_LEVEL_DEBUG;
        if (level == "info")     return SPDLOG_LEVEL_INFO;
        if (level == "warn")     return SPDLOG_LEVEL_WARN;
        if (level == "error")    return SPDLOG_LEVEL_ERROR;
        if (level == "critical") return SPDLOG_LEVEL_CRITICAL;
    }

    // Default level: INFO in release, DEBUG in debug builds
    #if defined(NDEBUG)
        return SPDLOG_LEVEL_INFO;
    #else
        return SPDLOG_LEVEL_DEBUG;
    #endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Public Interface
///////////////////////////////////////////////////////////////////////////////////////////////////

bool Logger::Initialize() {
    if (s_initialized) {
        return true;  // Already initialized
    }

    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Check environment variables for sink configuration
        char enableFile[8] = {0};
        char enableConsole[8] = {0};
        GetEnvironmentVariableA("AEROFLY_BRIDGE_LOG_FILE", enableFile, sizeof(enableFile));
        GetEnvironmentVariableA("AEROFLY_BRIDGE_LOG_CONSOLE", enableConsole, sizeof(enableConsole));

        bool useFile = (strlen(enableFile) == 0) || (atoi(enableFile) != 0);  // Default: enabled
        bool useConsole = (strlen(enableConsole) == 0) || (atoi(enableConsole) != 0);  // Default: enabled

        // Add console sink (OutputDebugString on Windows)
        if (useConsole) {
            auto console_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");
            sinks.push_back(console_sink);
        }

        // Add rotating file sink (5MB max, 3 files kept)
        if (useFile) {
            std::string logFile = GetLogFilePath();
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFile,
                1024 * 1024 * 5,  // 5 MB max size
                3                 // Keep 3 rotated files
            );
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
            sinks.push_back(file_sink);
        }

        // Create the logger with all sinks
        s_logger = std::make_shared<spdlog::logger>("AeroflyBridge", sinks.begin(), sinks.end());

        // Set log level from environment
        s_logger->set_level(static_cast<spdlog::level::level_enum>(GetLogLevelFromEnv()));

        // Flush every 3 seconds
        s_logger->flush_on(spdlog::level::warn);  // Also flush on warnings and above
        spdlog::flush_every(std::chrono::seconds(3));

        // Register as default logger
        spdlog::set_default_logger(s_logger);

        s_initialized = true;

        LOG_INFO("Logging system initialized successfully");
        LOG_DEBUG("Log level: {}", spdlog::level::to_string_view(s_logger->level()));

        return true;
    }
    catch (const std::exception& ex) {
        // Fallback to OutputDebugString if logger initialization fails
        std::string error = "Failed to initialize logging system: ";
        error += ex.what();
        OutputDebugStringA(error.c_str());
        return false;
    }
}

void Logger::Shutdown() {
    if (s_initialized && s_logger) {
        LOG_INFO("Shutting down logging system");
        s_logger->flush();
        spdlog::shutdown();
        s_logger = nullptr;
        s_initialized = false;
    }
}

std::shared_ptr<spdlog::logger> Logger::GetLogger() {
    return s_logger;
}

void Logger::Flush() {
    if (s_initialized && s_logger) {
        s_logger->flush();
    }
}

bool Logger::IsInitialized() {
    return s_initialized;
}

} // namespace AeroflyBridge
