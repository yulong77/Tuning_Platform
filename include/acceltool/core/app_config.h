#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace acceltool
{
    enum class AxisMode
    {
        XY,
        XYZ
    };

    struct AppConfig
    {
        // Wireless base station
        std::string port;
        std::uint32_t baudrate = 0;

        // Wireless node
        int nodeAddress = 0;

        // Sampling / startup
        bool forceSetToIdle = false;
        bool configureNode = false;
        bool useSyncSampling = false;
        bool useLxrsPlus = false;
        std::uint32_t sampleRateHz = 0;
        std::uint32_t inactivityTimeoutSeconds = 0;
        bool unlimitedDuration = false;

        // Acquisition
        // 0 means unlimited. Any positive value means stop after that many samples.
        std::size_t maxSamples = 0;
        std::uint32_t readTimeoutMs = 0;
        AxisMode axisMode = AxisMode::XYZ;
        double spec = 0.0;

        // Queues
        std::size_t rawQueueCapacityBatches = 256;
        std::size_t writeQueueCapacityBatches = 256;

        // Display aggregation
        std::size_t displayAggregationSamples = 100;
        std::string outputDisplayCsvPath;

        // Output
        std::string outputCsvPath;
        bool printToConsole = false;

        // Debug
        bool dumpSweepChannelsAtStartup = false;
        bool printCurrentNodeConfig = false;
    };

    bool loadConfigFromFile(const std::string& path, AppConfig& config, std::string& errorMessage);
    bool validateConfig(const AppConfig& config, std::string& errorMessage);
}
