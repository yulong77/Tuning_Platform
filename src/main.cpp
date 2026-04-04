#include <cctype>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <mscl/mscl.h>

#include "acceltool/backend/wireless_accelerometer_manager.h"
#include "acceltool/core/app_config.h"
#include "acceltool/core/sampling_session.h"
#include "acceltool/utils/logger.h"

namespace acceltool
{
    void waitForEnterToExit(const std::string& message)
    {
        std::cout << '\n' << message << '\n';
        std::cout << "Press Enter to exit...";
        std::cout.flush();

        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
    }

    std::string readCommand(const std::string& prompt)
    {
        std::cout << '\n' << prompt;
        std::cout.flush();

        std::string line;
        std::getline(std::cin, line);
        return line;
    }
}

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
        logInfo("Raw/result CSV will be written when sampling starts: " + config.outputCsvPath);
        logInfo("Display CSV will be written when sampling starts: " + config.outputDisplayCsvPath);

        WirelessAccelerometerManager manager;
        manager.connect(config);

        bool deviceReady = false;
        bool samplingHasRun = false;
        std::unique_ptr<SamplingSession> session;

        std::cout << "\nAccelTool interactive mode\n"
                  << "  I = Set to Idle + initialize device\n"
                  << "  S = Start receiving data\n"
                  << "  T = Stop receiving data\n"
                  << "  Q = Quit\n";

        while (true)
        {
            if (session && session->isFinished())
            {
                try
                {
                    session->stop();
                    std::cout << "\nSampling stopped. CSV files were written successfully.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Sampling session failed: ") + e.what());
                    std::cout << "\nSampling stopped with an error:\n" << e.what() << "\n";
                }

                session.reset();
                samplingHasRun = true;
                deviceReady = false;
                std::cout << "Device returned to idle. Run I again before starting another session.\n";
            }

            std::string command = readCommand("\nEnter command [I/S/T/Q]: ");

            if (command.empty())
            {
                continue;
            }

            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));

            if (c == 'Q')
            {
                if (session && session->isRunning())
                {
                    std::cout << "Stopping active sampling session before exit...\n";
                    session->stop();
                }
                break;
            }

            if (c == 'I')
            {
                if (session && session->isRunning())
                {
                    std::cout << "Sampling is currently running. Stop it first.\n";
                    continue;
                }

                try
                {
                    std::cout << "\nRunning manual Set to Idle + initialization...\n";
                    manager.initialize(true);
                    deviceReady = true;
                    std::cout << "\nDevice is ready. You can now press S to start receiving data.\n";
                }
                catch (const mscl::Error& e)
                {
                    deviceReady = false;
                    logError(std::string("MSCL ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. This is often recoverable; press I again to retry.\n"
                              << "MSCL details: " << e.what() << "\n";
                }
                catch (const std::exception& e)
                {
                    deviceReady = false;
                    logError(std::string("STD ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. Press I again to retry.\n"
                              << "Details: " << e.what() << "\n";
                }

                continue;
            }

            if (c == 'S')
            {
                if (samplingHasRun)
                {
                    std::cout << "This run already wrote CSV output once. Restart the program for a fresh capture, or press I and then S if you want to overwrite the CSV files again.\n";
                }

                if (session && session->isRunning())
                {
                    std::cout << "Sampling is already running.\n";
                    continue;
                }

                if (!deviceReady)
                {
                    std::cout << "Device is not ready yet. Press I first until initialization succeeds.\n";
                    continue;
                }

                try
                {
                    session = std::make_unique<SamplingSession>(manager, config);
                    session->start();
                    std::cout << "\nSampling started. Press T when you want to stop receiving data.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Failed to start sampling session: ") + e.what());
                    std::cout << "\nFailed to start sampling:\n" << e.what() << "\n";
                    session.reset();
                    deviceReady = false;
                }

                continue;
            }

            if (c == 'T')
            {
                if (!session || !session->isRunning())
                {
                    std::cout << "Sampling is not running.\n";
                    continue;
                }

                try
                {
                    std::cout << "Stopping sampling...\n";
                    session->stop();
                    std::cout << "Sampling stopped.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Error while stopping sampling: ") + e.what());
                    std::cout << "Sampling stopped with an error:\n" << e.what() << "\n";
                }

                session.reset();
                samplingHasRun = true;
                deviceReady = false;
                std::cout << "Device returned to idle. Press I again before the next start.\n";
                continue;
            }

            std::cout << "Unknown command. Use I, S, T, or Q.\n";
        }

        logInfo("Program finished successfully.");
        shutdownLogger();
        return 0;
    }
    catch (const mscl::Error& e)
    {
        acceltool::logError(std::string("MSCL ERROR: ") + e.what());
        acceltool::shutdownLogger();
        acceltool::waitForEnterToExit(
            std::string("Program failed.\n\nDetails: ") + e.what());
        return 1;
    }
    catch (const std::exception& e)
    {
        acceltool::logError(std::string("STD ERROR: ") + e.what());
        acceltool::shutdownLogger();
        acceltool::waitForEnterToExit(
            std::string("Program failed.\n\nDetails: ") + e.what());
        return 1;
    }
}

