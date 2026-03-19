#include <exception>
#include <string>

#include "acceltool/backend/wireless_accelerometer_manager.h"
#include "acceltool/core/app_config.h"
#include "acceltool/processing/magnitude_calculator.h"
#include "acceltool/utils/csv_writer.h"
#include "acceltool/utils/logger.h"

int main(int argc, char* argv[])
{
    using namespace acceltool;

    try
    {
        AppConfig config;

        std::string configPath = "config/acceltool.ini";
        if (argc >= 2)
        {
            configPath = argv[1];
        }

        std::string loadError;
        if (!loadConfigFromFile(configPath, config, loadError))
        {
            throw std::runtime_error(
                "Failed to load config file.\nPath: " + configPath + "\n" + loadError);
        }

        std::string validateError;
        if (!validateConfig(config, validateError))
        {
            throw std::runtime_error("Invalid configuration.\n" + validateError);
        }

        initLogger(LogLevel::Debug, true, true, "acceltool.log");
        logInfo("AccelTool started.");
        logInfo("Loaded config file: " + configPath);

        logInfo("Using port: " + config.port);
        logInfo("Using node address: " + std::to_string(config.nodeAddress));
        logInfo("Using baudrate: " + std::to_string(config.baudrate));
        logInfo("Requested sample rate from ini: " + std::to_string(config.sampleRateHz));
        logInfo("Opening CSV file: " + config.outputCsvPath);

        WirelessAccelerometerManager manager;
        MagnitudeCalculator calculator;
        CsvWriter writer;

        writer.open(config.outputCsvPath);
        writer.writeHeader();

        manager.connect(config);
        manager.initialize();
        manager.startSampling();

        std::size_t count = 0;

        while (count < config.maxSamples)
        {
            auto rawOpt = manager.readNextSample(config.readTimeoutMs);
            if (!rawOpt.has_value())
            {
                continue;
            }

            const ProcessedSample processed = calculator.process(*rawOpt);

            writer.writeRow(processed);
            ++count;

            if (config.printToConsole && (count % config.printEvery == 0))
            {
                logInfo(
                    "sample=" + std::to_string(processed.sampleIndex) +
                    ", x=" + std::to_string(processed.x) +
                    ", y=" + std::to_string(processed.y) +
                    ", z=" + std::to_string(processed.z) +
                    ", magXY=" + std::to_string(processed.magnitudeXY) +
                    ", magXYZ=" + std::to_string(processed.magnitudeXYZ));
            }
        }

        writer.flush();
        manager.stopSampling();

        logInfo("Program finished successfully.");
        shutdownLogger();
        return 0;
    }
    catch (const mscl::Error& e)
    {
        acceltool::logError(std::string("MSCL ERROR: ") + e.what());
        acceltool::shutdownLogger();
        return 1;
    }
    catch (const std::exception& e)
    {
        acceltool::logError(std::string("STD ERROR: ") + e.what());
        acceltool::shutdownLogger();
        return 1;
    }
}
